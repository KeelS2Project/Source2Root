#include <source2root_cstrike>

public bool OnPluginStart()
{
    return RegisterCommand("sr_cs_capabilities", "", Capabilities, "Show supported Counter-Strike player actions")
        && RegisterCommand("sr_cs_respawn_self", "admin.slay", RespawnSelf, "Respawn your current T/CT pawn")
        && RegisterCommand("sr_cs_stats", "", Statistics, "Show your current CS2 score and MVP count")
        && RegisterCommand("sr_cs_round_capabilities", "", RoundCapabilities, "Show round control support")
        && RegisterCommand("sr_cs_draw", "admin.map", Draw, "End the current round as a draw with a five-second delay")
        && RegisterCommand("sr_cs_match_stats", "", MatchStatistics, "Show your money and match kills/deaths/assists");
}
public void Capabilities(Player caller, const char[] arguments)
{
    int mask;
    char text[256];
    if (!CS_GetCapabilities(mask)) GetLastError(text, sizeof(text));
    else Format(text, sizeof(text), "CS2 actions: respawn=%d, team change=%d, team switch=%d",
        (mask & CS_CAP_RESPAWN) != 0, (mask & CS_CAP_CHANGE_TEAM) != 0, (mask & CS_CAP_SWITCH_TEAM) != 0);
    ReplyToCommand(caller, text);
}
public void RespawnSelf(Player caller, const char[] arguments)
{
    if (caller == NoPlayer) { ReplyToCommand(caller, "Use this command as a connected T/CT player."); return; }
    if (CS_RespawnPlayer(caller)) ReplyToCommand(caller, "Respawn dispatched.");
    else { char error[256]; GetLastError(error, sizeof(error)); ReplyToCommand(caller, error); }
}
public void Statistics(Player caller, const char[] arguments)
{
    if (caller == NoPlayer) { ReplyToCommand(caller, "Use this command as a connected player."); return; }
    int score, mvps;
    char text[256];
    if (!CS_GetPlayerScore(caller, score) || !CS_GetPlayerMVPs(caller, mvps)) GetLastError(text, sizeof(text));
    else Format(text, sizeof(text), "CS2 score=%d, MVPs=%d", score, mvps);
    ReplyToCommand(caller, text);
}
public void RoundCapabilities(Player caller, const char[] arguments)
{
    int mask;
    char text[256];
    if (!CS_GetRoundCapabilities(mask)) GetLastError(text, sizeof(text));
    else Format(text, sizeof(text), "CS2 round termination supported=%d", (mask & CS_ROUND_CAP_TERMINATE) != 0);
    ReplyToCommand(caller, text);
}
public void Draw(Player caller, const char[] arguments)
{
    if (CS_TerminateRound(5.0, CSRound_Draw)) ReplyToCommand(caller, "Round draw dispatched.");
    else { char error[256]; GetLastError(error, sizeof(error)); ReplyToCommand(caller, error); }
}
public void MatchStatistics(Player caller, const char[] arguments)
{
    if (caller == NoPlayer) { ReplyToCommand(caller, "Use this command as a connected player."); return; }
    int money, kills, deaths, assists;
    char text[256];
    if (!CS_GetPlayerMoney(caller, money) || !CS_GetPlayerKills(caller, kills)
        || !CS_GetPlayerDeaths(caller, deaths) || !CS_GetPlayerAssists(caller, assists)) GetLastError(text, sizeof(text));
    else Format(text, sizeof(text), "Money=%d, match kills=%d, deaths=%d, assists=%d", money, kills, deaths, assists);
    ReplyToCommand(caller, text);
}
