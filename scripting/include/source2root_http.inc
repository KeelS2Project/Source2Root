#if defined _source2root_http_included
    #endinput
#endif
#define _source2root_http_included
#include <source2root>

enum HttpRequest { NoHttpRequest = 0 }
enum HttpMethod { Http_GET, Http_HEAD, Http_POST, Http_PUT, Http_PATCH, Http_DELETE }
typedef HttpCallback = function void(HttpRequest request, any data, const char[] error);

// HTTP/HTTPS only; URL <=4095 bytes, no user credentials or IPv6 zone IDs.
// TLS peer/hostname verification is always enabled, minimum TLS1.2. No implicit
// proxy, netrc, cookie jar or ambient credentials. Explicit headers can carry
// authorization/cookies. Trusted scripts can access public or private endpoints.
// Provider limits:64requests,32queued/running/paused completions,2workers.
// Each request is mutable until submitted exactly once, then immutable. Domain
// failures set GetLastError; wrong/stale/foreign handles raise native errors.
native HttpRequest HTTP_Create(const char[] url, HttpMethod method = Http_GET);
// Close cancels pending work and suppresses callbacks; it cannot undo remote writes.
native bool HTTP_Close(HttpRequest request);
// Replaces a case-insensitive header; empty value sends an empty header. At most
//32headers/16KiB. CR/LF and framing/proxy headers are rejected. Values <=4095bytes.
native bool HTTP_SetHeader(HttpRequest request, const char[] name, const char[] value);
// Raw body and multipart form are mutually exclusive. GET/HEAD cannot have a body.
// SetBody replaces raw text; AppendBytes adds up to1024cells, each0..255. The
// aggregate raw/form input is bounded to1MiB; upload bodies may contain NUL bytes.
native bool HTTP_SetBody(HttpRequest request, const char[] text);
native bool HTTP_AppendBytes(HttpRequest request, const int[] bytes, int count);
// Multipart POST only, at most32parts. Text field names <=128bytes; values <=4095.
native bool HTTP_Form(HttpRequest request, const char[] name, const char[] value);
// Simple filename under data/<script-id> (the script's GetDataPath directory);
// no paths/symlinks. File is read into bounded memory by the worker at send time.
// Operators must finish updating upload files before sending; the file is not
// snapshotted when configuring the builder. File and inline fields total <=1MiB.
native bool HTTP_FormFile(HttpRequest request, const char[] name, const char[] filename, const char[] contentType = "application/octet-stream");
// Total timeout100..60000ms, response1..1048576decompressed bytes, redirects0..5.
// Defaults10000ms/256KiB/3. Limits include discarded intermediate redirect bodies.
// Headers across all hops <=64KiB, final headers <=128fields/4095bytes per value.
// Redirects preserve same-origin headers; crossing origin strips all custom
// headers and refuses body/method forwarding except GET/HEAD. HTTPS downgrade
// is rejected. POST301/302 and non-HEAD303 become GET,307/308 preserve methods.
// Zero redirects returns the original3xx response. Reaching an enabled limit is
// an error, with no partial output. Compressed response limits apply after decode.
native bool HTTP_Limits(HttpRequest request, int timeoutMs, int responseBytes, int redirects);
// Optional operator-provided PEM file under configs/extensions/source2root.http.
// Simple filename only; empty restores default platform trust. No insecure mode.
native bool HTTP_TrustFile(HttpRequest request, const char[] filename);
// Callback executes on the server thread; delivery waits while script is paused.
// Close/cancel/script retirement suppress it. Callback may close its own request.
// Queue failure leaves the builder editable. Transfer uses a copied input.
// Cancellation is cooperative; OS DNS/file operations can exceed the timeout.
native bool HTTP_Send(HttpRequest request, HttpCallback callback, any data = 0);
// Cancel suppresses a pending callback (even if transport completed while paused).
// IsDone then returns true, Error explains cancellation, and no response is exposed.
native bool HTTP_Cancel(HttpRequest request);
native bool HTTP_IsDone(HttpRequest request);
native bool HTTP_Error(HttpRequest request, char[] output, int capacity);

// HTTP4xx/5xx are transport successes; inspect Status. Transport failure/cancel
// exposes no partial headers/body. Before completion these calls set GetLastError.
native int HTTP_Status(HttpRequest request);
// Size/HeaderCount return -1 on failure. Read functions return bytes copied,
//0at end, -1on failure. Bytes are raw data; text reads do not align UTF8 boundaries.
native int HTTP_Size(HttpRequest request);
// Reads <=capacity-1bytes from byte offset; chunk containing NUL fails/clears output.
native int HTTP_ReadText(HttpRequest request, char[] output, int capacity, int offset = 0);
// Reads <=capacity bytes (one byte per cell); capacity1..1024. Clears output first.
native int HTTP_ReadBytes(HttpRequest request, int[] output, int capacity, int offset = 0);
// Repeated headers are preserved in arrival order; names normalized lowercase.
// Header indices start at zero; buffers must hold the entire header name/value.
native int HTTP_HeaderCount(HttpRequest request);
native bool HTTP_HeaderName(HttpRequest request, int index, char[] output, int capacity);
native bool HTTP_HeaderValue(HttpRequest request, int index, char[] output, int capacity);
native bool HTTP_URL(HttpRequest request, char[] output, int capacity);
