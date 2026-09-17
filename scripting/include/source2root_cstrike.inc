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
// These capability bits describe respawn/team actions only, not statistics.
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

// Read CS2's CCSPlayerController.m_iScore / m_iMVPs as signed 32-bit fields.
// Output is zero on failure; GetLastError supplies the reason. These are the
// current controller values, not account-wide totals or round award events.
// Requires keels2.entities/schema/players APIs 1. Resolves schema for each call.
native bool CS_GetPlayerScore(Player player, int &score);
native bool CS_GetPlayerMVPs(Player player, int &mvps);
// Requires optional keels2.entity_writes API 1 and an admitted game build.
// Score accepts any signed int; MVP count must be nonnegative. Direct field
// mutation plus network notification does not award achievements, fire an MVP
// event or invoke scoring rules. Game logic may subsequently overwrite values.
// Success means write/notification dispatched, not verified client replication.
// A failed notification can follow a completed write; no rollback is promised.
native bool CS_SetPlayerScore(Player player, int score);
native bool CS_SetPlayerMVPs(Player player, int mvps);
