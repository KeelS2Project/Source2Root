#include "players.h"
#include <array>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <thread>

void RunStatisticsChecks();
void RunRoundChecks();

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
unsigned readable = 15, writable = 15, reads = 0, writes = 0;
std::array<std::int32_t,4> statistics{800,7,3,2};
KeelResult stat_cap_result = KEEL_RESULT_OK, read_result = KEEL_RESULT_OK, write_result = KEEL_RESULT_OK;
KeelPlayerManagementAction last{};
std::function<void()> reenter;
void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

void Fails(const std::function<void()>& call) {
    bool failed = false;

    try {
        call();
    } catch (const Error&) {
        failed = true;
    }

    Check(failed, "expected a domain error");
}

KeelResult Thread(KeelPluginHandle plugin) {
    return plugin == owner ? (std::this_thread::get_id() == game_thread ? KEEL_RESULT_OK : KEEL_RESULT_WRONG_THREAD)
                           : KEEL_RESULT_NOT_READY;
}

KeelResult Capabilities(KeelPluginHandle plugin, unsigned* result) {
    if (Thread(plugin) != KEEL_RESULT_OK)
        return KEEL_RESULT_WRONG_THREAD;

    *result = caps;
    return cap_result;
}

KeelResult Validate(KeelPluginHandle plugin, const KeelPlayerConnection* conn, KeelPlayerInfo* info) {
    if (Thread(plugin) != KEEL_RESULT_OK)
        return KEEL_RESULT_WRONG_THREAD;

    ++validates;

    if (conn->slot != 2 || conn->generation != 19 || (mode == 1 && validates == 2) || (mode == 12 && validates == 3))
        return KEEL_RESULT_NOT_FOUND;

    *info = {};
    info->size = sizeof(*info);
    info->slot = 2;
    info->connection = 19;
    info->flags = KEELS2_PLAYER_CONNECTED;
    info->controller_handle = source;
    info->pawn_handle = 0x23004;

    if (mode == 2 && validates == 2)
        ++info->controller_handle;

    if (mode == 3)
        info->flags |= KEELS2_PLAYER_SOURCE_TV;

    if (mode == 4)
        info->flags = 0;

    if (mode == 5)
        info->size = 0;

    if (mode == 6)
        info->controller_handle = UINT32_MAX;

    return KEEL_RESULT_OK;
}

KeelResult Find(KeelPluginHandle plugin, unsigned handle, KeelEntityHandle* output) {
    if (plugin != owner || handle != source)
        return KEEL_RESULT_NOT_FOUND;

    *output = next_handle++;
    entities[*output] = handle;
    return KEEL_RESULT_OK;
}

KeelResult Release(KeelPluginHandle plugin, KeelEntityHandle entity) {
    return plugin == owner && entities.erase(entity) ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND;
}

KeelResult Describe(KeelPluginHandle plugin, KeelEntityHandle entity, KeelEntityInfo* info) {
    if (plugin != owner || !entities.contains(entity))
        return KEEL_RESULT_NOT_FOUND;

    ++describes;
    *info = {sizeof(*info), 3, source, 0, 99};

    if (mode == 7)
        ++info->source2_handle;

    if (mode == 8 && describes == 2)
        ++info->epoch;

    if (mode == 9 && describes == 2)
        ++info->index;

    if (mode == 10)
        info->reserved = 1;

    if (mode == 11 && describes == 2)
        return KEEL_RESULT_NOT_FOUND;

    if (mode == 13 && describes == 3)
        ++info->epoch;

    if (mode == 14 && describes == 3)
        ++info->source2_handle;

    return KEEL_RESULT_OK;
}

KeelResult Apply(KeelPluginHandle plugin, KeelEntityHandle entity, const KeelPlayerManagementAction* action) {
    if (plugin != owner || !entities.contains(entity))
        return KEEL_RESULT_NOT_FOUND;

    ++actions;
    last = *action;

    if (reenter)
        reenter();

    return action_result;
}

