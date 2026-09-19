#include <source2root>
typedef Persistent = function int(int input, int &output);
native bool Persistent_Store(Persistent callback);
native int Persistent_Invoke();

public bool OnPluginStart()
{
    return Persistent_Store(Callback) && RegisterCommand("sr_persistent_module", "", Run);
}

public void Run(Player caller, const char[] arguments)
{
    if (Persistent_Invoke() == 42)
        LogMessage("PERSISTENT_NESTED_OK");
    else
        LogMessage("PERSISTENT_FAILED nested invocation");
}

public int Callback(int input, int &output)
{
    if ((input != 20 && input != 40) || output != 0)
    {
        LogMessage("PERSISTENT_FAILED arguments");
        return -1;
    }

    output = input + 1;
    return 42;
}
