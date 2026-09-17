#include "players.h"
#include <keels2/detail/authoring_status.hpp>
#include <string>

namespace source2root::cstrike {
namespace {
void Check(KeelResult result, const char* operation) {
    if (result != KEEL_RESULT_OK) throw Error(std::string(operation) + ": " + keels2::detail::ResultDescription(result) + ".");
}
bool Identity(const KeelEntityInfo& info, unsigned source) {
    return info.size == sizeof(info) && info.index >= 0 && !info.reserved && info.epoch &&
        info.source2_handle == source && source != KEELS2_INVALID_SOURCE2_ENTITY_HANDLE;
}
}
Players::Players(KeelPluginHandle plugin, const KeelEntitiesApi& entities, const KeelPlayersApi& players,
    const KeelNativeRuntimeApi& runtime, const KeelPlayerManagementApi& management)
    : plugin_(plugin), entities_(entities), players_(players), runtime_(runtime), management_(management) {
    if (!plugin || entities.size != sizeof(entities) || entities.api_version != KEELS2_ENTITIES_API_VERSION ||
        !entities.find_by_source2_handle || !entities.release || !entities.describe ||
        players.size != sizeof(players) || players.api_version != KEELS2_PLAYERS_API_VERSION || !players.validate_connection ||
        runtime.size != sizeof(runtime) || runtime.api_version != KEELS2_NATIVE_RUNTIME_API_VERSION || !runtime.check_game_thread ||
        management.size != sizeof(management) || management.api_version != KEELS2_PLAYER_MANAGEMENT_API_VERSION ||
        !management.capabilities || !management.apply) throw Error("Incompatible player management services.");
}
void Players::Thread() const { Check(runtime_.check_game_thread(plugin_), "Counter-Strike operation"); }
unsigned Players::Capabilities() const {
    Thread();
    unsigned capabilities = 0;
    Check(management_.capabilities(plugin_, &capabilities), "Counter-Strike capabilities");
    // Other capabilities can be added to the host independently of this facade.
    return capabilities & (KEELS2_PLAYER_MANAGEMENT_RESPAWN | KEELS2_PLAYER_MANAGEMENT_CHANGE_TEAM |
        KEELS2_PLAYER_MANAGEMENT_SWITCH_TEAM);
}
KeelPlayerInfo Players::Player(const KeelPlayerConnection& connection) const {
    if (connection.slot < 0 || connection.reserved || !connection.generation) throw Error("Player connection is invalid.");
    KeelPlayerInfo player{}; player.size = sizeof(player);
    Check(players_.validate_connection(plugin_, &connection, &player), "Validate player connection");
    if (player.size != sizeof(player) || player.reserved || player.slot != connection.slot ||
        player.connection != connection.generation || !(player.flags & KEELS2_PLAYER_CONNECTED) ||
        (player.flags & KEELS2_PLAYER_SOURCE_TV) || player.controller_handle == KEELS2_INVALID_SOURCE2_ENTITY_HANDLE)
        throw Error("Player connection is unavailable or has no controller.");
    return player;
}
void Players::Apply(const KeelPlayerConnection& connection, unsigned kind, int team) {
    Thread();
    if ((kind != KEELS2_PLAYER_MANAGEMENT_RESPAWN && kind != KEELS2_PLAYER_MANAGEMENT_CHANGE_TEAM &&
        kind != KEELS2_PLAYER_MANAGEMENT_SWITCH_TEAM) ||
        (kind == KEELS2_PLAYER_MANAGEMENT_RESPAWN ? team != 0 : team < (kind == KEELS2_PLAYER_MANAGEMENT_SWITCH_TEAM ? 2 : 1) || team > 3))
        throw Error("Expected respawn, a team from 1..3 for ChangeTeam, or T/CT (2..3) for SwitchTeam.");
    if (active_ >= 8) throw Error("Counter-Strike action recursion limit (8) reached.");
    struct Active { unsigned& count; explicit Active(unsigned& value) : count(value) { ++count; } ~Active() { --count; } } active(active_);
    if (!(Capabilities() & kind)) throw Error("This Counter-Strike action is unavailable for the current game build.");
    const auto before = Player(connection);
    KeelEntityHandle entity = 0;
    Check(entities_.find_by_source2_handle(plugin_, before.controller_handle, &entity), "Find player controller");
    if (!entity) throw Error("Host returned an empty player controller handle.");
    struct EntityHold {
        const KeelEntitiesApi& api; KeelPluginHandle plugin; KeelEntityHandle entity;
        ~EntityHold() { api.release(plugin, entity); }
    } hold{entities_, plugin_, entity};
    KeelEntityInfo first{}; first.size = sizeof(first);
    Check(entities_.describe(plugin_, entity, &first), "Describe player controller");
    if (!Identity(first, before.controller_handle)) throw Error("Host returned an inconsistent player controller.");
    const auto after = Player(connection);
    if (after.controller_handle != before.controller_handle) throw Error("Player controller changed during the action lookup.");
    KeelEntityInfo current{}; current.size = sizeof(current);
    Check(entities_.describe(plugin_, entity, &current), "Revalidate player controller");
    if (!Identity(current, before.controller_handle) || current.epoch != first.epoch || current.index != first.index)
        throw Error("Player controller identity changed during the action lookup.");
    const KeelPlayerManagementAction action{sizeof(action), kind, team, 0};
    Check(management_.apply(plugin_, entity, &action), "Counter-Strike player action");
    // Engine callbacks may deliberately change the connection/pawn. Do not
    // reinterpret a successfully dispatched action as failure after its effect.
}
}
