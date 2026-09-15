#if defined _admin_shared_included
    #endinput
#endif
#define _admin_shared_included

#include <source2root>

stock bool Ready(Player caller, const char[] arguments, const char[] permission, const char[] usage)
{
    if (!HasPermission(caller, permission))
    {
        ReplyToCommand(caller, "You do not have access to this command.");
        return false;
    }
    if (GetArgumentCount(arguments) != 0)
    {
        ReplyToCommand(caller, usage);
        return false;
    }
    return true;
}

stock void Failure(Player caller)
{
    char error[512];
    GetLastError(error, sizeof(error));
    ReplyToCommand(caller, error);
}

stock void SafeName(char[] name)
{
    for (int i = 0; name[i]; i++)
        if ((name[i] > 0 && name[i] < 32) || name[i] == 127) name[i] = ' ';
}

stock void EscapeField(const char[] text, char[] output, int capacity)
{
    int out;
    for (int i = 0; text[i] && out + 2 < capacity; i++)
    {
        int c = text[i];
        if ((c > 0 && c < 32) || c == 127) c = ' ';
        if (c == '"' || c == '\\') output[out++] = '\\';
        output[out++] = c;
    }
    output[out] = 0;
}

stock bool ActorField(Player caller, char[] output, int capacity)
{
    if (caller == NoPlayer) { Format(output, capacity, "actor=console"); return true; }
    char identity[24], name[129], escaped[259];
    if (!GetPlayerSteamID(caller, identity, sizeof(identity)) || !GetPlayerName(caller, name, sizeof(name))) return false;
    EscapeField(name, escaped, sizeof(escaped));
    Format(output, capacity, "actor=\"%s\" steamid=%s", escaped, identity);
    return true;
}

stock void Audit(Player caller, const char[] action)
{
    char actor[384], record[512];
    if (!ActorField(caller, actor, sizeof(actor))) return;
    Format(record, sizeof(record), "%s: %s result=success", action, actor);
    LogMessage(record);
}

stock void TargetField(Player target, char[] output, int capacity)
{
    char name[129], escaped[259], identity[24];
    int userid;
    if (!GetPlayerName(target, name, sizeof(name)) || !GetPlayerUserID(target, userid))
    {
        Format(output, capacity, "target=unavailable");
        return;
    }
    EscapeField(name, escaped, sizeof(escaped));
    if (GetPlayerSteamID(target, identity, sizeof(identity)))
        Format(output, capacity, "target=\"%s\" target_steamid=%s target_userid=%d", escaped, identity, userid);
    else Format(output, capacity, "target=\"%s\" identity=unavailable target_userid=%d", escaped, userid);
}

stock void AuditTarget(const char[] actor, const char[] command, const char[] target,
    bool success, int damage, bool slap, const char[] error)
{
    char detail[1025], record[2048], amount[32];
    EscapeField(error, detail, sizeof(detail));
    if (slap) Format(amount, sizeof(amount), " damage=%d", damage);
    Format(record, sizeof(record), "%s: %s %s%s result=%s detail=\"%s\"", command, actor, target,
        amount, success ? "success" : "failed", detail);
    LogMessage(record);
}
