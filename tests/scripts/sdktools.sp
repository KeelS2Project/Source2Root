#include <source2root_sdktools>
Entity Retained;
SchemaField Health;
Player SavedPlayer;
Entity ClosingEntity;
SchemaField ClosingField;
Entity ToolEntity;
float ToolPosition[3];
char ToolModel[64];
int ToolFailures;
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
        && RegisterCommand("sr_sdk_close_active", "", CloseActive)
        && RegisterCommand("sr_sdk_tools", "", Tools)
        && RegisterCommand("sr_sdk_tool_unavailable", "", ToolUnavailable)
        && RegisterCommand("sr_sdk_tool_cap_error", "", ToolCapError)
        && RegisterCommand("sr_sdk_tool_error", "", ToolError)
        && RegisterCommand("sr_sdk_tool_callback", "", ToolCallback)
        && RegisterCommand("sr_sdk_tool_callback_error", "", ToolCallbackError)
        && RegisterCommand("sr_sdk_tool_cap_close", "", ToolCapClose)
        && RegisterCommand("sr_sdk_tool_close", "", ToolClose)
        && RegisterCommand("sr_sdk_tool_recursion", "", ToolRecursion)
        && RegisterCommand("sr_sdk_tool_reenter", "", ToolReenter);
}
public void CheckRead(Player caller, const char[] arguments)
{
    int value;
    LogMessage(Entity_ReadInt(Retained, Health, value) && value == 73 ? "SDKTOOLS_READ_OK" : "SDKTOOLS_FAILED");
}
public void Wrong(Player caller, const char[] arguments)
{
    Entity_Remove(view_as<Entity>(Health));
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
        || Entity_WriteInt(Retained, Health, 90) || Entity_Remove(Retained) || !Entity_Close(Retained)) { LogMessage("SDKTOOLS_FAILED"); return; }
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

public void Tools(Player caller, const char[] arguments)
{
    int capabilities;
    float p[3] = {1.0,2.0,3.0}, a[3] = {4.0,5.0,6.0}, v[3] = {7.0,8.0,9.0};
    if (!Entity_GetToolCapabilities(capabilities) || capabilities != 7 ||
        !Entity_Teleport(Retained,p,a,v) || Entity_Teleport(Retained,p,a,v,0) || Entity_Teleport(Retained,p,a,v,8))
    { LogMessage("SDKTOOLS_FAILED teleport"); return; }
    p[0] = view_as<float>(0x7fc00000);
    if (Entity_Teleport(Retained,p,a,v,ENTITY_TELEPORT_POSITION) || !Entity_Teleport(Retained,p,a,v,ENTITY_TELEPORT_VELOCITY) ||
        Entity_SetModel(Retained,"") || Entity_SetModel(Retained,"bad\nmodel") || !Entity_SetModel(Retained,"models/test.vmdl"))
    { LogMessage("SDKTOOLS_FAILED model/vectors"); return; }
    Entity other = Entity_Find(5);
    if (other == NoEntity || !Entity_Remove(other) || Entity_IsValid(other) || Entity_Remove(other) ||
        Entity_Find(5) != NoEntity || !Entity_Close(other))
    { LogMessage("SDKTOOLS_FAILED remove"); return; }
    LogMessage("SDKTOOLS_TOOLS_OK");
}
public void ToolUnavailable(Player caller, const char[] arguments)
{
    int caps = 99; float zero[3];
    LogMessage(Entity_GetToolCapabilities(caps) && caps == 0 && !Entity_Teleport(Retained,zero,zero,zero) &&
        !Entity_SetModel(Retained,"model") && !Entity_Remove(Retained) ? "SDKTOOLS_TOOL_UNAVAILABLE_OK" : "SDKTOOLS_FAILED unavailable tools");
}
public void ToolCapError(Player caller, const char[] arguments)
{
    int caps = 99;
    LogMessage(!Entity_GetToolCapabilities(caps) && caps == 0 && !Entity_Remove(Retained)
        ? "SDKTOOLS_TOOL_CAP_ERROR_OK" : "SDKTOOLS_FAILED tool cap error");
}
public void ToolError(Player caller, const char[] arguments)
{
    LogMessage(!Entity_SetModel(Retained,"models/error.vmdl") ? "SDKTOOLS_TOOL_ERROR_OK" : "SDKTOOLS_FAILED tool error");
}
void CallbackTool(int kind, bool expectFailure)
{
    ToolEntity = Entity_Find(kind == 4 ? 5 : 4);
    ToolPosition[0] = 11.0; ToolPosition[1] = 12.0; ToolPosition[2] = 13.0;
    Format(ToolModel,sizeof(ToolModel),"models/held.vmdl");
    float zero[3];
    bool result;
    if (kind == 1) result = Entity_Teleport(ToolEntity,ToolPosition,zero,zero,ENTITY_TELEPORT_POSITION);
    else if (kind == 2) result = Entity_SetModel(ToolEntity,ToolModel);
    else result = Entity_Remove(ToolEntity);
    LogMessage(result != expectFailure && ToolEntity == NoEntity && ToolPosition[0] == 99.0 && ToolModel[0] == 'c'
        ? "SDKTOOLS_TOOL_CALLBACK_OK" : "SDKTOOLS_FAILED tool callback");
}
public void ToolCallback(Player caller, const char[] arguments)
{
    int kind;
    if (!ParseInt(arguments,kind,1,4) || kind == 3) { LogMessage("SDKTOOLS_FAILED callback kind"); return; }
    CallbackTool(kind,false);
}
public void ToolCallbackError(Player caller, const char[] arguments) { CallbackTool(2,true); }
public void ToolCapClose(Player caller, const char[] arguments) { CallbackTool(2,true); }
public void ToolClose(Player caller, const char[] arguments)
{
    if (!Entity_Close(ToolEntity)) { LogMessage("SDKTOOLS_FAILED closing tool entity"); return; }
    ToolEntity = NoEntity; ToolPosition[0] = 99.0;
    Format(ToolModel,sizeof(ToolModel),"changed");
}
public void ToolRecursion(Player caller, const char[] arguments)
{
    ToolEntity = Entity_Find(4); ToolFailures = 0;
    bool result = Entity_SetModel(ToolEntity,"models/outer.vmdl"); Entity_Close(ToolEntity); ToolEntity = NoEntity;
    LogMessage(result && ToolFailures == 1 ? "SDKTOOLS_TOOL_RECURSION_OK" : "SDKTOOLS_FAILED tool recursion");
}
public void ToolReenter(Player caller, const char[] arguments)
{
    if (!Entity_SetModel(ToolEntity,"models/nested.vmdl")) ++ToolFailures;
}
