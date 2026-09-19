#include <source2root>

public bool OnPluginStart()
{
    return RegisterCommand("sr_status", "demo.status", ShowStatus);
}

public void ShowStatus(Player caller, const char[] arguments)
{
    if (arguments[0] != '\0')
    {
        ReplyToCommand(caller, "Usage: sr_status");
        return;
    }

    if (caller == NoPlayer)
    {
        ReplyToCommand(caller, "Use this command in the game.");
        return;
    }

    Menu menu = CreateMenu("Your player", "demo.status", StatusSelected);
    AddMenuItem(menu, "Show name");
    AddMenuItem(menu, "Show Steam ID");
    AddMenuItem(menu, "Check health");

    if (!ShowMenu(menu, caller))
    {
        CloseMenu(menu);
        ReplyToCommand(caller, "Could not open the menu.");
    }
}

public void StatusSelected(Player player, int item)
{
    if (!HasPermission(player, "demo.status"))
    {
        ReplyToCommand(player, "You do not have access to this command.");
        return;
    }

    char value[128];

    if (item == 0 && GetPlayerName(player, value, sizeof(value)))
        ReplyToCommand(player, value);
    else if (item == 1 && GetPlayerSteamID(player, value, sizeof(value)))
        ReplyToCommand(player, value);
    else if (item == 2)
    {
        int health;

        if (!GetPlayerHealth(player, health))
            ReplyToCommand(player, "Your health is unavailable.");
        else
            ReplyToCommand(player, health > 0 ? "You are alive." : "You are dead.");
    }
}
