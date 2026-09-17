#include <source2root_cstrike>
Player Saved;
public bool OnPluginStart()
{
    int capabilities;
    Player players[4];
    if (!CS_GetCapabilities(capabilities) || capabilities != 7 || GetPlayers(players, sizeof(players)) != 1) return false;
    Saved = players[0];
    LogMessage("CSTRIKE_SCRIPT_OK");
    return RegisterCommand("sr_cs_check", "", CheckActions) && RegisterCommand("sr_cs_invalid", "", Invalid)
        && RegisterCommand("sr_cs_unavailable", "", Unavailable) && RegisterCommand("sr_cs_caps_error", "", CapsError)
        && RegisterCommand("sr_cs_action_error", "", ActionError) && RegisterCommand("sr_cs_reconnected", "", Reconnected)
        && RegisterCommand("sr_cs_stats", "", Statistics) && RegisterCommand("sr_cs_stats_unavailable", "", StatsUnavailable)
        && RegisterCommand("sr_cs_stats_error", "", StatsError)
        && RegisterCommand("sr_cs_round", "", Round) && RegisterCommand("sr_cs_round_unavailable", "", RoundUnavailable)
        && RegisterCommand("sr_cs_round_caps_error", "", RoundCapsError) && RegisterCommand("sr_cs_round_error", "", RoundError);
}
public void CheckActions(Player caller, const char[] arguments)
{
    if (!CS_RespawnPlayer(Saved) || !CS_ChangeTeam(Saved, CS_Terrorist) || !CS_SwitchTeam(Saved, CS_CounterTerrorist))
        LogMessage("CSTRIKE_FAILED actions");
    else LogMessage("CSTRIKE_ACTIONS_OK");
}
public void Invalid(Player caller, const char[] arguments)
{
    int score = 99;
    if (CS_ChangeTeam(Saved, view_as<CSTeam>(99)) || CS_SwitchTeam(Saved, view_as<CSTeam>(0)) || CS_SwitchTeam(Saved, CS_Spectator) || CS_RespawnPlayer(NoPlayer))
        LogMessage("CSTRIKE_FAILED invalid input");
    else if (CS_SetPlayerMVPs(Saved, -1) || CS_SetPlayerScore(NoPlayer, 1) || CS_GetPlayerScore(NoPlayer, score) || score != 0)
        LogMessage("CSTRIKE_FAILED invalid statistic");
    else if (CS_TerminateRound(-1.0, CSRound_Draw) || CS_TerminateRound(0.0, view_as<CSRoundEndReason>(0))
        || CS_TerminateRound(0.0, CSRound_Draw, 1)) LogMessage("CSTRIKE_FAILED invalid round");
    else LogMessage("CSTRIKE_INVALID_OK");
}
public void Unavailable(Player caller, const char[] arguments)
{
    int capabilities = 99;
    if (!CS_GetCapabilities(capabilities) || capabilities != 0 || CS_RespawnPlayer(Saved)) LogMessage("CSTRIKE_FAILED unsupported action");
    else LogMessage("CSTRIKE_UNAVAILABLE_OK");
}
public void CapsError(Player caller, const char[] arguments)
{
    int capabilities = 99;
    char error[256];
    if (CS_GetCapabilities(capabilities) || capabilities != 0) { LogMessage("CSTRIKE_FAILED capability result"); return; }
    GetLastError(error, sizeof(error));
    LogMessage(error[0] ? "CSTRIKE_CAPS_ERROR_OK" : "CSTRIKE_FAILED missing capability error");
}
public void ActionError(Player caller, const char[] arguments)
{
    char error[256];
    if (CS_RespawnPlayer(Saved)) { LogMessage("CSTRIKE_FAILED engine result"); return; }
    GetLastError(error, sizeof(error));
    LogMessage(error[0] ? "CSTRIKE_ACTION_ERROR_OK" : "CSTRIKE_FAILED missing engine error");
}
public void Reconnected(Player caller, const char[] arguments)
{
    if (CS_RespawnPlayer(Saved)) { LogMessage("CSTRIKE_FAILED stale player"); return; }
    int score = 99;
    if (CS_GetPlayerScore(Saved, score) || score != 0 || CS_SetPlayerMVPs(Saved, 1)) { LogMessage("CSTRIKE_FAILED stale statistic player"); return; }
    Player players[4];
    if (GetPlayers(players, sizeof(players)) != 1) { LogMessage("CSTRIKE_FAILED new player"); return; }
    Saved = players[0];
    LogMessage("CSTRIKE_RECONNECTED_OK");
}
public void Statistics(Player caller, const char[] arguments)
{
    int score, mvps;
    if (!CS_GetPlayerScore(Saved, score) || score != 12 || !CS_GetPlayerMVPs(Saved, mvps) || mvps != 2
        || !CS_SetPlayerScore(Saved, -7) || !CS_SetPlayerMVPs(Saved, 9)
        || !CS_GetPlayerScore(Saved, score) || score != -7 || !CS_GetPlayerMVPs(Saved, mvps) || mvps != 9
        || !CS_SetPlayerScore(Saved, 12) || !CS_SetPlayerMVPs(Saved, 2)) LogMessage("CSTRIKE_FAILED statistic mapping or write");
    else LogMessage("CSTRIKE_STATS_OK");
}
public void StatsUnavailable(Player caller, const char[] arguments)
{
    int score;
    if (!CS_GetPlayerScore(Saved, score) || score != 12 || CS_SetPlayerScore(Saved, 13) || CS_SetPlayerMVPs(Saved, 3))
        LogMessage("CSTRIKE_FAILED unsupported statistic write");
    else LogMessage("CSTRIKE_STATS_UNAVAILABLE_OK");
}
public void StatsError(Player caller, const char[] arguments)
{
    int score;
    char error[256];
    if (CS_SetPlayerScore(Saved, 13)) { LogMessage("CSTRIKE_FAILED statistic write error"); return; }
    GetLastError(error, sizeof(error));
    if (!error[0] || !CS_GetPlayerScore(Saved, score) || score != 12) LogMessage("CSTRIKE_FAILED statistic error propagation");
    else LogMessage("CSTRIKE_STATS_ERROR_OK");
}

