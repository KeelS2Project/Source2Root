#include <source2root>
#include <admin_shared>

char maps[1024][65], selections[128][8][65];
Player owners[128];
int pages[128];

bool SameText(const char[] left, const char[] right)
{
    int i;

    while (left[i] && left[i] == right[i])
        i++;

    return left[i] == right[i];
}

int ReadMaps(const char[] filename)
{
    ConfigFile file = OpenConfigFile(filename);

    if (file == NoConfigFile)
        return -1;

    char line[4096];
    int count, status;

    while ((status = ReadConfigLine(file, line, sizeof(line))) > 0)
    {
        int first, last;

        while (line[first] == ' ' || line[first] == '\t')
            first++;

        if (!line[first] || (line[first] == '/' && line[first + 1] == '/'))
            continue;

        last = first;

        while (line[last])
            last++;

        while (last > first && (line[last - 1] == ' ' || line[last - 1] == '\t'))
            last--;

        if (last - first > 64 || count == sizeof(maps))
        {
            CloseConfigFile(file);
            return -2;
        }

        for (int i = first; i < last; i++)
            if (!((line[i] >= 'a' && line[i] <= 'z') || (line[i] >= '0' && line[i] <= '9') || line[i] == '_'))
            {
                CloseConfigFile(file);
                return -2;
            }

        line[last] = 0;
        Format(maps[count++], sizeof(maps[]), "%s", line[first]);
    }

    CloseConfigFile(file);
    return status < 0 ? -1 : count;
}

void ListFailure(Player caller, int result)
{
    if (result == -2)
        ReplyToCommand(caller,
                       "Map lists accept at most 1024 names, using 1..64 lowercase letters, digits or underscores.");
    else
        Failure(caller);
}

public bool OnPluginStart()
{
    int count = ReadMaps("allowed_maps.txt");

    if (count < 0)
    {
        if (count == -2)
            LogMessage("Invalid allowed_maps.txt: expected at most 1024 lowercase map names.");
        else
        {
            char error[512];
            GetLastError(error, sizeof(error));
            LogMessage(error);
        }

        return false;
    }

    return RegisterCommand("sr_map", "admin.changemap", Map, "sr_map <map>")
        && RegisterCommand("sr_restart", "admin.restart", Restart, "sr_restart [seconds]")
        && SetCommandMenu("sr_map", MapMenu)
        && SetCommandMenu("sr_restart", RestartMenu);
}

void Change(Player caller, const char[] name)
{
    if (!HasPermission(caller, "admin.changemap"))
    {
        ReplyToCommand(caller, "You do not have access to this command.");
        return;
    }

    int count = ReadMaps("allowed_maps.txt");

    if (count < 0)
    {
        ListFailure(caller, count);
        return;
    }

    bool allowed = count == 0;

    for (int i = 0; i < count; i++)
        if (SameText(name, maps[i]))
        {
            allowed = true;
            break;
        }

    if (!allowed)
    {
        ReplyToCommand(caller, "Map is not allowed by allowed_maps.txt.");
        return;
    }

    if (!IsMapInstalled(name))
    {
        Failure(caller);
        return;
    }

    char actor[384], target[96], action[128], error[512];

    if (!ActorField(caller, actor, sizeof(actor)))
    {
        Failure(caller);
        return;
    }

    Format(target, sizeof(target), "map=%s", name);
    bool success = ChangeMap(name);

    if (!success)
        GetLastError(error, sizeof(error));

    AuditTarget(actor, "sr_map", target, success, 0, false, error);

    if (!success)
    {
        ReplyToCommand(caller, error);
        return;
    }

    Format(action, sizeof(action), "requested a map change to %s", name);

    if (!ShowActivity(caller, action))
    {
        GetLastError(error, sizeof(error));
        LogMessage(error);
    }
}

public void Map(Player caller, const char[] arguments)
{
    if (GetArgumentCount(arguments) != 1)
    {
        ReplyToCommand(caller, "Usage: sr_map <map>");
        return;
    }

    char name[65];

    if (!GetArgument(arguments, 0, name, sizeof(name)))
    {
        Failure(caller);
        return;
    }

    Change(caller, name);
}

void RequestRestart(Player caller, int seconds)
{
    if (!HasPermission(caller, "admin.restart"))
    {
        ReplyToCommand(caller, "You do not have access to this command.");
        return;
    }

    char actor[384], target[64], action[128], error[512];

    if (!ActorField(caller, actor, sizeof(actor)))
    {
        Failure(caller);
        return;
    }

    Format(target, sizeof(target), "seconds=%d", seconds);
    bool success = RestartRound(seconds);

    if (!success)
        GetLastError(error, sizeof(error));

    AuditTarget(actor, "sr_restart", target, success, 0, false, error);

    if (!success)
    {
        ReplyToCommand(caller, error);
        return;
    }

    Format(action, sizeof(action), "requested a round restart in %d %s", seconds, seconds == 1 ? "second" : "seconds");

    if (!ShowActivity(caller, action))
    {
        GetLastError(error, sizeof(error));
        LogMessage(error);
    }
}

