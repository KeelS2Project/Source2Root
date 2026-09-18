#include <source2root_sdkcall>
SDKCall prepared;
SDKCall stale;
DHook hook;
int mode;
int visits;
public bool OnPluginStart()
{
    DHookTarget target = DHook_Open("scalar");
    if (target == NoDHookTarget) return false;
    prepared = SDKCall_Prepare(target);
    if (prepared == NoSDKCall) return false;
    DHookTarget denied = DHook_Open("observe_only");
    if (denied == NoDHookTarget || SDKCall_Prepare(denied) != NoSDKCall) return false;
    DHook_CloseTarget(denied);
    hook = DHook_Add(target, DHook_Both, Intercept, 0, 0);
    DHook_CloseTarget(target);
    if (hook == NoDHook) return false;
    LogMessage("SDKCALL_READY");
    return RegisterCommand("sr_sdkcall", "", Check)
        && RegisterCommand("sr_sdkcall_close", "", CloseDuringCall)
        && RegisterCommand("sr_sdkcall_stale", "", Stale);
}
bool Initialize()
{
    return SDKCall_SetInt(prepared, 1, 3) && SDKCall_SetFloat(prepared, 2, 2.0);
}
public void Check(Player player, const char[] arguments)
{
    int result = -1;
    if (SDKCall_ArgumentCount(prepared) != 2 || SDKCall_ValueType(prepared, 0) != DHook_Int32
        || SDKCall_GetInt(prepared, 0, result) || result != 0 || SDKCall_Execute(prepared)
        || !Initialize() || !SDKCall_Execute(prepared) || !SDKCall_GetInt(prepared, 0, result)
        || result != 8 || visits != 0)
    {
        LogMessage("SDKCALL_FAILED original or initialization"); return;
    }
    mode = 1;
    if (!SDKCall_Execute(prepared, SDKCALL_INVOKE_HOOKS) || !SDKCall_GetInt(prepared, 0, result)
        || result != 90 || visits != 1 || !SDKCall_GetInt(prepared, 1, result) || result != 3
        || !SDKCall_Reset(prepared) || SDKCall_Execute(prepared) || SDKCall_GetInt(prepared, 0, result)
        || result != 0)
    {
        LogMessage("SDKCALL_FAILED hook or reset"); return;
    }
    LogMessage("SDKCALL_CHECK_OK");
}
public void CloseDuringCall(Player player, const char[] arguments)
{
    mode = 2; stale = prepared;
    if (!Initialize() || !SDKCall_Execute(prepared, SDKCALL_INVOKE_HOOKS) || prepared != NoSDKCall)
        LogMessage("SDKCALL_FAILED self close");
    else LogMessage("SDKCALL_CLOSE_OK");
}
public void Stale(Player player, const char[] arguments)
{
    SDKCall_ArgumentCount(stale);
    LogMessage("SDKCALL_FAILED stale handle admitted");
}
public DHookAction Intercept(DHookFrame frame, DHookPhase phase, int data)
{
    if (phase == DHook_Post)
    {
        DHook_SetInt(frame, 0, 90); return DHook_Override;
    }
    visits++;
    if (mode == 1)
    {
        int result = -1;
        if (SDKCall_Execute(prepared) || SDKCall_SetInt(prepared, 1, 4) || SDKCall_Reset(prepared)
            || SDKCall_GetInt(prepared, 0, result) || result != 0)
            LogMessage("SDKCALL_FAILED busy call admitted");
    }
    if (mode == 2)
    {
        SDKCall_Close(prepared); prepared = NoSDKCall;
        DHook_Close(hook); hook = NoDHook;
    }
    DHook_SetInt(frame, 1, 10); DHook_SetFloat(frame, 2, 4.0);
    return DHook_Continue;
}
