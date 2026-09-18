#include <source2root>
typedef Persistent = function int(int mode, float scalar, const char[] text, int &integer,
    float &real, int[] integers, float[] reals, const int[] input_values);
native bool CapturePersistent(Persistent callback);
native int PersistentControl(int operation);

public bool OnPluginStart()
{
    return CapturePersistent(Repeated) && RegisterCommand("sr_persistent", "", Run);
}
public void Run(Player caller, const char[] arguments)
{
    if (PersistentControl(0) != 43) LogMessage("PERSISTENT_FAILED nested native dispatch");
}
public int Repeated(int mode, float scalar, const char[] text, int &integer,
    float &real, int[] integers, float[] reals, const int[] input_values)
{
    if (scalar != 2.5 || text[0] != 'o' || text[1] != 'k' || text[2] != 0
        || integer != 3 || real != 1.25 || integers[0] != 4 || integers[1] != 5
        || reals[0] != 1.0 || reals[1] != 2.0 || input_values[0] != 9 || input_values[1] != 10)
    {
        LogMessage("PERSISTENT_FAILED argument mapping");
        return -100;
    }
    integer = 10; real = 3.5;
    integers[0] = 40; integers[1] = 50;
    reals[0] = 4.0; reals[1] = 5.0;
    if (PersistentControl(1) != 1) return -101;
    if (mode == 1) return PersistentControl(3) + 1;
    if (mode == 2 && PersistentControl(2) != 1) return -102;
    if (mode == 3) return 42 / PersistentControl(4);
    if (mode == 4 && PersistentControl(5) != 1) return -103;
    return 43;
}
