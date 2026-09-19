#include <source2root_sdkhooks>
SDKHook hook;
SDKHookFrame stale;
int mode;

public bool OnPluginStart()
{
    hook = SDKHook_Add(0x23004, "damage", SDKHook_Both, Damage, 0, 77);

    if (hook == NoSDKHook)
        return false;

    LogMessage("SDKHOOKS_READY");
    return RegisterCommand("sr_sdkhook_continue", "", Continue)
        && RegisterCommand("sr_sdkhook_block", "", Block)
        && RegisterCommand("sr_sdkhook_close", "", Close)
        && RegisterCommand("sr_sdkhook_stale", "", Stale)
        && RegisterCommand("sr_sdkhook_fault", "", Fault);
}

public
void Continue(Player player, const char[] arguments)
{
    mode = 1;
}

public
void Block(Player player, const char[] arguments)
{
    mode = 2;
}

public
void Close(Player player, const char[] arguments)
{
    mode = 3;
}

public
void Fault(Player player, const char[] arguments)
{
    mode = 4;
}

public
void Stale(Player player, const char[] arguments)
{
    SDKHook_Entity(stale);
}

public SDKHookAction Damage(SDKHookFrame frame, SDKHookPhase phase, int data)
{
    stale = frame;
    float damage, force[3], position[3];
    int type, custom, inflictor, attacker, ability;

    if (data != 77 || SDKHook_Kind(frame) != SDKHook_Damage || SDKHook_Entity(frame) != 0x23004 ||
        SDKHook_Other(frame) != -1 ||
        !SDKHook_GetDamage(frame, damage, type, custom, inflictor, attacker, ability, force, position) ||
        type != view_as<int>(0x80000040) || custom != -7 || inflictor != 0x45005 || attacker != -1 || ability != -1)
    {
        LogMessage("SDKHOOKS_FAILED snapshot");
        return SDKHook_Continue;
    }

    if (phase == SDKHook_Post)
    {
        if (SDKHook_SetDamage(frame, 1.0, type, force, position))
            LogMessage("SDKHOOKS_FAILED post edit");

        LogMessage("SDKHOOKS_POST_OK");
        return SDKHook_Continue;
    }

    if (!SDKHook_SetDamage(frame, 12.0, type, force, position))
        LogMessage("SDKHOOKS_FAILED stage edit");

    LogMessage("SDKHOOKS_PRE_OK");

    if (mode == 1)
        return SDKHook_Continue;

    if (mode == 2)
        return SDKHook_Block;

    if (mode == 3)
    {
        SDKHook_Close(hook);
        hook = NoSDKHook;
    }

    if (mode == 4)
        SDKHook_Entity(NoSDKHookFrame);

    return SDKHook_Changed;
}
