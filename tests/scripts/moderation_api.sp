#include <source2root>
#include <admin_shared>

Player remembered;

public bool OnPluginStart()
{
    bool removed;

    if (AddBan(NoPlayer, "[U:1:5000]", 0, "startup") || RemoveBan("[U:1:5000]", removed) ||
        KickPlayer(NoPlayer, "startup"))
        return false;

    return RegisterCommand("sr_banapi", "", Check);
}

public void Check(Player caller, const char[] arguments)
{
    if (arguments[0] == 'k')
    {
        remembered = caller;

        if (!KickPlayer(caller, "One self-kick result") || !ShowActivity(caller, "kicked self"))
        {
            Failure(caller);
            return;
        }
    }
    else if (arguments[0] == 's')
    {
        if (ShowActivity(remembered, "forged later activity"))
            LogMessage("FAILED: kick receipt escaped its callback");
        else
            ReplyToCommand(caller, "stored kick receipt refused");
    }
    else if (arguments[0] == 't')
        CreateTimer(1000, Tick, caller, true);
    else
    {
        char id[24] = "[U:1:5000]";

        if (!NormalizeSteamID(id, id, sizeof(id)))
        {
            Failure(caller);
            return;
        }

        char invalid[24] = "must clear";

        if (NormalizeSteamID("invalid", invalid, sizeof(invalid)) || invalid[0])
            LogMessage("FAILED: identity failure output");

        if (AddBan(caller, id, -1, "bad") || AddBan(caller, id, 5256001, "bad"))
            LogMessage("FAILED: native ban bounds");

        bool removed = true;

        if (RemoveBan("bad", removed) || removed)
            LogMessage("FAILED: invalid removal output");

        if (GetBanIdentity(-1, invalid, sizeof(invalid)) || invalid[0])
            LogMessage("FAILED: invalid entry output");

        if (KickPlayer(view_as<Player>(999999), "bad"))
            LogMessage("FAILED: invalid player kick");

        ReplyToCommand(caller, "moderation API bounds passed");
    }
}

public
void Tick(Player player)
{
    LogMessage("FAILED: player timer survived maps");
}

public void OnPluginStop()
{
    bool removed;

    if (AddBan(NoPlayer, "[U:1:5000]", 0, "stop") || RemoveBan("[U:1:5000]", removed) || KickPlayer(NoPlayer, "stop"))
        LogMessage("FAILED: retiring mutations");
}
