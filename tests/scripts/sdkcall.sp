#include <source2root_sdkcall>
SDKCall prepared;
SDKCall stale;
DHook hook;
SDKCall buffer_call;
DHook buffer_hook;
int buffer_mode;
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
        && RegisterCommand("sr_sdkcall_buffers", "", CheckBuffers)
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
bool InitializeBuffers()
{
    int values[3] = {7, 9, 11}; float vector[3] = {1.0, 2.0, 3.0};
    bool result = SDKCall_SetString(buffer_call, 1, "hello", 8)
        && SDKCall_SetArray(buffer_call, 3, values, 3)
        && SDKCall_SetVector(buffer_call, 5, vector);
    values[0] = 100; vector[0] = 100.0;
    return result;
}
public void CheckBuffers(Player player, const char[] arguments)
{
    DHookTarget target = DHook_Open("buffers");
    buffer_call = SDKCall_Prepare(target);
    if (target == NoDHookTarget || buffer_call == NoSDKCall)
    {
        LogMessage("SDKCALL_FAILED buffer prepare"); return;
    }
    char text[16]; int output[4], written, result; float vector[3];
    if (SDKCall_SetInt(buffer_call, 2, 99) || SDKCall_SetNull(buffer_call, 1)
        || SDKCall_GetString(buffer_call, 1, text, sizeof(text)) || text[0]
        || !InitializeBuffers() || !SDKCall_GetInt(buffer_call, 2, result) || result != 8
        || !SDKCall_GetInt(buffer_call, 4, result) || result != 3 || !SDKCall_Execute(buffer_call)
        || !SDKCall_GetInt(buffer_call, 0, result) || result != 54
        || !SDKCall_GetString(buffer_call, 1, text, sizeof(text)) || text[0] != 'c' || text[6] != 'd' || text[7]
        || !SDKCall_GetArray(buffer_call, 3, output, sizeof(output), written) || written != 3
        || output[0] != 14 || output[1] != 18 || output[2] != 22 || output[3]
        || !SDKCall_GetVector(buffer_call, 5, vector) || vector[0] != 2.0 || vector[1] != 4.0 || vector[2] != 6.0)
    {
        LogMessage("SDKCALL_FAILED buffer mapping"); return;
    }
    if (SDKCall_GetString(buffer_call, 1, text, 2) || text[0]
        || SDKCall_GetArray(buffer_call, 3, output, 2, written) || written || output[0] || output[1]
        || SDKCall_SetString(buffer_call, 1, "too long", 4)
        || !SDKCall_GetInt(buffer_call, 0, result) || result != 54
        || !SDKCall_Reset(buffer_call) || SDKCall_Execute(buffer_call)
        || SDKCall_GetVector(buffer_call, 5, vector) || vector[0] || vector[1] || vector[2])
    {
        LogMessage("SDKCALL_FAILED buffer bounds or reset"); return;
    }
    LogMessage("SDKCALL_BUFFERS_OK");
    buffer_hook = DHook_Add(target, DHook_Pre, BufferIntercept, 0, 0);
    DHook_CloseTarget(target);
    if (buffer_hook == NoDHook) { LogMessage("SDKCALL_FAILED buffer hook"); return; }
    for (buffer_mode = 1; buffer_mode <= 3; buffer_mode++)
    {
        if (!InitializeBuffers() || !SDKCall_Execute(buffer_call, SDKCALL_INVOKE_HOOKS)
            || !SDKCall_GetInt(buffer_call, 0, result) || result != 54)
        {
            LogMessage("SDKCALL_FAILED buffer hook bounds"); return;
        }
    }
    LogMessage("SDKCALL_BUFFER_HOOK_BOUNDS_OK");
    buffer_mode = 0;
    if (buffer_hook == NoDHook || !InitializeBuffers() || !SDKCall_Execute(buffer_call, SDKCALL_INVOKE_HOOKS)
        || buffer_call != NoSDKCall || buffer_hook != NoDHook)
        LogMessage("SDKCALL_FAILED buffer self close");
    else LogMessage("SDKCALL_BUFFER_CLOSE_OK");
}
public DHookAction BufferIntercept(DHookFrame frame, DHookPhase phase, int data)
{
    if (buffer_mode == 1) { DHook_SetInt(frame, 2, 16); DHook_SetInt(frame, 0, 99); return DHook_Override; }
    if (buffer_mode == 2) { DHook_SetNull(frame, 1); return DHook_Continue; }
    if (buffer_mode == 3) { DHook_CopyValue(frame, 5, 1); return DHook_Continue; }
    char text[16]; int values[3] = {1, 2, 3};
    if (SDKCall_SetString(buffer_call, 1, "busy", 8) || SDKCall_SetArray(buffer_call, 3, values, 3)
        || SDKCall_GetString(buffer_call, 1, text, sizeof(text)) || text[0])
        LogMessage("SDKCALL_FAILED busy buffer admitted");
    SDKCall_Close(buffer_call); buffer_call = NoSDKCall;
    DHook_Close(buffer_hook); buffer_hook = NoDHook;
    return DHook_Continue;
}
