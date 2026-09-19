#pragma once
#include <keels2/entities.h>
#include <keels2/native_runtime.h>
#include <keels2/player_management.h>
#include <keels2/players.h>
#include <keels2/player_statistics.h>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <utility>

namespace source2root::cstrike {
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
// Uses only the public host C services. The extension query restricts this
// facade's team identifiers and semantics to CS2. Calls are game-thread only.
class Players final {
public:
    Players(KeelPluginHandle plugin, const KeelEntitiesApi& entities, const KeelPlayersApi& players,
        const KeelNativeRuntimeApi& runtime, const KeelPlayerManagementApi& management,
        const KeelPlayerStatisticsApi* statistics = nullptr);

    Players(const Players&) = delete;
    Players& operator=(const Players&) = delete;
    unsigned Capabilities() const;
    void Apply(const KeelPlayerConnection& player, unsigned kind, int team);
    std::pair<unsigned,unsigned> StatisticsCapabilities() const;
    std::int32_t GetStatistic(const KeelPlayerConnection& player, unsigned key);
    void SetStatistic(const KeelPlayerConnection& player, unsigned key, std::int32_t value);

private:
    void Controller(const KeelPlayerConnection& connection,
                    const std::function<void(KeelEntityHandle)>& operation,
                    bool validate_after);

    KeelPlayerInfo Player(const KeelPlayerConnection& connection) const;
    void Thread() const;
    KeelPluginHandle plugin_;
    const KeelEntitiesApi entities_;
    const KeelPlayersApi players_;
    const KeelNativeRuntimeApi runtime_;
    const KeelPlayerManagementApi management_;
    const KeelPlayerStatisticsApi statistics_;
    unsigned active_ = 0;
};
}
