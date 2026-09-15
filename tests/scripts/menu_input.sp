#include <source2root>

public bool OnPluginStart()
{
    return RegisterCommand("sr_inputmenu", "test.menu", Open);
}

public void Open(Player caller, const char[] arguments)
{
    Menu menu = CreateMenu("Input test", "test.menu", Selected);
    AddMenuItem(menu, "Submenu", true, 10);
    AddMenuItem(menu, "Disabled", false, 11);
    AddMenuItem(menu, "Third", true, 12);
    AddMenuItem(menu, "Fourth", true, 13);
    AddMenuItem(menu, "Fifth", true, 14);
    ShowMenu(menu, caller, 10000);
}

public void Selected(Player caller, int value)
{
    if (value == 10)
    {
        Menu menu = CreateMenu("Submenu test", "test.menu", Selected);
        AddMenuItem(menu, "Confirm", true, 20);
        ShowMenu(menu, caller, 10000);
    }
    else
    {
        char text[32];
        Format(text, sizeof(text), "selected=%d", value);
        ReplyToCommand(caller, text);
    }
}
