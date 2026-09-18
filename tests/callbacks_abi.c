#include <source2root/callbacks.h>
#include <source2root/consumers.h>
#include <stddef.h>

_Static_assert(sizeof(SrCallbackValue) == 8, "callback value ABI");
_Static_assert(sizeof(SrCallbackArgument) == 24, "callback argument ABI");
_Static_assert(offsetof(SrCallbackArgument, value) == 16, "callback value offset");
_Static_assert(sizeof(SrCallbackApi) == 40, "callback API ABI");
_Static_assert(offsetof(SrCallbackApi, context) == 8, "callback context offset");
_Static_assert(offsetof(SrCallbackApi, retain) == 16, "callback retain offset");
_Static_assert(offsetof(SrCallbackApi, invoke) == 24, "callback invoke offset");
_Static_assert(offsetof(SrCallbackApi, cancel) == 32, "callback cancel offset");
_Static_assert(sizeof(SrConsumerApi) == 32, "consumer API ABI");
_Static_assert(offsetof(SrConsumerApi, player_handle) == 16, "consumer player offset");
_Static_assert(offsetof(SrConsumerApi, check_permission) == 24, "consumer permission offset");
