#include <source2root>
#include <admin_shared>

int entered;
public bool OnPluginStart()
{
    if (SlapPlayer(NoPlayer) || SlayPlayer(NoPlayer) || OpenCommandMenu(NoPlayer, "sr_slap")) return false;
    if (SetCommandMenu("sr_slap", Recursive)) return false;
    return RegisterCommand("sr_routes", "", Run) && SetCommandMenu("sr_routes", Recursive);
}
public void Run(Player caller, const char[] arguments)
{
    if (arguments[0] == 'r')
    {
        entered = 0;
        OpenCommandMenu(caller, "sr_routes");
        char text[64];
        Format(text, sizeof(text), "entered=%d", entered);
        ReplyToCommand(caller, text);
    }
    else if (arguments[0] == 'u')
    {
        RegisterCommand("sr_invalid", "", Run, "sr_other");
        LogMessage("FAILED: malformed usage was not rejected");
    }
    else if (arguments[0] == 's')
    {
        char usage[2]; bool hasMenu;
        if (GetCommandInfo(caller, "sr_slap", usage, sizeof(usage), hasMenu) || usage[0] || hasMenu) LogMessage("FAILED: short usage");
        else Failure(caller);
    }
    else if (arguments[0] == 'd')
    {
        if (SlapPlayer(caller, -1) || SlapPlayer(caller, 1001)) LogMessage("FAILED: invalid damage");
        else Failure(caller);
    }
    else if (arguments[0] == 'b')
    {
        Menu menu = CreateMenu("Wrong handle type", "", Selected);
        if (SlayPlayer(view_as<Player>(menu))) LogMessage("FAILED: wrong handle");
        else Failure(caller);
        CloseMenu(menu);
    }
    else if (!OpenCommandMenu(caller, "sr_slap")) Failure(caller);
}
public void Recursive(Player caller, const char[] arguments)
{
    entered++;
    if (!OpenCommandMenu(caller, "sr_routes")) Failure(caller);
}
public void Selected(Player caller, int value) {}
public void OnPluginStop()
{
    if (SlayPlayer(NoPlayer) || SetCommandMenu("sr_routes", Recursive)) LogMessage("FAILED: retiring mutation");
}
