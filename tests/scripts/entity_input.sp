#include <source2root_sdktools>
Entity Current, Participant;
char Text[32] = "payload";
int Depth, Visits;
bool Bad, LastInvoked;

public bool OnPluginStart()
{
    int direct, queued;

    if (!Entity_GetInputCapabilities(direct, queued) || direct != 511 || queued != 383)
        return false;

    LogMessage("INPUT_SCRIPT_READY");
    return RegisterCommand("sr_input_all", "", All)
        && RegisterCommand("sr_input_invalid", "", Invalid)
        && RegisterCommand("sr_input_error", "", EngineError)
        && RegisterCommand("sr_input_retry", "", Retry)
        && RegisterCommand("sr_input_close", "", CloseCase)
        && RegisterCommand("sr_input_callback", "", CloseCallback)
        && RegisterCommand("sr_input_recursion", "", Recursion)
        && RegisterCommand("sr_input_recurse", "", Recurse)
        && RegisterCommand("sr_input_leave", "", Leave)
        && RegisterCommand("sr_input_stale", "", Stale)
        && RegisterCommand("sr_input_bad_payload", "", BadPayload)
        && RegisterCommand("sr_input_bad_participant", "", BadParticipant)
        && RegisterCommand("sr_input_refusal", "", Refusal);
}

void Clean()
{
    if (Current != NoEntity)
        Entity_Close(Current);

    if (Participant != NoEntity)
        Entity_Close(Participant);

    Current = Participant = NoEntity;
}

bool Prepare()
{
    Clean();
    Current = Entity_Find(4);
    Participant = Entity_Find(3);
    return Current != NoEntity && Participant != NoEntity;
}

void Report(bool valid, const char[] success)
{
    if (valid)
        LogMessage(success);
    else
    {
        char error[256];
        GetLastError(error, sizeof(error));
        LogMessage("INPUT_FAILED");
        LogMessage(error);
    }
}

public void All(Player player, const char[] arguments)
{
    bool valid = Prepare(), invoked;
    float vector[3] = {1.0, 2.0, 3.0}, angles[3] = {4.0, 5.0, 6.0};
    int color[4] = {10, 20, 30, 255};

    for (int i = 0; i < 2; ++i)
    {
        bool queue = i != 0;
        float delay = queue ? 1.25 : 0.0;
        valid = Entity_InputVoid(Current,"Enable",invoked,Participant,Current,queue,delay) && invoked && valid;
        valid = Entity_InputString(Current,"Enable",invoked,"payload",Participant,Current,queue,delay) && invoked && valid;
        valid = Entity_InputBool(Current,"Enable",invoked,true,Participant,Current,queue,delay) && invoked && valid;
        valid = Entity_InputInt(Current,"Enable",invoked,-17,Participant,Current,queue,delay) && invoked && valid;
        valid = Entity_InputFloat(Current,"Enable",invoked,1.25,Participant,Current,queue,delay) && invoked && valid;
        valid = Entity_InputVector(Current,"Enable",invoked,vector,Participant,Current,queue,delay) && invoked && valid;
        valid = Entity_InputAngles(Current,"Enable",invoked,angles,Participant,Current,queue,delay) && invoked && valid;
        bool result = Entity_InputColor(Current,"Enable",invoked,color,Participant,Current,queue,delay);
        valid = (queue ? (!result && !invoked) : (result && invoked)) && valid;
        valid = Entity_InputEntity(Current,"Enable",invoked,Participant,Participant,Current,queue,delay) && invoked && valid;
    }

    Clean();
    Report(valid, "INPUT_ALL_OK");
}

public void Invalid(Player player, const char[] arguments)
{
    bool valid = Prepare(), invoked = true;
    valid = !Entity_InputVoid(Current,"",invoked) && !invoked && valid;
    valid = !Entity_InputBool(Current,"Enable",invoked,view_as<bool>(2)) && !invoked && valid;
    valid = !Entity_InputFloat(Current,"Enable",invoked,view_as<float>(0x7f800000)) && !invoked && valid;
    valid = !Entity_InputVoid(Current,"Enable",invoked,NoEntity,NoEntity,true,-1.0) && !invoked && valid;
    valid = !Entity_InputVoid(Current,"Enable",invoked,NoEntity,NoEntity,false,1.0) && !invoked && valid;
    valid = !Entity_InputVoid(Current,"Enable",invoked,NoEntity,NoEntity,view_as<bool>(2)) && !invoked && valid;
    Clean();
    Report(valid, "INPUT_INVALID_OK");
}

public void EngineError(Player player, const char[] arguments)
{
    bool valid = Prepare(), invoked;
    valid = !Entity_InputVoid(Current,"Enable",invoked) && invoked && valid;
    Clean();
    Report(valid, "INPUT_ERROR_OK");
}

public void Retry(Player player, const char[] arguments)
{
    bool valid = Prepare(), invoked = true;
    valid = !Entity_InputVoid(Current,"Enable",invoked) && !invoked && valid;
    Clean();
    Report(valid, "INPUT_RETRY_OK");
}

public void CloseCase(Player player, const char[] arguments)
{
    bool valid = Prepare(), invoked;
    valid = Entity_InputString(Current,"Enable",invoked,Text,Participant,Current) && invoked && valid;
    Report(valid && Current == NoEntity && Participant == NoEntity && Text[0] == 'X',"INPUT_CLOSE_OK");
    Text[0] = 'p';
}

public
void CloseCallback(Player player, const char[] arguments)
{
    Text[0] = 'X';
    Clean();
}

public void Recursion(Player player, const char[] arguments)
{
    bool valid = Prepare(), invoked;
    Depth = Visits = 0;
    Bad = false;
    valid = Entity_InputVoid(Current,"Enable",invoked) && invoked && valid;
    Clean();
    Report(valid && !Bad && Visits == 8, "INPUT_RECURSION_OK");
}

public void Recurse(Player player, const char[] arguments)
{
    ++Depth;
    ++Visits;
    bool invoked;
    bool result = Entity_InputVoid(Current,"Enable",invoked);

    if (Depth == 8 ? (result || invoked) : (!result || !invoked))
        Bad = true;

    --Depth;
}

public
void Leave(Player player, const char[] arguments)
{
    Report(Prepare(), "INPUT_LEAVE_OK");
}

public void Stale(Player player, const char[] arguments)
{
    bool invoked = true;
    bool valid = !Entity_InputVoid(Current,"Enable",invoked,Participant) && !invoked;
    Clean();
    Report(valid, "INPUT_STALE_OK");
}

public void BadPayload(Player player, const char[] arguments)
{
    Prepare();
    LastInvoked = true;
    Entity_InputEntity(Current,"Enable",LastInvoked,NoEntity);
    LogMessage("INPUT_FAILED bad payload accepted");
}

public void BadParticipant(Player player, const char[] arguments)
{
    Prepare();
    LastInvoked = true;
    Entity old = Participant;
    Entity_Close(Participant);
    Participant = NoEntity;
    Entity_InputVoid(Current,"Enable",LastInvoked,old);
    LogMessage("INPUT_FAILED closed participant accepted");
}

public void Refusal(Player player, const char[] arguments)
{
    Report(!LastInvoked, "INPUT_REFUSAL_OK");
    Clean();
}
