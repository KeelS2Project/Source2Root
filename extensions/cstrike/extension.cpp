#include "players.h"
#include "statistics.h"
#include "rounds.h"
#include <source2root/extension.hpp>
#include <cstring>
#include <memory>

namespace {
using namespace keels2::authoring;
using source2root::NativeCall;
namespace cs = source2root::cstrike;

class CounterStrike final : public source2root::Extension {
public:
    static constexpr PluginInfo Info{
        "Source2Root Counter-Strike", "KeelS2 Project", "1.0.0", "CS2 players, statistics and round control"};

    static constexpr PluginRequirement Requirements[]{{"Source2Root", "1.0.0", DependencyRequirement::exact}};
    CounterStrike() : Extension("source2root.cstrike") {}

private:
    std::unique_ptr<cs::Players> players_;
    std::unique_ptr<cs::Statistics> statistics_;
    std::unique_ptr<cs::Rounds> rounds_;
    template <typename T> const T& Require(const char* name, unsigned version) {
        const void* raw = nullptr;

        if (HostContext().QueryService(name, version, &raw) != KEEL_RESULT_OK || !raw)
            throw cs::Error(std::string("Required host service is unavailable: ") + name);

        const auto* api = static_cast<const T*>(raw);

        if (api->size != sizeof(T) || api->api_version != version)
            throw cs::Error("Incompatible host service table.");

        return *api;
    }

    const KeelEntityWritesApi* OptionalWrites() {
        const void* raw = nullptr;
        const auto result =
            HostContext().QueryService(KEELS2_ENTITY_WRITES_SERVICE_NAME, KEELS2_ENTITY_WRITES_API_VERSION, &raw);

        if (result == KEEL_RESULT_NOT_FOUND || result == KEEL_RESULT_UNSUPPORTED)
            return nullptr;

        if (result != KEEL_RESULT_OK || !raw)
            throw cs::Error("Could not query entity write service.");

        return static_cast<const KeelEntityWritesApi*>(raw);
    }

    const KeelPlayerStatisticsApi* OptionalStatistics() {
        const void* raw = nullptr;
        const auto result = HostContext().QueryService(
            KEELS2_PLAYER_STATISTICS_SERVICE_NAME, KEELS2_PLAYER_STATISTICS_API_VERSION, &raw);

        if (result == KEEL_RESULT_NOT_FOUND || result == KEEL_RESULT_UNSUPPORTED)
            return nullptr;

        if (result != KEEL_RESULT_OK || !raw)
            throw cs::Error("Could not query player statistics service.");

        return static_cast<const KeelPlayerStatisticsApi*>(raw);
    }

