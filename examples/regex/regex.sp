#include <source2root_regex>
RegexPattern commandToken;
public bool OnPluginStart()
{
    char error[256]; int offset;
    commandToken = Regex_Compile("(?<word>[A-Za-z]+)", Regex_None, error, sizeof(error), offset);
    if (!commandToken) { LogMessage(error); return false; }
    return RegisterCommand("sr_words", "", Words);
}
public void Words(Player player, const char[] arguments)
{
    char error[256];
    RegexResult result = Regex_Match(commandToken, arguments, true, 0, error, sizeof(error));
    if (!result) { ReplyToCommand(player, error); return; }
    for (int i = 0; i < Regex_MatchCount(result); i++) {
        char word[4096];
        if (Regex_CaptureName(result, i, "word", word, sizeof(word))) ReplyToCommand(player, word);
    }
    Regex_CloseResult(result);
}
