#include <source2root_clientprefs>
#if defined PREFS_NETWORK
#include <source2root_database>
void NetworkQuery()
{
    SQLQuery query = SQL_CreateQuery("SELECT SLEEP(0.05),73");
    SQLRequest request = SQL_ExecuteAsync("acceptance", query, NetworkSaved);
    SQL_CloseQuery(query);

    if (!request)
        LogMessage("PREFS_FAILED_NETWORK_REQUEST");
}

public void NetworkSaved(SQLRequest request, any data, const char[] error)
{
    int value;

    if (error[0] || !SQL_ResultInt(request, 0, 1, value) || value != 73)
        LogMessage("PREFS_FAILED_NETWORK_QUERY");
    else
        LogMessage("PREFS_NETWORK_OK");

    SQL_CloseRequest(request);
}
#endif

PrefCookie cookies[3];
Player client;
int registered;
PrefMenuItem prefab;
PrefMenu settings;
int menuType;

public bool OnPluginStart()
{
#if defined PREFS_NETWORK
    NetworkQuery();
#endif
    cookies[0] = Prefs_RegisterCookie("music", "Music preference", Pref_Public);
    cookies[1] = Prefs_RegisterCookie("rank", "Server-managed rank", Pref_Protected);
    cookies[2] = Prefs_RegisterCookie("internal", "Internal value", Pref_Private);
    prefab = Prefs_SetPrefabMenu(cookies[0], Pref_YesNo, "Music setting");

    if (!prefab || Prefs_SetPrefabMenu(cookies[1], Pref_YesNo, "Rank") ||
        Prefs_SetPrefabMenu(cookies[2], Pref_YesNo, "Private"))
        return false;
    // Repeated close must release both token and wait bookkeeping immediately.
    for (int i = 0; i < 300; ++i) {
        PrefRequest canceled = Prefs_WhenCookieReady(cookies[0], Unexpected);

        if (!canceled || !Prefs_CloseRequest(canceled))
            return false;
    }

    for (int i = 0; i < sizeof(cookies); ++i)
        if (!cookies[i] || !Prefs_WhenCookieReady(cookies[i], CookieReady, i))
            return false;

    return RegisterCommand("sr_prefs_pending", "", Pending) && RegisterCommand("sr_prefs_final", "", FinalWrite) &&
        RegisterCommand("sr_prefs_menu", "", ShowSettings) && RegisterCommand("sr_prefs_type", "", SetType) &&
        RegisterCommand("sr_prefs_observe", "", Observe) && RegisterCommand("sr_prefs_drop", "", DropPrefab) &&
        RegisterCommand("sr_prefs_close", "", CloseSettings) && RegisterCommand("sr_prefs_offline", "", Offline);
}

public void CookieReady(PrefRequest request, Player player, any data, const char[] error)
{
    Prefs_CloseRequest(request);

    if (player != NoPlayer || error[0] || Prefs_CookieState(cookies[data]) != Pref_Ready) {
        LogMessage("PREFS_FAILED_REGISTRATION");
        return;
    }

    if (++registered != 3)
        return;

    Player players[2];

    if (!Prefs_IsReady() || GetPlayers(players, sizeof(players)) != 1)
    {
        LogMessage("PREFS_FAILED_PLAYERS");
        return;
    }

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
        LogMessage("PREFS_FAILED_CACHE");
        return;
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
        LogMessage("PREFS_FAILED_SAVE");
        return;
    }

    if (!Prefs_Refresh(player) || Prefs_IsCached(player) || !Prefs_WhenCached(player, Refreshed))
        LogMessage("PREFS_FAILED_REFRESH");
}

public void Refreshed(PrefRequest request, Player player, any data, const char[] error)
{
    Prefs_CloseRequest(request);
    char value[256];

    if (error[0] || !Prefs_IsCached(player) || !Prefs_Get(player, cookies[0], value, sizeof(value)) || value[0] != 'l')
        LogMessage("PREFS_FAILED_REFRESH_CACHE");
    else
        LogMessage("PREFS_SCRIPT_OK");
}

