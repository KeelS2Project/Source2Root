#include <source2root>

ConVar count;
ConVar fraction;
ConVar text;

public bool OnPluginStart()
{
    count = CreateIntConVar("sr_test_count", 1, "Count", -10, 10);
    fraction = CreateFloatConVar("sr_test_fraction", 0.5, "Fraction", 0.0, 1.0);
    text = CreateStringConVar("sr_test_text", "initial", "Text");
    if (count == NoConVar || fraction == NoConVar || text == NoConVar)
        return false;
    ConVar whole = CreateIntConVar("sr_test_unbounded_int", -2147483647 - 1);
    ConVar real = CreateFloatConVar("sr_test_unbounded_float", -3.402823466e38);
    if (whole == NoConVar || real == NoConVar || !CloseConVar(whole) || !CloseConVar(real))
        return false;
    if (SetConVarInt(count, 7) || CreateIntConVar("sr_test_count", 1, "Count", -10, 10) != NoConVar)
        return false;
    return RegisterCommand("sr_variables", "", Variables);
}

public void Variables(Player caller, const char[] arguments)
{
    if (arguments[0] == 'w')
    {
        if (!SetConVarInt(count, 999) || !SetConVarFloat(fraction, -5.0) || !SetConVarString(text, "changed"))
            return;
        int number;
        float part;
        if (GetConVarInt(count, number) && GetConVarFloat(fraction, part) && number == 10 && part == 0.0)
            ReplyToCommand(caller, "typed writes and bounds passed");
    }
    else if (arguments[0] == 'm')
    {
        float part = 1.0;
        if (!GetConVarFloat(count, part) && part == 0.0 && !SetConVarString(count, "wrong type"))
            ReplyToCommand(caller, "type mismatch rejected");
    }
    else if (arguments[0] == 'c')
    {
        if (CloseConVar(text)) ReplyToCommand(caller, "closed");
    }
    else if (arguments[0] == 'n')
    {
        SetConVarFloat(fraction, view_as<float>(0x7f800000));
        ReplyToCommand(caller, "invalid float accepted");
    }
    else
    {
        char value[513];
        if (GetConVarString(text, value, sizeof(value))) ReplyToCommand(caller, value);
    }
}
