#include <source2root_topmenus>

TopMenu tools;
TopMenuObject item;
bool fault;

public bool OnPluginStart()
{
    tools = TopMenu_Open("fixture", "Contributor title ignored");
    if (!tools) return false;
    item = TopMenu_AddItem(tools, "players", "contributed", "Contributed action", Handle, "demo.status", -1, 22);
    if (!item) return false;
    LogMessage("TOP_CONTRIBUTOR_LOADED");
    return RegisterCommand("sr_top_fault", "", Fault);
}
public void Fault(Player caller, const char[] arguments)
{
    fault = true;
}
public TopMenuAccess Handle(TopMenuObject entry, Player player, TopMenuAction action, int data)
{
    char name[128];
    if (entry != item || data != 22 || !GetPlayerName(player, name, sizeof(name))) LogMessage("TOP_FAILED_CONTRIBUTOR_IDENTITY");
    if (fault) TopMenu_SetLabel(view_as<TopMenuObject>(-1), "Invalid resource");
    if (action == TopMenu_Select) LogMessage("TOP_OTHER_SELECTED");
    return TopMenu_Enabled;
}
