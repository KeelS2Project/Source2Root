#ifndef SOURCE2ROOT_NATIVE_H
#define SOURCE2ROOT_NATIVE_H

#include <source2root/extension.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SR_NATIVE_SERVICE "source2root.native_calls"
#define SR_NATIVE_API_VERSION 1u
#define SR_NATIVE_BUFFER_LIMIT 4096u

typedef void (*SrResourceDestroy)(void* value);
typedef uint64_t SrCallback;

typedef struct SrPlayerIdentity {
    uint32_t size;
    int32_t slot;
    uint64_t connection;
    uint64_t steam_id;
    KeelBool authenticated;
    KeelBool bot;
} SrPlayerIdentity;

typedef struct SrNativeCall {
    uint32_t size;
    uint32_t api_version;
    void* context;
    uint64_t script_owner;
    uint32_t argument_count;
    const char* error;
    KeelResult (*read_cell)(void*, uint32_t index, int32_t* value);
    KeelResult (*read_string)(void*, uint32_t index, char* output, uint32_t capacity);
    KeelResult (*read_array)(void*, uint32_t index, int32_t* output, uint32_t count);
    KeelResult (*write_string)(void*, uint32_t index, uint32_t capacity, const char* value);
    KeelResult (*write_cell)(void*, uint32_t index, int32_t value);
    KeelResult (*write_array)(void*, uint32_t index, uint32_t capacity, const int32_t* values, uint32_t count);
    KeelResult (*data_path)(void*, KeelBool shared, char* output, uint32_t capacity);
    KeelResult (*create_resource)(void*, uint32_t type, void* value, SrResourceDestroy destroy, int32_t* handle);
    KeelResult (*get_resource)(void*, int32_t handle, uint32_t type, void** value);
    KeelResult (*close_resource)(void*, int32_t handle, uint32_t type);
    KeelResult (*set_error)(void*, const char* message);
    /* Capture a required function argument. The token belongs to this script
       generation and provider; it is invalidated before resource cleanup. */
    KeelResult (*capture_callback)(void*, uint32_t index, SrCallback* callback);
    KeelResult (*config_path)(void*, char* output, uint32_t capacity);
    KeelResult (*script_id)(void*, char* output, uint32_t capacity);
    /* Resolves this script's Player handle against its current connection. */
    KeelResult (*player_identity)(void*, int32_t handle, SrPlayerIdentity* player);
} SrNativeCall;

typedef KeelResult (*SrContextNativeFunction)(void* user_data, const SrNativeCall* call,
    int32_t* result, char* error, uint32_t error_capacity);

typedef struct SrContextNativeSpec {
    uint32_t size;
    uint32_t api_version;
    const char* name;
    uint32_t argument_count;
    uint32_t reserved;
    const char* provider_service;
    uint32_t provider_version;
    SrContextNativeFunction invoke;
    void* user_data;
} SrContextNativeSpec;

typedef struct SrNativeApi {
    uint32_t size;
    uint32_t api_version;
    void* context;
    KeelResult (*register_native)(void*, KeelPluginHandle owner, const SrContextNativeSpec*, SrRegistration*);
    KeelResult (*unregister_native)(void*, KeelPluginHandle owner, SrRegistration);
    /* Server thread only. Cells are followed by text, if non-NULL. At most 16
       total arguments and 4095 text bytes. OK/ENGINE_FAILURE consume the token;
       BUSY retains it during pause, loading or reentrant script execution.
       NOT_FOUND means it was canceled, consumed or its script retired. */
    KeelResult (*deliver_callback)(void*, KeelPluginHandle owner, SrCallback,
        const int32_t* cells, uint32_t count, const char* text);
    KeelResult (*cancel_callback)(void*, KeelPluginHandle owner, SrCallback);
    /* Complete server-thread snapshot; no partial output on failure. At most
       128 players. These identities are not script Player handles. */
    KeelResult (*player_snapshot)(void*, SrPlayerIdentity* players, uint32_t capacity, uint32_t* count);
} SrNativeApi;

#ifdef __cplusplus
}
#endif

#endif
