#include <source2root>
#include <source2root_random>

public bool OnPluginStart()
{
    return RegisterCommand("sr_roll", "", Roll, "sr_roll [sides]");
}

public void Roll(Player caller, const char[] arguments)
{
    int count = GetArgumentCount(arguments), sides = 6;
    char text[32];

    if (count < 0 || count > 1 || (count == 1
        && (!GetArgument(arguments, 0, text, sizeof(text)) || !ParseInt(text, sides, 1, 1000000))))
    {
        ReplyToCommand(caller, "Usage: sr_roll [sides: 1-1000000]");
        return;
    }

    char message[96];
    Format(message, sizeof(message), "Rolled %d of %d.", RandomInt(1, sides), sides);
    ReplyToCommand(caller, message);
}
