#include <source2root_clientprefs>

PrefCookie cookies[3];
Player client;
int registered;

public bool OnPluginStart()
{
    cookies[0] = Prefs_RegisterCookie("music", "Music preference", Pref_Public);
    cookies[1] = Prefs_RegisterCookie("rank", "Server-managed rank", Pref_Protected);
    cookies[2] = Prefs_RegisterCookie("internal", "Internal value", Pref_Private);
    // Repeated close must release both token and wait bookkeeping immediately.
    for (int i = 0; i < 300; ++i) {
        PrefRequest canceled = Prefs_WhenCookieReady(cookies[0], Unexpected);
        if (!canceled || !Prefs_CloseRequest(canceled)) return false;
    }
    for (int i = 0; i < sizeof(cookies); ++i)
        if (!cookies[i] || !Prefs_WhenCookieReady(cookies[i], CookieReady, i)) return false;
    return RegisterCommand("sr_prefs_pending", "", Pending) && RegisterCommand("sr_prefs_final", "", FinalWrite);
}

public void CookieReady(PrefRequest request, Player player, any data, const char[] error)
{
    Prefs_CloseRequest(request);
    if (player != NoPlayer || error[0] || Prefs_CookieState(cookies[data]) != Pref_Ready) {
        LogMessage("PREFS_FAILED_REGISTRATION"); return;
    }
    if (++registered != 3) return;
    Player players[2];
    if (!Prefs_IsReady() || GetPlayers(players, sizeof(players)) != 1) { LogMessage("PREFS_FAILED_PLAYERS"); return; }
    client = players[0];
    Prefs_WhenCached(client, Cached);
}

public void Cached(PrefRequest request, Player player, any data, const char[] error)
{
    Prefs_CloseRequest(request);
    char value[256], name[31];
    if (error[0] || player != client || data || !Prefs_IsCached(player) ||
        Prefs_UserCookieCount() != 2 || !Prefs_UserCookieName(0, name, sizeof(name)) || name[0] != 'm' ||
        Prefs_GetAccess(cookies[1]) != Pref_Protected ||
        Prefs_UserSet(player, "rank", "forbidden") || Prefs_UserSet(player, "internal", "forbidden") ||
        !Prefs_Set(player, cookies[2], "script-private") || !Prefs_UserSet(player, "music", "first") ||
        !Prefs_Set(player, cookies[0], "latest") || !Prefs_Get(player, cookies[0], value, sizeof(value)) || value[0] != 'l') {
        LogMessage("PREFS_FAILED_CACHE"); return;
    }
    Prefs_WhenSaved(player, Saved);
}

public void Saved(PrefRequest request, Player player, any data, const char[] error)
{
    Prefs_CloseRequest(request);
    char value[256], timestamp[32];
    if (error[0] || player != client || data || !Prefs_IsSaved(player) ||
        !Prefs_Get(player, cookies[0], value, sizeof(value)) || value[0] != 'l' ||
        !Prefs_GetTime(player, cookies[0], timestamp, sizeof(timestamp)) || timestamp[0] == '0') {
        LogMessage("PREFS_FAILED_SAVE"); return;
    }
    LogMessage("PREFS_SCRIPT_OK");
}

public void Pending(Player caller, const char[] arguments)
{
    Prefs_WhenCached(client, Unexpected);
    Prefs_WhenSaved(client, Unexpected);
}
public void FinalWrite(Player caller, const char[] arguments)
{
    if (!Prefs_Set(client, cookies[0], "on-unload")) LogMessage("PREFS_FAILED_FINAL");
    Prefs_WhenSaved(client, Unexpected);
}
public void Unexpected(PrefRequest request, Player player, any data, const char[] error)
{
    Prefs_CloseRequest(request);
    LogMessage("PREFS_UNEXPECTED_CALLBACK");
}
