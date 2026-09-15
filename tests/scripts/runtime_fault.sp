#include <source2root>
native int TestArraySum(const int[] values, int count);
native int TestNativeDelay();

public int ArraySum()
{
    int values[] = { 7, 8, 9 };
    return TestArraySum(values, sizeof(values));
}

public void BadArray()
{
    int values[] = { 1, 2 };
    TestArraySum(values, -1);
}

public void Runaway()
{
    int value;
    while (true)
        value++;
}

public void Divide(int zero)
{
    int values[1];
    values[0] = 10 / zero;
    TestArraySum(values, 1);
}

public int DelayedNative()
{
    int value = TestNativeDelay();
    for (int i = 0; i < 8; i++)
        value++;
    return value;
}
