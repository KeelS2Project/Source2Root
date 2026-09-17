#include <source2root_http>
public bool OnPluginStart() { return RegisterCommand("sr_http", "server.http", Fetch); }
public void Fetch(Player player, const char[] arguments)
{
    HttpRequest request = HTTP_Create(arguments);
    if (!request || !HTTP_Send(request, Completed)) {
        char error[256]; GetLastError(error, sizeof(error)); ReplyToCommand(player, error);
        if (request) HTTP_Close(request);
    } else ReplyToCommand(player, "Request submitted; completion will be logged.");
}
public void Completed(HttpRequest request, any data, const char[] error)
{
    if (error[0]) LogMessage(error);
    else {
        char message[128];
        Format(message, sizeof(message), "HTTP status %d, response %d bytes.", HTTP_Status(request), HTTP_Size(request));
        LogMessage(message);
    }
    HTTP_Close(request);
}
