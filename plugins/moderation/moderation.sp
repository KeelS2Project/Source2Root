#include <source2root>
#include <admin_shared>

Player owners[128], checked[128];
int durations[128], pages[128], retry[128];
bool enforced[128];
char accounts[128][8][24];

public bool OnPluginStart()
{
    return LoadBans()
        && RegisterCommand("sr_kick", "admin.kick", Kick, "sr_kick <target> [reason]")
        && RegisterCommand("sr_ban", "admin.ban", Ban, "sr_ban <target-or-SteamID> <minutes> [reason]")
        && RegisterCommand("sr_unban", "admin.unban", Unban, "sr_unban <SteamID>")
        && SetCommandMenu("sr_kick", KickMenu)
        && SetCommandMenu("sr_ban", BanMenu)
        && SetCommandMenu("sr_unban", UnbanMenu)
        && CreateTimer(1000, EnforceBans, NoPlayer, true) != NoTimer;
}

int UserSlot(Player caller)
{
    int free = -1;
    for (int i = 0; i < sizeof(owners); i++)
    {
        if (owners[i] == caller) return i;
        if (free < 0 && !IsPlayerConnected(owners[i])) free = i;
    }
    if (free >= 0) { owners[free] = caller; durations[free] = 0; pages[free] = 0; }
    return free;
}

void Announce(Player caller, const char[] action, const char[] confirmation = "")
{
    if (!ShowActivity(caller, action, confirmation))
    {
        char error[512]; GetLastError(error, sizeof(error)); LogMessage(error);
    }
}

void KickResult(char[] output, int capacity, const char[] subject, int successes, int failed, const char[] error)
{
    if (!successes) Format(output, capacity, "No players were kicked. %d %s failed: %s", failed, failed == 1 ? "target" : "targets", error);
    else if (failed) Format(output, capacity, "Kicked %s; %d %s failed: %s", subject, failed, failed == 1 ? "target" : "targets", error);
    else Format(output, capacity, "Kicked %s", subject);
}

void KickTargets(Player caller, Player[] targets, int count, const char[] reason)
{
    char actor[384], firstError[256], lastName[129];
    if (!ActorField(caller, actor, sizeof(actor))) { Failure(caller); return; }
    for (int i = 0; i < count - 1; i++) if (targets[i] == caller)
    {
        Player last = targets[count - 1]; targets[count - 1] = caller; targets[i] = last; break;
    }
    int successes, failed;
    for (int i = 0; i < count; i++)
    {
        char target[384], name[129], error[512], disconnect[301];
        TargetField(targets[i], target, sizeof(target));
        bool success = false;
        if (!CanTarget(caller, targets[i], "admin.kick") || !GetPlayerName(targets[i], name, sizeof(name))) GetLastError(error, sizeof(error));
        else
        {
            SafeName(name);
            Format(disconnect, sizeof(disconnect), "%s", reason);
            if (targets[i] == caller)
            {
                char subject[129];
                if (count == 1) Format(subject, sizeof(subject), "%s", name);
                else Format(subject, sizeof(subject), "%d %s", successes + 1, successes ? "players" : "player");
                KickResult(disconnect, sizeof(disconnect), subject, successes + 1, failed, firstError);
            }
            success = KickPlayer(targets[i], disconnect);
            if (!success) GetLastError(error, sizeof(error));
        }
        AuditTarget(actor, "sr_kick", target, success, 0, false, success ? reason : error);
        if (success) { successes++; Format(lastName, sizeof(lastName), "%s", name); }
        else { failed++; if (!firstError[0]) Format(firstError, sizeof(firstError), "%s", error); }
    }
    char subject[129], action[256], confirmation[512];
    if (count == 1) Format(subject, sizeof(subject), "%s", lastName);
    else Format(subject, sizeof(subject), "%d %s", successes, successes == 1 ? "player" : "players");
    KickResult(confirmation, sizeof(confirmation), subject, successes, failed, firstError);
    if (!successes) { ReplyToCommand(caller, count == 1 ? firstError : confirmation); return; }
    Format(action, sizeof(action), "kicked %s", subject);
    Announce(caller, action, confirmation);
}

