#include <source2root_sdkhooks>
#include <source2root_sdktools>
SDKHook observer;
// Disabled by default. Install both native extensions and configure a reviewed
// damage target before enabling. This example binds the caller's current pawn.
public bool OnPluginStart()
{
    return RegisterCommand("sr_observe_damage", "admin.slay", ObserveSelf, "Observe damage to your current pawn");
}

public void ObserveSelf(Player player, const char[] arguments)
{
    Entity pawn = Entity_FromPlayer(player);

    if (pawn == NoEntity)
        return;

    int reference;
    bool found = Entity_SourceHandle(pawn, reference);
    Entity_Close(pawn);

    if (!found)
        return;

    if (observer != NoSDKHook)
        SDKHook_Close(observer);

    observer = SDKHook_Add(reference, "example_damage", SDKHook_Post, Observe);

    if (observer != NoSDKHook)
        ReplyToCommand(player, "Damage observation attached to your current pawn.");
    else
    {
        char error[256];
        GetLastError(error, sizeof(error));
        ReplyToCommand(player, error);
    }
}

public SDKHookAction Observe(SDKHookFrame frame, SDKHookPhase phase, int data)
{
    float damage, force[3], position[3];
    int type, custom, inflictor, attacker, ability;

    if (SDKHook_GetDamage(frame, damage, type, custom, inflictor, attacker, ability, force, position))
    {
        char text[128];
        Format(text, sizeof(text), "Pawn reference %d received a damage call of %.2f", SDKHook_Entity(frame), damage);
        LogMessage(text);
    }

    return SDKHook_Continue;
}
