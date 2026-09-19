#include <source2root>

public bool OnPluginStart()
{
    return RegisterCommand("sr_health", "", Health);
}

public void Health(Player player, const char[] arguments)
{
    int health = 999;

    if (GetPlayerHealth(player, health))
        ReplyToCommand(player, health == 73 ? "entity health 73" : "unexpected entity health");
    else
    {
        char error[128];
        GetLastError(error, sizeof(error));
        ReplyToCommand(player, error);
    }
}
