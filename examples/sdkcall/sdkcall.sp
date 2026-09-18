#include <source2root_sdkcall>

// Disabled until an operator configures a reviewed scalar function that can
// be explicitly called with these values. The catalog target is a placeholder,
// not an engine signature. Calls happen only on the permission-gated command.
SDKCall prepared;
public bool OnPluginStart()
{
    DHookTarget target = DHook_Open("example_scalar_call");
    if (target == NoDHookTarget) return false;
    prepared = SDKCall_Prepare(target);
    DHook_CloseTarget(target);
    return prepared != NoSDKCall && RegisterCommand("sr_scalar_call", "generic", Run)
        && RegisterCommand("sr_buffer_call", "generic", RunBuffers);
}
public void RunBuffers(Player player, const char[] arguments)
{
    DHookTarget target = DHook_Open("example_buffer_call");
    if (target == NoDHookTarget) { ReplyToCommand(player, "The buffer target is unavailable."); return; }
    SDKCall buffers = SDKCall_Prepare(target);
    DHook_CloseTarget(target);
    if (buffers == NoSDKCall) { ReplyToCommand(player, "The buffer call could not be prepared."); return; }
    int values[3] = {7, 9, 11}, output[3], written, result;
    float vector[3] = {1.0, 2.0, 3.0}; char text[128];
    bool completed = SDKCall_SetString(buffers, 1, "hello", sizeof(text))
        && SDKCall_SetArray(buffers, 3, values, sizeof(values))
        && SDKCall_SetVector(buffers, 5, vector)
        && SDKCall_Execute(buffers) && SDKCall_GetInt(buffers, 0, result)
        && SDKCall_GetString(buffers, 1, text, sizeof(text))
        && SDKCall_GetArray(buffers, 3, output, sizeof(output), written)
        && SDKCall_GetVector(buffers, 5, vector);
    SDKCall_Close(buffers);
    if (!completed) { ReplyToCommand(player, "The configured buffer call failed."); return; }
    char reply[256];
    Format(reply, sizeof(reply), "Returned %d; text: %s; elements: %d; first value: %d", result, text, written, output[0]);
    ReplyToCommand(player, reply);
}
public void Run(Player player, const char[] arguments)
{
    int result;
    if (!SDKCall_SetInt(prepared, 1, 3) || !SDKCall_SetFloat(prepared, 2, 2.0)
        || !SDKCall_Execute(prepared) || !SDKCall_GetInt(prepared, 0, result))
    {
        ReplyToCommand(player, "The configured call failed."); return;
    }
    char text[96];
    Format(text, sizeof(text), "Configured call returned %d", result);
    ReplyToCommand(player, text);
}
