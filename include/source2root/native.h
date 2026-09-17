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
} SrNativeApi;

#ifdef __cplusplus
}
#endif

#endif
