#if defined _source2root_dhooks_included
    #endinput
#endif
#define _source2root_dhooks_included
#include <source2root>

enum DHookTarget { NoDHookTarget = 0 }
enum DHook { NoDHook = 0 }
enum DHookFrame { NoDHookFrame = 0 }
enum DHookPhase { DHook_Pre = 1, DHook_Post = 2, DHook_Both = 3 }
enum DHookAction { DHook_Continue = 0, DHook_Override = 1, DHook_Supercede = 2 }
enum DHookType
{
    DHook_Void = 0, DHook_Bool, DHook_Int8, DHook_UInt8, DHook_Int16, DHook_UInt16,
    DHook_Int32, DHook_UInt32, DHook_Int64, DHook_UInt64, DHook_Pointer, DHook_Float32, DHook_Float64
}
#define DHOOK_ORIGINAL_CALLED 1
#define DHOOK_RECALLED 2
typedef DHookCallback = function DHookAction(DHookFrame frame, DHookPhase phase, int data);

// Configured detours over KeelHook API5. These are Source2Root APIs, not drop-in
// SourceMod DHooks functions. Operators supply targets.json under
// configs/extensions/source2root.dhooks with schema1 and a targets object.
// Each named target defines allow_plugins, source (profile/symbol/pattern),
// return and arguments (scalar type names). Resolver fields: module, symbol,
// pattern, profile, offset and occurrence. Optional method=true requires a
// leading pointer argument. Pattern targets require the exact Keel profile;
// profile targets use a name from the host's reviewed target catalog.
// Symbol/prototype correctness is the operator's responsibility. No target
// configuration or game-specific signatures are installed automatically.
// The backend is game-independent; hook targets and ABIs are platform-specific.
// At most64 native targets and256 callbacks per provider, plus script quotas.
native DHookTarget DHook_Open(const char[] name);
// Existing hooks retain their target after closing this script handle.
native bool DHook_CloseTarget(DHookTarget target);
native DHook DHook_Add(DHookTarget target, DHookPhase phases, DHookCallback callback, int priority = 0, int data = 0);
// Close cancels immediately and queues physical hook retirement. Retryable host
// restoration failure retains native storage and prevents provider unloading.
native bool DHook_Close(DHook hook);
native bool DHook_Enable(DHook hook, bool enabled);
// True while registration remains usable, including disabled/paused hooks.
// False after a script fault retires it. A closed resource handle is invalid.
native bool DHook_IsActive(DHook hook);

// Frames are scoped to one active callback and script generation. Retaining or
// passing a frame to another script does not extend its lifetime. Invalid frame
// or owned-resource handles raise native errors. Slots:0=result,1..count=args.
// Getters/setters otherwise return false and set GetLastError on type/range
// errors; outputs are zero/empty on failure. IntegerText is exact signed/unsigned
// 64-bit decimal. Float narrows to finite float32; FloatText preserves float64
// precision. Numeric setters accept only finite floats and exact integer ranges.
native int DHook_ArgumentCount(DHookFrame frame);
native int DHook_FrameFlags(DHookFrame frame);
native DHookType DHook_ValueType(DHookFrame frame, int slot);
native bool DHook_GetInt(DHookFrame frame, int slot, int& value);
native bool DHook_GetIntegerText(DHookFrame frame, int slot, char[] value, int capacity);
native bool DHook_GetFloat(DHookFrame frame, int slot, float& value);
native bool DHook_GetFloatText(DHookFrame frame, int slot, char[] value, int capacity);
native bool DHook_IsNull(DHookFrame frame, int slot, bool& value);
native bool DHook_SetInt(DHookFrame frame, int slot, int value);
native bool DHook_SetIntegerText(DHookFrame frame, int slot, const char[] value);
native bool DHook_SetFloat(DHookFrame frame, int slot, float value);
native bool DHook_SetFloatText(DHookFrame frame, int slot, const char[] value);
native bool DHook_SetNull(DHookFrame frame, int slot);
// Same-type values only. Pointers stay native and may only be tested for null,
// nulled or copied between slots in the same frame; no address truncation,
// dereferencing, string reads or pointer persistence API.
native bool DHook_CopyValue(DHookFrame frame, int destination, int source);

// Pre: Continue commits arguments; Override also selects the scripted result
// while allowing the original; Supercede skips the original. Post may only
// Continue or Override and cannot change arguments. Edits are private until a
// successful callback returns a valid action. Script faults/invalid actions
// discard edits and retire that hook; game side effects in the script remain.
// Script pause/loading, non-game threads and recursion beyond8 skip script
// execution and leave the native call unchanged. Unload/reload cancels tokens.
// Virtual-instance hooks, aggregates, variadics, pointer memory access,
// CallOriginal/Recall and standalone BinTools/SDKCall are still unimplemented.