public void Kick(Player caller, const char[] arguments)
{
    int count = GetArgumentCount(arguments);
    if (count < 1) { ReplyToCommand(caller, "Usage: sr_kick <target> [reason]"); return; }
    char selector[257], reason[257] = "Kicked by an administrator";
    if (!GetArgument(arguments, 0, selector, sizeof(selector))
        || (count > 1 && !GetRemainingArguments(arguments, 1, reason, sizeof(reason)))) { Failure(caller); return; }
    if (!reason[0]) { ReplyToCommand(caller, "Kick reason cannot be empty."); return; }
    Player targets[128];
    int found = FindTargets(caller, selector, targets, sizeof(targets), true);
    if (found < 0) { Failure(caller); return; }
    KickTargets(caller, targets, found, reason);
}

void BanAccount(Player caller, const char[] identity, int minutes, const char[] reason)
{
    char actor[384], target[96], error[512], action[256], confirmation[512];
    if (!ActorField(caller, actor, sizeof(actor))) { Failure(caller); return; }
    Format(target, sizeof(target), "target_steamid=%s minutes=%d", identity, minutes);
    bool saved = CanTargetIdentity(caller, identity, "admin.ban") && AddBan(caller, identity, minutes, reason);
    if (!saved) GetLastError(error, sizeof(error));
    AuditTarget(actor, "sr_ban", target, saved, 0, false, saved ? reason : error);
    if (!saved) { ReplyToCommand(caller, error); return; }
    if (minutes) {
        Format(action, sizeof(action), "banned %s for %d %s", identity, minutes, minutes == 1 ? "minute" : "minutes");
        Format(confirmation, sizeof(confirmation), "Banned %s for %d %s", identity, minutes, minutes == 1 ? "minute" : "minutes");
    } else {
        Format(action, sizeof(action), "banned %s permanently", identity);
        Format(confirmation, sizeof(confirmation), "Banned %s permanently", identity);
    }
    Player players[128];
    int count = GetPlayers(players, sizeof(players));
    if (count < 0) GetLastError(error, sizeof(error));
    else for (int pass = 0; pass < 2; pass++) for (int i = 0; i < count; i++)
    {
        if ((players[i] == caller) != (pass == 1)) continue;
        char current[24], disconnect[301], kickTarget[384], kickError[512];
        if (!GetPlayerSteamID(players[i], current, sizeof(current)) || !SameIdentity(current, identity)) continue;
        Format(disconnect, sizeof(disconnect), "Source2Root ban: %s", reason);
        if (players[i] == caller) {
            if (error[0]) Format(disconnect, sizeof(disconnect), "Ban saved for %s; disconnect pending: %s", identity, error);
            else Format(disconnect, sizeof(disconnect), "%s", confirmation);
        }
        TargetField(players[i], kickTarget, sizeof(kickTarget));
        bool disconnected = KickPlayer(players[i], disconnect);
        if (!disconnected) {
            GetLastError(kickError, sizeof(kickError));
            Format(error, sizeof(error), "%s", kickError);
        } else RememberEnforced(players[i]);
        AuditTarget(actor, "ban_disconnect", kickTarget, disconnected, 0, false, disconnected ? reason : kickError);
    }
    if (error[0]) Format(confirmation, sizeof(confirmation), "Ban saved for %s; disconnect pending: %s", identity, error);
    Announce(caller, action, confirmation);
}

bool SameIdentity(const char[] left, const char[] right)
{
    for (int i = 0; ; i++) { if (left[i] != right[i]) return false; if (!left[i]) return true; }
}

public void Ban(Player caller, const char[] arguments)
{
    int count = GetArgumentCount(arguments), minutes;
    if (count < 2) { ReplyToCommand(caller, "Usage: sr_ban <target-or-SteamID> <minutes> [reason]"); return; }
    char selector[257], amount[257], identity[24], reason[257] = "Banned by an administrator";
    if (!GetArgument(arguments, 0, selector, sizeof(selector)) || !GetArgument(arguments, 1, amount, sizeof(amount))
        || !ParseInt(amount, minutes, 0, 5256000)
        || (count > 2 && !GetRemainingArguments(arguments, 2, reason, sizeof(reason)))) { Failure(caller); return; }
    if (!reason[0]) { ReplyToCommand(caller, "Ban reason cannot be empty."); return; }
    if (!NormalizeSteamID(selector, identity, sizeof(identity)))
    {
        Player targets[1];
        if (FindTargets(caller, selector, targets, sizeof(targets)) != 1
            || !GetPlayerSteamID(targets[0], identity, sizeof(identity))) { Failure(caller); return; }
    }
    BanAccount(caller, identity, minutes, reason);
}

