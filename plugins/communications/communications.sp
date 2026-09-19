#include <source2root>
#include <admin_shared>

char commands[][] = {"sr_mute", "sr_unmute", "sr_gag", "sr_ungag", "sr_silence", "sr_unsilence"};
char permissions[][] = {"admin.mute", "admin.unmute", "admin.gag", "admin.ungag", "admin.silence", "admin.unsilence"};
char verbs[][] = {"muted", "unmuted", "gagged", "ungagged", "silenced", "unsilenced"};
char titles[][] = {
    "Mute a player", "Unmute a player", "Gag a player", "Ungag a player", "Silence a player", "Unsilence a player"};

Player menuOwners[128];
int menuActions[128];

public bool OnPluginStart()
{
    return RegisterCommand("sr_mute", "admin.mute", Mute, "sr_mute <target>")
        && RegisterCommand("sr_unmute", "admin.unmute", Unmute, "sr_unmute <target>")
        && RegisterCommand("sr_gag", "admin.gag", Gag, "sr_gag <target>")
        && RegisterCommand("sr_ungag", "admin.ungag", Ungag, "sr_ungag <target>")
        && RegisterCommand("sr_silence", "admin.silence", Silence, "sr_silence <target>")
        && RegisterCommand("sr_unsilence", "admin.unsilence", Unsilence, "sr_unsilence <target>")
        && RegisterCommand("sr_say", "admin.say", Say, "sr_say <message>")
        && SetCommandMenu("sr_mute", MuteMenu) && SetCommandMenu("sr_unmute", UnmuteMenu)
        && SetCommandMenu("sr_gag", GagMenu) && SetCommandMenu("sr_ungag", UngagMenu)
        && SetCommandMenu("sr_silence", SilenceMenu) && SetCommandMenu("sr_unsilence", UnsilenceMenu);
}

int MenuSlot(Player caller)
{
    int free = -1;

    for (int i = 0; i < sizeof(menuOwners); i++) {
        if (menuOwners[i] == caller)
            return i;

        if (free < 0 && !IsPlayerConnected(menuOwners[i]))
            free = i;
    }

    if (free >= 0)
        menuOwners[free] = caller;

    return free;
}

bool Change(Player player, int action, char[] error, int capacity)
{
    bool enable = action % 2 == 0;

    if (action < 2 || action >= 4) {
        if (!SetPlayerMuted(player, enable))
        {
            GetLastError(error, capacity);
            return false;
        }
    }

    if (action >= 2) {
        if (!SetPlayerGagged(player, enable)) {
            GetLastError(error, capacity);

            if (action >= 4)
                Format(error, capacity, "Voice state changed; chat restriction failed: %s", error);

            return false;
        }
    }

    if (!enable) {
        bool voice = (action < 2 || action >= 4) && IsPlayerMuted(player);
        bool chat = action >= 2 && IsPlayerGagged(player);

        if (voice || chat) {
            Format(error, capacity, "Your restriction was removed; another plugin still restricts %s.",
                voice && chat ? "voice and chat" : voice ? "voice" : "chat");

            return false;
        }
    }

    return true;
}

void Apply(Player caller, const Player[] targets, int count, int operation)
{
    char actor[384], firstError[256], lastName[129];

    if (!ActorField(caller, actor, sizeof(actor)))
    {
        Failure(caller);
        return;
    }

    int successes, failed;

    for (int i = 0; i < count; i++) {
        char target[384], name[129], error[512];
        TargetField(targets[i], target, sizeof(target));
        bool success = false;

        if (!CanTarget(caller, targets[i], permissions[operation]) || !GetPlayerName(targets[i], name, sizeof(name)))
            GetLastError(error, sizeof(error));
        else
            success = Change(targets[i], operation, error, sizeof(error));

        AuditTarget(actor, commands[operation], target, success, 0, false, error);

        if (success)
        {
            successes++;
            SafeName(name);
            Format(lastName, sizeof(lastName), "%s", name);
        }
        else
        {
            failed++;

            if (!firstError[0])
                Format(firstError, sizeof(firstError), "%s", error);
        }
    }

    char action[256], confirmation[512], subject[129];

    if (!successes) {
        if (count == 1)
            ReplyToCommand(caller, firstError);
        else {
            Format(confirmation,
                   sizeof(confirmation),
                   "No players were %s. %d %s failed: %s",
                   verbs[operation],
                   failed,
                   failed == 1 ? "target" : "targets",
                   firstError);

            ReplyToCommand(caller, confirmation);
        }

        return;
    }

    if (count == 1)
        Format(subject, sizeof(subject), "%s", lastName);
    else
        Format(subject, sizeof(subject), "%d %s", successes, successes == 1 ? "player" : "players");

    Format(action, sizeof(action), "%s %s", verbs[operation], subject);

    if (failed) {
        Format(confirmation,
               sizeof(confirmation),
               "%s %s; %d %s failed: %s",
               verbs[operation],
               subject,
               failed,
               failed == 1 ? "target" : "targets",
               firstError);

        if (confirmation[0] >= 'a' && confirmation[0] <= 'z')
            confirmation[0] -= 'a' - 'A';
    }

    if (!ShowActivity(caller, action, confirmation)) {
        char error[512];
        GetLastError(error, sizeof(error));
        LogMessage(error);
    }
}

