#include <source2root>

public bool OnPluginStart()
{
    return CreateTimer(250, Started) != NoTimer;
}

public void Started(Player target)
{
    LogMessage("startup timer");
}
