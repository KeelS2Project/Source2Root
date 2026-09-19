#include <source2root_cstrike>
Player Saved;

public bool OnPluginStart()
{
    int capabilities;
    Player players[4];

    if (!CS_GetCapabilities(capabilities) || capabilities != 7 || GetPlayers(players, sizeof(players)) != 1)
        return false;

    Saved = players[0];
    LogMessage("CSTRIKE_SCRIPT_OK");
    return RegisterCommand("sr_cs_check", "", CheckActions) && RegisterCommand("sr_cs_invalid", "", Invalid)
        && RegisterCommand("sr_cs_unavailable", "", Unavailable) && RegisterCommand("sr_cs_caps_error", "", CapsError)
        && RegisterCommand("sr_cs_action_error", "", ActionError) && RegisterCommand("sr_cs_reconnected", "", Reconnected)
        && RegisterCommand("sr_cs_stats", "", Statistics) && RegisterCommand("sr_cs_stats_unavailable", "", StatsUnavailable)
        && RegisterCommand("sr_cs_stats_error", "", StatsError)
        && RegisterCommand("sr_cs_round", "", Round) && RegisterCommand("sr_cs_round_unavailable", "", RoundUnavailable)
        && RegisterCommand("sr_cs_round_caps_error", "", RoundCapsError) && RegisterCommand("sr_cs_round_error", "", RoundError)
        && RegisterCommand("sr_cs_components", "", Components)
        && RegisterCommand("sr_cs_components_readonly", "", ComponentsReadOnly)
        && RegisterCommand("sr_cs_components_caps_error", "", ComponentsCapsError)
        && RegisterCommand("sr_cs_components_read_error", "", ComponentsReadError)
        && RegisterCommand("sr_cs_components_write_error", "", ComponentsWriteError)
        && RegisterCommand("sr_cs_components_restore", "", ComponentsRestore);
}

public void CheckActions(Player caller, const char[] arguments)
{
    if (!CS_RespawnPlayer(Saved) || !CS_ChangeTeam(Saved, CS_Terrorist) || !CS_SwitchTeam(Saved, CS_CounterTerrorist))
        LogMessage("CSTRIKE_FAILED actions");
    else
        LogMessage("CSTRIKE_ACTIONS_OK");
}

public void Invalid(Player caller, const char[] arguments)
{
    int score = 99;

    if (CS_ChangeTeam(Saved, view_as<CSTeam>(99)) || CS_SwitchTeam(Saved, view_as<CSTeam>(0)) ||
        CS_SwitchTeam(Saved, CS_Spectator) || CS_RespawnPlayer(NoPlayer))
        LogMessage("CSTRIKE_FAILED invalid input");
    else if (CS_SetPlayerMVPs(Saved, -1) || CS_SetPlayerScore(NoPlayer, 1) || CS_GetPlayerScore(NoPlayer, score) ||
             score != 0)
        LogMessage("CSTRIKE_FAILED invalid statistic");
    else if (CS_SetPlayerMoney(Saved, -1) || CS_SetPlayerKills(Saved, -1) || CS_SetPlayerDeaths(Saved, -1)
        || CS_SetPlayerAssists(Saved, -1) || CS_SetPlayerMoney(NoPlayer, 1)
        || CS_GetPlayerMoney(NoPlayer, score) || score != 0) LogMessage("CSTRIKE_FAILED invalid component statistic");
    else if (CS_TerminateRound(-1.0, CSRound_Draw) || CS_TerminateRound(0.0, view_as<CSRoundEndReason>(0))
        || CS_TerminateRound(0.0, CSRound_Draw, 1)) LogMessage("CSTRIKE_FAILED invalid round");
    else
        LogMessage("CSTRIKE_INVALID_OK");
}

public void Unavailable(Player caller, const char[] arguments)
{
    int capabilities = 99;

    if (!CS_GetCapabilities(capabilities) || capabilities != 0 || CS_RespawnPlayer(Saved))
        LogMessage("CSTRIKE_FAILED unsupported action");
    else
        LogMessage("CSTRIKE_UNAVAILABLE_OK");
}

public void CapsError(Player caller, const char[] arguments)
{
    int capabilities = 99;
    char error[256];

    if (CS_GetCapabilities(capabilities) || capabilities != 0)
    {
        LogMessage("CSTRIKE_FAILED capability result");
        return;
    }

    GetLastError(error, sizeof(error));
    LogMessage(error[0] ? "CSTRIKE_CAPS_ERROR_OK" : "CSTRIKE_FAILED missing capability error");
}