void UnbanAccount(Player caller, const char[] identity)
{
    char actor[384], target[64], error[512], action[128]; bool removed;
    if (!ActorField(caller, actor, sizeof(actor))) { Failure(caller); return; }
    Format(target, sizeof(target), "target_steamid=%s", identity);
    bool success = CanTargetIdentity(caller, identity, "admin.unban") && RemoveBan(identity, removed);
    if (!success) GetLastError(error, sizeof(error));
    AuditTarget(actor, "sr_unban", target, success, 0, false, success ? (removed ? "ban removed" : "no stored ban") : error);
    if (!success) { ReplyToCommand(caller, error); return; }
    if (!removed) { Format(action, sizeof(action), "No stored ban for %s", identity); ReplyToCommand(caller, action); return; }
    Format(action, sizeof(action), "removed the ban for %s", identity); Announce(caller, action);
}

public void Unban(Player caller, const char[] arguments)
{
    if (GetArgumentCount(arguments) != 1) { ReplyToCommand(caller, "Usage: sr_unban <SteamID>"); return; }
    char value[257], identity[24];
    if (!GetArgument(arguments, 0, value, sizeof(value)) || !NormalizeSteamID(value, identity, sizeof(identity))) { Failure(caller); return; }
    UnbanAccount(caller, identity);
}

int EnforcementSlot(Player player)
{
    int free = -1;
    for (int i = 0; i < sizeof(checked); i++) {
        if (checked[i] == player) return i;
        if (free < 0 && !IsPlayerConnected(checked[i])) free = i;
    }
    if (free >= 0) { checked[free] = player; retry[free] = 0; enforced[free] = false; }
    return free;
}

void RememberEnforced(Player player)
{
    int slot = EnforcementSlot(player);
    if (slot >= 0) enforced[slot] = true;
}

public void EnforceBans(Player unused)
{
    if (CreateTimer(1000, EnforceBans, NoPlayer, true) == NoTimer) { LogMessage("Could not schedule ban enforcement."); return; }
    Player players[128];
    int count = GetPlayers(players, sizeof(players));
    if (count < 0) { char error[512]; GetLastError(error, sizeof(error)); LogMessage(error); return; }
    for (int i = 0; i < count; i++) {
        char identity[24], reason[257];
        if (!GetPlayerSteamID(players[i], identity, sizeof(identity))) continue;
        int slot = EnforcementSlot(players[i]);
        if (slot < 0) { LogMessage("Too many active ban enforcement connections."); return; }
        if (!GetBan(identity, reason, sizeof(reason))) { enforced[slot] = false; retry[slot] = 0; continue; }
        if (enforced[slot]) continue;
        if (retry[slot] > 0) { retry[slot]--; continue; }
        char target[384], disconnect[301], error[512];
        TargetField(players[i], target, sizeof(target));
        Format(disconnect, sizeof(disconnect), "Source2Root ban: %s", reason);
        bool success = KickPlayer(players[i], disconnect);
        if (!success) GetLastError(error, sizeof(error));
        AuditTarget("actor=console", "ban_enforcement", target, success, 0, false, success ? reason : error);
        if (success) enforced[slot] = true;
        else retry[slot] = 4;
    }
}

void PlayerMenu(Player caller, bool ban)
{
    Player players[128]; int count = GetPlayers(players, sizeof(players)), added;
    if (count < 0) { Failure(caller); return; }
    char permission[16]; Format(permission, sizeof(permission), ban ? "admin.ban" : "admin.kick");
    Menu menu = CreateMenu(ban ? "Ban a player" : "Kick a player", permission, ban ? BanSelected : KickSelected);
    if (menu == NoMenu) { Failure(caller); return; }
    for (int i = 0; i < count; i++) {
        char name[129], label[96], identity[24]; int userid;
        if (!GetPlayerName(players[i], name, sizeof(name)) || !GetPlayerUserID(players[i], userid)) continue;
        SafeName(name); Format(label, sizeof(label), "#%d %s", userid, name);
        bool allowed = CanTarget(caller, players[i], permission);
        if (ban) allowed = allowed && GetPlayerSteamID(players[i], identity, sizeof(identity));
        if (!AddMenuItem(menu, label, allowed, view_as<int>(players[i]))) { Failure(caller); CloseMenu(menu); return; }
        added++;
    }
    if (!added) { ReplyToCommand(caller, "No players are connected."); CloseMenu(menu); return; }
    if (!ShowMenu(menu, caller)) { Failure(caller); CloseMenu(menu); }
}

