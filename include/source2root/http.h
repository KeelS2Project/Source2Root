#ifndef SOURCE2ROOT_HTTP_H
#define SOURCE2ROOT_HTTP_H
#include <keels2/plugin.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
#define SR_HTTP_SERVICE "source2root.http"
#define SR_HTTP_API_VERSION 1u
typedef enum SrHttpMethod { SR_HTTP_GET, SR_HTTP_HEAD, SR_HTTP_POST, SR_HTTP_PUT, SR_HTTP_PATCH, SR_HTTP_DELETE } SrHttpMethod;
typedef struct SrHttpHeader { const char* name; const char* value; } SrHttpHeader;
typedef struct SrHttpPart {
    const char* name;
    const uint8_t* data;
    uint32_t data_size;
    const char* filename;
    const char* content_type;
    /* Optional local file instead of data; read into bounded memory on caller's
       worker thread. Caller is responsible for authorizing its path. */
    const char* source_file;
} SrHttpPart;
typedef struct SrHttpRequest {
    uint32_t size;
    SrHttpMethod method;
    const char* url;
    const SrHttpHeader* headers;
    uint32_t header_count;
    const uint8_t* body;
    uint32_t body_size;
    const SrHttpPart* parts;
    uint32_t part_count;
    const char* ca_file; /* optional PEM trust file; default is platform trust */
    uint32_t timeout_ms; /* 100..60000 including redirects */
    uint32_t response_limit; /* 1..1048576 decompressed bytes across all hops */
    uint32_t redirects; /* 0..5; zero exposes a redirect response unchanged */
} SrHttpRequest;
typedef struct SrHttpResponse {
    uint32_t size;
    uint32_t status;
    const char* url;
    const SrHttpHeader* headers;
    uint32_t header_count;
    const uint8_t* body;
    uint32_t body_size;
} SrHttpResponse;
typedef KeelBool (*SrHttpCanceled)(void* user_data);
typedef void (*SrHttpComplete)(void* user_data, const SrHttpResponse* response);
typedef struct SrHttpApi {
    uint32_t size;
    uint32_t api_version;
    void* context;
    /* Synchronous WORKER-THREAD ONLY API. Acquire the Keel service lease on the
       server thread and retain it until all Perform calls/callbacks finish.
       Join workers before releasing that lease or unloading the consumer.
       Input must remain valid for the call. Completion runs on this same worker;
       its response pointers are borrowed only for callback duration. Neither
       callback may invoke engine/VM/host APIs or throw across this C boundary.
       At most 8 concurrent native calls. Request input/body/form data <=1MiB,
       <=32 headers/parts; response headers <=64KiB and128fields. No proxy/netrc,
       implicit cookies, URL credentials or TLS verification bypass. Redirects
       strip all custom headers across origins and refuse upload forwarding or
       HTTPS downgrade. Transport errors return failure, without a partial
       response. HTTP4xx/5xx are completed responses; inspect status explicitly.
       Polling and curl timeouts bound ordinary I/O; OS DNS/file I/O can exceed
       the requested deadline. Canceling cannot undo remote side effects. */
    KeelResult (*perform)(void* context, const SrHttpRequest* request, SrHttpCanceled canceled,
        SrHttpComplete complete, void* user_data, char* error, uint32_t error_capacity);
} SrHttpApi;
#ifdef __cplusplus
}
#endif
#endif