std::size_t StatisticIndex(unsigned key) {
    switch (key) {
        case KEELS2_PLAYER_STAT_MONEY: return 0;
        case KEELS2_PLAYER_STAT_MATCH_KILLS: return 1;
        case KEELS2_PLAYER_STAT_MATCH_DEATHS: return 2;
        case KEELS2_PLAYER_STAT_MATCH_ASSISTS: return 3;
        default: throw std::runtime_error("invalid statistic reached host fixture");
    }
}

KeelResult StatisticsCapabilities(KeelPluginHandle plugin, unsigned* read, unsigned* write) {
    if (Thread(plugin) != KEEL_RESULT_OK)
        return KEEL_RESULT_WRONG_THREAD;

    *read = readable;
    *write = writable;
    return stat_cap_result;
}

KeelResult ReadStatistic(KeelPluginHandle plugin, KeelEntityHandle entity, unsigned key, std::int32_t* value) {
    if (plugin != owner || !entities.contains(entity))
        return KEEL_RESULT_NOT_FOUND;

    ++reads;
    *value = statistics[StatisticIndex(key)];

    if (reenter)
        reenter();

    return read_result;
}

KeelResult WriteStatistic(KeelPluginHandle plugin, KeelEntityHandle entity, unsigned key, std::int32_t value) {
    if (plugin != owner || !entities.contains(entity))
        return KEEL_RESULT_NOT_FOUND;

    ++writes;
    statistics[StatisticIndex(key)] = value;

    if (reenter)
        reenter();

    return write_result;
}

void Reset() {
    caps = 7;
    actions = describes = validates = mode = reads = writes = 0;
    readable = writable = 15;
    statistics = {800, 7, 3, 2};
    cap_result = action_result = stat_cap_result = read_result = write_result = KEEL_RESULT_OK;
    reenter = {};
}
}

