#if defined _source2root_clientprefs_included
    #endinput
#endif
#define _source2root_clientprefs_included
#include <source2root>

// Storage defaults to data/extensions/source2root.clientprefs/clientprefs.sqlite.
// An optional configs/extensions/source2root.clientprefs/databases.json selects
// profile "clientprefs" (SQLite or MySQL/MariaDB), allowing source2root.clientprefs.
// Configuration is read on workers; credentials/CA can be repaired then retried.
// Changing the database target requires draining writes and reloading the
// extension. A missing/broken remote configuration never falls back to SQLite.

enum PrefCookie { NoPrefCookie = 0 }
enum PrefRequest { NoPrefRequest = 0 }
enum PrefAccess { Pref_Public, Pref_Protected, Pref_Private }
enum PrefState { Pref_Loading, Pref_Ready, Pref_Failed }
enum PrefMenuItem { NoPrefMenuItem = 0 }
enum PrefMenu { NoPrefMenu = 0 }
enum PrefMenuType { Pref_YesNo, Pref_YesNoInteger, Pref_OnOff, Pref_OnOffInteger }

// Called on the server thread. Empty error means success. Catalog/cookie
// callbacks receive NoPlayer. Close the request when finished. Pause retains
// callbacks; script unload, request close or player disconnect suppresses them.
typedef PrefCallback = function void (PrefRequest request, Player player, any data, const char[] error);

// Registration is asynchronous. Names are global, 1..30 letters/digits/._-;
// descriptions and values are at most 255 bytes. Matching registrations share
// one definition. Conflicting persisted descriptions/access modes fail; inspect
// Prefs_CookieState/Error or use Prefs_WhenCookieReady before reading/writing.
native PrefCookie Prefs_RegisterCookie(const char[] name, const char[] description, PrefAccess access);
native PrefCookie Prefs_FindCookie(const char[] name);
native bool Prefs_CloseCookie(PrefCookie cookie);
native PrefState Prefs_CookieState(PrefCookie cookie);
native bool Prefs_CookieError(PrefCookie cookie, char[] output, int capacity);
native PrefAccess Prefs_GetAccess(PrefCookie cookie);
native bool Prefs_IsReady();

// Caches automatically follow authenticated human connections after first use
// of this extension. Bots/pending IDs and stale/foreign Player handles cannot
// access them. Prefs_Get returns an empty value for an unset registered cookie.
native bool Prefs_IsCached(Player player);
native bool Prefs_Get(Player player, PrefCookie cookie, char[] output, int capacity);
// Success acknowledges the cache only. Accepted writes survive script unload
// and player disconnect; failures stay dirty. Use WhenSaved/IsSaved to observe
// successful database commit. A process crash can lose uncommitted cache changes.
native bool Prefs_Set(Player player, PrefCookie cookie, const char[] value);
native bool Prefs_IsSaved(Player player);
// Decimal Unix seconds, including timestamps beyond a 32-bit SourcePawn cell.
native bool Prefs_GetTime(Player player, PrefCookie cookie, char[] output, int capacity);
native bool Prefs_PlayerError(Player player, char[] output, int capacity);
native PrefRequest Prefs_WhenReady(PrefCallback callback, any data = 0);
native PrefRequest Prefs_WhenCookieReady(PrefCookie cookie, PrefCallback callback, any data = 0);
native PrefRequest Prefs_WhenCached(Player player, PrefCallback callback, any data = 0);
native PrefRequest Prefs_WhenSaved(Player player, PrefCallback callback, any data = 0);
native bool Prefs_CloseRequest(PrefRequest request);
// Retry catalog/failed writes (including disconnected accounts) after fixing
// storage. A player also retries that account's failed load. NoPlayer supports
// operator recovery with nobody connected. Unload refuses accepted dirty writes.
native bool Prefs_Retry(Player player = NoPlayer);
// Shared SQL storage synchronizes durable values, not live caches. Refresh or
// reconnect to observe another server's changes. Refresh refuses pending/dirty
// writes, marks the cache loading, and completes via WhenCached. Same-cookie
// writes use last committed value; unrelated cookies are never overwritten.
native bool Prefs_Refresh(Player player);

// Player settings interfaces must use these access-aware operations: public
// values are visible/writable, protected visible/read-only, private hidden.
// Access controls player presentation, not trusted script-to-script secrecy.
native int Prefs_UserCookieCount();
native bool Prefs_UserCookieName(int index, char[] output, int capacity);
native bool Prefs_UserSet(Player player, const char[] name, const char[] value);

// Public cookies can offer built-in yes/no, 1/0, on/off or 1/0 choices. Labels
// are 1..64 bytes. Registrations belong to the registering script and disappear
// on close/unload; paused plugins' choices are disabled. The latest live
// registration for a cookie wins during staged plugin reload.
native PrefMenuItem Prefs_SetPrefabMenu(PrefCookie cookie, PrefMenuType type, const char[] label);
native bool Prefs_ClosePrefabMenu(PrefMenuItem item);
// Shared, paginated menu across registered cookies. Protected settings appear
// read only, private settings are hidden; public settings without a prefab are
// shown read only. Values shown are cached, not a promise of durable storage.
// Call from a running script callback after caching. The returned display
// belongs to the caller and closes on pause/unload/disconnect/authentication
// loss. Closing is completed on a subsequent frame if host cleanup must retry.
// Close the resource even after timeout/selection. Submenus have a Back item.
native PrefMenu Prefs_ShowMenu(Player player);
native bool Prefs_MenuOpen(PrefMenu menu);
native bool Prefs_CloseMenu(PrefMenu menu);
