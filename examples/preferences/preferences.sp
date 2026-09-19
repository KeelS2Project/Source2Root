#include <source2root_clientprefs>

PrefCookie music;
PrefMenuItem musicMenu;
PrefMenu menus[16];
Player owners[16];

public bool OnPluginStart()
{
    music = Prefs_RegisterCookie("example.music", "Enable music", Pref_Public);

    if (music == NoPrefCookie)
        return false;

    musicMenu = Prefs_SetPrefabMenu(music, Pref_OnOff, "Music");
    return musicMenu != NoPrefMenuItem &&
           RegisterCommand("sr_preferences", "", Preferences, "Open your player preferences.");
}

public void Preferences(Player player, const char[] arguments)
{
    if (player == NoPlayer)
    {
        ReplyToCommand(player, "Use this command while connected to the server.");
        return;
    }

    if (Prefs_CookieState(music) != Pref_Ready || !Prefs_IsCached(player)) {
        ReplyToCommand(player, "Settings are loading. Try again shortly.");
        return;
    }

    int free = -1;

    for (int i = 0; i < sizeof(menus); ++i) {
        if (menus[i] != NoPrefMenu && (owners[i] == player || !IsPlayerConnected(owners[i]) || !Prefs_MenuOpen(menus[i]))) {
            Prefs_CloseMenu(menus[i]);
            menus[i] = NoPrefMenu;
            owners[i] = NoPlayer;
        }

        if (menus[i] == NoPrefMenu && free == -1)
            free = i;
    }

    if (free == -1)
    {
        ReplyToCommand(player, "The settings menu is busy. Try again shortly.");
        return;
    }

    menus[free] = Prefs_ShowMenu(player);

    if (menus[free] == NoPrefMenu)
        ReplyToCommand(player, "The settings menu is temporarily unavailable.");
    else
        owners[free] = player;
}
