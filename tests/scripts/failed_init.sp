#include <source2root>

public bool OnPluginStart()
{
    if (!RegisterCommand("sr_partial", "", Partial))
        return false;

    return RegisterCommand("sr_partial", "", Partial);
}

public void Partial(Player caller, const char[] arguments)
{
    LogMessage("partial initialization must not dispatch");
}
