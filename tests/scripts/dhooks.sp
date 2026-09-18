#include <source2root_dhooks>
DHookTarget target;
DHook hook;
DHookFrame stale;
int mode;
public bool OnPluginStart()
{
    target = DHook_Open("scalar");
    if (target == NoDHookTarget) return false;
    hook = DHook_Add(target, DHook_Both, Intercept, 10, 73);
    if (hook == NoDHook) return false;
    LogMessage("DHOOKS_READY");
    return RegisterCommand("sr_dhook_mode", "", Mode)
        && RegisterCommand("sr_dhook_disable", "", Disable)
        && RegisterCommand("sr_dhook_enable", "", Enable)
        && RegisterCommand("sr_dhook_faulted", "", Faulted)
        && RegisterCommand("sr_dhook_stale", "", Stale);
}
public void Mode(Player player, const char[] arguments)
{
    mode++;
}
public void Disable(Player player, const char[] arguments)
{
    if (!DHook_Enable(hook, false)) LogMessage("DHOOKS_FAILED disable");
}
public void Enable(Player player, const char[] arguments)
{
    if (!DHook_Enable(hook, true)) LogMessage("DHOOKS_FAILED enable");
}
public void Stale(Player player, const char[] arguments)
{
    DHook_ArgumentCount(stale);
    LogMessage("DHOOKS_FAILED stale frame admitted");
}
public void Faulted(Player player, const char[] arguments)
{
    if (!DHook_IsActive(hook)) LogMessage("DHOOKS_FAULT_RETIRED_OK");
    else LogMessage("DHOOKS_FAILED fault did not retire hook");
}
public DHookAction Intercept(DHookFrame frame, DHookPhase phase, int data)
{
    stale = frame;
    int integer;
    float real;
    if (data != 73 || DHook_ArgumentCount(frame) != 2 || DHook_ValueType(frame, 1) != DHook_Int32
        || !DHook_GetInt(frame, 1, integer) || !DHook_GetFloat(frame, 2, real))
    {
        LogMessage("DHOOKS_FAILED argument mapping");
        return DHook_Continue;
    }
    if (phase == DHook_Post)
    {
        if (mode == 0)
        {
            if (!DHook_SetInt(frame, 0, 90)) LogMessage("DHOOKS_FAILED post return");
            LogMessage("DHOOKS_POST_OK");
            return DHook_Override;
        }
        return DHook_Continue;
    }
    if (integer != 3 || real != 2.0 || !DHook_SetInt(frame, 1, 10) || !DHook_SetFloat(frame, 2, 4.0))
        LogMessage("DHOOKS_FAILED pre arguments");
    if (mode == 1)
    {
        DHook_SetInt(frame, 0, 70);
        return DHook_Supercede;
    }
    if (mode == 2)
    {
        DHook_Close(hook); hook = NoDHook;
        DHook_CloseTarget(target); target = NoDHookTarget;
        DHook_SetInt(frame, 0, 60);
        LogMessage("DHOOKS_SELF_CLOSE_OK");
        return DHook_Supercede;
    }
    if (mode == 3) return view_as<DHookAction>(42 / (mode - 3));
    LogMessage("DHOOKS_PRE_OK");
    return DHook_Continue;
}
