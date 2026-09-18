#if defined _source2root_sdkcall_included
    #endinput
#endif
#define _source2root_sdkcall_included
#include <source2root_dhooks>

enum SDKCall { NoSDKCall = 0 }
#define SDKCALL_INVOKE_HOOKS 1

// Initial scalar direct calls, provided by source2root.dhooks and optional
// keels2.keelcall API1. These are Source2Root APIs, not SourceMod SDKCall syntax.
// DHook_Open uses the operator target catalog and script allowlist. A target
// additionally requires allow_calls:true (default false) to prepare a call.
// Each alias keeps its own permission even if native target leases are shared.
// The operator must review the exact native signature and whether explicit
// calls are valid in the current game state. No game signatures ship enabled.
//
// Call resources belong to their script generation and retain their target
// independently of DHook_CloseTarget. Slot 0 is the result; arguments are
// 1..ArgumentCount. Every argument must be explicitly set before execution.
// All integer widths and bool, float32/64, pointer-null arguments and void
// returns are supported. Integer/float text preserve 64-bit values. Pointer
// results support IsNull only; no address exposure, dereference or reuse.
// Methods, entities, strings, arrays, aggregates and variadics need later
// adapters and are not supported by this script interface.
//
// Execute synchronously calls the original function with flags=0, or includes
// configured hooks with SDKCALL_INVOKE_HOOKS. Other hooks may still fire from
// inside the native function. Native/callback side effects are not rolled back
// on failure. At most eight nested direct calls; reusing the same active call
// or changing/resetting it during execution fails. Closing it in a callback is
// safe: in-flight state lives until return. Native callback installation and
// removal on that target return busy during calls; closed script hooks become
// inert immediately and cleanup retries afterward.
//
// Successful setters invalidate the old result. Failed setters preserve it.
// Execute attempts invalidate the old result; Reset clears all initialized
// arguments. Uninitialized/busy getters fail and clear their output. Types and
// counts are available before setting values. GetFloat may round float64;
// GetFloatText keeps its precision. GetInt refuses overflow. Value errors return
// false/zero and set GetLastError; invalid or foreign resources are script errors.
native SDKCall SDKCall_Prepare(DHookTarget target);
native bool SDKCall_Close(SDKCall call);
native bool SDKCall_Reset(SDKCall call);
native bool SDKCall_Execute(SDKCall call, int flags = 0);
native int SDKCall_ArgumentCount(SDKCall call);
native DHookType SDKCall_ValueType(SDKCall call, int slot);
native bool SDKCall_GetInt(SDKCall call, int slot, int &value);
native bool SDKCall_GetIntegerText(SDKCall call, int slot, char[] value, int maximum);
native bool SDKCall_GetFloat(SDKCall call, int slot, float &value);
native bool SDKCall_GetFloatText(SDKCall call, int slot, char[] value, int maximum);
native bool SDKCall_IsNull(SDKCall call, int slot, bool &value);
native bool SDKCall_SetInt(SDKCall call, int slot, int value);
native bool SDKCall_SetIntegerText(SDKCall call, int slot, const char[] value);
native bool SDKCall_SetFloat(SDKCall call, int slot, float value);
native bool SDKCall_SetFloatText(SDKCall call, int slot, const char[] value);
native bool SDKCall_SetNull(SDKCall call, int slot);