public void Pending(Player caller, const char[] arguments)
{
    Player players[2];

    if (GetPlayers(players, sizeof(players)) != 1)
    {
        LogMessage("PREFS_FAILED_PENDING_PLAYER");
        return;
    }

    client = players[0];

    if (!Prefs_WhenCached(client, Unexpected) || !Prefs_WhenSaved(client, Unexpected))
        LogMessage("PREFS_FAILED_PENDING");
}

public void FinalWrite(Player caller, const char[] arguments)
{
    if (!Prefs_Set(client, cookies[0], "on-unload"))
        LogMessage("PREFS_FAILED_FINAL");

    Prefs_WhenSaved(client, Unexpected);

    if (!Prefs_SetIdentity("76561198000000002", cookies[0], "offline-on-unload") ||
        !Prefs_WhenIdentitySaved("76561198000000002", Unexpected)) LogMessage("PREFS_FAILED_OFFLINE_FINAL");
}

public void Offline(Player caller, const char[] arguments)
{
    if (Prefs_SetIdentity("display name", cookies[0], "invalid") ||
        Prefs_SetIdentity("076561198000000001", cookies[0], "invalid") ||
        Prefs_SetIdentity("76561198000000001x", cookies[0], "invalid") ||
        !Prefs_SetIdentity("76561198000000001", cookies[0], "offline") ||
        !Prefs_SetIdentity("76561198000000001", cookies[0], "offline-latest") ||
        Prefs_IsIdentitySaved("76561198000000001") ||
        !Prefs_WhenIdentitySaved("76561198000000001", OfflineSaved, 42)) LogMessage("PREFS_FAILED_OFFLINE");
}

public void OfflineSaved(PrefRequest request, Player player, any data, const char[] error)
{
    Prefs_CloseRequest(request);
    char failure[256];

    if (error[0] || player != NoPlayer || data != 42 || !Prefs_IsIdentitySaved("76561198000000001") ||
        !Prefs_IdentityError("76561198000000001", failure, sizeof(failure)) || failure[0])
        LogMessage("PREFS_FAILED_OFFLINE_SAVE");
    else
        LogMessage("PREFS_OFFLINE_SAVED");
}

public void Unexpected(PrefRequest request, Player player, any data, const char[] error)
{
    Prefs_CloseRequest(request);
    LogMessage("PREFS_UNEXPECTED_CALLBACK");
}

public void ShowSettings(Player caller, const char[] arguments)
{
    if (settings != NoPrefMenu)
        Prefs_CloseMenu(settings);

    settings = Prefs_ShowMenu(client);

    if (!settings || !Prefs_MenuOpen(settings))
        LogMessage("PREFS_FAILED_MENU");
}

public void SetType(Player caller, const char[] arguments)
{
    if (!ParseInt(arguments, menuType, 0, 3))
    {
        LogMessage("PREFS_FAILED_TYPE");
        return;
    }

    if (prefab != NoPrefMenuItem)
        Prefs_ClosePrefabMenu(prefab);

    prefab = Prefs_SetPrefabMenu(cookies[0], view_as<PrefMenuType>(menuType), "Music setting");

    if (!prefab)
        LogMessage("PREFS_FAILED_PREFAB");
}

public void CloseSettings(Player caller, const char[] arguments)
{
    if (settings != NoPrefMenu)
    {
        Prefs_CloseMenu(settings);
        settings = NoPrefMenu;
    }
}

public void DropPrefab(Player caller, const char[] arguments)
{
    Prefs_ClosePrefabMenu(prefab);
    prefab = NoPrefMenuItem;
}

public void Observe(Player caller, const char[] arguments)
{
#if defined PREFS_NETWORK
    NetworkQuery();
#endif
    Prefs_WhenSaved(client, Observed, menuType);
}

public void Observed(PrefRequest request, Player player, any data, const char[] error)
{
    Prefs_CloseRequest(request);
    char value[256], message[320];

    if (error[0] || !Prefs_Get(player, cookies[0], value, sizeof(value)))
    {
        LogMessage("PREFS_FAILED_OBSERVE");
        return;
    }

    Format(message, sizeof(message), "PREFS_MENU_VALUE_%d:%s", data, value);
    LogMessage(message);
    LogMessage("PREFS_MENU_SAVED");
}
