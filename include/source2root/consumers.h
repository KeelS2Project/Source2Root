#ifndef SOURCE2ROOT_CONSUMERS_H
#define SOURCE2ROOT_CONSUMERS_H

#include <source2root/native.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SR_CONSUMER_SERVICE "source2root.consumers"
#define SR_CONSUMER_API_VERSION 1u

typedef struct SrConsumerApi {
    uint32_t size;
    uint32_t api_version;
    void* context;
    /* Game thread only. The consumer must be a running script generation
       using this provider. Resolves the exact connection into that script's
       Player handle; handles remain subject to normal script resource limits. */
    KeelResult (*player_handle)(void*, KeelPluginHandle provider, uint64_t consumer,
        const KeelPlayerConnection* player, int32_t* handle);

    /* Rechecks the connection and current core permission rules. Empty
       permission permits any current player. Output is false on failure. */
    KeelResult (*check_permission)(void*, KeelPluginHandle provider, uint64_t consumer,
        const KeelPlayerConnection* player, const char* permission, KeelBool* allowed);
} SrConsumerApi;

#ifdef __cplusplus
}
#endif
#endif