public void ActionError(Player caller, const char[] arguments)
{
    char error[256];

    if (CS_RespawnPlayer(Saved))
    {
        LogMessage("CSTRIKE_FAILED engine result");
        return;
    }

    GetLastError(error, sizeof(error));
    LogMessage(error[0] ? "CSTRIKE_ACTION_ERROR_OK" : "CSTRIKE_FAILED missing engine error");
}

public void Reconnected(Player caller, const char[] arguments)
{
    if (CS_RespawnPlayer(Saved))
    {
        LogMessage("CSTRIKE_FAILED stale player");
        return;
    }

    int score = 99;

    if (CS_GetPlayerScore(Saved, score) || score != 0 || CS_SetPlayerMVPs(Saved, 1))
    {
        LogMessage("CSTRIKE_FAILED stale statistic player");
        return;
    }

    if (CS_GetPlayerMoney(Saved, score) || score != 0 || CS_SetPlayerMoney(Saved, 1))
    {
        LogMessage("CSTRIKE_FAILED stale component player");
        return;
    }

    Player players[4];

    if (GetPlayers(players, sizeof(players)) != 1)
    {
        LogMessage("CSTRIKE_FAILED new player");
        return;
    }

    Saved = players[0];
    LogMessage("CSTRIKE_RECONNECTED_OK");
}

public void Statistics(Player caller, const char[] arguments)
{
    int score, mvps;

    if (!CS_GetPlayerScore(Saved, score) || score != 12 || !CS_GetPlayerMVPs(Saved, mvps) || mvps != 2 ||
        !CS_SetPlayerScore(Saved, -7) || !CS_SetPlayerMVPs(Saved, 9) || !CS_GetPlayerScore(Saved, score) ||
        score != -7 || !CS_GetPlayerMVPs(Saved, mvps) || mvps != 9 || !CS_SetPlayerScore(Saved, 12) ||
        !CS_SetPlayerMVPs(Saved, 2))
        LogMessage("CSTRIKE_FAILED statistic mapping or write");
    else
        LogMessage("CSTRIKE_STATS_OK");
}

public void StatsUnavailable(Player caller, const char[] arguments)
{
    int score;

    if (!CS_GetPlayerScore(Saved, score) || score != 12 || CS_SetPlayerScore(Saved, 13) || CS_SetPlayerMVPs(Saved, 3))
        LogMessage("CSTRIKE_FAILED unsupported statistic write");
    else
        LogMessage("CSTRIKE_STATS_UNAVAILABLE_OK");
}

public void StatsError(Player caller, const char[] arguments)
{
    int score;
    char error[256];

    if (CS_SetPlayerScore(Saved, 13))
    {
        LogMessage("CSTRIKE_FAILED statistic write error");
        return;
    }

    GetLastError(error, sizeof(error));

    if (!error[0] || !CS_GetPlayerScore(Saved, score) || score != 12)
        LogMessage("CSTRIKE_FAILED statistic error propagation");
    else
        LogMessage("CSTRIKE_STATS_ERROR_OK");
}

public void Round(Player caller, const char[] arguments)
{
    int mask;

    if (!CS_GetRoundCapabilities(mask) || mask != 1 || !CS_TerminateRound(0.0, CSRound_Draw)
        || !CS_TerminateRound(2.5, CSRound_CTsWin, 3) || !CS_TerminateRound(3600.0, CSRound_TerroristsWin, 2))
        LogMessage("CSTRIKE_FAILED round dispatch");
    else
        LogMessage("CSTRIKE_ROUND_OK");
}

public void RoundUnavailable(Player caller, const char[] arguments)
{
    int mask = 99;

    if (!CS_GetRoundCapabilities(mask) || mask != 0 || CS_TerminateRound(0.0, CSRound_Draw))
        LogMessage("CSTRIKE_FAILED round unavailable");
    else
        LogMessage("CSTRIKE_ROUND_UNAVAILABLE_OK");
}

public void RoundCapsError(Player caller, const char[] arguments)
{
    int mask = 99;
    char error[256];

    if (CS_GetRoundCapabilities(mask) || mask != 0)
    {
        LogMessage("CSTRIKE_FAILED round capability result");
        return;
    }

    GetLastError(error, sizeof(error));
    LogMessage(error[0] ? "CSTRIKE_ROUND_CAPS_ERROR_OK" : "CSTRIKE_FAILED round capability error");
}