    bool OnExtensionStart() override {
        players_ = std::make_unique<cs::Players>(
            HostContext().PluginHandle(),
            Require<KeelEntitiesApi>(KEELS2_ENTITIES_SERVICE_NAME, KEELS2_ENTITIES_API_VERSION),
            Require<KeelPlayersApi>(KEELS2_PLAYERS_SERVICE_NAME, KEELS2_PLAYERS_API_VERSION),
            Require<KeelNativeRuntimeApi>(KEELS2_NATIVE_RUNTIME_SERVICE_NAME, KEELS2_NATIVE_RUNTIME_API_VERSION),
            Require<KeelPlayerManagementApi>(KEELS2_PLAYER_MANAGEMENT_SERVICE_NAME,
                                             KEELS2_PLAYER_MANAGEMENT_API_VERSION),
            OptionalStatistics());

        statistics_ = std::make_unique<cs::Statistics>(std::make_shared<source2root::sdktools::Service>(
            HostContext().PluginHandle(),
            Require<KeelEntitiesApi>(KEELS2_ENTITIES_SERVICE_NAME, KEELS2_ENTITIES_API_VERSION),
            Require<KeelSchemaApi>(KEELS2_SCHEMA_SERVICE_NAME, KEELS2_SCHEMA_API_VERSION),
            Require<KeelPlayersApi>(KEELS2_PLAYERS_SERVICE_NAME, KEELS2_PLAYERS_API_VERSION),
            Require<KeelNativeRuntimeApi>(KEELS2_NATIVE_RUNTIME_SERVICE_NAME, KEELS2_NATIVE_RUNTIME_API_VERSION),
            OptionalWrites()));

        const void* round = nullptr;
        const auto round_status =
            HostContext().QueryService(KEELS2_ROUND_CONTROL_SERVICE_NAME, KEELS2_ROUND_CONTROL_API_VERSION, &round);

        if (round_status != KEEL_RESULT_OK && round_status != KEEL_RESULT_NOT_FOUND && round_status != KEEL_RESULT_UNSUPPORTED)
            throw cs::Error("Could not query round control service.");

        if (round_status == KEEL_RESULT_OK && !round)
            throw cs::Error("Host returned an empty round control service.");

        rounds_ = std::make_unique<cs::Rounds>(HostContext().PluginHandle(),
            Require<KeelNativeRuntimeApi>(KEELS2_NATIVE_RUNTIME_SERVICE_NAME,KEELS2_NATIVE_RUNTIME_API_VERSION),
            round_status == KEEL_RESULT_OK ? static_cast<const KeelRoundControlApi*>(round) : nullptr);

        return RegisterNative("CS_GetCapabilities", 1, &CounterStrike::Capabilities)
            && RegisterNative("CS_RespawnPlayer", 1, &CounterStrike::Respawn)
            && RegisterNative("CS_ChangeTeam", 2, &CounterStrike::ChangeTeam)
            && RegisterNative("CS_SwitchTeam", 2, &CounterStrike::SwitchTeam)
            && RegisterNative("CS_GetPlayerScore", 2, &CounterStrike::GetScore)
            && RegisterNative("CS_SetPlayerScore", 2, &CounterStrike::SetScore)
            && RegisterNative("CS_GetPlayerMVPs", 2, &CounterStrike::GetMVPs)
            && RegisterNative("CS_SetPlayerMVPs", 2, &CounterStrike::SetMVPs)
            && RegisterNative("CS_GetRoundCapabilities", 1, &CounterStrike::RoundCapabilities)
            && RegisterNative("CS_TerminateRound", 3, &CounterStrike::TerminateRound)
            && RegisterNative("CS_GetPlayerStatisticsCapabilities", 2, &CounterStrike::StatisticsCapabilities)
            && RegisterNative("CS_GetPlayerMoney", 2, &CounterStrike::GetMoney)
            && RegisterNative("CS_SetPlayerMoney", 2, &CounterStrike::SetMoney)
            && RegisterNative("CS_GetPlayerKills", 2, &CounterStrike::GetKills)
            && RegisterNative("CS_SetPlayerKills", 2, &CounterStrike::SetKills)
            && RegisterNative("CS_GetPlayerDeaths", 2, &CounterStrike::GetDeaths)
            && RegisterNative("CS_SetPlayerDeaths", 2, &CounterStrike::SetDeaths)
            && RegisterNative("CS_GetPlayerAssists", 2, &CounterStrike::GetAssists)
            && RegisterNative("CS_SetPlayerAssists", 2, &CounterStrike::SetAssists);
    }

    template <typename Function> static std::int32_t Invoke(NativeCall& call, Function function) {
        try {
            function();
            return 1;
        } catch (const cs::Error& error) {
            return call.Fail(error.what());
        }
    }

    std::int32_t Capabilities(NativeCall& call) {
        call.OutputCell(1, 0);
        return Invoke(call, [&] {
            call.OutputCell(1, players_->Capabilities());
        });
    }

    std::int32_t Action(NativeCall& call, unsigned kind, int team) {
        return Invoke(call, [&] {
            SrPlayerIdentity player{};

            if (!call.Player(call.Int(1), player))
                throw cs::Error("Player connection is no longer available.");

            players_->Apply({player.slot, 0, player.connection}, kind, team);
        });
    }

    std::int32_t Respawn(NativeCall& call) {
        return Action(call, KEELS2_PLAYER_MANAGEMENT_RESPAWN, 0);
    }

    std::int32_t ChangeTeam(NativeCall& call) {
        return Action(call, KEELS2_PLAYER_MANAGEMENT_CHANGE_TEAM, call.Int(2));
    }

