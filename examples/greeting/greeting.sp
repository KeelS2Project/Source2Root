#include <source2root>

ConVar enabled;
ConVar message;

public bool OnPluginStart()
{
    enabled = CreateIntConVar("sr_greeting_enabled", 1, "Enable the greeting command", 0, 1);
    message = CreateStringConVar("sr_greeting_message", "Welcome to the server!", "Greeting command reply");
    return enabled != NoConVar && message != NoConVar
        && RegisterCommand("sr_greeting", "", Greeting);
}

public void Greeting(Player caller, const char[] arguments)
{
    if (arguments[0])
    {
        ReplyToCommand(caller, "Usage: sr_greeting");
        return;
    }
    int active;
    char text[513];
    if (!GetConVarInt(enabled, active) || !GetConVarString(message, text, sizeof(text)))
    {
        ReplyToCommand(caller, "Could not read the greeting settings.");
        return;
    }
    ReplyToCommand(caller, active ? text : "The greeting command is disabled.");
}
