#include <source2root_clientprefs>

PrefCookie music;
Menu menus[128];
Player owners[128];

public bool OnPluginStart()
{
    music = Prefs_RegisterCookie("example.music", "Enable music", Pref_Public);
    return music != NoPrefCookie && RegisterCommand("sr_preference", "", Preferences, "Choose your music preference.");
}

public void Preferences(Player player, const char[] arguments)
{
    if (player == NoPlayer) { ReplyToCommand(player, "Use this command while connected to the server."); return; }
    Prefs_WhenCookieReady(music, Registered, view_as<int>(player));
}

public void Registered(PrefRequest request, Player unused, any data, const char[] error)
{
    Prefs_CloseRequest(request);
    Player player = view_as<Player>(data);
    if (!IsPlayerConnected(player)) return;
    if (error[0]) { ReplyToCommand(player, "Preferences are temporarily unavailable."); LogMessage(error); return; }
    Prefs_WhenCached(player, Cached);
}

public void Cached(PrefRequest request, Player player, any data, const char[] error)
{
    Prefs_CloseRequest(request);
    if (error[0]) { ReplyToCommand(player, "Preferences are temporarily unavailable."); LogMessage(error); return; }
    int free = -1;
    for (int i = 0; i < sizeof(menus); ++i) {
        if (menus[i] != NoMenu && (owners[i] == player || !IsPlayerConnected(owners[i]))) {
            CloseMenu(menus[i]); menus[i] = NoMenu; owners[i] = NoPlayer;
        }
        if (menus[i] == NoMenu && free == -1) free = i;
    }
    if (free == -1) { ReplyToCommand(player, "The settings menu is busy. Try again shortly."); return; }
    Menu menu = CreateMenu("Music preference", "", Selected);
    if (menu == NoMenu) return;
    if (!AddMenuItem(menu, "On", true, 1) || !AddMenuItem(menu, "Off", true, 0) || !ShowMenu(menu, player)) {
        CloseMenu(menu); return;
    }
    menus[free] = menu; owners[free] = player;
}

public void Selected(Player player, int item)
{
    for (int i = 0; i < sizeof(menus); ++i) if (owners[i] == player) {
        CloseMenu(menus[i]); menus[i] = NoMenu; owners[i] = NoPlayer;
    }
    if (item < 0 || item > 1) return;
    char value[4];
    if (item == 1) Format(value, sizeof(value), "on");
    else Format(value, sizeof(value), "off");
    if (!Prefs_UserSet(player, "example.music", value)) {
        ReplyToCommand(player, "Your preference could not be changed."); return;
    }
    Prefs_WhenSaved(player, Saved);
}

public void Saved(PrefRequest request, Player player, any data, const char[] error)
{
    Prefs_CloseRequest(request);
    if (error[0]) { ReplyToCommand(player, "Your preference changed, but could not be saved yet."); LogMessage(error); }
    else ReplyToCommand(player, "Your music preference has been saved.");
}
