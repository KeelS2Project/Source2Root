#include "players.h"
#include <functional>
#include <iostream>
#include <map>
#include <thread>

void RunStatisticsChecks();

namespace {
using source2root::cstrike::Players;
using source2root::cstrike::Error;
constexpr KeelPluginHandle owner = 7;
constexpr unsigned source = 0x12003;
const std::thread::id game_thread = std::this_thread::get_id();
std::map<KeelEntityHandle, unsigned> entities;
KeelEntityHandle next_handle = 1;
unsigned caps = 7, actions = 0, describes = 0, validates = 0, mode = 0;
KeelResult cap_result = KEEL_RESULT_OK, action_result = KEEL_RESULT_OK;
KeelPlayerManagementAction last{};
std::function<void()> reenter;
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Fails(const std::function<void()>& call) { bool failed = false; try { call(); } catch (const Error&) { failed = true; } Check(failed, "expected a domain error"); }
KeelResult Thread(KeelPluginHandle plugin) {
    return plugin == owner ? (std::this_thread::get_id() == game_thread ? KEEL_RESULT_OK : KEEL_RESULT_WRONG_THREAD) : KEEL_RESULT_NOT_READY;
}
KeelResult Capabilities(KeelPluginHandle plugin, unsigned* result) {
    if (Thread(plugin) != KEEL_RESULT_OK) return KEEL_RESULT_WRONG_THREAD;
    *result = caps; return cap_result;
}
KeelResult Validate(KeelPluginHandle plugin, const KeelPlayerConnection* conn, KeelPlayerInfo* info) {
    if (Thread(plugin) != KEEL_RESULT_OK) return KEEL_RESULT_WRONG_THREAD;
    ++validates;
    if (conn->slot != 2 || conn->generation != 19 || (mode == 1 && validates == 2)) return KEEL_RESULT_NOT_FOUND;
    *info = {}; info->size = sizeof(*info); info->slot = 2; info->connection = 19; info->flags = KEELS2_PLAYER_CONNECTED;
    info->controller_handle = source; info->pawn_handle = 0x23004;
    if (mode == 2 && validates == 2) ++info->controller_handle;
    if (mode == 3) info->flags |= KEELS2_PLAYER_SOURCE_TV;
    if (mode == 4) info->flags = 0;
    if (mode == 5) info->size = 0;
    if (mode == 6) info->controller_handle = UINT32_MAX;
    return KEEL_RESULT_OK;
}
KeelResult Find(KeelPluginHandle plugin, unsigned handle, KeelEntityHandle* output) {
    if (plugin != owner || handle != source) return KEEL_RESULT_NOT_FOUND;
    *output = next_handle++; entities[*output] = handle; return KEEL_RESULT_OK;
}
KeelResult Release(KeelPluginHandle plugin, KeelEntityHandle entity) {
    return plugin == owner && entities.erase(entity) ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND;
}
KeelResult Describe(KeelPluginHandle plugin, KeelEntityHandle entity, KeelEntityInfo* info) {
    if (plugin != owner || !entities.contains(entity)) return KEEL_RESULT_NOT_FOUND;
    ++describes;
    *info = {sizeof(*info), 3, source, 0, 99};
    if (mode == 7) ++info->source2_handle;
    if (mode == 8 && describes == 2) ++info->epoch;
    if (mode == 9 && describes == 2) ++info->index;
    if (mode == 10) info->reserved = 1;
    if (mode == 11 && describes == 2) return KEEL_RESULT_NOT_FOUND;
    return KEEL_RESULT_OK;
}
KeelResult Apply(KeelPluginHandle plugin, KeelEntityHandle entity, const KeelPlayerManagementAction* action) {
    if (plugin != owner || !entities.contains(entity)) return KEEL_RESULT_NOT_FOUND;
    ++actions; last = *action;
    if (reenter) reenter();
    return action_result;
}
void Reset() { caps = 7; actions = describes = validates = mode = 0; cap_result = action_result = KEEL_RESULT_OK; reenter = {}; }
}
int main() {
    try {
        const KeelEntitiesApi entity_api{sizeof(KeelEntitiesApi),1,nullptr,&Find,&Release,&Describe,nullptr,nullptr};
        const KeelPlayersApi player_api{sizeof(KeelPlayersApi),1,nullptr,nullptr,&Validate};
        const KeelNativeRuntimeApi runtime{sizeof(KeelNativeRuntimeApi),1,&Thread,nullptr,nullptr,nullptr};
        const KeelPlayerManagementApi management{sizeof(KeelPlayerManagementApi),1,&Capabilities,&Apply};
        Players players(owner,entity_api,player_api,runtime,management);
        const KeelPlayerConnection connection{2,0,19};
        Check(players.Capabilities() == 7, "capability mask");
        caps = UINT32_MAX; Check(players.Capabilities() == 7, "unknown capability bits are hidden");
        for (auto kind : {1u,2u,4u}) {
            Reset(); players.Apply(connection, kind, kind == 1 ? 0 : 3);
            Check(actions == 1 && last.kind == kind && last.team == (kind == 1 ? 0 : 3) && !last.reserved && entities.empty(), "dispatch and temporary handle cleanup");
        }
        for (unsigned fault = 1; fault <= 11; ++fault) {
            Reset(); mode = fault;
            Fails([&] { players.Apply(connection,1,0); });
            Check(!actions && entities.empty(), "identity validation prevents dispatch and releases handles");
        }
        for (const int team : {-1,0,4,255}) {
            Reset(); Fails([&] { players.Apply(connection,2,team); }); Check(!validates && !actions, "team validated before engine lookup");
        }
        Reset(); Fails([&] { players.Apply(connection,4,1); }); Fails([&] { players.Apply(connection,1,2); }); Fails([&] { players.Apply(connection,7,2); });
        Fails([&] { players.Apply({2,1,19},1,0); }); Fails([&] { players.Apply({2,0,20},1,0); });
        Check(!actions && entities.empty(), "invalid action and stale connection");
        Reset(); cap_result = KEEL_RESULT_UNSUPPORTED;
        Fails([&] { players.Capabilities(); }); Fails([&] { players.Apply(connection,1,0); }); Check(!validates && !actions, "unsupported binary");
        Reset(); caps = 2; Fails([&] { players.Apply(connection,1,0); }); Check(!validates, "missing action capability");
        Reset(); action_result = KEEL_RESULT_ENGINE_FAILURE;
        Fails([&] { players.Apply(connection,1,0); }); Check(actions == 1 && entities.empty(), "failure cleanup");
        Reset(); unsigned nested = 0;
        reenter = [&] { ++nested; players.Apply(connection,1,0); };
        Fails([&] { players.Apply(connection,1,0); }); Check(nested == 8 && entities.empty(), "bounded reentrant dispatch");
        reenter = {}; players.Apply(connection,1,0); Check(entities.empty(), "recursion count restored after failure");
        Reset(); std::exception_ptr worker_error;
        std::thread worker([&] { try { Fails([&] { players.Capabilities(); }); Fails([&] { players.Apply(connection,1,0); }); } catch (...) { worker_error = std::current_exception(); } });
        worker.join(); if (worker_error) std::rethrow_exception(worker_error);
        Check(!actions && !validates && entities.empty(), "wrong-thread rejection precedes lookup");
        auto bad = management; bad.apply = nullptr;
        Fails([&] { Players invalid(owner,entity_api,player_api,runtime,bad); });
        RunStatisticsChecks();
        std::cout << "Counter-Strike backend checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
