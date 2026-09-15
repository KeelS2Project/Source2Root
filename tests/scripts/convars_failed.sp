#include <source2root>

public bool OnPluginStart()
{
    CreateStringConVar("sr_test_partial", "partial");
    return false;
}
