#include <source2root_topmenus>

TopMenu tools;
TopMenuObject localItem;
TopMenuDisplay display;
bool hideLocal;
bool mutateAccess;
bool removeSelected;

public bool OnPluginStart()
{
    tools = TopMenu_Open("fixture", "Shared tools");

    if (!tools || !TopMenu_AddCategory(tools, "players", "Player actions", "demo.hello"))
        return false;

    localItem = TopMenu_AddItem(tools, "players", "local", "Local action", Handle, "", 0, 11);

    if (!localItem || !TopMenu_AddItem(tools, "", "denied", "Denied action", Handle, "not.granted") ||
        !TopMenu_AddItem(tools, "", "hidden", "Hidden action", Hidden) ||
        !TopMenu_AddItem(tools, "", "disabled", "Disabled action", Disabled, "", 100))
        return false;

    LogMessage("TOP_PRIMARY_LOADED");
    return RegisterCommand("sr_top_show", "", Show) && RegisterCommand("sr_top_hide", "", Hide) &&
        RegisterCommand("sr_top_title", "", Title) && RegisterCommand("sr_top_mutate", "", Mutate) &&
        RegisterCommand("sr_top_selfclose", "", SelfClose) && RegisterCommand("sr_top_close", "", Close) &&
        RegisterCommand("sr_top_closed", "", Closed) && RegisterCommand("sr_top_permissions", "", Permissions);
}

Player Target()
{
    Player players[2];
    return GetPlayers(players, sizeof(players)) == 1 ? players[0] : NoPlayer;
}

public void Show(Player caller, const char[] arguments)
{
    if (display != NoTopMenuDisplay)
        TopMenu_CloseDisplay(display);

    display = TopMenu_Show(tools, Target());

    if (display == NoTopMenuDisplay || !TopMenu_IsOpen(display))
        LogMessage("TOP_FAILED_SHOW");
}

public void Close(Player caller, const char[] arguments)
{
    if (display != NoTopMenuDisplay)
    {
        TopMenu_CloseDisplay(display);
        display = NoTopMenuDisplay;
    }
}

public void Closed(Player caller, const char[] arguments)
{
    if (display != NoTopMenuDisplay && !TopMenu_IsOpen(display))
        LogMessage("TOP_DISPLAY_CLOSED");
    else
        LogMessage("TOP_FAILED_CLOSED");
}

public void Permissions(Player caller, const char[] arguments)
{
    if (!ReloadAdmins(caller))
        LogMessage("TOP_FAILED_PERMISSIONS");
}

public void Hide(Player caller, const char[] arguments)
{
    hideLocal = true;
}

public void Title(Player caller, const char[] arguments)
{
    if (!TopMenu_SetTitle(tools, "Changed title"))
        LogMessage("TOP_FAILED_TITLE");
}

public void Mutate(Player caller, const char[] arguments)
{
    mutateAccess = true;

    if (display != NoTopMenuDisplay)
        TopMenu_CloseDisplay(display);

    display = TopMenu_Show(tools, Target());

    if (display != NoTopMenuDisplay)
        LogMessage("TOP_FAILED_MUTATION");
    else
        LogMessage("TOP_MUTATION_REJECTED");
}

public void SelfClose(Player caller, const char[] arguments)
{
    removeSelected = true;
}

public TopMenuAccess Handle(TopMenuObject entry, Player player, TopMenuAction action, int data)
{
    if (!IsPlayerConnected(player) || data != 11 || entry != localItem)
        LogMessage("TOP_FAILED_CALLBACK_IDENTITY");

    if (action == TopMenu_CheckAccess) {
        if (mutateAccess)
        {
            mutateAccess = false;
            TopMenu_SetLabel(entry, "Mutated action");
        }

        return hideLocal ? TopMenu_Hidden : TopMenu_Enabled;
    }

    LogMessage("TOP_LOCAL_SELECTED");

    if (removeSelected) {
        TopMenu_Remove(entry);
        localItem = NoTopMenuObject;

        if (display != NoTopMenuDisplay)
            TopMenu_CloseDisplay(display);

        display = TopMenu_Show(tools, player);

        if (!display)
            LogMessage("TOP_FAILED_REOPEN");
        else
            LogMessage("TOP_SELF_CLOSED");
    }

    return TopMenu_Enabled;
}

public TopMenuAccess Hidden(TopMenuObject entry, Player player, TopMenuAction action, int data)
{
    if (action == TopMenu_Select)
        LogMessage("TOP_FAILED_HIDDEN_SELECT");

    return TopMenu_Hidden;
}

public TopMenuAccess Disabled(TopMenuObject entry, Player player, TopMenuAction action, int data)
{
    if (action == TopMenu_Select)
        LogMessage("TOP_FAILED_DISABLED_SELECT");

    return TopMenu_Disabled;
}
