#include <source2root>

public bool OnPluginStart()
{
    if (SetPlayerMuted(NoPlayer, true) || SetPlayerGagged(NoPlayer, true)) return false;
    return RegisterCommand("sr_comms_owner", "admin.test", Command);
}
public void Command(Player caller, const char[] arguments)
{
    char selector[257], operation[32]; Player players[1];
    if (!GetArgument(arguments, 0, selector, sizeof(selector)) || !GetArgument(arguments, 1, operation, sizeof(operation))
        || FindTargets(caller, selector, players, sizeof(players)) != 1) { ReplyToCommand(caller, "fixture target failed"); return; }
    if (operation[0] == 'm') SetPlayerMuted(players[0], true);
    if (operation[0] == 'g') SetPlayerGagged(players[0], true);
    if (operation[0] == 'c') { SetPlayerMuted(players[0], false); SetPlayerGagged(players[0], false); }
    if (operation[0] == 's') {
        char result[64]; Format(result, sizeof(result), "muted=%d gagged=%d", IsPlayerMuted(players[0]), IsPlayerGagged(players[0])); ReplyToCommand(caller, result);
    }
}
public void OnPluginStop()
{
    if (SetPlayerMuted(NoPlayer, true) || SetPlayerGagged(NoPlayer, true)) LogMessage("FAILED: retiring restrictions were accepted");
}
