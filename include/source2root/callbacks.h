#ifndef SOURCE2ROOT_CALLBACKS_H
#define SOURCE2ROOT_CALLBACKS_H
#include <source2root/native.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SR_CALLBACK_SERVICE "source2root.callbacks"
#define SR_CALLBACK_API_VERSION 1u
#define SR_CALLBACK_MAX_ARGUMENTS 16u
#define SR_CALLBACK_MAX_ARRAY 1024u
#define SR_CALLBACK_MAX_PAYLOAD 16384u
#define SR_CALLBACK_MAX_DEPTH 8u
#define SR_CALLBACK_COPYBACK 1u
#define SR_CALLBACK_INT32 1u
#define SR_CALLBACK_FLOAT32 2u
#define SR_CALLBACK_STRING 3u
#define SR_CALLBACK_INT32_REF 4u
#define SR_CALLBACK_FLOAT32_REF 5u
#define SR_CALLBACK_INT32_ARRAY 6u
#define SR_CALLBACK_FLOAT32_ARRAY 7u

typedef union SrCallbackValue {
    int32_t integer;
    float real;
    const char* string;
    int32_t* integers;
    float* reals;
} SrCallbackValue;

typedef struct SrCallbackArgument {
    uint32_t size;
    uint32_t type;
    /* Zero for values/strings, one for references, 1..1024 for arrays. */
    uint32_t count;
    /* Only arrays accept COPYBACK. References always copy back on success. */
    uint32_t flags;
    SrCallbackValue value;
} SrCallbackArgument;

typedef struct SrCallbackApi {
    uint32_t size;
    uint32_t api_version;
    void* context;
    /* All operations are game-thread only. Convert a freshly captured callback
       to a persistent callback. Repeating
       retain is harmless. Existing deliver_callback rejects persistent tokens.
       Provider ownership, script generation and resource quota are retained. */
    KeelResult (*retain)(void*, KeelPluginHandle provider, SrCallback callback);
    /* Game thread only. Paused/loading/management activity and nesting beyond
       eight calls return BUSY without consuming the token. Reentry is allowed.
       Scalar result is zero on failure. Reference/array writes copy back only
       on successful execution, in argument order; buffers must remain valid
       throughout callbacks. Input buffers are snapshots, not shared aliases;
       result must not overlap argument storage. The provider must match the
       script function's declared signature. Strings are input only,
       NUL terminated, <=4095 bytes. Total buffer payload
       <=16KiB. Invalid input preserves the token. A script execution fault
       invalidates it; NOT_FOUND also covers cancellation or script retirement.
       The caller must not apply hook changes when invocation fails. */
    KeelResult (*invoke)(void*, KeelPluginHandle provider, SrCallback callback,
        const SrCallbackArgument* arguments, uint32_t count, int32_t* result);

    /* Cancellation is safe from inside the callback. Script cleanup also
       cancels tokens before destroying resources or releasing providers. */
    KeelResult (*cancel)(void*, KeelPluginHandle provider, SrCallback callback);
} SrCallbackApi;

#ifdef __cplusplus
}
#endif
#endif
