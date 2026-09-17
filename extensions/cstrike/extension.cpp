#include "players.h"
#include <source2root/extension.hpp>
#include <cstring>
#include <memory>

namespace {
using namespace keels2::authoring;
using source2root::NativeCall;
namespace cs = source2root::cstrike;
class CounterStrike final : public source2root::Extension {
public:
    static constexpr PluginInfo Info{"Source2Root Counter-Strike", "KeelS2 Project", "1.0.0", "CS2 player team and respawn actions"};
    static constexpr PluginRequirement Requirements[]{{"Source2Root", "1.0.0", DependencyRequirement::exact}};
    CounterStrike() : Extension("source2root.cstrike") {}
private:
    std::unique_ptr<cs::Players> players_;
    template <typename T> const T& Require(const char* name, unsigned version) {
        const void* raw = nullptr;
        if (HostContext().QueryService(name, version, &raw) != KEEL_RESULT_OK || !raw)
            throw cs::Error(std::string("Required host service is unavailable: ") + name);
        const auto* api = static_cast<const T*>(raw);
        if (api->size != sizeof(T) || api->api_version != version) throw cs::Error("Incompatible host service table.");
        return *api;
    }
    bool OnExtensionStart() override {
        players_ = std::make_unique<cs::Players>(HostContext().PluginHandle(),
            Require<KeelEntitiesApi>(KEELS2_ENTITIES_SERVICE_NAME, KEELS2_ENTITIES_API_VERSION),
            Require<KeelPlayersApi>(KEELS2_PLAYERS_SERVICE_NAME, KEELS2_PLAYERS_API_VERSION),
            Require<KeelNativeRuntimeApi>(KEELS2_NATIVE_RUNTIME_SERVICE_NAME, KEELS2_NATIVE_RUNTIME_API_VERSION),
            Require<KeelPlayerManagementApi>(KEELS2_PLAYER_MANAGEMENT_SERVICE_NAME, KEELS2_PLAYER_MANAGEMENT_API_VERSION));
        return RegisterNative("CS_GetCapabilities", 1, &CounterStrike::Capabilities)
            && RegisterNative("CS_RespawnPlayer", 1, &CounterStrike::Respawn)
            && RegisterNative("CS_ChangeTeam", 2, &CounterStrike::ChangeTeam)
            && RegisterNative("CS_SwitchTeam", 2, &CounterStrike::SwitchTeam);
    }
    template <typename Function> static std::int32_t Invoke(NativeCall& call, Function function) {
        try { function(); return 1; } catch (const cs::Error& error) { return call.Fail(error.what()); }
    }
    std::int32_t Capabilities(NativeCall& call) {
        call.OutputCell(1, 0);
        return Invoke(call, [&] { call.OutputCell(1, players_->Capabilities()); });
    }
    std::int32_t Action(NativeCall& call, unsigned kind, int team) {
        return Invoke(call, [&] {
            SrPlayerIdentity player{};
            if (!call.Player(call.Int(1), player)) throw cs::Error("Player connection is no longer available.");
            players_->Apply({player.slot, 0, player.connection}, kind, team);
        });
    }
    std::int32_t Respawn(NativeCall& call) { return Action(call, KEELS2_PLAYER_MANAGEMENT_RESPAWN, 0); }
    std::int32_t ChangeTeam(NativeCall& call) { return Action(call, KEELS2_PLAYER_MANAGEMENT_CHANGE_TEAM, call.Int(2)); }
    std::int32_t SwitchTeam(NativeCall& call) { return Action(call, KEELS2_PLAYER_MANAGEMENT_SWITCH_TEAM, call.Int(2)); }
};
}
extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Query(const KeelHostQuery* query, KeelPluginInfo* info) {
    if (!query || query->size != sizeof(*query) || !query->game || std::strcmp(query->game, "cs2")) return KEEL_FALSE;
    return keels2::detail::AuthoringAdapter<CounterStrike>::Query(query, info);
}
extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Manifest(const KeelHostQuery* query, KeelPluginManifest* manifest) {
    return keels2::detail::AuthoringAdapter<CounterStrike>::Manifest(query, manifest);
}
extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Load(const KeelHostApi* api, KeelPluginHandle plugin) {
    return keels2::detail::AuthoringAdapter<CounterStrike>::Load(api, plugin);
}
extern "C" KEELS2_PLUGIN_EXPORT void KeelPlugin_Unload(KeelPluginHandle plugin) {
    keels2::detail::AuthoringAdapter<CounterStrike>::Unload(plugin);
}