public void RoundError(Player caller, const char[] arguments)
{
    char error[256];

    if (CS_TerminateRound(0.0, CSRound_Draw))
    {
        LogMessage("CSTRIKE_FAILED round engine result");
        return;
    }

    GetLastError(error, sizeof(error));
    LogMessage(error[0] ? "CSTRIKE_ROUND_ERROR_OK" : "CSTRIKE_FAILED round engine error");
}

public void Components(Player caller, const char[] arguments)
{
    int readable, writable, money, kills, deaths, assists;

    if (!CS_GetPlayerStatisticsCapabilities(readable, writable) || readable != 15 || writable != 15 ||
        !CS_GetPlayerMoney(Saved, money) || money != 800 || !CS_GetPlayerKills(Saved, kills) || kills != 7 ||
        !CS_GetPlayerDeaths(Saved, deaths) || deaths != 3 || !CS_GetPlayerAssists(Saved, assists) || assists != 2 ||
        !CS_SetPlayerMoney(Saved, 16000) || !CS_SetPlayerKills(Saved, 19) || !CS_SetPlayerDeaths(Saved, 11) ||
        !CS_SetPlayerAssists(Saved, 5) || !CS_GetPlayerMoney(Saved, money) || money != 16000 ||
        !CS_GetPlayerKills(Saved, kills) || kills != 19 || !CS_GetPlayerDeaths(Saved, deaths) || deaths != 11 ||
        !CS_GetPlayerAssists(Saved, assists) || assists != 5 || !CS_SetPlayerMoney(Saved, 800) ||
        !CS_SetPlayerKills(Saved, 7) || !CS_SetPlayerDeaths(Saved, 3) || !CS_SetPlayerAssists(Saved, 2))
        LogMessage("CSTRIKE_FAILED component mapping or write");
    else
        LogMessage("CSTRIKE_COMPONENTS_OK");
}

public void ComponentsReadOnly(Player caller, const char[] arguments)
{
    int readable, writable, money;

    if (!CS_GetPlayerStatisticsCapabilities(readable, writable) || readable != 15 || writable != 0
        || !CS_GetPlayerMoney(Saved, money) || money != 800 || CS_SetPlayerMoney(Saved, 900)
        || CS_SetPlayerKills(Saved, 8) || CS_SetPlayerDeaths(Saved, 4) || CS_SetPlayerAssists(Saved, 3))
        LogMessage("CSTRIKE_FAILED component read-only behavior");
    else
        LogMessage("CSTRIKE_COMPONENTS_READONLY_OK");
}

public void ComponentsCapsError(Player caller, const char[] arguments)
{
    int readable = 99, writable = 99;
    char error[256];

    if (CS_GetPlayerStatisticsCapabilities(readable, writable) || readable != 0 || writable != 0)
    {
        LogMessage("CSTRIKE_FAILED component capability result");
        return;
    }

    GetLastError(error, sizeof(error));
    LogMessage(error[0] ? "CSTRIKE_COMPONENTS_CAPS_ERROR_OK" : "CSTRIKE_FAILED component capability error");
}

public void ComponentsReadError(Player caller, const char[] arguments)
{
    int money = 99;
    char error[256];

    if (CS_GetPlayerMoney(Saved, money) || money != 0)
    {
        LogMessage("CSTRIKE_FAILED component read error");
        return;
    }

    GetLastError(error, sizeof(error));
    LogMessage(error[0] ? "CSTRIKE_COMPONENTS_READ_ERROR_OK" : "CSTRIKE_FAILED missing component read error");
}

public void ComponentsWriteError(Player caller, const char[] arguments)
{
    int money;
    char error[256];

    if (CS_SetPlayerMoney(Saved, 900))
    {
        LogMessage("CSTRIKE_FAILED component notification result");
        return;
    }

    GetLastError(error, sizeof(error));

    if (!error[0] || !CS_GetPlayerMoney(Saved, money) || money != 900)
        LogMessage("CSTRIKE_FAILED component notification error");
    else
        LogMessage("CSTRIKE_COMPONENTS_WRITE_ERROR_OK");
}

public void ComponentsRestore(Player caller, const char[] arguments)
{
    if (!CS_SetPlayerMoney(Saved, 800))
        LogMessage("CSTRIKE_FAILED component restore");
    else
        LogMessage("CSTRIKE_COMPONENTS_RESTORE_OK");
}