int main() {
    try {
        const KeelEntitiesApi entity_api{
            sizeof(KeelEntitiesApi), 1, nullptr, &Find, &Release, &Describe, nullptr, nullptr};

        const KeelPlayersApi player_api{sizeof(KeelPlayersApi), 1, nullptr, nullptr, &Validate};
        const KeelNativeRuntimeApi runtime{sizeof(KeelNativeRuntimeApi), 1, &Thread, nullptr, nullptr, nullptr};
        const KeelPlayerManagementApi management{sizeof(KeelPlayerManagementApi), 1, &Capabilities, &Apply};
        Players players(owner,entity_api,player_api,runtime,management);
        const KeelPlayerConnection connection{2,0,19};
        Check(players.Capabilities() == 7, "capability mask");
        caps = UINT32_MAX;
        Check(players.Capabilities() == 7, "unknown capability bits are hidden");

        for (auto kind : {1u,2u,4u}) {
            Reset();
            players.Apply(connection, kind, kind == 1 ? 0 : 3);
            Check(actions == 1 && last.kind == kind && last.team == (kind == 1 ? 0 : 3) && !last.reserved &&
                      entities.empty(),
                  "dispatch and temporary handle cleanup");
        }

        for (unsigned fault = 1; fault <= 11; ++fault) {
            Reset();
            mode = fault;
            Fails([&] {
                players.Apply(connection, 1, 0);
            });
            Check(!actions && entities.empty(), "identity validation prevents dispatch and releases handles");
        }

        for (const int team : {-1,0,4,255}) {
            Reset();
            Fails([&] {
                players.Apply(connection, 2, team);
            });
            Check(!validates && !actions, "team validated before engine lookup");
        }

        Reset();
        Fails([&] {
            players.Apply(connection, 4, 1);
        });
        Fails([&] {
            players.Apply(connection, 1, 2);
        });
        Fails([&] {
            players.Apply(connection, 7, 2);
        });
        Fails([&] {
            players.Apply({2, 1, 19}, 1, 0);
        });
        Fails([&] {
            players.Apply({2, 0, 20}, 1, 0);
        });
        Check(!actions && entities.empty(), "invalid action and stale connection");
        Reset();
        cap_result = KEEL_RESULT_UNSUPPORTED;
        Fails([&] {
            players.Capabilities();
        });
        Fails([&] {
            players.Apply(connection, 1, 0);
        });
        Check(!validates && !actions, "unsupported binary");
        Reset();
        caps = 2;
        Fails([&] {
            players.Apply(connection, 1, 0);
        });
        Check(!validates, "missing action capability");
        Reset();
        action_result = KEEL_RESULT_ENGINE_FAILURE;
        Fails([&] {
            players.Apply(connection, 1, 0);
        });
        Check(actions == 1 && entities.empty(), "failure cleanup");
        Reset();
        unsigned nested = 0;
        reenter = [&] {
            ++nested;
            players.Apply(connection, 1, 0);
        };
        Fails([&] {
            players.Apply(connection, 1, 0);
        });
        Check(nested == 8 && entities.empty(), "bounded reentrant dispatch");
        reenter = {};
        players.Apply(connection, 1, 0);
        Check(entities.empty(), "recursion count restored after failure");
        Reset();
        std::exception_ptr worker_error;
        std::thread worker([&] {
            try {
                Fails([&] {
                    players.Capabilities();
                });
                Fails([&] {
                    players.Apply(connection, 1, 0);
                });
            } catch (...) {
                worker_error = std::current_exception();
            }
        });
        worker.join();

        if (worker_error)
            std::rethrow_exception(worker_error);

        Check(!actions && !validates && entities.empty(), "wrong-thread rejection precedes lookup");
        auto bad = management;
        bad.apply = nullptr;
        Fails([&] {
            Players invalid(owner, entity_api, player_api, runtime, bad);
        });
        const KeelPlayerStatisticsApi stats_api{
            sizeof(KeelPlayerStatisticsApi), 1, &StatisticsCapabilities, &ReadStatistic, &WriteStatistic};

        Players component_players(owner,entity_api,player_api,runtime,management,&stats_api);
        Reset();
        Fails([&] {
            players.StatisticsCapabilities();
        });
        Fails([&] {
            players.GetStatistic(connection, 1);
        });
        Fails([&] {
            players.SetStatistic(connection, 1, 0);
        });
        players.Apply(connection,1,0);
        Check(actions == 1 && !reads && !writes && entities.empty(), "optional statistics service preserves player actions");
        readable = writable = UINT32_MAX;
        Check(component_players.StatisticsCapabilities() == std::pair<unsigned, unsigned>{15, 15},
              "unknown statistic capability bits are hidden");

        for (const unsigned key : {1u,2u,4u,8u}) {
            Reset();
            const auto index = StatisticIndex(key);
            Check(component_players.GetStatistic(connection,key) == statistics[index], "component statistic key mapping");

            for (const auto value : {0,std::numeric_limits<std::int32_t>::max()}) {
                auto expected = statistics;
                expected[index] = value;
                component_players.SetStatistic(connection,key,value);
                Check(statistics == expected && component_players.GetStatistic(connection,key) == value && entities.empty(),
                    "component setter boundaries and independent fields");
            }
        }

        Reset();

        for (const unsigned key : {0u,3u,16u,UINT32_MAX}) {
            Fails([&] {
                component_players.GetStatistic(connection, key);
            });
            Fails([&] {
                component_players.SetStatistic(connection, key, 0);
            });
        }

        Fails([&] {
            component_players.SetStatistic(connection, 1, -1);
        });
        Check(!validates && !reads && !writes, "invalid component arguments rejected before lookup");

        for (unsigned fault = 1; fault <= 14; ++fault) {
            Reset();
            mode = fault;
            Fails([&] {
                component_players.GetStatistic(connection, 1);
            });
            Check(reads == (fault >= 12 ? 1u : 0u) && entities.empty(),
                  "component read validates identity before and after host read");

            if (fault <= 11) {
                Reset();
                mode = fault;
                Fails([&] {
                    component_players.SetStatistic(connection, 1, 900);
                });
                Check(!writes && statistics[0] == 800 && entities.empty(), "stale component setter never reaches host");
            }
        }

        Reset();
        writable = 0;
        Check(component_players.GetStatistic(connection,1) == 800, "read-only component capability");
        Fails([&] {
            component_players.SetStatistic(connection, 1, 0);
        });
        Check(!writes, "read-only component blocks writes");
        Reset();
        readable = 0;
        Fails([&] {
            component_players.GetStatistic(connection, 1);
        });
        Check(!validates && !reads, "missing read capability precedes lookup");
        Reset();
        stat_cap_result = KEEL_RESULT_ENGINE_FAILURE;
        Fails([&] {
            component_players.StatisticsCapabilities();
        });
        Fails([&] {
            component_players.GetStatistic(connection, 1);
        });
        Fails([&] {
            component_players.SetStatistic(connection, 1, 0);
        });
        Check(!validates && !reads && !writes, "component capability failure precedes lookup");
        Reset();
        read_result = KEEL_RESULT_ENGINE_FAILURE;
        Fails([&] {
            component_players.GetStatistic(connection, 1);
        });
        Check(reads == 1 && entities.empty(), "component read failure cleanup");
        Reset();
        write_result = KEEL_RESULT_ENGINE_FAILURE;
        Fails([&] {
            component_players.SetStatistic(connection, 1, 900);
        });
        Check(writes == 1 && statistics[0] == 900 && entities.empty(),
              "component notification failure does not promise rollback");

        Reset();
        mode = 12;
        component_players.SetStatistic(connection,1,900);
        Check(writes == 1 && validates == 2 && entities.empty(),
              "dispatched component write does not revalidate after callback");

        Reset();
        reenter = [&] {
            component_players.SetStatistic(connection, 1, 900);
        };
        Fails([&] {
            component_players.SetStatistic(connection, 1, 900);
        });
        Check(writes == 8 && entities.empty(), "component write recursion is bounded");
        reenter = {};
        component_players.SetStatistic(connection, 1, 901);
        Check(writes == 9 && entities.empty(), "component recursion count recovers");
        Reset();
        reenter = [&] {
            component_players.GetStatistic(connection, 1);
        };
        Fails([&] {
            component_players.GetStatistic(connection, 1);
        });
        Check(reads == 8 && entities.empty(), "component read recursion is bounded");
        Reset();
        worker_error = {};
        std::thread stats_worker([&] {
            try {
                Fails([&] {
                    component_players.StatisticsCapabilities();
                });
                Fails([&] {
                    component_players.GetStatistic(connection, 1);
                });
                Fails([&] {
                    component_players.SetStatistic(connection, 1, 0);
                });
            } catch (...) {
                worker_error = std::current_exception();
            }
        });
        stats_worker.join();

        if (worker_error)
            std::rethrow_exception(worker_error);

        Check(!reads && !writes && !validates && entities.empty(), "component operations reject worker thread before lookup");

        for (unsigned fault = 0; fault < 5; ++fault) {
            auto invalid_stats = stats_api;

            if (fault == 0)
                invalid_stats.size = 0;

            if (fault == 1)
                ++invalid_stats.api_version;

            if (fault == 2)
                invalid_stats.capabilities = nullptr;

            if (fault == 3)
                invalid_stats.read = nullptr;

            if (fault == 4)
                invalid_stats.write = nullptr;

            Fails([&] {
                Players invalid(owner, entity_api, player_api, runtime, management, &invalid_stats);
            });
        }

        RunStatisticsChecks();
        RunRoundChecks();
        std::cout << "Counter-Strike backend checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
