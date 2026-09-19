#include <source2root_sdktools>

public bool OnPluginStart()
{
    return RegisterCommand("sr_entity_stop", "admin.root", Stop, "Set your pawn velocity to zero")
        && RegisterCommand("sr_entity_model", "admin.root", Model, "Set a model entity asset: <index> <model>")
        && RegisterCommand("sr_entity_remove", "admin.root", Remove, "Request entity removal: <index>");
}
void Result(Player caller, bool success, const char[] message)
{
    if (success) ReplyToCommand(caller,message);
    else { char error[256]; GetLastError(error,sizeof(error)); ReplyToCommand(caller,error); }
}
public void Stop(Player caller, const char[] arguments)
{
    if (caller == NoPlayer || !IsPlayerAlive(caller)) { ReplyToCommand(caller,"Use this command as a living player."); return; }
    Entity pawn = Entity_FromPlayer(caller);
    if (pawn == NoEntity) { Result(caller,false,""); return; }
    float zero[3];
    bool success = Entity_Teleport(pawn,zero,zero,zero,ENTITY_TELEPORT_VELOCITY);
    Result(caller,success,"Velocity set to zero.");
    Entity_Close(pawn);
}
Entity Target(const char[] arguments)
{
    char number[24]; int index;
    if (!GetArgument(arguments,0,number,sizeof(number)) || !ParseInt(number,index,1)) return NoEntity;
    return Entity_Find(index);
}
public void Model(Player caller, const char[] arguments)
{
    char asset[512];
    if (GetArgumentCount(arguments) != 2 || !GetArgument(arguments,1,asset,sizeof(asset)))
    { ReplyToCommand(caller,"Usage: sr_entity_model <index> <model asset>"); return; }
    Entity entity = Target(arguments);
    if (entity == NoEntity) { Result(caller,false,""); return; }
    Result(caller,Entity_SetModel(entity,asset),"Model setter invoked.");
    Entity_Close(entity);
}
public void Remove(Player caller, const char[] arguments)
{
    if (GetArgumentCount(arguments) != 1) { ReplyToCommand(caller,"Usage: sr_entity_remove <index>"); return; }
    Entity entity = Target(arguments);
    if (entity == NoEntity) { Result(caller,false,""); return; }
    Result(caller,Entity_Remove(entity),"Entity removal requested.");
    Entity_Close(entity);
}
