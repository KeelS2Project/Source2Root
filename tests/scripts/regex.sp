#include <source2root_regex>
RegexPattern saved;
RegexResult retained;

public bool OnPluginStart()
{
    char error[256], text[256];
    int offset, start, end;

    if (Regex_Compile("(", Regex_None, error, sizeof(error), offset) || !error[0] || offset != 1)
        return false;

    if (Regex_Compile("(", Regex_None, error, 1, offset) || error[0] || offset != 1)
        return false;

    saved = Regex_Compile("(?<word>[a-z]+)(?:=(\\d+))?", Regex_Caseless, error, sizeof(error), offset);

    if (!saved || error[0] || offset != -1)
        return false;

    retained = Regex_Match(saved, "One=42 two", true, 0, error, sizeof(error));

    if (!retained || error[0] || Regex_MatchCount(retained) != 2 || Regex_GroupCount(retained) != 2 ||
        !Regex_CaptureName(retained, 0, "word", text, sizeof(text)) || text[0] != 'O' ||
        !Regex_Offsets(retained, 1, 0, start, end) || start != 7 || end != 10 ||
        Regex_Capture(retained, 1, 2, text, sizeof(text)) || text[0] || Regex_Offsets(retained, 1, 2, start, end) ||
        start != -1 || end != -1 || Regex_Capture(retained, 0, 0, text, 2) || text[0])
        return false;

    RegexResult none = Regex_Match(saved, "123", false, 0, error, sizeof(error));

    if (!none || error[0] || Regex_MatchCount(none) || !Regex_CloseResult(none))
        return false;

    if (Regex_Match(saved, "a", false, -1, error, sizeof(error)) || !error[0])
        return false;

    if (Regex_Replace(saved, "a=1 b", "${word}:$2", text, sizeof(text), true, error, sizeof(error)) != 2 || error[0] ||
        text[0] != 'a' || text[1] != ':' || text[2] != '1' || text[4] != 'b')
        return false;

    if (Regex_Replace(saved, "a=1", "$0!", text, 2, false, error, sizeof(error)) != -1 || text[0] || !error[0])
        return false;

    text[0] = 'a';
    text[1] = '=';
    text[2] = '1';
    text[3] = 0;

    if (Regex_Replace(saved, text, "$2", text, sizeof(text), true, error, sizeof(error)) != 1 || text[0] != '1' ||
        text[1] || error[0])
        return false;

    if (!Regex_Close(saved) || !Regex_Capture(retained, 0, 1, text, sizeof(text)) || text[0] != 'O')
        return false;

    saved = Regex_Compile("", Regex_UTF, error, sizeof(error), offset);
    RegexResult utf = Regex_Match(saved, "é界", true, 0, error, sizeof(error));

    if (!utf || Regex_MatchCount(utf) != 3 || !Regex_Offsets(utf, 2, 0, start, end) || start != 5 || end != 5 ||
        !Regex_Capture(utf, 0, 0, text, sizeof(text)) || text[0] || !Regex_CloseResult(utf))
        return false;

    if (Regex_Match(saved, "é", false, 1, error, sizeof(error)) || !error[0])
        return false;

    Regex_Close(saved);
    saved = Regex_Compile("x", Regex_None, error, sizeof(error), offset);

    if (!saved)
        return false;

    LogMessage("REGEX_SCRIPT_OK");
    return RegisterCommand("sr_regex_wrong", "", WrongType) && RegisterCommand("sr_regex_stale", "", Stale) &&
        RegisterCommand("sr_regex_check", "", CheckRetained) && RegisterCommand("sr_regex_quota", "", Quota);
}

public void WrongType(Player player, const char[] arguments)
{
    Regex_MatchCount(view_as<RegexResult>(saved));
    LogMessage("REGEX_FAILED_WRONG_TYPE");
}

public void Stale(Player player, const char[] arguments)
{
    char error[256];
    RegexResult stale = Regex_Match(saved, "x", false, 0, error, sizeof(error));
    Regex_CloseResult(stale);
    Regex_MatchCount(stale);
    LogMessage("REGEX_FAILED_STALE");
}

public void CheckRetained(Player player, const char[] arguments)
{
    char text[16];

    if (!Regex_Capture(retained, 1, 1, text, sizeof(text)) || text[0] != 't')
        LogMessage("REGEX_FAILED_RETAINED");
    else
        LogMessage("REGEX_RETAINED_OK");
}

public void Quota(Player player, const char[] arguments)
{
    char error[256];
    int offset;
    RegexPattern patterns[63];

    for (int i = 0; i < sizeof(patterns); i++) {
        patterns[i] = Regex_Compile("x", Regex_None, error, sizeof(error), offset);

        if (!patterns[i])
        {
            LogMessage("REGEX_FAILED_PATTERN_QUOTA");
            return;
        }
    }

    if (Regex_Compile("x", Regex_None, error, sizeof(error), offset) || !error[0])
        LogMessage("REGEX_FAILED_PATTERN_OVERFLOW");

    for (int i = 0; i < sizeof(patterns); i++)
        Regex_Close(patterns[i]);

    RegexResult results[63];

    for (int i = 0; i < sizeof(results); i++) {
        results[i] = Regex_Match(saved, "x", false, 0, error, sizeof(error));

        if (!results[i])
        {
            LogMessage("REGEX_FAILED_RESULT_QUOTA");
            return;
        }
    }

    if (Regex_Match(saved, "x", false, 0, error, sizeof(error)) || !error[0])
        LogMessage("REGEX_FAILED_RESULT_OVERFLOW");

    for (int i = 0; i < sizeof(results); i++)
        Regex_CloseResult(results[i]);

    LogMessage("REGEX_QUOTA_OK");
}
