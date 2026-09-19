#include <source2root>

Player remembered;
char protectedText[8] = "safe";

public bool OnPluginStart()
{
    return RegisterCommand("sr_find", "", FindMany) && RegisterCommand("sr_find_one", "", FindOne)
        && RegisterCommand("sr_info", "", Info) && RegisterCommand("sr_tools", "", Tools)
        && RegisterCommand("sr_remember", "", Remember) && RegisterCommand("sr_current", "", Current)
        && RegisterCommand("sr_badformat", "", BadFormat);
}

bool Equal(const char[] first, const char[] second)
{
    int i;

    while (first[i] && first[i] == second[i])
        i++;

    return first[i] == second[i];
}

void Check(bool result, const char[] message)
{
    if (!result)
        LogMessage(message);
}

void Find(Player caller, const char[] selector, bool multiple)
{
    Player players[128];
    char text[512];
    int count = FindTargets(caller, selector, players, sizeof(players), multiple);

    if (count < 0)
    {
        Check(players[0] == NoPlayer, "FAILED: failed selection output");
        GetLastError(text, sizeof(text));
        ReplyToCommand(caller, text);
        return;
    }

    Format(text, sizeof(text), "count=%d", count);
    ReplyToCommand(caller, text);

    for (int i = 0; i < count; i++)
    {
        int userid;
        GetPlayerUserID(players[i], userid);
        Format(text, sizeof(text), "userid=%d", userid);
        ReplyToCommand(caller, text);
    }
}

public
void FindMany(Player caller, const char[] arguments)
{
    Find(caller, arguments, true);
}

public
void FindOne(Player caller, const char[] arguments)
{
    Find(caller, arguments, false);
}

public
void Remember(Player caller, const char[] arguments)
{
    remembered = caller;
}

public
void Current(Player caller, const char[] arguments)
{
    ReplyToCommand(caller, IsPlayerConnected(remembered) ? "connected" : "disconnected");
}

public
void BadFormat(Player caller, const char[] arguments)
{
    Format(protectedText, sizeof(protectedText), "%n", 42);
}

public void Info(Player caller, const char[] arguments)
{
    int userid, team;
    char text[128];

    if (GetPlayerUserID(caller, userid) && GetPlayerTeam(caller, team))
    {
        Format(text, sizeof(text), "id=%d team=%d alive=%d bot=%d", userid, team, IsPlayerAlive(caller), IsPlayerBot(caller));
        ReplyToCommand(caller, text);
    }

    if (GetPlayerSteamID(caller, text, sizeof(text), SteamID2))
        ReplyToCommand(caller, text);

    if (GetPlayerSteamID(caller, text, sizeof(text), SteamID3))
        ReplyToCommand(caller, text);
}

public void Tools(Player caller, const char[] arguments)
{
    char output[256];
    int value;
    Check(Equal(protectedText, "safe"), "FAILED: bad format modified output");
    Check(Format(output,
                 sizeof(output),
                 "%s %d %i %u %x %X %.2f %c %%",
                 "string",
                 -2147483647 - 1,
                 42,
                 -1,
                 255,
                 255,
                 1.25,
                 65) > 0 &&
              Equal(output, "string -2147483648 42 4294967295 ff FF 1.25 A %"),
          "FAILED: varargs formatting");

    Format(output, sizeof(output), "[%05d][%-5s][%.3s]", -12, "x", "abcdef");
    Check(Equal(output, "[-0012][x    ][abc]"), "FAILED: field formatting");
    Check(Format(output, 4, "12345") == 3 && Equal(output, "123"), "FAILED: bounded formatting");
    Format(output, sizeof(output), "%s end", output);
    Check(Equal(output, "123 end"), "FAILED: overlapping format output");
    Check(GetArgumentCount(" one \"two three\" \"\" four\\\\five ") == 4, "FAILED: argument count");
    Check(GetArgument("one \"two three\"", 1, output, sizeof(output)) && Equal(output, "two three"),
          "FAILED: quoted argument");

    Check(GetArgument("\"a\\\"b\"", 0, output, sizeof(output)) && Equal(output, "a\"b"), "FAILED: escaped argument");
    Check(GetArgument("\"\"", 0, output, sizeof(output)) && !output[0], "FAILED: empty argument");
    Check(GetRemainingArguments("target \"a reason\" with spaces", 1, output, sizeof(output)) &&
              Equal(output, "a reason with spaces"),
          "FAILED: remaining arguments");

    Check(GetRemainingArguments("one", 1, output, sizeof(output)) && !output[0], "FAILED: empty argument tail");
    Check(!GetArgument("long", 0, output, 2) && !output[0], "FAILED: argument capacity");
    Check(!GetArgument("one", 1, output, sizeof(output)) && !output[0], "FAILED: missing argument");
    Check(GetArgumentCount("\"unfinished") == -1 && GetArgumentCount("one;two") == -1
        && GetArgumentCount("one\"two\"") == -1 && GetArgumentCount("\"one\"two") == -1, "FAILED: malformed arguments");

    Check(ParseInt("-2147483648", value) && value == (-2147483647 - 1), "FAILED: signed minimum");
    Check(ParseInt("2147483647", value) && value == 2147483647, "FAILED: signed maximum");
    Check(ParseInt("0", value, 0, 1000) && !value, "FAILED: bounded integer");
    Check(!ParseInt("2147483648", value) && !value && !ParseInt("1x", value) && !ParseInt("+1", value)
        && !ParseInt(" 1", value) && !ParseInt("", value) && !ParseInt("1001", value, 0, 1000), "FAILED: rejected integer");

    Player one[1], players[128];
    Check(GetPlayers(one, sizeof(one)) == -1 && one[0] == NoPlayer, "FAILED: no partial player array");
    int count = GetPlayers(players, sizeof(players));
    Check(count > 1 && IsPlayerConnected(players[0]) && !IsPlayerConnected(NoPlayer), "FAILED: current player handles");
    ReplyToCommand(caller, "tools completed");
}
