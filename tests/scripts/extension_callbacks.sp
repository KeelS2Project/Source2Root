#include <source2root>

typedef Completion = function void(int value, const char[] text);
native bool CaptureCallback(Completion callback, int mode);

public bool OnPluginStart()
{
    return CaptureCallback(INVALID_FUNCTION, 1)
        && (!CaptureCallback(Completed, 4) ||
            (RegisterCommand("sr_queue", "", Queue)
            && RegisterCommand("sr_fault", "", Fault)
            && RegisterCommand("sr_quota", "", Quota)))
        && CaptureCallback(Completed, 0);
}

public
void Queue(Player caller, const char[] arguments)
{
    CaptureCallback(Completed, 2);
}

public
void Fault(Player caller, const char[] arguments)
{
    CaptureCallback(Failed, 2);
}

public
void Quota(Player caller, const char[] arguments)
{
    CaptureCallback(Completed, 3);
}

public void Completed(int value, const char[] text)
{
    if (value == 42 && text[0] == 'o' && text[1] == 'k')
        LogMessage("ASYNC_CALLBACK_OK");
}

public void Failed(int value, const char[] text)
{
    int result = 42 / value;

    if (result)
        LogMessage(text);
}
