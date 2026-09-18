#include <source2root_dhooks>

// Disabled by default. Configure a reviewed int32(int32,float32) target before
// enabling this example. The example catalog name is a placeholder; it is not
// an engine signature. This observer does not change native arguments/results.
public bool OnPluginStart()
{
    DHookTarget target = DHook_Open("example_scalar");
    if (target == NoDHookTarget) return false;
    DHook hook = DHook_Add(target, DHook_Both, Observe);
    DHook_CloseTarget(target);
    return hook != NoDHook;
}
public DHookAction Observe(DHookFrame frame, DHookPhase phase, int data)
{
    int integer;
    float real;
    if (phase == DHook_Pre && DHook_GetInt(frame, 1, integer) && DHook_GetFloat(frame, 2, real))
    {
        char message[128];
        Format(message, sizeof(message), "Configured hook entered with int=%d float=%.2f", integer, real);
        LogMessage(message);
    }
    return DHook_Continue;
}
