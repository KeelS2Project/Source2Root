#include <source2root>

public bool OnPluginStart()
{
    return RegisterCommand("sr_bad_handle", "", BadHandle)
        && RegisterCommand("sr_bad_timer", "", BadTimer)
        && RegisterCommand("sr_bad_array", "", BadArray);
}

public void BadHandle(Player caller, const char[] arguments)
{
    CloseMenu(view_as<Menu>(4097));
}

public void BadTimer(Player caller, const char[] arguments)
{
    CreateTimer(-1, TimerExpired);
}

public void BadArray(Player caller, const char[] arguments)
{
    Player values[2];
    GetPlayers(values, -100);
}

public void TimerExpired(Player target)
{
    LogMessage("invalid timer must not fire");
}
