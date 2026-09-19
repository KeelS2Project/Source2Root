#include <source2root_http>
char base[256], secure[256];
HttpRequest retained;

bool Same(const char[] left, const char[] right)
{
    for (int i = 0;; i++)
    {
        if (left[i] != right[i])
            return false;

        if (!left[i])
            return true;
    }
}

HttpRequest Request(const char[] path, HttpMethod method = Http_GET)
{
    char url[512];
    Format(url, sizeof(url), "%s%s", base, path);
    return HTTP_Create(url, method);
}

public bool OnPluginStart()
{
    ConfigFile config = OpenConfigFile("http-fixture.txt");

    if (!config || !ReadConfigLine(config, base, sizeof(base)) || !ReadConfigLine(config, secure, sizeof(secure)))
        return false;

    CloseConfigFile(config);

    if (HTTP_Create("file:///must-not-open"))
        return false;

    retained = Request("/hello");

    if (!retained || HTTP_SetHeader(retained, "Host", "bad") || HTTP_SetHeader(retained, "X-Value", "bad\r\nheader") ||
        HTTP_SetBody(retained, "GET body") || !HTTP_SetHeader(retained, "X-Script", "value") ||
        !HTTP_Send(retained, Hello, 73) || HTTP_Send(retained, Unexpected) || HTTP_SetBody(retained, "late"))
        return false;

    HttpRequest raw = Request("/echo", Http_POST);
    int bytes[] = {65, 0, 255, 90};

    if (!raw || !HTTP_AppendBytes(raw, bytes, sizeof(bytes)) || !HTTP_Send(raw, Binary))
        return false;

    HttpRequest form = Request("/echo", Http_POST);

    if (!form || !HTTP_Form(form, "word", "hello") || HTTP_FormFile(form, "file", "../upload.bin") ||
        !HTTP_FormFile(form, "file", "upload.bin") || !HTTP_Send(form, Formed))
        return false;

    char url[512];
    Format(url, sizeof(url), "%s/hello", secure);
    HttpRequest tls = HTTP_Create(url), bad = HTTP_Create(url);

    if (!tls || !bad || HTTP_TrustFile(tls, "../ca.pem") || !HTTP_TrustFile(tls, "ca.pem") ||
        !HTTP_TrustFile(bad, "bad-ca.pem") || !HTTP_Send(tls, TLS) || !HTTP_Send(bad, Failed))
        return false;

    HttpRequest canceled = Request("/delay");

    if (!canceled || !HTTP_Send(canceled, Unexpected) || !HTTP_Cancel(canceled) || !HTTP_IsDone(canceled))
        return false;

    HTTP_Close(canceled);
    LogMessage("HTTP_SCRIPT_START");
    return RegisterCommand("sr_http_check", "", CheckRetained) && RegisterCommand("sr_http_pending", "", Pending) &&
        RegisterCommand("sr_http_stale", "", Stale);
}

public
void Unexpected(HttpRequest request, any data, const char[] error)
{
    LogMessage("HTTP_SCRIPT_FAILED_UNEXPECTED");
}

public void Hello(HttpRequest request, any data, const char[] error)
{
    char text[256], name[128], value[256];
    int repeated;

    if (request != retained || data != 73 || error[0] || !HTTP_IsDone(request) || HTTP_Status(request) != 200 ||
        HTTP_Size(request) != 8 || HTTP_ReadText(request, text, sizeof(text)) != 8 || !Same(text, "hello é") ||
        HTTP_ReadText(request, text, sizeof(text), 8) != 0 || !HTTP_URL(request, text, sizeof(text)))
    {
        LogMessage("HTTP_SCRIPT_FAILED_HELLO");
        return;
    }

    for (int i = 0; i < HTTP_HeaderCount(request); i++) {
        if (!HTTP_HeaderName(request, i, name, sizeof(name)) || !HTTP_HeaderValue(request, i, value, sizeof(value)))
        {
            LogMessage("HTTP_SCRIPT_FAILED_HEADER");
            return;
        }

        if (Same(name, "x-repeated"))
            repeated++;
    }

    if (repeated != 2)
        LogMessage("HTTP_SCRIPT_FAILED_REPEATED");
    else
        LogMessage("HTTP_SCRIPT_HELLO");
}

public void Binary(HttpRequest request, any data, const char[] error)
{
    char text[16];
    int bytes[8];

    if (error[0] || HTTP_Status(request) != 200 || HTTP_Size(request) != 4 ||
        HTTP_ReadText(request, text, sizeof(text)) != -1 || text[0] ||
        HTTP_ReadBytes(request, bytes, sizeof(bytes)) != 4 || bytes[0] != 65 || bytes[1] || bytes[2] != 255 ||
        bytes[3] != 90 || bytes[4])
        LogMessage("HTTP_SCRIPT_FAILED_BINARY");
    else
        LogMessage("HTTP_SCRIPT_BINARY");

    HTTP_Close(request);
}

public void Formed(HttpRequest request, any data, const char[] error)
{
    int bytes[1024];

    if (error[0] || HTTP_Status(request) != 200 || HTTP_Size(request) < 100 ||
        HTTP_ReadBytes(request, bytes, sizeof(bytes)) < 100)
        LogMessage("HTTP_SCRIPT_FAILED_FORM");
    else
        LogMessage("HTTP_SCRIPT_FORM");

    HTTP_Close(request);
}

public void TLS(HttpRequest request, any data, const char[] error)
{
    if (error[0] || HTTP_Status(request) != 200)
        LogMessage("HTTP_SCRIPT_FAILED_TLS");
    else
        LogMessage("HTTP_SCRIPT_TLS");

    HTTP_Close(request);
}

public void Failed(HttpRequest request, any data, const char[] error)
{
    char stored[256];

    if (!error[0] || !HTTP_IsDone(request) || !HTTP_Error(request, stored, sizeof(stored)) || !stored[0] ||
        HTTP_Status(request))
        LogMessage("HTTP_SCRIPT_FAILED_ERROR");
    else
        LogMessage("HTTP_SCRIPT_ERROR");

    HTTP_Close(request);
}

public void CheckRetained(Player player, const char[] arguments)
{
    char text[32];

    if (HTTP_ReadText(retained, text, sizeof(text)) != 8 || !Same(text, "hello é"))
        LogMessage("HTTP_SCRIPT_FAILED_RETAINED");
    else
        LogMessage("HTTP_SCRIPT_RETAINED");
}

public void Pending(Player player, const char[] arguments)
{
    HttpRequest pending = Request("/delay");

    if (!pending || !HTTP_Send(pending, Unexpected))
        LogMessage("HTTP_SCRIPT_FAILED_PENDING");
}

public void Stale(Player player, const char[] arguments)
{
    HttpRequest stale = Request("/hello");
    HTTP_Close(stale);
    HTTP_Status(stale);
    LogMessage("HTTP_SCRIPT_FAILED_STALE");
}
