#include <source2root_sdktools>
#include <source2root_sdkhooks>
Entity Current;
SDKHook SpawnHook;
int Reference, Pre, Post, Mode;
bool Bad;

public bool OnPluginStart()
{
    if (!Entity_ConstructionAvailable())
        return false;

    LogMessage("CONSTRUCTION_SCRIPT_READY");
    return RegisterCommand("sr_construct_normal", "", Normal)
        && RegisterCommand("sr_construct_block", "", Block)
        && RegisterCommand("sr_construct_close", "", CloseOwner)
        && RegisterCommand("sr_construct_close_hook", "", CloseHook)
        && RegisterCommand("sr_construct_error", "", EngineError)
        && RegisterCommand("sr_construct_retry", "", Retry)
        && RegisterCommand("sr_construct_finish", "", Finish)
        && RegisterCommand("sr_construct_leave", "", Leave)
        && RegisterCommand("sr_construct_stale", "", Stale)
        && RegisterCommand("sr_construct_cancel", "", Cancel)
        && RegisterCommand("sr_construct_cancel_grow", "", Grow);
}

bool Prepare()
{
    Current = Entity_Create("prop_dynamic");

    if (Current == NoEntity || !Entity_IsValid(Current) || !Entity_IsPending(Current) ||
        !Entity_Same(Current, Current) || !Entity_SourceHandle(Current, Reference) ||
        Entity_FromHandle(Reference) != NoEntity)
        return false;

    float origin[3] = {1.0,2.0,3.0}, angles[3] = {4.0,5.0,6.0};
    int color[4] = {10,20,30,255};

    if (!Entity_SetKeyString(Current, "model", "models/test.vmdl") || !Entity_SetKeyBool(Current, "solid", true) ||
        !Entity_SetKeyInt(Current, "flags", 7) || !Entity_SetKeyFloat(Current, "scale", 1.5) ||
        !Entity_SetKeyVector(Current, "origin", origin) || !Entity_SetKeyAngles(Current, "angles", angles) ||
        !Entity_SetKeyColor(Current, "rendercolor", color))
        return false;

    color[1] = -1;

    if (Entity_SetKeyColor(Current, "rendercolor", color) || Entity_SetKeyBool(Current, "solid", view_as<bool>(2)) ||
        Entity_SetKeyFloat(Current, "scale", view_as<float>(0x7f800000)) ||
        Entity_SetKeyString(Current, "classname", "other"))
        return false;

    float position[3] = {10.0,20.0,30.0};

    if (!Entity_Teleport(Current, position, angles, origin, ENTITY_TELEPORT_POSITION))
        return false;

    SchemaField health = Schema_Find("CBaseEntity","m_iHealth",Schema_Int32);
    int value;
    bool strict = health != NoSchemaField && !Entity_ReadInt(Current,health,value)
        && !Entity_WriteInt(Current,health,7) && !Entity_SetModel(Current,"model.vmdl") && !Entity_Remove(Current);

    Schema_Close(health);

    if (!strict)
        return false;

    Pre = Post = 0;
    Bad = false;
    SpawnHook = SDKHook_Add(Reference,"spawn",SDKHook_Both,OnSpawn);
    return SpawnHook != NoSDKHook;
}

void Clean()
{
    if (SpawnHook != NoSDKHook)
    {
        SDKHook_Close(SpawnHook);
        SpawnHook = NoSDKHook;
    }

    if (Current != NoEntity)
    {
        Entity_Close(Current);
        Current = NoEntity;
    }
}

public SDKHookAction OnSpawn(SDKHookFrame frame, SDKHookPhase phase, int data)
{
    if (SDKHook_Entity(frame) != Reference || SDKHook_Kind(frame) != SDKHook_Spawn || data != 0)
        Bad = true;

    if (phase == SDKHook_Pre)
    {
        ++Pre;

        if (!Entity_IsPending(Current) || Entity_SetKeyInt(Current, "flags", 99))
            Bad = true;

        if (Mode == 1)
            return SDKHook_Block;

        if (Mode == 2)
        {
            if (!Entity_Close(Current))
                Bad = true;

            Current = NoEntity;
        }

        if (Mode == 4)
        {
            SDKHook_Close(SpawnHook);
            SpawnHook = NoSDKHook;
        }
    }
    else
    {
        ++Post;

        if (Mode == 1)
        {
            if ((SDKHook_Flags(frame) & SDKHOOK_ORIGINAL_CALLED) || !Entity_IsPending(Current))
                Bad = true;
        }
        else if (!(SDKHook_Flags(frame) & SDKHOOK_ORIGINAL_CALLED))
            Bad = true;
    }

    return SDKHook_Continue;
}

