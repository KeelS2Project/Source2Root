#include <source2root>

public bool OnPluginStart()
{
    return RegisterCommand("sr_wave", "", Wave);
}

public void Wave(Player caller, const char[] arguments)
{
    if (arguments[0])
    {
        ReplyToCommand(caller, "Usage: sr_wave");
        return;
    }

    if (!ShowActivity(caller, "waved."))
    {
        char error[256];
        GetLastError(error, sizeof(error));
        LogMessage(error);
    }
}