public void KickMenu(Player caller, const char[] arguments)
{
    if (Ready(caller, arguments, "admin.kick", "Usage: sr_kick <target> [reason]")) PlayerMenu(caller, false);
}
public void KickSelected(Player caller, int value)
{
    Player targets[1]; targets[0] = view_as<Player>(value);
    KickTargets(caller, targets, 1, "Kicked by an administrator");
}
public void BanMenu(Player caller, const char[] arguments)
{
    if (!Ready(caller, arguments, "admin.ban", "Usage: sr_ban <target-or-SteamID> <minutes> [reason]")) return;
    Menu menu = CreateMenu("Ban duration", "admin.ban", DurationSelected);
    if (menu == NoMenu) { Failure(caller); return; }
    int values[] = {0, 30, 60, 1440, 10080};
    for (int i = 0; i < sizeof(values); i++) {
        char label[32];
        if (values[i]) Format(label, sizeof(label), "%d minutes", values[i]);
        else Format(label, sizeof(label), "Permanent");
        if (!AddMenuItem(menu, label, true, values[i])) { Failure(caller); CloseMenu(menu); return; }
    }
    if (!ShowMenu(menu, caller)) { Failure(caller); CloseMenu(menu); }
}
public void DurationSelected(Player caller, int minutes)
{
    int slot = UserSlot(caller);
    if (slot < 0) { ReplyToCommand(caller, "Too many active menu users."); return; }
    if (!HasPermission(caller, "admin.ban") || minutes < 0 || minutes > 5256000) return;
    durations[slot] = minutes; PlayerMenu(caller, true);
}
public void BanSelected(Player caller, int value)
{
    int slot = UserSlot(caller); if (slot < 0) return;
    Player target = view_as<Player>(value); char identity[24];
    if (!CanTarget(caller, target, "admin.ban") || !GetPlayerSteamID(target, identity, sizeof(identity))) { Failure(caller); return; }
    BanAccount(caller, identity, durations[slot], "Banned by an administrator");
}

void StoredBansMenu(Player caller)
{
    int slot = UserSlot(caller), count = GetBanCount();
    if (slot < 0) { ReplyToCommand(caller, "Too many active menu users."); return; }
    if (count < 0) { Failure(caller); return; }
    if (!count) { ReplyToCommand(caller, "No bans are stored."); return; }
    if (pages[slot] >= count) pages[slot] = ((count - 1) / 8) * 8;
    Menu menu = CreateMenu("Remove a stored ban", "admin.unban", UnbanSelected);
    if (menu == NoMenu) { Failure(caller); return; }
    for (int i = 0; i < 8; i++) {
        accounts[slot][i][0] = 0;
        if (pages[slot] + i >= count) continue;
        char label[64], reason[257];
        if (!GetBanIdentity(pages[slot] + i, accounts[slot][i], sizeof(accounts[][]))) { Failure(caller); CloseMenu(menu); return; }
        Format(label, sizeof(label), "%s%s", accounts[slot][i], GetBan(accounts[slot][i], reason, sizeof(reason)) ? "" : " (expired)");
        if (!AddMenuItem(menu, label, CanTargetIdentity(caller, accounts[slot][i], "admin.unban"), i)) { Failure(caller); CloseMenu(menu); return; }
    }
    if (pages[slot] > 0 && !AddMenuItem(menu, "Previous bans", true, 1000)) { Failure(caller); CloseMenu(menu); return; }
    if (pages[slot] + 8 < count && !AddMenuItem(menu, "More bans", true, 1001)) { Failure(caller); CloseMenu(menu); return; }
    if (!ShowMenu(menu, caller)) { Failure(caller); CloseMenu(menu); }
}
public void UnbanMenu(Player caller, const char[] arguments)
{
    if (!Ready(caller, arguments, "admin.unban", "Usage: sr_unban <SteamID>")) return;
    int slot = UserSlot(caller); if (slot < 0) return;
    pages[slot] = 0; StoredBansMenu(caller);
}
public void UnbanSelected(Player caller, int value)
{
    if (!HasPermission(caller, "admin.unban")) { ReplyToCommand(caller, "You do not have access to this command."); return; }
    int slot = UserSlot(caller); if (slot < 0) return;
    if (value == 1000) { pages[slot] = pages[slot] >= 8 ? pages[slot] - 8 : 0; StoredBansMenu(caller); }
    else if (value == 1001) { pages[slot] += 8; StoredBansMenu(caller); }
    else if (value >= 0 && value < 8 && accounts[slot][value][0]) UnbanAccount(caller, accounts[slot][value]);
}
