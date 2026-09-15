#include <source2root>
#include <source2root_random>

public bool OnPluginStart()
{
    if (RandomInt(2147483647, 2147483647) != 2147483647
        || RandomInt(-2147483647 - 1, -2147483647 - 1) != -2147483647 - 1
        || RandomInt(-9, -9) != -9)
        return false;
    RandomInt(-2147483647 - 1, 2147483647);
    for (int i = 0; i < 64; i++)
    {
        int result = RandomInt(-10, 6);
        if (result < -10 || result > 6)
            return false;
    }
    LogMessage("Random native bounds passed.");
    return RegisterCommand("sr_random_error", "", Error);
}

public void Error(Player caller, const char[] arguments)
{
    RandomInt(2, 1);
}