public void Restart(Player caller, const char[] arguments)
{
    int count = GetArgumentCount(arguments), seconds = 1;

    if (count < 0 || count > 1)
    {
        ReplyToCommand(caller, "Usage: sr_restart [seconds]");
        return;
    }

    char value[65];

    if (count && (!GetArgument(arguments, 0, value, sizeof(value)) || !ParseInt(value, seconds, 1, 60)))
    {
        ReplyToCommand(caller, "Restart delay must be from 1 to 60 seconds.");
        return;
    }

    RequestRestart(caller, seconds);
}

int UserSlot(Player caller)
{
    int free = -1;

    for (int i = 0; i < sizeof(owners); i++)
    {
        if (owners[i] == caller)
            return i;

        if (free < 0 && !IsPlayerConnected(owners[i]))
            free = i;
    }

    if (free >= 0)
    {
        owners[free] = caller;
        pages[free] = 0;
    }

    return free;
}

void MapsMenu(Player caller)
{
    if (!HasPermission(caller, "admin.changemap"))
    {
        ReplyToCommand(caller, "You do not have access to this command.");
        return;
    }

    int slot = UserSlot(caller);

    if (slot < 0)
    {
        ReplyToCommand(caller, "Too many active menu users.");
        return;
    }

    int count = ReadMaps("allowed_maps.txt");

    if (count == 0)
        count = ReadMaps("map_menu.txt");

    if (count < 0)
    {
        ListFailure(caller, count);
        return;
    }

    int installed;

    for (int i = 0; i < count; i++) if (IsMapInstalled(maps[i]))
    {
        if (installed != i)
            Format(maps[installed], sizeof(maps[]), "%s", maps[i]);

        installed++;
    }

    if (!installed)
    {
        ReplyToCommand(
            caller,
            "No installed maps from the configured list are available. Use sr_map <map> or update the map list.");

        return;
    }

    if (pages[slot] >= installed)
        pages[slot] = ((installed - 1) / 8) * 8;

    Menu menu = CreateMenu("Change map", "admin.changemap", MapSelected, BackToAdministration);

    if (menu == NoMenu)
    {
        Failure(caller);
        return;
    }

    for (int i = 0; i < 8; i++)
    {
        selections[slot][i][0] = 0;

        if (pages[slot] + i >= installed)
            continue;

        Format(selections[slot][i], sizeof(selections[][]), "%s", maps[pages[slot] + i]);

        if (!AddMenuItem(menu, selections[slot][i], true, i))
        {
            Failure(caller);
            CloseMenu(menu);
            return;
        }
    }

    if ((pages[slot] > 0 && !AddMenuItem(menu, "Previous maps", true, 1000)) ||
        (pages[slot] + 8 < installed && !AddMenuItem(menu, "More maps", true, 1001)) || !ShowMenu(menu, caller))
    {
        Failure(caller);
        CloseMenu(menu);
    }
}

public void MapMenu(Player caller, const char[] arguments)
{
    if (!Ready(caller, arguments, "admin.changemap", "Usage: sr_map <map>"))
        return;

    if (caller == NoPlayer)
    {
        ReplyToCommand(caller, "Use sr_map <map> in the server console.");
        return;
    }

    int slot = UserSlot(caller);

    if (slot < 0)
    {
        ReplyToCommand(caller, "Too many active menu users.");
        return;
    }

    pages[slot] = 0;
    MapsMenu(caller);
}

public void MapSelected(Player caller, int value)
{
    int slot = UserSlot(caller);

    if (slot < 0)
        return;

    if (value == 1000)
    {
        pages[slot] = pages[slot] >= 8 ? pages[slot] - 8 : 0;
        MapsMenu(caller);
    }
    else if (value == 1001)
    {
        pages[slot] += 8;
        MapsMenu(caller);
    }
    else if (value >= 0 && value < 8 && selections[slot][value][0])
    {
        char name[65];
        Format(name, sizeof(name), "%s", selections[slot][value]);
        Change(caller, name);
    }
}

public void RestartMenu(Player caller, const char[] arguments)
{
    if (!Ready(caller, arguments, "admin.restart", "Usage: sr_restart [seconds]"))
        return;

    if (caller == NoPlayer)
    {
        ReplyToCommand(caller, "Use sr_restart [seconds] in the server console.");
        return;
    }

    Menu menu = CreateMenu("Restart round", "admin.restart", RestartSelected, BackToAdministration);

    if (menu == NoMenu)
    {
        Failure(caller);
        return;
    }

    int delays[] = {1, 5, 10, 30, 60};

    for (int i = 0; i < sizeof(delays); i++)
    {
        char label[64];
        Format(label, sizeof(label), "Restart in %d %s", delays[i], delays[i] == 1 ? "second" : "seconds");

        if (!AddMenuItem(menu, label, true, delays[i]))
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
    }
}

public
void RestartSelected(Player caller, int seconds)
{
    RequestRestart(caller, seconds);
}
