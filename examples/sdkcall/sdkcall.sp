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
    return prepared != NoSDKCall && RegisterCommand("sr_scalar_call", "generic", Run);
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
