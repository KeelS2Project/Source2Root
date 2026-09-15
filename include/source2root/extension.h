#ifndef SOURCE2ROOT_EXTENSION_H
#define SOURCE2ROOT_EXTENSION_H

#include <keels2/plugin.h>
#include <keels2/players.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SR_EXTENSION_SERVICE "source2root.extensions"
#define SR_EXTENSION_API_VERSION 1u

typedef uint64_t SrRegistration;
typedef uint64_t SrMenuSession;

typedef KeelResult (*SrNativeFunction)(void* user_data, const int32_t* arguments,
    uint32_t count, int32_t* result, char* error, uint32_t error_capacity);

typedef struct SrNativeSpec {
    uint32_t size;
    uint32_t api_version;
    const char* name;
    uint32_t argument_count;
    uint32_t reserved;
    const char* provider_service;
    uint32_t provider_version;
    SrNativeFunction invoke;
    void* user_data;
} SrNativeSpec;

typedef struct SrMenuItem {
    const char* text;
    KeelBool enabled;
} SrMenuItem;

typedef void (*SrMenuCallback)(void* user_data, const KeelPlayerConnection* player, int32_t item);

typedef struct SrMenuSpec {
    uint32_t size;
    uint32_t api_version;
    const char* title;
    const char* permission;
    const SrMenuItem* items;
    uint32_t item_count;
    uint32_t timeout_milliseconds;
    SrMenuCallback selected;
    void* user_data;
    const char* provider_service;
    uint32_t provider_version;
} SrMenuSpec;

typedef struct SrExtensionApi {
    uint32_t size;
    uint32_t api_version;
    void* context;
    KeelResult (*register_native)(void* context, KeelPluginHandle owner,
        const SrNativeSpec* spec, SrRegistration* registration);
    KeelResult (*unregister_native)(void* context, KeelPluginHandle owner, SrRegistration registration);
    KeelResult (*open_menu)(void* context, KeelPluginHandle owner,
        const KeelPlayerConnection* player, const SrMenuSpec* spec, SrMenuSession* session);
    KeelResult (*close_menu)(void* context, KeelPluginHandle owner, SrMenuSession session);
} SrExtensionApi;

#ifdef __cplusplus
}
#endif

#endif
