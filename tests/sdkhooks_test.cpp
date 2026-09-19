#include "../extensions/sdkhooks/hooks.h"
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <thread>

using namespace source2root::sdkhooks;
namespace dh = source2root::dhooks;
namespace {
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> void Reject(F function, const char* message) {
    bool failed = false; try { function(); } catch (const Error&) { failed = true; } Check(failed,message);
}
struct Host {
    static Host* active;
    std::thread::id thread = std::this_thread::get_id();
    std::map<KeelHookCallbackHandle,KeelHookCallbackSpec> callbacks;
    std::map<KeelEntityHandle,KeelEntityInfo> leases;
    KeelEntityHandle next_entity = 1;
    KeelHookCallbackHandle next_hook = 1;
    std::uint64_t epoch = 1;
    int objects[2]{11,22}, component = 33;
    bool alive[2]{true,true}, canonical_component = true, fail_remove = false;
    unsigned captures = 0, writes = 0, observed = 0, pending_visits = 0;
    bool pending = false, owner_closed = false;
    std::set<KeelEntityHandle> observers;
    KeelDamageInfo damage{sizeof(damage),0,42.5f,0x80000040,-7,0x1008,UINT32_MAX,UINT32_MAX,{1,2,3},{4,5,6}};
    std::function<void()> write_lookup;
    Host() { active = this; }
    static KeelResult Thread(KeelPluginHandle owner) {
        return owner == 17 && std::this_thread::get_id() == active->thread ? KEEL_RESULT_OK : KEEL_RESULT_WRONG_THREAD;
    }
    static KeelResult Resolve(KeelPluginHandle, const KeelHookTargetSpec*, const KeelHookPrototype*, KeelHookTargetHandle* out) { *out = 9; return KEEL_RESULT_OK; }
    static KeelResult Release(KeelPluginHandle, KeelHookTargetHandle) { return active->callbacks.empty() ? KEEL_RESULT_OK : KEEL_RESULT_BUSY; }
    static KeelResult Add(KeelPluginHandle, KeelHookTargetHandle, const KeelHookCallbackSpec* spec, KeelHookCallbackHandle* out) {
        Check(spec->phases == KH_PHASE_BOTH,"internal pre capture and post cleanup always installed");
        *out = active->next_hook++; active->callbacks[*out] = *spec; return KEEL_RESULT_OK;
    }
    static KeelResult Remove(KeelPluginHandle, KeelHookCallbackHandle id) {
        if (active->fail_remove) return KEEL_RESULT_BUSY;
        return active->callbacks.erase(id) ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND;
    }
    static KeelResult Enable(KeelPluginHandle, KeelHookCallbackHandle, KeelBool) { return KEEL_RESULT_OK; }
    static KeelResult Find(KeelPluginHandle, std::uint32_t source, KeelEntityHandle* out) {
        *out = 0;
        if (source < 0x1007 || source > 0x1008 || !active->alive[source-0x1007] || (source == 0x1007 && active->pending)) return KEEL_RESULT_NOT_FOUND;
        *out = active->next_entity++; active->leases[*out] = {sizeof(KeelEntityInfo),static_cast<int>(source&0xfff),source,0,active->epoch};
        return KEEL_RESULT_OK;
    }
    static KeelResult Close(KeelPluginHandle, KeelEntityHandle handle) {
        Check(active->leases.erase(handle) == 1,"lease released exactly once"); active->observers.erase(handle); return KEEL_RESULT_OK;
    }
    static KeelResult Describe(KeelPluginHandle, KeelEntityHandle handle, KeelEntityInfo* out) {
        const auto found = active->leases.find(handle);
        if (found == active->leases.end() || found->second.epoch != active->epoch || !active->alive[found->second.source2_handle-0x1007] ||
            (found->second.source2_handle == 0x1007 && active->pending) ||
            (active->observers.contains(handle) && active->owner_closed)) return KEEL_RESULT_NOT_FOUND;
        *out = found->second; return KEEL_RESULT_OK;
    }
    static KeelResult Visit(KeelPluginHandle owner, const KeelEntityAccessSpec* specs, unsigned count, KeelEntityAccessCallback callback, void* data) {
        Check(count == 1,"one owned entity match"); KeelEntityInfo info{};
        const auto status = Describe(owner,specs[0].entity,&info); if (status != KEEL_RESULT_OK) return status;
        if (std::strcmp(specs[0].class_name,"CCSPlayerPawn")) return KEEL_RESULT_INCOMPATIBLE;
        void* pointers[]{&active->objects[info.source2_handle-0x1007]}; return callback(data,pointers,1);
    }
    static KeelResult Observe(KeelPluginHandle, std::uint32_t source, KeelEntityHandle* out) {
        *out = 0;
        if (source != 0x1007 || !active->pending || active->owner_closed || !active->alive[0]) return KEEL_RESULT_NOT_FOUND;
        *out = active->next_entity++;
        active->leases[*out] = {sizeof(KeelEntityInfo),7,source,0,active->epoch};
        active->observers.insert(*out); ++active->observed; return KEEL_RESULT_OK;
    }
    static KeelResult DescribePending(KeelPluginHandle, KeelEntityHandle handle, KeelEntityInfo* out) {
        const auto found = active->leases.find(handle);
        if (found == active->leases.end() || !active->observers.contains(handle) || !active->pending ||
            active->owner_closed || !active->alive[0] || found->second.epoch != active->epoch) return KEEL_RESULT_NOT_FOUND;
        *out = found->second; return KEEL_RESULT_OK;
    }
    static KeelResult VisitPending(KeelPluginHandle owner, KeelEntityHandle handle, const char* name,
        KeelEntityAccessCallback callback, void* data) {
        KeelEntityInfo info{}; const auto result = DescribePending(owner,handle,&info);
        if (result != KEEL_RESULT_OK) return result;
        if (std::strcmp(name,"CCSPlayerPawn")) return KEEL_RESULT_INCOMPATIBLE;
        ++active->pending_visits;
        void* pointers[]{&active->objects[0]}; return callback(data,pointers,1);
    }
    static KeelResult Capture(KeelPluginHandle owner, const void* pointer, KeelEntityHandle* out) {
        ++active->captures;
        for (unsigned i = 0; i < 2; ++i) if (pointer == &active->objects[i]) return Find(owner,0x1007+i,out);
        *out = 0; return KEEL_RESULT_NOT_FOUND;
    }
    static KeelResult Read(KeelPluginHandle, const void* pointer, KeelDamageInfo* out) {
        Check(pointer == &active->damage,"damage native pointer stays internal"); *out = active->damage; return KEEL_RESULT_OK;
    }
    static KeelResult Write(KeelPluginHandle, void* pointer, const KeelDamageEdit* edit) {
        Check(pointer == &active->damage,"damage write target");
        if (active->write_lookup) { active->write_lookup(); return KEEL_RESULT_NOT_FOUND; }
        active->damage.damage = edit->damage; active->damage.damage_type = edit->damage_type;
        std::copy_n(edit->force,3,active->damage.force); std::copy_n(edit->position,3,active->damage.position); ++active->writes;
        return KEEL_RESULT_OK;
    }
    static KeelResult Weapon(KeelPluginHandle owner, KeelEntityHandle handle, const void* pointer, KeelBool* out) {
        *out = KEEL_FALSE; KeelEntityInfo info{}; const auto status = Describe(owner,handle,&info);
        if (status != KEEL_RESULT_OK) return status;
        *out = active->canonical_component && pointer == &active->component ? KEEL_TRUE : KEEL_FALSE; return KEEL_RESULT_OK;
    }
    KeelHookApi hooks{sizeof(hooks),KEELHOOK_API_VERSION,Resolve,Release,Add,Remove,nullptr,nullptr,nullptr,Enable};
    KeelNativeRuntimeApi runtime{sizeof(runtime),KEELS2_NATIVE_RUNTIME_API_VERSION,Thread,nullptr,nullptr,nullptr};
    KeelEntitiesApi entities{sizeof(entities),KEELS2_ENTITIES_API_VERSION,nullptr,Find,Close,Describe,nullptr,nullptr};
    KeelEntityAccessApi access{sizeof(access),KEELS2_ENTITY_ACCESS_API_VERSION,Visit};
    KeelEntityCaptureApi capture{sizeof(capture),KEELS2_ENTITY_CAPTURE_API_VERSION,Capture};
    KeelEntityHookDataApi data{sizeof(data),KEELS2_ENTITY_HOOK_DATA_API_VERSION,Read,Write,Weapon};
    KeelEntityConstructionApi construction{sizeof(construction),1,nullptr,nullptr,DescribePending,nullptr,nullptr,nullptr,Observe,VisitPending};
    std::shared_ptr<Service> Make(bool observe = false) { return std::make_shared<Service>(17,hooks,runtime,entities,access,capture,data,observe ? &construction : nullptr); }
    unsigned Fire(KeelHookFrame& frame, KeelHookCallbackHandle id) { const auto callback = callbacks.at(id); return callback.callback(&frame,callback.user_data); }
};
Host* Host::active = nullptr;
dh::Definition Definition(const char* kind = "damage") {
    dh::Definition value; value.source = KH_TARGET_SYMBOL; value.module = "fixture"; value.symbol = "Callback";
    value.method = true; value.entity_hook = {kind,"CCSPlayerPawn","preserve_result"};
    unsigned count = !std::strcmp(kind,"damage") ? 3 : !std::strcmp(kind,"weapon_drop") ? 4 : 2;
    value.result = !std::strcmp(kind,"weapon_can_use") ? KH_VALUE_BOOL : KH_VALUE_VOID;
    value.arguments.assign(count,KH_VALUE_POINTER); return value;
}
struct Native {
    std::array<KeelHookValue,4> arguments{};
    std::array<unsigned char,64> result_storage{};
    KeelHookFrame frame{sizeof(frame),KH_PHASE_PRE,9,3,0,arguments.data(),{}};
    Native(Host& host, const char* kind = "damage") {
        const auto definition = Definition(kind); frame.argument_count = static_cast<unsigned>(definition.arguments.size());
        frame.result.type = definition.result; result_storage.fill(0x5a);
        for (auto& arg : arguments) arg.type = KH_VALUE_POINTER;
        const bool weapon = std::string_view(kind).starts_with("weapon");
        arguments[0].scalar.pointer = weapon ? &host.component : &host.objects[0];
        arguments[1].scalar.pointer = !std::strcmp(kind,"damage") ? static_cast<void*>(&host.damage) : &host.objects[1];
        arguments[2].scalar.pointer = result_storage.data();
    }
    void Post() { frame.phase = KH_PHASE_POST; frame.flags = KH_FRAME_ORIGINAL_CALLED; }
};
void Edit(Frame& frame, float damage = 12) {
    auto old = frame.Damage(); KeelDamageEdit value{sizeof(value),0,damage,old.damage_type,{7,8,9},{10,11,12}}; frame.Damage(value);
}
void Damage() {
    Host host; auto service = host.Make(); unsigned pre = 0, post = 0, retired = 0; int action = 1;
    auto hook = service->Attach(Definition(),0x1007,KH_PHASE_BOTH,0,[&](Frame& frame) {
        Check(frame.Entity() == 0x1007 && frame.Other() == UINT32_MAX && frame.Type() == Kind::damage,"typed frame identity");
        if (frame.Phase() == KH_PHASE_PRE) { ++pre; Edit(frame); return action; }
        ++post; Check(frame.Damage().damage == 12 && frame.Flags() == KH_FRAME_ORIGINAL_CALLED,"post reads actual damage");
        Reject([&] { Edit(frame); },"post mutation rejected"); return 0;
    },[&] { ++retired; });
    Native native(host); const auto result_storage = native.result_storage;
    Check(host.Fire(native.frame,1) == KH_ACTION_CONTINUE && host.damage.damage == 12 && host.writes == 1,"changed pre commits bounded damage edit");
    Check(host.damage.damage_custom == -7 && host.damage.inflictor == 0x1008 && result_storage == native.result_storage,"references/custom and damage result preserved");
    native.Post(); host.Fire(native.frame,1); Check(pre == 1 && post == 1,"both phases deliver");
    native.frame.phase = KH_PHASE_PRE; host.damage.damage = 42; action = 0;
    host.Fire(native.frame,1); Check(host.damage.damage == 42 && host.writes == 1,"continue discards staged edits");
    action = 2; Check(host.Fire(native.frame,1) == KH_ACTION_SUPERSEDE && host.damage.damage == 42 && native.result_storage == result_storage,"block preserves native records");
    std::thread worker([&] { Check(host.Fire(native.frame,1) == KH_ACTION_CONTINUE,"worker skips scripts"); }); worker.join(); Check(pre == 3,"worker did not invoke");
    hook->Enable(false); host.Fire(native.frame,1); Check(pre == 3,"disabled hook skips script"); hook->Enable(true);
    action = -2; Check(host.Fire(native.frame,1) == KH_ACTION_CONTINUE && !hook->Active() && retired == 1,"fault retires and discards edits");
    host.fail_remove = true; hook.reset(); service->Collect(); Check(!service->Empty(),"failed retirement retains callback storage");
    host.fail_remove = false; service->Collect(); Check(service->Empty() && host.leases.empty(),"retry removes native and entity leases");
}
void Lifetime() {
    for (int mode = 0; mode < 4; ++mode) {
        Host host; auto service = host.Make(); std::unique_ptr<Hook> hook;
        hook = service->Attach(Definition(),0x1007,KH_PHASE_PRE,0,[&](Frame& frame) {
            Edit(frame); if (mode == 0) host.alive[0] = false;
            if (mode == 1) { ++host.epoch; return -2; }
            if (mode == 2) hook.reset();
            if (mode == 3) host.write_lookup = [&] { ++host.epoch; };
            return 1;
        },[] {});
        Native native(host); auto action = host.Fire(native.frame,1);
        Check(action == (mode == 2 ? KH_ACTION_CONTINUE : KH_ACTION_SUPERSEDE),"deletion/map change prevents original even on fault");
        Check(host.writes == (mode == 2 ? 1u : 0u),"closed hook state survives current valid callback; stale edits discarded");
        hook.reset(); service->Collect(); Check(service->Empty(),"lifetime cleanup");
    }
}
void Touch() {
    Host host; auto service = host.Make(); unsigned delivered = 0;
    auto hook = service->Attach(Definition("touch"),0x1007,KH_PHASE_POST,0,[&](Frame& frame) {
        Check(frame.Other() == 0x1008 && frame.Type() == Kind::touch,"typed other entity reference"); ++delivered; return 0;
    },[] {});
    Native native(host,"touch"); host.Fire(native.frame,1); Check(delivered == 0 && host.captures == 1,"post-only registration still captures pre identity");
    host.alive[1] = false; native.Post(); host.Fire(native.frame,1);
    Check(delivered == 0 && host.captures == 1,"deleted post entity is never dereferenced for capture");
    host.alive[1] = true; native.frame.phase = KH_PHASE_PRE; host.Fire(native.frame,1); service->Collect(); native.Post(); host.Fire(native.frame,1);
    Check(delivered == 1 && host.captures == 2,"live post uses earlier captured identity");
    host.Fire(native.frame,1); Check(delivered == 1,"unpaired post ignored");
    hook.reset(); service->Collect(); Check(service->Empty(),"secondary handles released");
}
void Weapon() {
    Host host; auto service = host.Make(); int action = 1; bool replace = false;
    auto hook = service->Attach(Definition("weapon_can_use"),0x1007,KH_PHASE_BOTH,0,[&](Frame& frame) {
        Check(frame.Entity() == 0x1007 && frame.Other() == 0x1008,"weapon component maps to owned pawn");
        frame.Result(true); if (replace) host.canonical_component = false; return action;
    },[] {});
    Native native(host,"weapon_can_use"); Check(host.Fire(native.frame,1) == KH_ACTION_OVERRIDE && native.frame.result.scalar.boolean,"boolean pre override");
    native.Post(); Check(host.Fire(native.frame,1) == KH_ACTION_OVERRIDE && native.frame.result.scalar.boolean,"boolean post override");
    native.frame.phase = KH_PHASE_PRE; action = 2;
    Check(host.Fire(native.frame,1) == KH_ACTION_SUPERSEDE && !native.frame.result.scalar.boolean,"weapon block returns false");
    action = 1; replace = true;
    Check(host.Fire(native.frame,1) == KH_ACTION_SUPERSEDE && !native.frame.result.scalar.boolean,"replaced component prevents original");
    hook.reset(); service->Collect(); Check(service->Empty(),"weapon cleanup");
}
void Capacity() {
    Host host; auto service = host.Make(); unsigned delivered = 0;
    std::vector<std::unique_ptr<Hook>> hooks;
    std::vector<std::unique_ptr<Native>> invocations;
    for (unsigned i = 0; i < 256; ++i) {
        hooks.push_back(service->Attach(Definition("touch"),0x1007,KH_PHASE_PRE,0,[&](Frame&) { ++delivered; return 0; },[] {}));
        invocations.push_back(std::make_unique<Native>(host,"touch"));
    }
    for (unsigned i = 0; i < 256; ++i) host.Fire(invocations[i]->frame,i+1);
    Check(delivered == 256 && host.leases.size() == 512,"registration plus secondary capture limit");
    Native nested(host,"touch");
    Check(host.Fire(nested.frame,1) == KH_ACTION_CONTINUE && delivered == 256 && hooks[0]->Active(),"temporary capture capacity skips script without blocking or retiring");
    for (unsigned i = 0; i < 256; ++i) { invocations[i]->Post(); host.Fire(invocations[i]->frame,i+1); }
    Check(host.leases.size() == 256,"post cleanup frees temporary captures");
    host.Fire(nested.frame,1); Check(delivered == 257,"next invocation recovers after capture capacity frees");
    hooks.clear(); service->Collect(); Check(service->Empty() && host.leases.empty(),"capacity cleanup");
}
void Construction() {
    for (unsigned mode = 0; mode < 6; ++mode) {
        Host host; host.pending = true; auto service = host.Make(true); unsigned pre{}, post{};
        auto hook = service->Attach(Definition("spawn"),0x1007,KH_PHASE_BOTH,0,[&](Frame& frame) {
            Check(frame.Entity() == 0x1007 && frame.Other() == UINT32_MAX,"pending spawn reference");
            if (frame.Phase() == KH_PHASE_PRE) {
                ++pre;
                if (mode == 1) host.owner_closed = true;
                if (mode == 2) ++host.epoch;
                return mode == 3 || mode == 5 ? 2 : 0;
            }
            ++post;
            if (mode == 5) Check(frame.Flags() == 0,"blocked pending post reports no original call");
            return 0;
        },[] {});
        Check(host.observed == 1 && host.pending_visits == 1,"pending lease and exact class validation");
        Native native(host,"spawn");
        if (mode == 4) {
            hook.reset(); service->Collect();
            Check(host.pending && !host.owner_closed && host.leases.empty(),"observer close never cancels pending owner");
            continue;
        }
        Check(host.Fire(native.frame,1) == (mode ? KH_ACTION_SUPERSEDE : KH_ACTION_CONTINUE) && pre == 1,
            "pending pre fires and owner loss/map/block prevents original");
        if (mode == 5) {
            // The post detour can run before the creator cancels a blocked
            // spawn. Its observation is still pending and original_called=0.
            native.frame.phase = KH_PHASE_POST; native.frame.flags = 0;
            host.Fire(native.frame,1);
            Check(post == 1 && host.pending,"blocked pending post observes before cancellation");
            host.alive[0] = false; native.frame.phase = KH_PHASE_PRE;
            host.Fire(native.frame,1); Check(pre == 1,"removed pending entity cannot deliver again");
        } else {
            host.pending = false; if (mode == 3) host.alive[0] = false;
            const auto visits = host.pending_visits;
            native.Post(); host.Fire(native.frame,1);
            Check(post == (mode == 0 ? 1u : 0u) && host.pending_visits == visits,
                "live observer uses ordinary access; canceled/stale/deleted post never delivers");
        }
        hook.reset(); service->Collect(); Check(service->Empty() && host.leases.empty(),"pending hook cleanup");
    }
    Host host; host.pending = true; auto legacy = host.Make();
    Reject([&] { legacy->Attach(Definition("spawn"),0x1007,KH_PHASE_PRE,0,[](Frame&) { return 0; },[] {}); },
        "legacy host cannot discover pending entity");
    auto service = host.Make(true); auto wrong = Definition("spawn"); wrong.entity_hook.class_name = "CBaseEntity";
    Reject([&] { service->Attach(wrong,0x1007,KH_PHASE_PRE,0,[](Frame&) { return 0; },[] {}); },"pending class mismatch fails closed");
    Check(host.leases.empty() && host.callbacks.empty() && host.pending,"failed attach releases only observer");
}
void Policy() {
    Host host; auto service = host.Make(); const auto attempt = [&](const dh::Definition& value) { service->Attach(value,0x1007,KH_PHASE_PRE,0,[](Frame&) { return 0; },[] {}); };
    auto value = Definition(); value.entity_hook.block.clear(); Reject([&] { attempt(value); },"unreviewed block contract refused");
    value = Definition(); value.arguments.pop_back(); Reject([&] { attempt(value); },"wrong damage prototype refused");
    value = Definition(); value.allow_calls = true; Reject([&] { attempt(value); },"SDKCall policy cannot enter typed hook catalog");
    value = Definition(); value.entity_hook.class_name = "CBaseEntity"; Reject([&] { attempt(value); },"wrong exact class refused");
    value = Definition("weapon_can_use"); value.entity_hook.class_name = "CBaseEntity"; Reject([&] { attempt(value); },"weapon requires pawn owner");
    Check(host.callbacks.empty() && host.leases.empty(),"policy failure leaves no published native callback");
    service->Collect(); Check(service->Empty(),"policy cleanup");
}
}
int main() {
    try { Construction(); Policy(); Damage(); Lifetime(); Touch(); Weapon(); Capacity(); std::cout << "SDKHooks backend checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
