#include <source2root>

Player remembered;

public bool OnPluginStart()
{
    if (ShowActivity(NoPlayer, "started."))
        return false;

    return RegisterCommand("sr_activity", "", Activity);
}

public void Activity(Player caller, const char[] arguments)
{
    if (arguments[0] == 'm')
    {
        Menu menu = CreateMenu("Activity fixture", "admin.kick", Selected);

        if (menu != NoMenu && (!AddMenuItem(menu, "Announce") || !ShowMenu(menu, caller)))
            CloseMenu(menu);
    }
    else if (arguments[0] == 'r')
        remembered = caller;
    else if (arguments[0] == 's')
        Announce(remembered);
    else if (arguments[0] == 'i')
        Report(ShowActivity(caller, "slapped\nKiddo"));
    else if (arguments[0] == 'e')
        Report(ShowActivity(caller, ""));
    else if (arguments[0] == 'o')
        Report(ShowActivity(
            caller, "slapped Kiddo (10 damage)", "Slapped Kiddo (10 damage); 1 target failed: private detail"));
    else if (arguments[0] == 'v')
        Report(ShowActivity(caller, "slapped Kiddo", "bad\nconfirmation"));
    else if (arguments[0] == 'p')
        Report(ShowActivity(caller, "set accuracy to 100% for %s."));
    else
        Announce(caller);
}

void Announce(Player caller)
{
    Report(ShowActivity(caller, "slapped Kiddo (10 damage)"));
}

void Report(bool delivered)
{
    if (delivered)
        LogMessage("activity delivered");
    else
    {
        char error[256];
        GetLastError(error, sizeof(error));
        LogMessage(error);
    }
}

public void Selected(Player caller, int item)
{
    if (item == 0 && HasPermission(caller, "admin.kick"))
        Announce(caller);
}

public void OnPluginStop()
{
    if (!ShowActivity(NoPlayer, "stopped."))
        LogMessage("stop activity refused");
}
