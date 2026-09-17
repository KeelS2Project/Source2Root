#include <source2root>

native int ExtensionProbe(int operation, int handle, const char[] input, char[] output, int capacity,
    int[] values, int count, int& scalar);
native int OtherExtensionProbe(int operation, int handle, const char[] input, char[] output, int capacity,
    int[] values, int count, int& scalar);

int resource;

public bool OnPluginStart()
{
    char output[64];
    int values[] = {7, 11, 13};
    int scalar;
    resource = ExtensionProbe(0, 0, "native text", output, sizeof(output), values, sizeof(values), scalar);
    if (!resource || scalar != 42 || values[0] != 31 || values[1] != 0 || output[0] != 'r')
        return false;
    if (ExtensionProbe(1, resource, "", output, sizeof(output), values, sizeof(values), scalar) != 42
        || OtherExtensionProbe(1, resource, "", output, sizeof(output), values, sizeof(values), scalar) != -1
        || ExtensionProbe(3, resource, "", output, sizeof(output), values, sizeof(values), scalar) != 1)
        return false;
    int closed = ExtensionProbe(0, 0, "native text", output, sizeof(output), values, sizeof(values), scalar);
    if (!closed || ExtensionProbe(2, closed, "", output, sizeof(output), values, sizeof(values), scalar) != 1
        || ExtensionProbe(1, closed, "", output, sizeof(output), values, sizeof(values), scalar) != -1)
        return false;
    return true;
}

public void OnPluginStop()
{
    char output[64];
    int values[3];
    int scalar;
    if (ExtensionProbe(1, resource, "", output, sizeof(output), values, sizeof(values), scalar) == 42)
        LogMessage("resource valid during stop");
}
