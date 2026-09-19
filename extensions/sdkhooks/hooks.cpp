#include "hooks.h"
#include <keels2/detail/authoring_status.hpp>
#include <algorithm>
#include <cmath>
#include <utility>

namespace source2root::sdkhooks {
namespace {
void Check(KeelResult result, const char* operation) {
    if (result != KEEL_RESULT_OK) throw Error(std::string(operation)+": "+keels2::detail::ResultDescription(result)+".");
}
bool Consistent(const KeelEntityInfo& value) {
    return value.size == sizeof(value) && !value.reserved && value.index >= 0 && value.epoch && value.source2_handle != UINT32_MAX;
}
bool Weapon(Kind kind) { return kind >= Kind::weapon_can_use; }
bool Secondary(Kind kind) { return kind == Kind::touch || Weapon(kind); }
void Numbers(const KeelDamageEdit& edit) {
    if (edit.size != sizeof(edit) || edit.reserved || !std::isfinite(edit.damage) || edit.damage < 0)
        throw Error("Damage edit requires finite nonnegative damage and a valid record.");
    for (unsigned i = 0; i < 3; ++i)
        if (!std::isfinite(edit.force[i]) || !std::isfinite(edit.position[i])) throw Error("Damage vectors must be finite.");
}
}
struct Lease {
    std::shared_ptr<Service> service;
    KeelEntityHandle handle = 0;
    KeelEntityInfo identity{};
    bool observer = false;
    ~Lease() {
        if (handle) { service->entities_.release(service->owner_,handle); --service->leases_; }
    }
};
struct Pending {
    const void* key = nullptr;
    const void* instance = nullptr;
    const void* secondary = nullptr;
    std::shared_ptr<Lease> other;
};
struct State {
    std::shared_ptr<Service> service;
    std::shared_ptr<Lease> entity;
    dhooks::Definition definition;
    Kind kind{};
    unsigned phases = 0;
    Callback callback;
    std::vector<Pending> pending;
};
const KeelDamageInfo& Frame::Damage() const {
    if (!damage_valid_) throw Error("This hook frame has no damage snapshot.");
    return damage_;
}
void Frame::Damage(const KeelDamageEdit& edit) {
    if (!damage_valid_ || phase_ != KH_PHASE_PRE) throw Error("Damage edits require a pre-damage callback.");
    Numbers(edit);
    edit_ = edit; edited_ = true;
    damage_.damage = edit.damage; damage_.damage_type = edit.damage_type;
    std::copy_n(edit.force,3,damage_.force); std::copy_n(edit.position,3,damage_.position);
}
bool Frame::Result() const {
    if (kind_ != Kind::weapon_can_use) throw Error("Only weapon-can-use hooks have a boolean result.");
    return result_;
}
void Frame::Result(bool result) {
    if (kind_ != Kind::weapon_can_use) throw Error("Only weapon-can-use hooks have a boolean result.");
    result_ = result;
}
Hook::~Hook() = default;
void Hook::Close() { native_->Close(); state_->pending.clear(); }
void Hook::Enable(bool enabled) { native_->Enable(enabled); state_->pending.clear(); }
bool Hook::Active() const { return native_->Active(); }
Service::Service(KeelPluginHandle owner, const KeelHookApi& hooks, const KeelNativeRuntimeApi& runtime,
    const KeelEntitiesApi& entities, const KeelEntityAccessApi& access,
    const KeelEntityCaptureApi& capture, const KeelEntityHookDataApi& data, const KeelEntityConstructionApi* construction)
    : owner_(owner), runtime_(runtime), entities_(entities), access_(access), capture_(capture), data_(data), construction_(construction ? *construction : KeelEntityConstructionApi{}),
      transport_(std::make_shared<dhooks::Service>(owner,hooks,runtime)) {
    if (entities.size != sizeof(entities) || entities.api_version != KEELS2_ENTITIES_API_VERSION ||
        !entities.find_by_source2_handle || !entities.describe || !entities.release ||
        access.size != sizeof(access) || access.api_version != KEELS2_ENTITY_ACCESS_API_VERSION || !access.visit ||
        capture.size != sizeof(capture) || capture.api_version != KEELS2_ENTITY_CAPTURE_API_VERSION || !capture.capture ||
        data.size != sizeof(data) || data.api_version != KEELS2_ENTITY_HOOK_DATA_API_VERSION ||
        !data.read_damage || !data.write_damage || !data.weapon_matches) throw Error("Incompatible SDKHooks host services.");
    if (construction && (construction->size != sizeof(*construction) ||
        construction->api_version != KEELS2_ENTITY_CONSTRUCTION_API_VERSION || !construction->describe ||
        !construction->observe || !construction->visit)) throw Error("Incompatible pending entity observation service.");
    states_.reserve(256);
}
void Service::Thread() const { Check(runtime_.check_game_thread(owner_),"SDKHooks operation"); }
Kind Service::Validate(const dhooks::Definition& value) {
    dhooks::Validate(value);
    const auto& policy = value.entity_hook;
    Kind kind{}; unsigned count = 0, result = KH_VALUE_VOID;
    if (policy.kind == "damage") { kind = Kind::damage; count = 3; }
    else if (policy.kind == "touch") { kind = Kind::touch; count = 2; }
    else if (policy.kind == "spawn") { kind = Kind::spawn; count = 2; }
    else if (policy.kind == "weapon_can_use") { kind = Kind::weapon_can_use; count = 2; result = KH_VALUE_BOOL; }
    else if (policy.kind == "weapon_select") { kind = Kind::weapon_select; count = 2; }
    else if (policy.kind == "weapon_drop") { kind = Kind::weapon_drop; count = 4; }
    else throw Error("Target has no supported SDKHooks kind.");
    if (!value.method || value.allow_calls || !value.buffers.empty() || !value.entities.empty() || value.result != result ||
        value.arguments != std::vector<KeelHookValueType>(count,KH_VALUE_POINTER)) throw Error("SDKHooks target prototype or adapters do not match its kind.");
    if (policy.class_name.empty() || policy.class_name.size() > 255 ||
        std::any_of(policy.class_name.begin(),policy.class_name.end(),[](unsigned char c) {
            return !((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == ':');
        })) throw Error("SDKHooks requires an exact entity schema class.");
    if (Weapon(kind) && policy.class_name != "CCSPlayerPawn") throw Error("Weapon hooks require the current CCSPlayerPawn component owner.");
    // The catalog explicitly attests to the target's no-write early-return
    // contract. In particular a damage result can contain owning native data.
    if (policy.block != "preserve_result") throw Error("SDKHooks requires a reviewed preserve_result blocking contract.");
    return kind;
}
std::shared_ptr<Lease> Service::Acquire(std::uint32_t source) {
    Thread();
    if (source == UINT32_MAX || leases_ >= 512) throw Error("Invalid entity reference or SDKHooks entity limit (512).");
    auto lease = std::make_shared<Lease>(); lease->service = shared_from_this();
    KeelEntityHandle handle{};
    auto result = entities_.find_by_source2_handle(owner_,source,&handle);
    if (result == KEEL_RESULT_NOT_FOUND && construction_.observe) {
        result = construction_.observe(owner_,source,&handle); lease->observer = result == KEEL_RESULT_OK;
    }
    Check(result,"Find hook entity");
    if (!handle) throw Error("Host returned an empty entity handle.");
    lease->handle = handle; ++leases_; lease->identity.size = sizeof(lease->identity);
    Check(Describe(*lease,lease->identity),"Describe hook entity");
    if (!Consistent(lease->identity) || lease->identity.source2_handle != source) throw Error("Host returned an invalid entity identity.");
    return lease;
}
std::shared_ptr<Lease> Service::Capture(const void* pointer) {
    if (!pointer) return {};
    if (leases_ >= 512) throw Error("SDKHooks entity limit (512) reached.");
    auto lease = std::make_shared<Lease>(); lease->service = shared_from_this();
    KeelEntityHandle handle{};
    Check(capture_.capture(owner_,pointer,&handle),"Capture hook argument entity");
    if (!handle) throw Error("Host returned an empty captured entity.");
    lease->handle = handle; ++leases_; lease->identity.size = sizeof(lease->identity);
    Check(entities_.describe(owner_,handle,&lease->identity),"Describe hook argument entity");
    if (!Consistent(lease->identity)) throw Error("Host returned an invalid captured entity.");
    return lease;
}
KeelResult Service::Describe(const Lease& lease, KeelEntityInfo& info) const {
    if (lease.observer) {
        const auto result = construction_.describe(owner_,lease.handle,&info);
        if (result != KEEL_RESULT_NOT_FOUND) return result;
    }
    return entities_.describe(owner_,lease.handle,&info);
}
KeelResult Service::Visit(const Lease& lease, const char* name, KeelEntityAccessCallback callback, void* data) const {
    if (lease.observer) {
        const auto result = construction_.visit(owner_,lease.handle,name,callback,data);
        if (result != KEEL_RESULT_NOT_FOUND) return result;
    }
    const KeelEntityAccessSpec spec{sizeof(spec),0,lease.handle,name};
    return access_.visit(owner_,&spec,1,callback,data);
}
bool Service::Valid(const Lease& lease) const {
    KeelEntityInfo value{}; value.size = sizeof(value);
    return Describe(lease,value) == KEEL_RESULT_OK && Consistent(value) &&
        value.source2_handle == lease.identity.source2_handle && value.epoch == lease.identity.epoch && value.index == lease.identity.index;
}
bool Service::Matches(const State& state, const void* pointer) const {
    if (!pointer || !Valid(*state.entity)) return false;
    if (Weapon(state.kind)) {
        KeelBool matches{};
        return data_.weapon_matches(owner_,state.entity->handle,pointer,&matches) == KEEL_RESULT_OK && matches == KEEL_TRUE;
    }
    struct Match { const void* expected; bool matched = false; } match{pointer};
    const auto visitor = [](void* raw, void* const* pointers, unsigned count) -> KeelResult {
        auto& context = *static_cast<Match*>(raw);
        context.matched = count == 1 && pointers && pointers[0] == context.expected;
        return KEEL_RESULT_OK;
    };
    return Visit(*state.entity,state.definition.entity_hook.class_name.c_str(),visitor,&match) == KEEL_RESULT_OK && match.matched;
}
std::unique_ptr<Hook> Service::Attach(const dhooks::Definition& definition, std::uint32_t entity,
    unsigned phases, std::int32_t priority, Callback callback, std::function<void()> retire) {
    Thread(); Collect();
    if (!phases || (phases & ~KH_PHASE_BOTH) || !callback || !retire) throw Error("Invalid SDKHooks registration.");
    auto state = std::make_shared<State>(); state->service = shared_from_this(); state->kind = Validate(definition);
    state->definition = definition; state->entity = Acquire(entity); state->phases = phases; state->callback = std::move(callback);
    state->pending.reserve(8);
    // Check the exact class before resolving/installing a native target.
    bool visited = false;
    Check(Visit(*state->entity,definition.entity_hook.class_name.c_str(),[](void* raw,void* const* pointers,unsigned count) -> KeelResult {
        if (count != 1 || !pointers || !pointers[0]) return KEEL_RESULT_INCOMPATIBLE;
        *static_cast<bool*>(raw) = true; return KEEL_RESULT_OK;
    },&visited),"Validate SDKHooks entity class");
    if (!visited) throw Error("Host did not validate the SDKHooks entity class.");
    auto target = transport_->Open(definition);
    auto hook = std::make_unique<Hook>(); hook->state_ = state;
    hook->native_ = transport_->Attach(*target,KH_PHASE_BOTH,priority,
        [state](dhooks::Frame& native) { return state->service->Dispatch(*state,native); },std::move(retire));
    states_.push_back(state);
    return hook;
}
int Service::Dispatch(State& state, dhooks::Frame& native) {
    // Transport has already validated phase/types/thread and retained storage.
    const auto* instance = native.Value(1).scalar.pointer;
    const auto* secondary = Secondary(state.kind) ? native.Value(2).scalar.pointer : nullptr;
    const bool pre = native.Phase() == KH_PHASE_PRE;
    struct Active { unsigned& value; ~Active() { --value; } } active{active_}; ++active_;
    Pending context{native.native_key_,instance,secondary,{}};
    auto found = std::find_if(state.pending.begin(),state.pending.end(),[&](const auto& value) { return value.key == context.key; });
    if (pre) {
        if (found != state.pending.end()) state.pending.erase(found);
        if (!Matches(state,instance)) return KH_ACTION_CONTINUE;
        if (state.pending.size() == 8 || (secondary && leases_ >= 512)) return KH_ACTION_CONTINUE;
        try { if (Secondary(state.kind)) context.other = Capture(secondary); }
        catch (...) { native.retire_ = true; if (state.kind == Kind::weapon_can_use) native.SetInteger(0,0); return KH_ACTION_SUPERSEDE; }
        state.pending.push_back(context);
    } else {
        if (found == state.pending.end()) return KH_ACTION_CONTINUE;
        context = std::move(*found); state.pending.erase(found);
        // A post call must match a pre capture. Never dereference post pointers
        // to recover an identity that the original function may have destroyed.
        if (context.instance != instance || context.secondary != secondary || !Matches(state,instance) ||
            (context.other && !Valid(*context.other))) return KH_ACTION_CONTINUE;
    }
    if (!(state.phases & native.Phase())) return KH_ACTION_CONTINUE;
    Frame frame; frame.kind_ = state.kind; frame.phase_ = native.Phase(); frame.flags_ = native.Flags();
    frame.entity_ = state.entity->identity.source2_handle;
    if (context.other) frame.other_ = context.other->identity.source2_handle;
    int action = -1;
    try {
        if (state.kind == Kind::damage) {
            frame.damage_.size = sizeof(frame.damage_);
            Check(data_.read_damage(owner_,native.Value(2).scalar.pointer,&frame.damage_),"Read hook damage");
            if (frame.damage_.size != sizeof(frame.damage_) || frame.damage_.reserved || !std::isfinite(frame.damage_.damage))
                throw Error("Host returned an invalid damage snapshot.");
            for (unsigned i = 0; i < 3; ++i)
                if (!std::isfinite(frame.damage_.force[i]) || !std::isfinite(frame.damage_.position[i])) throw Error("Invalid damage vector.");
            frame.damage_valid_ = true;
        }
        if (state.kind == Kind::weapon_can_use) frame.result_ = native.Integer(0) != 0;
        // A metadata lookup can reenter the host; avoid delivering stale data.
        if (Matches(state,instance) && (!context.other || Valid(*context.other))) action = state.callback(frame);
    } catch (...) { action = -2; }
    if (action < -2 || action > static_cast<int>(Action::block) ||
        (!pre && action == static_cast<int>(Action::block)) ||
        (!pre && action == static_cast<int>(Action::changed) && state.kind != Kind::weapon_can_use)) action = -2;
    if (action == -2) native.retire_ = true;
    // Revalidate even after callback faults or closure. If the callback removed
    // a required argument, continuing into the original would use freed memory.
    if (pre && (!Matches(state,instance) || (context.other && !Valid(*context.other)))) {
        if (state.kind == Kind::weapon_can_use) native.SetInteger(0,0);
        return KH_ACTION_SUPERSEDE;
    }
    if (action <= 0) return KH_ACTION_CONTINUE;
    if (action == static_cast<int>(Action::block)) {
        if (state.kind == Kind::weapon_can_use) native.SetInteger(0,0);
        return KH_ACTION_SUPERSEDE;
    }
    if (state.kind == Kind::damage && frame.edited_) {
        // Metadata is re-resolved by the host; failures preserve the record.
        const auto status = data_.write_damage(owner_,native.Value(2).scalar.pointer,&frame.edit_);
        if (status != KEEL_RESULT_OK) {
            native.retire_ = true;
            return Matches(state,instance) ? KH_ACTION_CONTINUE : KH_ACTION_SUPERSEDE;
        }
    }
    if (state.kind == Kind::weapon_can_use) {
        native.SetInteger(0,frame.result_ ? 1 : 0); return KH_ACTION_OVERRIDE;
    }
    return KH_ACTION_CONTINUE;
}
void Service::Collect(bool end_frame) {
    Thread(); transport_->Collect();
    for (auto it = states_.begin(); it != states_.end();) {
        if (auto state = it->lock()) {
            if (end_frame && !active_) state->pending.clear();
            ++it;
        } else it = states_.erase(it);
    }
}
bool Service::Empty() const { return transport_->Empty() && !leases_; }
}
