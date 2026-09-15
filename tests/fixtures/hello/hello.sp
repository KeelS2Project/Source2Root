#include <source2root>
native int SR_ExampleAdd(int left, int right);

public bool OnPluginStart()
{
    if (!RegisterCommand("sr_hello", "demo.hello", Hello))
        return false;
    if (!ListenEvent("round_start", RoundStart))
        return false;
    LogMessage("hello started");
    return true;
}

public void Hello(Player caller, const char[] arguments)
{
    if (SR_ExampleAdd(24, 18) != 42)
        return;
    ReplyToCommand(caller, "SourcePawn and the C++ extension returned 42.");
    CreateTimer(250, Later, caller);
    if (caller == NoPlayer)
        return;
    Menu menu = CreateMenu("Source2Root foundation", "demo.hello", Selected);
    AddMenuItem(menu, "Say hello");
    AddMenuItem(menu, "Unavailable action", false);
    AddMenuItem(menu, "Show identity");
    AddMenuItem(menu, "Read player health");
    AddMenuItem(menu, "Second page");
    if (!ShowMenu(menu, caller))
        CloseMenu(menu);
}

public void Later(Player target)
{
    ReplyToCommand(target, "The delayed SourcePawn callback ran.");
}

public void RoundStart(const char[] event)
{
    LogMessage("hello received round_start");
}

public void Selected(Player player, int item)
{
    if (!HasPermission(player, "demo.hello"))
        return;
    if (item == 3)
    {
        int health;
        if (GetPlayerHealth(player, health))
            ReplyToCommand(player, health > 0 ? "Your current pawn is alive." : "Your current pawn has no health.");
        else
            ReplyToCommand(player, "Your pawn health is unavailable.");
        return;
    }
    char identity[32];
    if (item == 2 && GetPlayerSteamID(player, identity, sizeof(identity)))
        ReplyToCommand(player, identity);
    else
        ReplyToCommand(player, "The menu selection ran in SourcePawn.");
}

public void OnPluginStop()
{
    LogMessage("hello stopped");
}
