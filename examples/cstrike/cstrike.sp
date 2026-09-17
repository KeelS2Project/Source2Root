#include <source2root_cstrike>

public bool OnPluginStart()
{
    return RegisterCommand("sr_cs_capabilities", "", Capabilities, "Show supported Counter-Strike player actions")
        && RegisterCommand("sr_cs_respawn_self", "admin.slay", RespawnSelf, "Respawn your current T/CT pawn");
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
