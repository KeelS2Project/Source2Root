#include <source2root>
#include <admin_shared>

char routes[][] = {"sr_help",
                   "sr_who",
                   "sr_reloadadmins",
                   "sr_slap",
                   "sr_slay",
                   "sr_kick",
                   "sr_ban",
                   "sr_unban",
                   "sr_mute",
                   "sr_unmute",
                   "sr_gag",
                   "sr_ungag",
                   "sr_silence",
                   "sr_unsilence",
                   "sr_map",
                   "sr_restart"};

char labels[][] = {"Command help",
                   "Connected players",
                   "Reload administrators",
                   "Slap a player",
                   "Slay a player",
                   "Kick a player",
                   "Ban a player",
                   "Remove a ban",
                   "Mute a player",
                   "Unmute a player",
                   "Gag a player",
                   "Ungag a player",
                   "Silence a player",
                   "Unsilence a player",
                   "Change map",
                   "Restart round"};

public bool OnPluginStart()
{
    return RegisterCommand("sr_help", "admin.help", Help)
        && RegisterCommand("sr_admin", "admin.menu", Admin)
        && RegisterCommand("sr_who", "admin.who", Who)
        && RegisterCommand("sr_reloadadmins", "admin.reloadadmins", Reload)
        && SetCommandMenu("sr_admin", Admin)
        && SetCommandMenu("sr_help", Help)
        && SetCommandMenu("sr_who", Who)
        && SetCommandMenu("sr_reloadadmins", Reload);
}

public void Help(Player caller, const char[] arguments)
{
    if (!Ready(caller, arguments, "admin.help", "Usage: sr_help"))
        return;

    ReplyToCommand(caller, "Source2Root administration:");
    char names[][] = {"sr_help",
                      "sr_admin",
                      "sr_who",
                      "sr_reloadadmins",
                      "sr_slap",
                      "sr_slay",
                      "sr_kick",
                      "sr_ban",
                      "sr_unban",
                      "sr_mute",
                      "sr_unmute",
                      "sr_gag",
                      "sr_ungag",
                      "sr_silence",
                      "sr_unsilence",
                      "sr_say",
                      "sr_map",
                      "sr_restart"};

    char descriptions[][] = {"Show command help",
                             "Open the administration menu",
                             "List connected players",
                             "Reload administrators and groups",
                             "Slap players",
                             "Slay players",
                             "Kick players",
                             "Ban an account",
                             "Remove a stored ban",
                             "Mute voice",
                             "Unmute voice",
                             "Block chat",
                             "Allow chat",
                             "Block voice and chat",
                             "Allow voice and chat",
                             "Send a public announcement",
                             "Change the map",
                             "Restart the round"};

    for (int i = 0; i < sizeof(names); i++)
    {
        char usage[257], line[384];
        bool hasMenu;

        if (GetCommandInfo(caller, names[i], usage, sizeof(usage), hasMenu))
        {
            Format(line, sizeof(line), "  %s - %s", usage, descriptions[i]);
            ReplyToCommand(caller, line);
        }
    }

    ReplyToCommand(caller, "Targets: #userid, SteamID, unique name, @me, @all, @alive, @dead, @t, @ct, @bots.");
    ReplyToCommand(caller,
                   "Quote names containing spaces. Group selectors require a command that accepts multiple targets.");

    Audit(caller, "sr_help");
}

public void Who(Player caller, const char[] arguments)
{
    if (!Ready(caller, arguments, "admin.who", "Usage: sr_who"))
        return;

    Player players[128];
    int count = GetPlayers(players, sizeof(players));

    if (count < 0)
    {
        Failure(caller);
        return;
    }

    if (!count)
        ReplyToCommand(caller, "No players are connected.");

    for (int i = 0; i < count; i++)
    {
        char name[129], identity[96], steam64[24], steam2[32], steam3[24], line[512], group[65];
        int userid, team, immunity;

        if (!GetPlayerName(players[i], name, sizeof(name)) || !GetPlayerUserID(players[i], userid) ||
            !GetPlayerTeam(players[i], team))
            continue;

        SafeName(name);

        if (IsPlayerBot(players[i]))
            Format(identity, sizeof(identity), "bot");
        else if (GetPlayerSteamID(players[i], steam64, sizeof(steam64))
            && GetPlayerSteamID(players[i], steam2, sizeof(steam2), SteamID2)
            && GetPlayerSteamID(players[i], steam3, sizeof(steam3), SteamID3))
            Format(identity, sizeof(identity), "%s %s %s", steam64, steam2, steam3);
        else
            Format(identity, sizeof(identity), "Steam authentication pending");

        Format(line, sizeof(line), "#%d %s | %s | team=%d %s", userid, name, identity, team,
            IsPlayerAlive(players[i]) ? "alive" : "not alive");

        if (GetAdminGroup(players[i], group, sizeof(group)) && GetAdminImmunity(players[i], immunity))
            Format(line, sizeof(line), "%s | %s immunity=%d", line, group, immunity);

        ReplyToCommand(caller, line);
    }

    Audit(caller, "sr_who");
}

public void Reload(Player caller, const char[] arguments)
{
    if (!Ready(caller, arguments, "admin.reloadadmins", "Usage: sr_reloadadmins"))
        return;

    if (!ReloadAdmins(caller))
    {
        Failure(caller);
        return;
    }

    ReplyToCommand(caller, "Administrator configuration reloaded.");
    Audit(caller, "sr_reloadadmins");
}

public void Admin(Player caller, const char[] arguments)
{
    if (!Ready(caller, arguments, "admin.menu", "Usage: sr_admin"))
        return;

    if (caller == NoPlayer)
    {
        ReplyToCommand(caller, "Use sr_help in the server console.");
        return;
    }

    Menu menu = CreateMenu("Source2Root administration", "admin.menu", Selected);

    if (menu == NoMenu)
    {
        Failure(caller);
        return;
    }

    for (int i = 0; i < sizeof(routes); i++)
    {
        char usage[257];
        bool hasMenu;
        bool available = GetCommandInfo(caller, routes[i], usage, sizeof(usage), hasMenu) && hasMenu;

        if (i >= 3 && !available)
            continue;

        if (!AddMenuItem(menu, labels[i], available, i))
        {
            Failure(caller);
            CloseMenu(menu);
            return;
        }
    }

    if (!ShowMenu(menu, caller))
    {
        Failure(caller);
        CloseMenu(menu);
        return;
    }

    Audit(caller, "sr_admin");
}

public void Selected(Player caller, int item)
{
    if (item >= 0 && item < sizeof(routes) && !OpenCommandMenu(caller, routes[item]))
        Failure(caller);
}
