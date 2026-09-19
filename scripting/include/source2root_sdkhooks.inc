#if defined _source2root_sdkhooks_included
    #endinput
#endif
#define _source2root_sdkhooks_included
#include <source2root>

enum SDKHook { NoSDKHook = 0 }
enum SDKHookFrame { NoSDKHookFrame = 0 }
enum SDKHookPhase { SDKHook_Pre = 1, SDKHook_Post = 2, SDKHook_Both = 3 }
enum SDKHookAction { SDKHook_Continue = 0, SDKHook_Changed = 1, SDKHook_Block = 2 }
enum SDKHookKind
{
    SDKHook_Damage = 1, SDKHook_Touch, SDKHook_Spawn,
    SDKHook_WeaponCanUse, SDKHook_WeaponSelect, SDKHook_WeaponDrop
}
#define SDKHOOK_ORIGINAL_CALLED 1
#define SDKHOOK_RECALLED 2
typedef SDKHookCallback = function SDKHookAction(SDKHookFrame frame, SDKHookPhase phase, int data);

// Typed Source2Root APIs; not SourceMod source/binary compatibility.
// entityReference is a full Source2 entity handle (for example from
// Entity_SourceHandle), not an entity index or a SourcePawn resource handle.
// Registration owns its identity and map epoch; deletion/reuse cannot retarget it.
// With the optional construction service, a tracked pending creation can be
// observed before DispatchSpawn. This lease never owns or cancels the entity.
// Closing its pending owner invalidates the hook, even if an active spawn later
// succeeds. A surviving observer becomes an ordinary live lease after spawn.
// targets.json lives under configs/extensions/source2root.sdkhooks. It uses the
// DHooks resolver catalog format plus sdkhook:{kind,class,block}. Each target
// requires a plugin allowlist, method:true and block:"preserve_result" explicitly
// attesting that skipping this reviewed function preserves its output storage.
// Kinds/prototypes: damage:void(pointer,pointer,pointer), touch and spawn:
// void(pointer,pointer), weapon_can_use:bool(pointer,pointer), weapon_select:
// void(pointer,pointer), weapon_drop:void(pointer,pointer,pointer,pointer).
// class is the exact dynamic entity schema class; weapon hooks require
// CCSPlayerPawn and match its current weapon-services component. Spawn means
// DispatchSpawn(this,keyvalues) for an already live entity; registering entities
// still in construction or subscribing to future entity creation is unsupported.
// No SDKCall buffers/entities/allow_calls adapters in these targets. Resolver
// profiles/signatures and pointee contracts must match the running game binary.
native SDKHook SDKHook_Add(int entityReference, const char[] target, SDKHookPhase phases,
    SDKHookCallback callback, int priority = 0, int data = 0);

native bool SDKHook_Close(SDKHook hook);
native bool SDKHook_Enable(SDKHook hook, bool enabled);
native bool SDKHook_IsActive(SDKHook hook);

// Frames are valid only during their own script callback. No native address is
// exposed. Entity/Other return full Source2 references, with -1 for no other
// entity. Touch/weapon post hooks use pre-captured identities; a deleted entity
// or unmatched pre/post context suppresses that script delivery. Raw references
// do not extend lifetime or retain map epochs when stored by a script.
native SDKHookKind SDKHook_Kind(SDKHookFrame frame);
native int SDKHook_Flags(SDKHookFrame frame);
native int SDKHook_Entity(SDKHookFrame frame);
native int SDKHook_Other(SDKHookFrame frame);
native bool SDKHook_GetDamage(SDKHookFrame frame, float& damage, int& damageType,
    int& damageCustom, int& inflictorReference, int& attackerReference,
    int& abilityReference, float force[3], float position[3]);
// Only pre-damage callbacks can stage finite damage >=0, type bits and finite
// vectors. Read-only references/custom and all owning native members remain
// intact. Damage result storage is never overwritten when blocking a call.
native bool SDKHook_SetDamage(SDKHookFrame frame, float damage, int damageType,
    const float force[3], const float position[3]);
// Boolean result is available only for weapon_can_use. Changed overrides it
// while allowing the original; Block always returns false without the original.
native bool SDKHook_GetResult(SDKHookFrame frame, bool& value);
native bool SDKHook_SetResult(SDKHookFrame frame, bool value);

// Continue discards staged edits; Changed commits valid pre-damage edits or a
// weapon boolean result. Block skips the original without committing edits.
// Post supports Continue and weapon boolean Changed only. Invalid actions or
// script faults discard edits and retire the hook. Script game side effects
// remain; deleting a required entity in pre prevents unsafe original dispatch,
// including on fault. Other hooks may still observe a superseded engine call.
// Script pause/loading, workers and recursion beyond8 skip script execution.
// Close/unload/reload cancel future callbacks; an already executing valid
// callback can finish. Native removal failures retain storage and block unload
// until cleanup succeeds. Getters clear outputs on reported failures.
