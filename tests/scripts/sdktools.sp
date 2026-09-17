#include <source2root_sdktools>
Entity Retained;
SchemaField Health;
Player SavedPlayer;
Entity ClosingEntity;
SchemaField ClosingField;
public bool OnPluginStart()
{
    Health = Schema_Find("CBaseEntity", "m_iHealth", Schema_Int32);
    Retained = Entity_Find(4);
    if (Health == NoSchemaField || Retained == NoEntity || Entity_Index(Retained) != 4 || !Entity_IsValid(Retained)) return false;
    int value, source;
    if (!Entity_ReadInt(Retained, Health, value) || value != 73 || !Entity_SourceHandle(Retained, source) || source != 0x23004) return false;
    Entity same = Entity_FromHandle(source);
    if (same == NoEntity || !Entity_Same(Retained, same) || !Entity_Close(same)) return false;
    Player players[4];
    if (GetPlayers(players, sizeof(players)) != 1) return false;
    SavedPlayer = players[0];
    Entity pawn = Entity_FromPlayer(SavedPlayer), controller = Entity_FromPlayer(SavedPlayer, false);
    if (pawn == NoEntity || controller == NoEntity || !Entity_Same(pawn, Retained) || Entity_Index(controller) != 3) return false;
    Entity_Close(pawn); Entity_Close(controller);
    char classname[64], name[64], profile[64], small[2];
    if (Schema_FieldType(Health) != Schema_Int32 || Schema_FieldSize(Health) != 4
        || !Schema_FieldInfo(Health, classname, sizeof(classname), name, sizeof(name), profile, sizeof(profile))
        || classname[0] != 'C' || name[0] != 'm' || profile[0] != 's'
        || Schema_FieldInfo(Health, small, sizeof(small), name, sizeof(name), profile, sizeof(profile))) return false;
    SchemaField wide = Schema_Find("CTestEntity", "m_uWide", Schema_UInt64);
    if (wide == NoSchemaField || Entity_ReadInt(Retained, wide, value) || value != 0
        || !Entity_ReadIntegerText(Retained, wide, name, sizeof(name)) || name[0] != '1' || name[19] != '5' || name[20] != 0
        || Entity_ReadIntegerText(Retained, wide, small, sizeof(small))) return false;
    Schema_Close(wide);
    SchemaField vector = Schema_Find("CTestEntity", "m_vecOrigin", Schema_Vector3);
    float position[3];
    if (vector == NoSchemaField || !Entity_ReadVector(Retained, vector, position) || position[0] != 1.0 || position[2] != 3.0
        || Entity_ReadInt(Retained, vector, value)) return false;
    Schema_Close(vector);
    SchemaField number = Schema_Find("CTestEntity", "m_fValue", Schema_Float32);
    float fraction;
    if (number == NoSchemaField || !Entity_ReadFloat(Retained, number, fraction) || fraction != 1.25) return false;
    Schema_Close(number);
    SchemaField flag = Schema_Find("CTestEntity", "m_bFlag", Schema_Bool);
    if (flag == NoSchemaField || !Entity_ReadInt(Retained, flag, value) || value != 1) return false;
    Schema_Close(flag);
    SchemaField link = Schema_Find("CTestEntity", "m_hOther", Schema_EntityHandle);
    if (link == NoSchemaField || !Entity_ReadSourceHandle(Retained, link, source) || source != 0x45005) return false;
    Entity other = Entity_ReadEntity(Retained, link);
    if (other == NoEntity || Entity_Index(other) != 5 || Entity_Same(Retained, other)) return false;
    Schema_Close(link); Entity_Close(other);
    if (Schema_Find("CBaseEntity", "m_iHealth", Schema_Float32) != NoSchemaField || Entity_Find(-1) != NoEntity
        || Entity_FromHandle(-1) != NoEntity || Schema_Find("CBaseEntity", "missing", Schema_Int32) != NoSchemaField) return false;
    LogMessage("SDKTOOLS_SCRIPT_OK");
    return RegisterCommand("sr_sdk_check", "", CheckRead) && RegisterCommand("sr_sdk_wrong", "", Wrong)
        && RegisterCommand("sr_sdk_stale", "", Stale) && RegisterCommand("sr_sdk_epoch", "", Epoch)
        && RegisterCommand("sr_sdk_player", "", Reconnected)
        && RegisterCommand("sr_sdk_write", "", WriteFields) && RegisterCommand("sr_sdk_write_unavailable", "", WriteUnavailable)
        && RegisterCommand("sr_sdk_write_error", "", WriteError) && RegisterCommand("sr_sdk_write_callback", "", WriteCallback)
        && RegisterCommand("sr_sdk_close_active", "", CloseActive);
}
public void CheckRead(Player caller, const char[] arguments)
{
    int value;
    LogMessage(Entity_ReadInt(Retained, Health, value) && value == 73 ? "SDKTOOLS_READ_OK" : "SDKTOOLS_FAILED");
}
public void Wrong(Player caller, const char[] arguments)
{
    Entity_IsValid(view_as<Entity>(Health));
    LogMessage("SDKTOOLS_FAILED");
}
public void Stale(Player caller, const char[] arguments)
{
    Entity entity = Entity_Find(4); Entity_Close(entity); Entity_IsValid(entity);
    LogMessage("SDKTOOLS_FAILED");
}
public void Epoch(Player caller, const char[] arguments)
{
    int value = 99;
    if (Entity_IsValid(Retained) || Entity_Index(Retained) != -1 || Entity_ReadInt(Retained, Health, value) || value != 0
        || Entity_WriteInt(Retained, Health, 90) || !Entity_Close(Retained)) { LogMessage("SDKTOOLS_FAILED"); return; }
    Retained = Entity_Find(4);
    LogMessage(Retained != NoEntity && Entity_IsValid(Retained) ? "SDKTOOLS_EPOCH_OK" : "SDKTOOLS_FAILED");
}
public void Reconnected(Player caller, const char[] arguments)
{
    LogMessage(Entity_FromPlayer(SavedPlayer) == NoEntity ? "SDKTOOLS_PLAYER_CHANGED_OK" : "SDKTOOLS_FAILED");
}