void Command(Player caller, const char[] arguments, int operation)
{
    if (GetArgumentCount(arguments) != 1) {
        char usage[64];
        Format(usage, sizeof(usage), "Usage: %s <target>", commands[operation]);
        ReplyToCommand(caller, usage);
        return;
    }

    char selector[257];
    Player targets[128];

    if (!GetArgument(arguments, 0, selector, sizeof(selector)))
    {
        Failure(caller);
        return;
    }

    int count = FindTargets(caller, selector, targets, sizeof(targets), true);

    if (count < 0)
    {
        Failure(caller);
        return;
    }

    Apply(caller, targets, count, operation);
}

public
void Mute(Player caller, const char[] arguments)
{
    Command(caller, arguments, 0);
}

public
void Unmute(Player caller, const char[] arguments)
{
    Command(caller, arguments, 1);
}

public
void Gag(Player caller, const char[] arguments)
{
    Command(caller, arguments, 2);
}

public
void Ungag(Player caller, const char[] arguments)
{
    Command(caller, arguments, 3);
}

public
void Silence(Player caller, const char[] arguments)
{
    Command(caller, arguments, 4);
}

public
void Unsilence(Player caller, const char[] arguments)
{
    Command(caller, arguments, 5);
}

void TargetsMenu(Player caller, const char[] arguments, int operation)
{
    char usage[64];
    Format(usage, sizeof(usage), "Usage: %s <target>", commands[operation]);

    if (!Ready(caller, arguments, permissions[operation], usage))
        return;

    int slot = MenuSlot(caller);

    if (slot < 0)
    {
        ReplyToCommand(caller, "Too many active menu users.");
        return;
    }

    menuActions[slot] = operation;
    Player players[128];
    int count = GetPlayers(players, sizeof(players)), added;

    if (count < 0)
    {
        Failure(caller);
        return;
    }

    Menu menu = CreateMenu(titles[operation], permissions[operation], Selected, BackToAdministration);

    if (menu == NoMenu)
    {
        Failure(caller);
        return;
    }

    for (int i = 0; i < count; i++) {
        char name[129], label[96];
        int userid;

        if (!GetPlayerName(players[i], name, sizeof(name)) || !GetPlayerUserID(players[i], userid))
            continue;

        SafeName(name);
        Format(label, sizeof(label), "#%d %s", userid, name);

        if (!AddMenuItem(menu, label, CanTarget(caller, players[i], permissions[operation]), view_as<int>(players[i])))
        {
            Failure(caller);
            CloseMenu(menu);
            return;
        }

        added++;
    }

    if (!added)
    {
        ReplyToCommand(caller, "No players are connected.");
        CloseMenu(menu);
        return;
    }

    if (!ShowMenu(menu, caller))
    {
        Failure(caller);
        CloseMenu(menu);
    }
}

public void Selected(Player caller, int value)
{
    int slot = MenuSlot(caller);

    if (slot < 0)
        return;

    Player targets[1];
    targets[0] = view_as<Player>(value);
    Apply(caller, targets, 1, menuActions[slot]);
}

public
void MuteMenu(Player caller, const char[] arguments)
{
    TargetsMenu(caller, arguments, 0);
}

public
void UnmuteMenu(Player caller, const char[] arguments)
{
    TargetsMenu(caller, arguments, 1);
}

public
void GagMenu(Player caller, const char[] arguments)
{
    TargetsMenu(caller, arguments, 2);
}

public
void UngagMenu(Player caller, const char[] arguments)
{
    TargetsMenu(caller, arguments, 3);
}

public
void SilenceMenu(Player caller, const char[] arguments)
{
    TargetsMenu(caller, arguments, 4);
}

public
void UnsilenceMenu(Player caller, const char[] arguments)
{
    TargetsMenu(caller, arguments, 5);
}

public void Say(Player caller, const char[] arguments)
{
    if (GetArgumentCount(arguments) < 1)
    {
        ReplyToCommand(caller, "Usage: sr_say <message>");
        return;
    }

    if (caller != NoPlayer && IsPlayerGagged(caller))
    {
        ReplyToCommand(caller, "You cannot send an announcement while gagged.");
        return;
    }

    char message[257], name[129] = "Server", actor[384], output[512], error[512];

    if (!GetRemainingArguments(arguments, 0, message, sizeof(message)) || !ActorField(caller, actor, sizeof(actor)) ||
        (caller != NoPlayer && !GetPlayerName(caller, name, sizeof(name))))
    {
        Failure(caller);
        return;
    }

    if (!message[0])
    {
        ReplyToCommand(caller, "Announcement cannot be empty.");
        return;
    }

    SafeName(name);
    Format(output, sizeof(output), "%s: %s", name, message);
    Player players[128];
    int count = GetPlayers(players, sizeof(players)), sent, failed;

    if (count < 0)
    {
        Failure(caller);
        return;
    }

    for (int i = 0; i < count; i++) {
        if (players[i] == caller || IsPlayerBot(players[i]))
            continue;

        if (ReplyToCommand(players[i], output))
            sent++;
        else
        {
            failed++;

            if (!error[0])
                GetLastError(error, sizeof(error));
        }
    }

    char target[96];
    Format(target, sizeof(target), "recipients=%d failed=%d", sent, failed);
    AuditTarget(actor, "sr_say", target, failed == 0, 0, false, failed ? error : message);

    if (failed)
        Format(output,
               sizeof(output),
               "Announcement reached %d players; %d %s failed: %s",
               sent,
               failed,
               failed == 1 ? "delivery" : "deliveries",
               error);
    else
        Format(output, sizeof(output), "Announcement sent: %s", message);

    ReplyToCommand(caller, output);
}