void Run(int mode)
{
    Mode = mode;

    if (!Prepare())
    {
        LogMessage("CONSTRUCTION_FAILED prepare");
        Clean();
        return;
    }

    bool invoked;
    bool success = Entity_DispatchSpawn(Current,invoked);
    bool valid = invoked && Pre == 1 && Post == ((mode == 2 || mode == 4) ? 0 : 1) && !Bad;

    if (mode == 0 || mode == 4)
    {
        valid = valid && success && Entity_IsValid(Current) && !Entity_IsPending(Current);
        bool repeated = true;
        valid = valid && !Entity_DispatchSpawn(Current,repeated) && !repeated;
        Entity found = Entity_FromHandle(Reference);
        valid = valid && found != NoEntity && Entity_Same(Current,found);
        Entity_Close(found);
    }
    else
        valid = valid && !success;

    Clean();
    LogMessage(valid ? "CONSTRUCTION_CASE_OK" : "CONSTRUCTION_FAILED spawn");
}

public
void Normal(Player caller, const char[] arguments)
{
    Run(0);
}

public
void Block(Player caller, const char[] arguments)
{
    Run(1);
}

public
void CloseOwner(Player caller, const char[] arguments)
{
    Run(2);
}

public
void CloseHook(Player caller, const char[] arguments)
{
    Run(4);
}

public
void EngineError(Player caller, const char[] arguments)
{
    Run(3);
}

public void Retry(Player caller, const char[] arguments)
{
    Mode = 0;

    if (!Prepare())
    {
        LogMessage("CONSTRUCTION_FAILED retry setup");
        return;
    }

    bool invoked = true;
    LogMessage(!Entity_DispatchSpawn(Current,invoked) && !invoked && Pre == 0 && Post == 0 && Entity_IsPending(Current)
        ? "CONSTRUCTION_RETRY_OK" : "CONSTRUCTION_FAILED noninvoked");
}

public void Finish(Player caller, const char[] arguments)
{
    bool invoked;
    bool valid = Entity_DispatchSpawn(Current, invoked) && invoked && Pre == 1 && Post == 1 && !Bad &&
                 !Entity_IsPending(Current);

    Clean();
    LogMessage(valid ? "CONSTRUCTION_FINISH_OK" : "CONSTRUCTION_FAILED retry finish");
}

public void Leave(Player caller, const char[] arguments)
{
    Mode = 0;
    LogMessage(Prepare() ? "CONSTRUCTION_LEFT_PENDING" : "CONSTRUCTION_FAILED leave");
}

public void Stale(Player caller, const char[] arguments)
{
    bool invoked = true;
    bool valid = !Entity_IsValid(Current) && !Entity_SetKeyInt(Current,"flags",1)
        && !Entity_DispatchSpawn(Current,invoked) && !invoked && Pre == 0 && Post == 0;

    Clean();
    LogMessage(valid ? "CONSTRUCTION_STALE_OK" : "CONSTRUCTION_FAILED stale");
}

public void Cancel(Player caller, const char[] arguments)
{
    Current = Entity_Create("prop_dynamic");
    bool valid = Current != NoEntity && Entity_Close(Current);
    Current = NoEntity;
    LogMessage(valid ? "CONSTRUCTION_CANCEL_OK" : "CONSTRUCTION_FAILED cancel");
}

public void Grow(Player caller, const char[] arguments)
{
    Entity temporary[96];
    bool valid = true;

    for (int i = 0; i < sizeof(temporary); i++)
    {
        temporary[i] = Entity_Find(4);

        if (temporary[i] == NoEntity)
            valid = false;
    }

    for (int i = 0; i < sizeof(temporary); i++)
        if (temporary[i] != NoEntity)
            Entity_Close(temporary[i]);

    LogMessage(valid ? "CONSTRUCTION_GROW_OK" : "CONSTRUCTION_FAILED cleanup growth");
}
