#if defined _source2root_topmenus_included
    #endinput
#endif
#define _source2root_topmenus_included
#include <source2root>

enum TopMenu { NoTopMenu = 0 }
enum TopMenuObject { NoTopMenuObject = 0 }
enum TopMenuDisplay { NoTopMenuDisplay = 0 }
enum TopMenuAction { TopMenu_CheckAccess = 0, TopMenu_Select = 1 }
enum TopMenuAccess { TopMenu_Hidden = 0, TopMenu_Enabled = 1, TopMenu_Disabled = 2 }
typedef TopMenuHandler = function TopMenuAccess(TopMenuObject entry, Player player, TopMenuAction action, int data);

// Named menus are shared across plugins; each returned handle remains private
// to its script. The first opener supplies the title. Closing a menu handle
// does not remove registrations or displays. Each must be closed separately;
// script unload cleans them all. There are at most32 menus and256 objects/menu.
native TopMenu TopMenu_Open(const char[] name, const char[] title);
native bool TopMenu_Close(TopMenu menu);
native bool TopMenu_SetTitle(TopMenu menu, const char[] title);
native TopMenuObject TopMenu_AddCategory(TopMenu menu, const char[] key, const char[] label,
    const char[] permission = "", int order = 0);
// An empty category places the item at the root. Otherwise its named category
// must be contributed by a running plugin to display the item. Empty categories
// are hidden. Keys are unique across a menu; staged reloads of the same plugin
// temporarily coexist, with the newest running registration selected.
native TopMenuObject TopMenu_AddItem(TopMenu menu, const char[] category, const char[] key,
    const char[] label, TopMenuHandler callback, const char[] permission = "", int order = 0, int data = 0);
native bool TopMenu_Remove(TopMenuObject entry);
native bool TopMenu_SetLabel(TopMenuObject entry, const char[] label);
native bool TopMenu_SetOrder(TopMenuObject entry, int order);
native TopMenuDisplay TopMenu_Show(TopMenu menu, Player player, int timeoutMilliseconds = 20000);
native bool TopMenu_Refresh(TopMenuDisplay display);
native bool TopMenu_CloseDisplay(TopMenuDisplay display);
native bool TopMenu_IsOpen(TopMenuDisplay display);

// The core permission and callback access result both gate each item. Access
// callbacks run on display/refresh and again before selection. They return
// Hidden/Enabled/Disabled; Select's return value is ignored. Player handles
// belong to the callback's script and identify the exact current connection.
// Access callbacks must not change the menu or recursively show it. A changed
// menu invalidates that display attempt; faults retire the registration.
// Selection closes the display before the callback, which may show another.
// Labels/titles/order changes refresh existing displays on the next frame.
// Permission, owner and connection checks reject stale selections. Per-player
// dynamic state changes become visible on explicit Refresh or navigation.
// A failed renderer close retains its native context until cleanup succeeds.
