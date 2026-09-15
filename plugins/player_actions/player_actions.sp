#include <source2root>
#include <admin_shared>

Player menuOwners[128];
int menuDamage[128];

public bool OnPluginStart()
{
    return RegisterCommand("sr_slap", "admin.slap", Slap, "sr_slap <target> [damage]")
        && RegisterCommand("sr_slay", "admin.slay", Slay, "sr_slay <target>")
        && SetCommandMenu("sr_slap", SlapMenu)
        && SetCommandMenu("sr_slay", SlayMenu);
}

int MenuSlot(Player caller)
{
    int free = -1;
    for (int i = 0; i < sizeof(menuOwners); i++)
    {
        if (menuOwners[i] == caller) return i;
        if (free == -1 && !IsPlayerConnected(menuOwners[i])) free = i;
    }
    if (free != -1) { menuOwners[free] = caller; menuDamage[free] = 0; }
    return free;
}

void Apply(Player caller, const Player[] targets, int count, bool slap, int damage)
{
    char actor[384], command[16], permission[16], firstError[256], lastName[129];
    if (!ActorField(caller, actor, sizeof(actor))) { Failure(caller); return; }
    Format(command, sizeof(command), slap ? "sr_slap" : "sr_slay");
    Format(permission, sizeof(permission), slap ? "admin.slap" : "admin.slay");
    int successes, failed;
    for (int i = 0; i < count; i++)
    {
        char target[384], name[129], error[512];
        TargetField(targets[i], target, sizeof(target));
        bool success = false;
        if (!CanTarget(caller, targets[i], permission)) GetLastError(error, sizeof(error));
        else if (!GetPlayerName(targets[i], name, sizeof(name))) GetLastError(error, sizeof(error));
        else if (!IsPlayerAlive(targets[i])) Format(error, sizeof(error), "Target is not alive.");
        else
        {
            success = slap ? SlapPlayer(targets[i], damage) : SlayPlayer(targets[i]);
            if (!success) GetLastError(error, sizeof(error));
        }
        AuditTarget(actor, command, target, success, damage, slap, error);
        if (success)
        {
            successes++;
            SafeName(name);
            Format(lastName, sizeof(lastName), "%s", name);
        }
        else
        {
            failed++;
            if (!firstError[0]) Format(firstError, sizeof(firstError), "%s", error);
        }
    }
    char action[256], confirmation[512], subject[129];
    if (!successes)
    {
        if (count == 1) ReplyToCommand(caller, firstError);
        else
        {
            Format(confirmation, sizeof(confirmation), "No players were %s. %d %s failed: %s",
                slap ? "slapped" : "killed", failed, failed == 1 ? "target" : "targets", firstError);
            ReplyToCommand(caller, confirmation);
        }
        return;
    }
    if (count == 1) Format(subject, sizeof(subject), "%s", lastName);
    else Format(subject, sizeof(subject), "%d %s", successes, successes == 1 ? "player" : "players");
    if (slap) Format(action, sizeof(action), "slapped %s (%d damage)", subject, damage);
    else Format(action, sizeof(action), "killed %s", subject);
    if (failed)
    {
        Format(confirmation, sizeof(confirmation), "Killed %s; %d %s failed: %s", subject, failed, failed == 1 ? "target" : "targets", firstError);
        if (slap) Format(confirmation, sizeof(confirmation), "Slapped %s (%d damage); %d %s failed: %s", subject, damage, failed, failed == 1 ? "target" : "targets", firstError);
    }
    if (!ShowActivity(caller, action, confirmation))
    {
        char error[512];
        GetLastError(error, sizeof(error));
        LogMessage(error);
    }
}

