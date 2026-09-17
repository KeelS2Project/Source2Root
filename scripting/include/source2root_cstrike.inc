#if defined _source2root_cstrike_included
 #endinput
#endif
#define _source2root_cstrike_included
#include <source2root>

enum CSTeam { CS_Spectator = 1, CS_Terrorist = 2, CS_CounterTerrorist = 3 };
#define CS_CAP_RESPAWN 1
#define CS_CAP_CHANGE_TEAM 2
#define CS_CAP_SWITCH_TEAM 4

// Requires the source2root.cstrike native provider and a host with
// keels2.player_management API 1. CS2 only. Exact binary support is checked by
// KeelS2; unsupported builds return false and set the extension error.
// Capability support does not guarantee a player or map is ready for an action.
native bool CS_GetCapabilities(int &capabilities);
// Respawn requires a current combat pawn and a T/CT controller. The player is
// rebound to that pawn before the game respawn operation. Success means the
// operation was dispatched; engine callbacks may change the pawn/connection.
native bool CS_RespawnPlayer(Player player);
// Uses the game-rules team change, which may kill the player or remove weapons.
// Only spectator, T and CT are admitted. Player handles retain their connection
// generation; old handles cannot target a replacement connection in that slot.
native bool CS_ChangeTeam(Player player, CSTeam team);
// Uses CS2's direct team-switch operation, accepting T/CT only. Use ChangeTeam
// to move to spectator. Does not promise preservation of
// weapons, money or life state across all game modes; check the resulting state.
native bool CS_SwitchTeam(Player player, CSTeam team);
