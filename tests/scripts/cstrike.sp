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
        && RegisterCommand("sr_cs_action_error", "", ActionError) && RegisterCommand("sr_cs_reconnected", "", Reconnected);
}
public void CheckActions(Player caller, const char[] arguments)
{
    if (!CS_RespawnPlayer(Saved) || !CS_ChangeTeam(Saved, CS_Terrorist) || !CS_SwitchTeam(Saved, CS_CounterTerrorist))
        LogMessage("CSTRIKE_FAILED actions");
    else LogMessage("CSTRIKE_ACTIONS_OK");
}
public void Invalid(Player caller, const char[] arguments)
{
    if (CS_ChangeTeam(Saved, view_as<CSTeam>(99)) || CS_SwitchTeam(Saved, view_as<CSTeam>(0)) || CS_SwitchTeam(Saved, CS_Spectator) || CS_RespawnPlayer(NoPlayer))
        LogMessage("CSTRIKE_FAILED invalid input");
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
    Player players[4];
    if (GetPlayers(players, sizeof(players)) != 1) { LogMessage("CSTRIKE_FAILED new player"); return; }
    Saved = players[0];
    LogMessage("CSTRIKE_RECONNECTED_OK");
}
