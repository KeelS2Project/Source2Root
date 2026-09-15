#include <source2root>

Player target;

public bool OnPluginStart()
{
    return RegisterCommand("sr_access", "", Access);
}

public void Access(Player caller, const char[] arguments)
{
    if (arguments[0] == 'r')
    {
        target = caller;
        ReplyToCommand(caller, "remembered");
    }
    else if (arguments[0] == 'g')
    {
        char group[65];
        int immunity;
        if (IsAdmin(caller) && GetAdminGroup(caller, group, sizeof(group)) && GetAdminImmunity(caller, immunity))
        {
            ReplyToCommand(caller, group);
            if (immunity == 100) ReplyToCommand(caller, "immunity 100");
        }
        else ReplyToCommand(caller, "no administrator");
    }
    else if (arguments[0] == 'm')
    {
        Menu menu = CreateMenu("Check target", "admin.kick", Selected);
        if (menu != NoMenu && (!AddMenuItem(menu, "Check target") || !ShowMenu(menu, caller))) CloseMenu(menu);
    }
    else if (arguments[0] == 'l')
    {
        ReplyToCommand(caller, ReloadAdmins(caller) ? "reloaded" : "reload denied or invalid");
    }
    else if (arguments[0] == 'i')
    {
        ReplyToCommand(caller, CanTargetIdentity(caller, "invalid", "admin.ban") ? "invalid identity accepted" : "invalid identity rejected");
    }
    else if (arguments[0] == 'o')
    {
        ReplyToCommand(caller, CanTargetIdentity(caller, "[U:1:123]", "admin.kick") ? "allowed" : "denied");
    }
    else ReplyToCommand(caller, CanTarget(caller, target, arguments[0] == 'b' ? "admin.ban" : "admin.kick") ? "allowed" : "denied");
}

public void Selected(Player caller, int item)
{
    if (item == 0) ReplyToCommand(caller, CanTarget(caller, target, "admin.kick") ? "allowed" : "denied");
}