    std::int32_t SwitchTeam(NativeCall& call) {
        return Action(call, KEELS2_PLAYER_MANAGEMENT_SWITCH_TEAM, call.Int(2));
    }

    std::int32_t Stat(NativeCall& call, cs::Statistic statistic, bool write) {
        if (!write)
            call.OutputCell(2, 0);

        return Invoke(call, [&] {
            SrPlayerIdentity player{};

            if (!call.Player(call.Int(1), player))
                throw cs::Error("Player connection is no longer available.");

            const KeelPlayerConnection connection{player.slot, 0, player.connection};

            if (write)
                statistics_->Set(connection, statistic, call.Int(2));
            else
                call.OutputCell(2, statistics_->Get(connection, statistic));
        });
    }

    std::int32_t GetScore(NativeCall& call) {
        return Stat(call, cs::Statistic::score, false);
    }

    std::int32_t SetScore(NativeCall& call) {
        return Stat(call, cs::Statistic::score, true);
    }

    std::int32_t GetMVPs(NativeCall& call) {
        return Stat(call, cs::Statistic::mvps, false);
    }

    std::int32_t SetMVPs(NativeCall& call) {
        return Stat(call, cs::Statistic::mvps, true);
    }

    std::int32_t RoundCapabilities(NativeCall& call) {
        call.OutputCell(1,0);
        return Invoke(call, [&] {
            call.OutputCell(1, rounds_->Capabilities());
        });
    }

    std::int32_t TerminateRound(NativeCall& call) {
        return Invoke(call, [&] {
            rounds_->Terminate(call.Float(1), call.Int(2), call.Int(3));
        });
    }

    std::int32_t StatisticsCapabilities(NativeCall& call) {
        call.OutputCell(1, 0);
        call.OutputCell(2, 0);
        return Invoke(call,[&] {
            const auto [readable,writable] = players_->StatisticsCapabilities();
            call.OutputCell(1, readable);
            call.OutputCell(2, writable);
        });
    }

    std::int32_t ComponentStat(NativeCall& call, unsigned key, bool write) {
        if (!write)
            call.OutputCell(2, 0);

        return Invoke(call,[&] {
            SrPlayerIdentity player{};

            if (!call.Player(call.Int(1), player))
                throw cs::Error("Player connection is no longer available.");

            const KeelPlayerConnection connection{player.slot,0,player.connection};

            if (write)
                players_->SetStatistic(connection, key, call.Int(2));
            else
                call.OutputCell(2, players_->GetStatistic(connection, key));
        });
    }

    std::int32_t GetMoney(NativeCall& call) {
        return ComponentStat(call, KEELS2_PLAYER_STAT_MONEY, false);
    }

    std::int32_t SetMoney(NativeCall& call) {
        return ComponentStat(call, KEELS2_PLAYER_STAT_MONEY, true);
    }

    std::int32_t GetKills(NativeCall& call) {
        return ComponentStat(call, KEELS2_PLAYER_STAT_MATCH_KILLS, false);
    }

    std::int32_t SetKills(NativeCall& call) {
        return ComponentStat(call, KEELS2_PLAYER_STAT_MATCH_KILLS, true);
    }

    std::int32_t GetDeaths(NativeCall& call) {
        return ComponentStat(call, KEELS2_PLAYER_STAT_MATCH_DEATHS, false);
    }

    std::int32_t SetDeaths(NativeCall& call) {
        return ComponentStat(call, KEELS2_PLAYER_STAT_MATCH_DEATHS, true);
    }

    std::int32_t GetAssists(NativeCall& call) {
        return ComponentStat(call, KEELS2_PLAYER_STAT_MATCH_ASSISTS, false);
    }

    std::int32_t SetAssists(NativeCall& call) {
        return ComponentStat(call, KEELS2_PLAYER_STAT_MATCH_ASSISTS, true);
    }
};
}

extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Query(const KeelHostQuery* query, KeelPluginInfo* info) {
    if (!query || query->size != sizeof(*query) || !query->game || std::strcmp(query->game, "cs2"))
        return KEEL_FALSE;

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
