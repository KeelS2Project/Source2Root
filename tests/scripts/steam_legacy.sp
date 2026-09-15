#pragma newdecls required

enum Player { NoPlayer = 0 }
typedef CommandCallback = function void (Player caller, const char[] arguments);
native bool RegisterCommand(const char[] name, const char[] permission, CommandCallback callback);
native bool GetPlayerSteamID(Player player, char[] output, int capacity);
native bool ReplyToCommand(Player player, const char[] message);

enum Timer { NoTimer = 0 }
typedef TimerCallback = function void (Player target);
native Timer CreateTimer(int milliseconds, TimerCallback callback, Player target = NoPlayer);
native bool CancelTimer(Timer timer);
public void Expired(Player target) {}

enum Menu { NoMenu = 0 }
typedef MenuCallback = function void (Player caller, int item);
native bool ShowActivity(Player caller, const char[] action);
native Menu CreateMenu(const char[] title, const char[] permission, MenuCallback callback);
native bool AddMenuItem(Menu menu, const char[] text, bool enabled = true);
native bool ShowMenu(Menu menu, Player target, int timeoutMilliseconds = 15000);

public bool OnPluginStart()
{
    if (!CancelTimer(CreateTimer(250, Expired))) return false;
    return RegisterCommand("sr_legacy_steam", "", Read)
        && RegisterCommand("sr_legacy_activity", "", Activity)
        && RegisterCommand("sr_legacy_menu", "", Open);
}
public void Activity(Player caller, const char[] arguments) { ShowActivity(caller, "used older activity bytecode"); }
public void Open(Player caller, const char[] arguments)
{
    Menu menu = CreateMenu("Older menu bytecode", "", Selected);
    AddMenuItem(menu, "Select");
    ShowMenu(menu, caller);
}
public void Selected(Player caller, int item)
{
    if (item == 0) ReplyToCommand(caller, "legacy index=0");
}

public void Read(Player caller, const char[] arguments)
{
    char identity[24];
    if (GetPlayerSteamID(caller, identity, sizeof(identity))) ReplyToCommand(caller, identity);
}