public void Round(Player caller, const char[] arguments)
{
    int mask;
    if (!CS_GetRoundCapabilities(mask) || mask != 1 || !CS_TerminateRound(0.0, CSRound_Draw)
        || !CS_TerminateRound(2.5, CSRound_CTsWin, 3) || !CS_TerminateRound(3600.0, CSRound_TerroristsWin, 2))
        LogMessage("CSTRIKE_FAILED round dispatch");
    else LogMessage("CSTRIKE_ROUND_OK");
}
public void RoundUnavailable(Player caller, const char[] arguments)
{
    int mask = 99;
    if (!CS_GetRoundCapabilities(mask) || mask != 0 || CS_TerminateRound(0.0, CSRound_Draw)) LogMessage("CSTRIKE_FAILED round unavailable");
    else LogMessage("CSTRIKE_ROUND_UNAVAILABLE_OK");
}
public void RoundCapsError(Player caller, const char[] arguments)
{
    int mask = 99;
    char error[256];
    if (CS_GetRoundCapabilities(mask) || mask != 0) { LogMessage("CSTRIKE_FAILED round capability result"); return; }
    GetLastError(error, sizeof(error));
    LogMessage(error[0] ? "CSTRIKE_ROUND_CAPS_ERROR_OK" : "CSTRIKE_FAILED round capability error");
}
public void RoundError(Player caller, const char[] arguments)
{
    char error[256];
    if (CS_TerminateRound(0.0, CSRound_Draw)) { LogMessage("CSTRIKE_FAILED round engine result"); return; }
    GetLastError(error, sizeof(error));
    LogMessage(error[0] ? "CSTRIKE_ROUND_ERROR_OK" : "CSTRIKE_FAILED round engine error");
}