public void WriteFields(Player caller, const char[] arguments)
{
    int capabilities, value;
    if (!Entity_GetWriteCapabilities(capabilities) || capabilities != ENTITY_WRITE_NUMERIC_FIELDS
        || !Entity_WriteInt(Retained, Health, 91) || !Entity_ReadInt(Retained, Health, value) || value != 91
        || !Entity_WriteInt(Retained, Health, 73) || Entity_WriteIntegerText(Retained, Health, "2147483648"))
    { LogMessage("SDKTOOLS_FAILED integer write"); return; }
    SchemaField wide = Schema_Find("CTestEntity", "m_uWide", Schema_UInt64);
    char text[32];
    if (wide == NoSchemaField || !Entity_WriteIntegerText(Retained, wide, "18446744073709551615")
        || !Entity_ReadIntegerText(Retained, wide, text, sizeof(text)) || text[19] != '5' || text[20] != 0
        || Entity_WriteInt(Retained, wide, -1)) { LogMessage("SDKTOOLS_FAILED wide write"); return; }
    Schema_Close(wide);
    SchemaField number = Schema_Find("CTestEntity", "m_fValue", Schema_Float32);
    float fraction;
    if (number == NoSchemaField || !Entity_WriteFloat(Retained, number, 2.5) || !Entity_ReadFloat(Retained, number, fraction)
        || fraction != 2.5 || !Entity_WriteFloat(Retained, number, 1.25)) { LogMessage("SDKTOOLS_FAILED float write"); return; }
    Schema_Close(number);
    SchemaField vector = Schema_Find("CTestEntity", "m_vecOrigin", Schema_Vector3);
    float input[3] = {4.0, 5.0, 6.0}, output[3];
    if (vector == NoSchemaField || !Entity_WriteVector(Retained, vector, input) || !Entity_ReadVector(Retained, vector, output)
        || output[0] != 4.0 || output[2] != 6.0) { LogMessage("SDKTOOLS_FAILED vector write"); return; }
    input[0] = 1.0; input[1] = 2.0; input[2] = 3.0;
    Entity_WriteVector(Retained, vector, input); Schema_Close(vector);
    SchemaField flag = Schema_Find("CTestEntity", "m_bFlag", Schema_Bool);
    if (flag == NoSchemaField || !Entity_WriteInt(Retained, flag, 0) || !Entity_ReadInt(Retained, flag, value) || value != 0
        || Entity_WriteInt(Retained, flag, 2) || !Entity_WriteInt(Retained, flag, 1)) { LogMessage("SDKTOOLS_FAILED bool write"); return; }
    Schema_Close(flag);
    LogMessage("SDKTOOLS_WRITE_OK");
}
public void WriteUnavailable(Player caller, const char[] arguments)
{
    int capabilities = 99;
    LogMessage(Entity_GetWriteCapabilities(capabilities) && capabilities == 0 && !Entity_WriteInt(Retained, Health, 50)
        ? "SDKTOOLS_WRITE_UNAVAILABLE_OK" : "SDKTOOLS_FAILED unsupported write");
}
public void WriteError(Player caller, const char[] arguments)
{
    LogMessage(!Entity_WriteInt(Retained, Health, 50) ? "SDKTOOLS_WRITE_ERROR_OK" : "SDKTOOLS_FAILED write error");
}
public void WriteCallback(Player caller, const char[] arguments)
{
    ClosingEntity = Entity_Find(4); ClosingField = Schema_Find("CBaseEntity", "m_iHealth", Schema_Int32);
    if (ClosingEntity == NoEntity || ClosingField == NoSchemaField || !Entity_WriteInt(ClosingEntity, ClosingField, 81)
        || ClosingEntity != NoEntity || ClosingField != NoSchemaField) { LogMessage("SDKTOOLS_FAILED close during write"); return; }
    int value;
    if (!Entity_ReadInt(Retained, Health, value) || value != 81 || !Entity_WriteInt(Retained, Health, 73))
        { LogMessage("SDKTOOLS_FAILED write callback state"); return; }
    LogMessage("SDKTOOLS_WRITE_CALLBACK_OK");
}
public void CloseActive(Player caller, const char[] arguments)
{
    Entity_Close(ClosingEntity); Schema_Close(ClosingField);
    ClosingEntity = NoEntity; ClosingField = NoSchemaField;
}
