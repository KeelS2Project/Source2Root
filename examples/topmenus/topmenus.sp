#include <source2root_topmenus>

TopMenu tools;
TopMenuObject players;
TopMenuObject status;
TopMenuDisplay displays[32];
Player clients[32];

public bool OnPluginStart()
{
    tools = TopMenu_Open("server_tools", "Server tools");
    if (!tools) return false;
    players = TopMenu_AddCategory(tools, "players", "Players");
    status = TopMenu_AddItem(tools, "players", "connection_status", "My connection", ConnectionStatus);
    return players != NoTopMenuObject && status != NoTopMenuObject && RegisterCommand("sr_tools", "", ShowTools);
}

public void ShowTools(Player caller, const char[] arguments)
{
    if (caller == NoPlayer) return;
    int available = -1;
    for (int i = 0; i < sizeof(displays); ++i) {
        if (displays[i] != NoTopMenuDisplay && (clients[i] == caller || !TopMenu_IsOpen(displays[i]))) {
            TopMenu_CloseDisplay(displays[i]);
            displays[i] = NoTopMenuDisplay;
        }
        if (displays[i] == NoTopMenuDisplay) available = i;
    }
    if (available < 0) { ReplyToCommand(caller, "Please try again shortly."); return; }
    clients[available] = caller;
    displays[available] = TopMenu_Show(tools, caller);
}

public TopMenuAccess ConnectionStatus(TopMenuObject entry, Player player, TopMenuAction action, int data)
{
    if (action == TopMenu_CheckAccess) return IsPlayerConnected(player) ? TopMenu_Enabled : TopMenu_Hidden;
    char name[128], message[160];
    if (GetPlayerName(player, name, sizeof(name))) {
        Format(message, sizeof(message), "Connected as %s.", name);
        ReplyToCommand(player, message);
    }
    return TopMenu_Enabled;
}
