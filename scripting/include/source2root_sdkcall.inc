#if defined _source2root_sdkcall_included
    #endinput
#endif
#define _source2root_sdkcall_included
#include <source2root_dhooks>

enum SDKCall { NoSDKCall = 0 }
#define SDKCALL_INVOKE_HOOKS 1

// Configured direct calls, provided by source2root.dhooks and optional
// keels2.keelcall API1. These are Source2Root APIs, not SourceMod SDKCall syntax.
// DHook_Open uses the operator target catalog and script allowlist. A target
// additionally requires allow_calls:true (default false) to prepare a call.
// Each alias keeps its own permission and buffer limits even if native target
// leases are shared.
// The operator must review the exact native signature and whether explicit
// calls are valid in the current game state. No game signatures ship enabled.
//
// Call resources belong to their script generation and retain their target
// independently of DHook_CloseTarget. Slot 0 is the result; arguments are
// 1..ArgumentCount. Set every argument before execution; buffer setters supply
// their associated lengths. All integer widths and bool, float32/64, unadapted
// pointer-null arguments and void returns are supported. Integer/float text
// preserve 64-bit values. Pointer
// results support IsNull only; no address exposure, dereference or reuse.
// Operator-declared buffer adapters support owned strings, int32 arrays and
// three-float vectors. Their memory remains private to the call; native code
// must not retain its pointers. Checked entity and method calls use the
// configured entity adapters below. Aggregate/variadic adapters are absent.
// Native functions must honor the configured buffer types
// and capacities; a wrong native signature is not made safe by these limits.
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

// A target's optional buffers array names argument, kind and capacity; kind is
// string or int32. Optional length_argument selects a distinct int32/uint32
// parameter populated by the setter (required for int32 arrays). vector3 only
// names argument and kind. Limits: 4096 bytes/string, 1024 int32 elements/array,
// 16KiB total configured storage/call. Ordinary setters cannot change buffer
// pointers or their length arguments. Other arguments still need setters.
//
// String capacity includes the trailing zero; zero uses strlen(value)+1. The
// selected capacity must fit the operator limit and is passed as its length.
// Arrays copy count elements and pass count; vectors copy exactly three finite
// floats. Getters copy current buffer contents, including native in/out edits.
// An execution failure may still change buffers. Reset invalidates all buffers;
// getters reject busy/uninitialized values and unterminated native strings.
// During calls, script hook edits for this target that replace or alias owned
// buffer pointers, or enlarge their length beyond the allocation, are discarded
// as one group. Nested native calls still require correct native signatures.
// Native plugins and native functions remain responsible for their own writes.
native bool SDKCall_SetString(SDKCall call, int slot, const char[] value, int capacity = 0);
native bool SDKCall_GetString(SDKCall call, int slot, char[] value, int maximum);
native bool SDKCall_SetArray(SDKCall call, int slot, const int[] value, int count);
native bool SDKCall_GetArray(SDKCall call, int slot, int[] value, int maximum, int &written);
native bool SDKCall_SetVector(SDKCall call, int slot, const float value[3]);
native bool SDKCall_GetVector(SDKCall call, int slot, float value[3]);

// A target's entities array names argument and class (exact dynamic schema
// class, not a base-class cast). Each slot must be a distinct pointer argument
// without a buffer adapter. A method requires such an adapter at argument 1.
// SetEntityReference accepts the serial-bearing Source 2 handle bits, not an
// entity index or SDKTools resource. The call retains the native identity and
// map epoch, revalidating them and the class immediately before dispatch.
// SetPlayer also binds the current Player connection and controller; pawn=true
// additionally binds its pawn. A reconnect or changed controller/pawn requires
// setting it again. Raw entity addresses never become script values.
// Entity calls only allow flags=0; pre-hooks could delete an entity before the
// original runs. Nested native calls may still invoke other hooks. Native code
// must not retain entity pointers or reuse them after operations that may
// destroy the entity. Reset releases all retained entities. Per-provider limit:
// 256 retained identities; replacing a value needs a free slot for its lease.
// These optional adapters require keels2.entity_access API1. IsNull reports
// false for an initialized entity argument; it is not an entity validity check.
native bool SDKCall_SetEntityReference(SDKCall call, int slot, int sourceHandle);
native bool SDKCall_SetPlayer(SDKCall call, int slot, Player player, bool pawn = false);
