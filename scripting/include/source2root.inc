#if defined _source2root_included
    #endinput
#endif
#define _source2root_included

#define SOURCE2ROOT_SCRIPT_API 2

#pragma newdecls required

enum Player { NoPlayer = 0 }
enum Timer { NoTimer = 0 }
enum Menu { NoMenu = 0 }
enum ConVar { NoConVar = 0 }
enum ConfigFile { NoConfigFile = 0 }
enum SteamIDFormat { SteamID64, SteamID2, SteamID3 }

typedef CommandCallback = function void (Player caller, const char[] arguments);
typedef TimerCallback = function void (Player target);
typedef EventCallback = function void (const char[] event);
typedef MenuCallback = function void (Player player, int item);
typedef MenuBackCallback = function void (Player player);

native bool RegisterCommand(const char[] name, const char[] permission, CommandCallback callback, const char[] usage = "");
native bool GetCommandInfo(Player caller, const char[] name, char[] usage, int capacity, bool& hasMenu);
native bool SetCommandMenu(const char[] name, CommandCallback callback);
native bool OpenCommandMenu(Player caller, const char[] name);
native ConfigFile OpenConfigFile(const char[] filename);
native int ReadConfigLine(ConfigFile file, char[] output, int capacity);
native bool CloseConfigFile(ConfigFile file);
native bool IsMapInstalled(const char[] map);
native bool ChangeMap(const char[] map);
native bool RestartRound(int seconds = 1);
native bool ListenEvent(const char[] name, EventCallback callback);
native Timer CreateTimer(int milliseconds, TimerCallback callback, Player target = NoPlayer, bool acrossMaps = false);
native bool CancelTimer(Timer timer);
native void LogMessage(const char[] message);
native bool ReplyToCommand(Player player, const char[] message);
native bool ShowActivity(Player caller, const char[] action, const char[] confirmation = "");
native bool GetPlayerName(Player player, char[] output, int capacity);
native bool GetPlayerSteamID(Player player, char[] output, int capacity, SteamIDFormat format = SteamID64);
native bool GetPlayerHealth(Player player, int& health);
native int GetPlayers(Player[] output, int capacity);
native int FindTargets(Player caller, const char[] selector, Player[] output, int capacity, bool multiple = false);
native bool GetPlayerUserID(Player player, int& userID);
native bool GetPlayerTeam(Player player, int& team);
native bool IsPlayerAlive(Player player);
native bool IsPlayerBot(Player player);
native bool IsPlayerConnected(Player player);
native bool SlapPlayer(Player player, int damage = 0);
native bool SlayPlayer(Player player);
native bool SetPlayerMuted(Player player, bool muted);
native bool IsPlayerMuted(Player player);
native bool SetPlayerGagged(Player player, bool gagged);
native bool IsPlayerGagged(Player player);
native bool KickPlayer(Player player, const char[] reason);
native bool NormalizeSteamID(const char[] identity, char[] output, int capacity);
native bool LoadBans();
native bool GetBan(const char[] identity, char[] reason, int capacity);
native int GetBanCount();
native bool GetBanIdentity(int index, char[] identity, int capacity);
native bool AddBan(Player caller, const char[] identity, int minutes, const char[] reason);
native bool RemoveBan(const char[] identity, bool& removed);
native int Format(char[] output, int capacity, const char[] format, any ...);
native int GetArgumentCount(const char[] arguments);
native bool GetArgument(const char[] arguments, int index, char[] output, int capacity);
native bool GetRemainingArguments(const char[] arguments, int first, char[] output, int capacity);
native bool ParseInt(const char[] text, int& value, int minimum = (-2147483647 - 1), int maximum = 2147483647);
native bool HasPermission(Player player, const char[] permission);
native bool IsAdmin(Player player);
native bool GetAdminGroup(Player player, char[] output, int capacity);
native bool GetAdminImmunity(Player player, int& immunity);
native bool CanTarget(Player caller, Player target, const char[] permission);
native bool CanTargetIdentity(Player caller, const char[] identity, const char[] permission);
native bool ReloadAdmins(Player caller);
native bool GetLastError(char[] output, int capacity);
native bool GetDataPath(char[] output, int capacity);
native Menu CreateMenu(const char[] title, const char[] permission, MenuCallback callback,
    MenuBackCallback back = INVALID_FUNCTION);
native bool AddMenuItem(Menu menu, const char[] text, bool enabled = true, int value = -1);
native bool ShowMenu(Menu menu, Player player, int timeoutMilliseconds = 20000);
native bool CloseMenu(Menu menu);
native ConVar CreateIntConVar(const char[] name, int defaultValue, const char[] description = "",
    int minimum = (-2147483647 - 1), int maximum = 2147483647);
native ConVar CreateFloatConVar(const char[] name, float defaultValue, const char[] description = "",
    float minimum = -3.402823466e38, float maximum = 3.402823466e38);
native ConVar CreateStringConVar(const char[] name, const char[] defaultValue, const char[] description = "");
native bool GetConVarInt(ConVar convar, int& value);
native bool GetConVarFloat(ConVar convar, float& value);
native bool GetConVarString(ConVar convar, char[] output, int capacity);
native bool SetConVarInt(ConVar convar, int value);
native bool SetConVarFloat(ConVar convar, float value);
native bool SetConVarString(ConVar convar, const char[] value);
native bool CloseConVar(ConVar convar);
