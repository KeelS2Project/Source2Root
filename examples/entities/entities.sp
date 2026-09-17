#include <source2root_sdktools>

public bool OnPluginStart()
{
    return RegisterCommand("sr_entity_health", "", Health, "Read the health field of your current pawn");
}
public void Health(Player caller, const char[] arguments)
{
    if (caller == NoPlayer) { ReplyToCommand(caller, "Use this command as a connected player."); return; }
    SchemaField health = Schema_Find("CBaseEntity", "m_iHealth", Schema_Int32);
    if (health == NoSchemaField) { ReplyToCommand(caller, "This game does not provide the expected health field."); return; }
    Entity pawn = Entity_FromPlayer(caller);
    if (pawn != NoEntity)
    {
        int value;
        if (Entity_ReadInt(pawn, health, value))
        {
            char text[96]; Format(text, sizeof(text), "Current pawn health: %d", value); ReplyToCommand(caller, text);
        }
        else ReplyToCommand(caller, "The health field is unavailable for this pawn.");
        Entity_Close(pawn);
    }
    else ReplyToCommand(caller, "Your current pawn is unavailable.");
    Schema_Close(health);
}