void Command(Player caller, const char[] arguments, bool slap)
{
    int maximum = slap ? 2 : 1;
    int count = GetArgumentCount(arguments), damage;
    if (count < 1 || count > maximum)
    {
        ReplyToCommand(caller, slap ? "Usage: sr_slap <target> [damage]" : "Usage: sr_slay <target>");
        return;
    }
    char selector[257], amount[257];
    if (!GetArgument(arguments, 0, selector, sizeof(selector))) { Failure(caller); return; }
    if (slap && count == 2)
    {
        if (!GetArgument(arguments, 1, amount, sizeof(amount)) || !ParseInt(amount, damage, 0, 1000)) { Failure(caller); return; }
    }
    Player targets[128];
    int found = FindTargets(caller, selector, targets, sizeof(targets), true);
    if (found < 0) { Failure(caller); return; }
    Apply(caller, targets, found, slap, damage);
}

public void Slap(Player caller, const char[] arguments) { Command(caller, arguments, true); }
public void Slay(Player caller, const char[] arguments) { Command(caller, arguments, false); }

void TargetsMenu(Player caller, bool slap, int damage)
{
    int slot = MenuSlot(caller);
    if (slot < 0) { ReplyToCommand(caller, "Too many active menu users."); return; }
    menuDamage[slot] = damage;
    Player players[128];
    int count = GetPlayers(players, sizeof(players));
    if (count < 0) { Failure(caller); return; }
    if (!count) { ReplyToCommand(caller, "No players are connected."); return; }
    char title[96], permission[16];
    if (slap) Format(title, sizeof(title), "Slap a player (%d damage)", damage);
    else Format(title, sizeof(title), "Slay a player");
    Format(permission, sizeof(permission), slap ? "admin.slap" : "admin.slay");
    Menu menu = CreateMenu(title, permission, slap ? SlapSelected : SlaySelected, slap ? BackToSlapDamage : BackToAdministration);
    if (menu == NoMenu) { Failure(caller); return; }
    int added;
    for (int i = 0; i < count; i++)
    {
        char name[129], label[96];
        int userid;
        if (!GetPlayerName(players[i], name, sizeof(name)) || !GetPlayerUserID(players[i], userid)) continue;
        SafeName(name);
        Format(label, sizeof(label), "#%d %s", userid, name);
        if (!AddMenuItem(menu, label, IsPlayerAlive(players[i]) && CanTarget(caller, players[i], permission), view_as<int>(players[i])))
        {
            Failure(caller); CloseMenu(menu); return;
        }
        added++;
    }
    if (!added) { ReplyToCommand(caller, "No players are connected."); CloseMenu(menu); return; }
    if (!ShowMenu(menu, caller)) { Failure(caller); CloseMenu(menu); }
}

public void SlapMenu(Player caller, const char[] arguments)
{
    if (!Ready(caller, arguments, "admin.slap", "Usage: sr_slap <target> [damage]")) return;
    Menu menu = CreateMenu("Slap damage", "admin.slap", DamageSelected, BackToAdministration);
    if (menu == NoMenu) { Failure(caller); return; }
    int amounts[] = {0, 5, 10, 25, 50, 100, 1000};
    for (int i = 0; i < sizeof(amounts); i++)
    {
        char label[32];
        Format(label, sizeof(label), "%d damage", amounts[i]);
        if (!AddMenuItem(menu, label, true, amounts[i])) { Failure(caller); CloseMenu(menu); return; }
    }
    if (!ShowMenu(menu, caller)) { Failure(caller); CloseMenu(menu); }
}

public void SlayMenu(Player caller, const char[] arguments)
{
    if (Ready(caller, arguments, "admin.slay", "Usage: sr_slay <target>")) TargetsMenu(caller, false, 0);
}

public void DamageSelected(Player caller, int damage)
{
    if (HasPermission(caller, "admin.slap") && damage >= 0 && damage <= 1000) TargetsMenu(caller, true, damage);
}

public void SlapSelected(Player caller, int item)
{
    int slot = MenuSlot(caller);
    if (slot < 0) return;
    Player targets[1];
    targets[0] = view_as<Player>(item);
    Apply(caller, targets, 1, true, menuDamage[slot]);
}

public void SlaySelected(Player caller, int item)
{
    Player targets[1];
    targets[0] = view_as<Player>(item);
    Apply(caller, targets, 1, false, 0);
}

public void BackToSlapDamage(Player caller)
{
    SlapMenu(caller, "");
}
