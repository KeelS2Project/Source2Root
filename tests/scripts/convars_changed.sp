#include <source2root>

public bool OnPluginStart()
{
    return CreateIntConVar("sr_test_count", 2, "Count", -10, 10) != NoConVar;
}
