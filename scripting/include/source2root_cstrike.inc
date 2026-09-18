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

enum CSRoundEndReason
{
    CSRound_TargetBombed = 1,
    CSRound_TerroristsEscaped = 4,
    CSRound_CTsPreventEscape = 5,
    CSRound_EscapingTerroristsNeutralized = 6,
    CSRound_BombDefused = 7,
    CSRound_CTsWin = 8,
    CSRound_TerroristsWin = 9,
    CSRound_Draw = 10,
    CSRound_AllHostagesRescued = 11,
    CSRound_TargetSaved = 12,
    CSRound_HostagesNotRescued = 13,
    CSRound_TerroristsNotEscaped = 14,
    CSRound_GameCommencing = 16,
    CSRound_TerroristsSurrender = 17,
    CSRound_CTsSurrender = 18,
    CSRound_TerroristsPlanted = 19,
    CSRound_CTsReachedHostage = 20,
    CSRound_SurvivalWin = 21,
    CSRound_SurvivalDraw = 22
};
#define CS_ROUND_CAP_TERMINATE 1
// Optional keels2.round_control API 1; absent service leaves player APIs usable.
// Returns binary support, not current map/round readiness. Output zero on error.
native bool CS_GetRoundCapabilities(int &capabilities);
// Dispatch the game's round termination routine for the current map. Delay is
// finite 0..3600 seconds. Reason identifiers are CS2 values, not Source1 enums.
// winningTeam is 0 (game chooses), 2 (T) or 3 (CT); arbitrary survival squad IDs
// are not supported. Select a reason appropriate for the current game mode.
// Engine callbacks may replace map/round state. Success means dispatch, not a
// completed transition. Repeated calls/game logic may supersede the result.
// No OnTerminateRound forward or blockhook option is provided by this API.
native bool CS_TerminateRound(float delay, CSRoundEndReason reason, int winningTeam = 0);

#define CS_STAT_MONEY 1
#define CS_STAT_MATCH_KILLS 2
#define CS_STAT_MATCH_DEATHS 4
#define CS_STAT_MATCH_ASSISTS 8
// Optional keels2.player_statistics API 1. Masks cover only the four component
// statistics below, not score/MVPs. Binary support does not imply map readiness.
// Both masks are zero on failure; existing player/round APIs remain usable when
// this optional host service is absent.
native bool CS_GetPlayerStatisticsCapabilities(int &readable, int &writable);
// Current in-game money and match totals from the controller's owned services.
// Getter output is zero on failure. Player connection/engine identity is checked
// around reads; stale Player handles cannot target replacement connections.
native bool CS_GetPlayerMoney(Player player, int &money);
native bool CS_GetPlayerKills(Player player, int &kills);
native bool CS_GetPlayerDeaths(Player player, int &deaths);
native bool CS_GetPlayerAssists(Player player, int &assists);
// Direct stored-value writes plus network notification. Values must be >=0.
// Scripts must respect game-mode limits such as mp_maxmoney; these setters do
// not clamp to that ConVar, perform transactions or simulate gameplay events.
// They do not award kill/assist credit, update every related aggregate or grant
// achievements. Game rules can overwrite them; notification failure can follow
// a completed write. Client-visible replication still requires game acceptance.
native bool CS_SetPlayerMoney(Player player, int money);
native bool CS_SetPlayerKills(Player player, int kills);
native bool CS_SetPlayerDeaths(Player player, int deaths);
native bool CS_SetPlayerAssists(Player player, int assists);
