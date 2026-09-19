#include "hooks.h"
#include <keels2/detail/authoring_status.hpp>
#include <algorithm>
#include <utility>

namespace source2root::dhooks {
namespace {
void Check(KeelResult result, const char* operation) {
    if (result != KEEL_RESULT_OK) throw Error(std::string(operation) + ": " + keels2::detail::ResultDescription(result) + ".");
}
bool Released(KeelResult result) { return result == KEEL_RESULT_OK || result == KEEL_RESULT_NOT_FOUND; }
}
struct TargetData {
    KeelHookTargetHandle handle = 0;
    Definition definition;
};
struct Registration {
    Service* service = nullptr;
    std::shared_ptr<TargetData> target;
    KeelHookCallbackHandle handle = 0;
    Callback callback;
    std::function<void()> retire;
    unsigned active = 0;
    bool closing = false, enabled = true;
};
Target::Target(std::shared_ptr<Service> service, std::shared_ptr<TargetData> data, Definition definition)
    : service_(std::move(service)), data_(std::move(data)), definition_(std::move(definition)) {}
Hook::Hook(std::shared_ptr<Service> service, std::shared_ptr<Registration> registration)
    : service_(std::move(service)), registration_(std::move(registration)) {}
Hook::~Hook() { if (registration_) service_->Close(*registration_); }
void Hook::Close() { service_->Thread(); service_->Close(*registration_); }
bool Hook::Active() const { service_->Thread(); return !registration_->closing && registration_->handle; }
void Hook::Enable(bool enabled) {
    service_->Thread();
    if (registration_->closing || !registration_->handle) throw Error("Hook is closing.");
    Check(service_->hooks_.set_callback_enabled(service_->owner_,registration_->handle,enabled ? KEEL_TRUE : KEEL_FALSE),"Enable hook");
    registration_->enabled = enabled;
}
Service::Service(KeelPluginHandle owner, const KeelHookApi& hooks, const KeelNativeRuntimeApi& runtime, const KeelCallApi* calls)
    : owner_(owner), hooks_(hooks), runtime_(runtime) {
    if (!owner || hooks.size != sizeof(hooks) || hooks.api_version != KEELHOOK_API_VERSION ||
        !hooks.resolve_target || !hooks.release_target || !hooks.add_callback || !hooks.remove_callback || !hooks.set_callback_enabled ||
        runtime.size != sizeof(runtime) || runtime.api_version != KEELS2_NATIVE_RUNTIME_API_VERSION || !runtime.check_game_thread)
        throw Error("Incompatible hook or game-thread service.");
    if (calls) {
        if (calls->size != sizeof(*calls) || calls->api_version != KEELCALL_API_VERSION || !calls->invoke)
            throw Error("Incompatible direct-call host service.");
        calls_ = *calls;
    }
    targets_.reserve(64); registrations_.reserve(256);
}
void Service::Thread() const { Check(runtime_.check_game_thread(owner_),"Hook operation"); }
std::unique_ptr<Target> Service::Open(const Definition& definition) {
    Thread(); Collect(); Validate(definition);
    if (targets_.size() >= 64) throw Error("Hook target limit (64) reached.");
    auto data = std::make_shared<TargetData>(); data->definition = definition;
    auto result = std::unique_ptr<Target>(new Target(shared_from_this(),data,definition));
    const KeelHookTargetSpec spec{sizeof(spec),definition.source,KH_MECHANISM_DETOUR,
        definition.method ? KH_TARGET_METHOD : 0u,definition.module.empty() ? nullptr : definition.module.c_str(),
        definition.symbol.empty() ? nullptr : definition.symbol.c_str(),definition.pattern.empty() ? nullptr : definition.pattern.c_str(),
        definition.profile.empty() ? nullptr : definition.profile.c_str(),nullptr,definition.offset,definition.occurrence,0};
    const KeelHookPrototype prototype{sizeof(prototype),KH_CALL_NATIVE,definition.result,
        static_cast<unsigned>(definition.arguments.size()),definition.arguments.data(),nullptr,nullptr,
        static_cast<unsigned>(definition.arguments.size()),0,nullptr,nullptr};
    Check(hooks_.resolve_target(owner_,&spec,&prototype,&data->handle),"Resolve hook target");
    if (!data->handle) throw Error("Host returned an empty hook target.");
    // Host leases are a set of native providers, not a count of script opens.
    // Aliases resolving to the same target therefore share one release.
    const auto existing = std::find_if(targets_.begin(),targets_.end(),[&](const auto& value) { return value->handle == data->handle; });
    if (existing != targets_.end()) {
        if ((*existing)->definition.result != definition.result || (*existing)->definition.arguments != definition.arguments ||
            (*existing)->definition.method != definition.method) throw Error("Host reused a hook target with a different prototype.");
        result->data_ = *existing;
    }
    else targets_.push_back(std::move(data));
    keepalive_ = shared_from_this();
    return result;
}
std::unique_ptr<Call> Service::Prepare(const Target& target) {
    Thread();
    if (!calls_.invoke) throw Error("Direct-call host service is unavailable.");
    if (target.service_.get() != this || !target.definition_.allow_calls) throw Error("This configured target does not allow direct calls.");
    if (target.definition_.method && std::none_of(target.definition_.entities.begin(), target.definition_.entities.end(),
        [](const auto& entity) { return entity.argument == 1; })) throw Error("Method calls require a configured entity adapter for argument 1.");
    if (!target.definition_.entities.empty() && !access_.visit) throw Error("Checked entity calls are unavailable on this host.");
    return std::unique_ptr<Call>(new Call(shared_from_this(),target.data_,target.definition_));
}
void Service::Invoke(const TargetData& target, unsigned flags, const std::vector<KeelHookValue>& arguments,
    const std::vector<BufferSpec>& bounds, KeelHookValue& result) {
    Thread();
    if (!calls_.invoke) throw Error("Direct-call host service is unavailable.");
    active_buffers_.push_back({target.handle,&arguments,&bounds});
    struct Scope { std::vector<BufferScope>& values; ~Scope() { values.pop_back(); } } scope{active_buffers_};
    Check(calls_.invoke(owner_,target.handle,flags,arguments.data(),static_cast<unsigned>(arguments.size()),&result),"Invoke configured target");
    if (result.type != target.definition.result || result.reserved ||
        (result.type == KH_VALUE_BOOL && result.scalar.boolean > 1)) throw Error("Invalid direct-call result from host.");
}
void Service::CheckBufferEdits(const KeelHookFrame& before, const Frame& after) const {
    if (before.phase != KH_PHASE_PRE) return;
    for (const auto& active : active_buffers_) {
        if (active.target != before.target) continue;
        for (const auto& bound : *active.bounds) {
            const auto pointer = (*active.arguments)[bound.argument - 1].scalar.pointer;
            for (unsigned slot = 1; slot <= after.Count(); ++slot) {
                const auto& value = after.Value(slot);
                if (value.type == KH_VALUE_POINTER && value.scalar.pointer == pointer && slot != bound.argument)
                    throw Error("Hook cannot alias owned SDKCall buffer pointers into other arguments.");
            }
            // A native function can recursively call the target with its own
            // memory. Protect only frames that still reference this allocation.
            if (before.arguments[bound.argument - 1].scalar.pointer != pointer) continue;
            if (after.Value(bound.argument).scalar.pointer != pointer)
                throw Error("Hook cannot replace an owned SDKCall buffer pointer.");
            if (bound.length_argument) {
                const auto& value = after.Value(bound.length_argument);
                if ((value.type == KH_VALUE_INT32 && (value.scalar.int32 < 0 || static_cast<unsigned>(value.scalar.int32) > bound.capacity)) ||
                    (value.type == KH_VALUE_UINT32 && value.scalar.uint32 > bound.capacity))
                    throw Error("Hook length exceeds the owned SDKCall buffer allocation.");
            }
        }
    }
}
std::unique_ptr<Hook> Service::Attach(const Target& target, unsigned phases, std::int32_t priority,
    Callback callback, std::function<void()> retire) {
    Thread(); Collect();
    if (target.service_.get() != this || !phases || (phases & ~KH_PHASE_BOTH) || !callback || !retire)
        throw Error("Invalid hook registration.");
    if (registrations_.size() >= 256) throw Error("Hook callback limit (256) reached.");
    auto registration = std::make_shared<Registration>();
    registration->service = this; registration->target = target.data_;
    registration->callback = std::move(callback); registration->retire = std::move(retire);
    auto result = std::unique_ptr<Hook>(new Hook(shared_from_this(),registration));
    // Keep storage before publishing user_data, including a host that enters
    // synchronously during installation. Loading VM callbacks return BUSY.
    registrations_.push_back(registration);
    const KeelHookCallbackSpec spec{sizeof(spec),phases,priority,0,&Dispatch,registration.get()};
    try {
        Check(hooks_.add_callback(owner_,target.data_->handle,&spec,&registration->handle),"Attach hook");
        if (!registration->handle) throw Error("Host returned an empty hook callback.");
    } catch (...) {
        Close(*registration);
        Collect();
        throw;
    }
    return result;
}
void Service::Close(Registration& registration) noexcept {
    if (registration.closing) return;
    registration.closing = true; registration.enabled = false;
    try { if (registration.retire) registration.retire(); } catch (...) {}
    registration.retire = {};
}
void Service::Collect() {
    Thread();
    auto lifetime = shared_from_this();
    if (collecting_) return;
    struct Guard { bool& value; ~Guard() { value = false; } } guard{collecting_};
    collecting_ = true;
    for (auto it = registrations_.begin(); it != registrations_.end();) {
        auto& registration = **it;
        if (!registration.closing || registration.active) { ++it; continue; }
        if (registration.handle) {
            const auto result = hooks_.remove_callback(owner_,registration.handle);
            if (!Released(result)) { ++it; continue; }
            registration.handle = 0;
        }
        it = registrations_.erase(it);
    }
    for (auto it = targets_.begin(); it != targets_.end();) {
        if (it->use_count() != 1) { ++it; continue; }
        if (!Released(hooks_.release_target(owner_,(*it)->handle))) { ++it; continue; }
        it = targets_.erase(it);
    }
    if (Empty()) keepalive_.reset();
}
KeelHookAction Service::Dispatch(KeelHookFrame* frame, void* raw) noexcept {
    auto* registration = static_cast<Registration*>(raw);
    if (!registration || !frame) return KH_ACTION_CONTINUE;
    // Nothing mutable in the bridge is touched by an engine worker. Keel's
    // remove_callback waits for such dispatches before freeing user_data.
    auto* self = registration->service;
    try {
        if (self->runtime_.check_game_thread(self->owner_) != KEEL_RESULT_OK) return KH_ACTION_CONTINUE;
        auto service = self->shared_from_this();
        if (registration->closing || !registration->enabled || self->depth_ >= 8 || frame->target != registration->target->handle)
            return KH_ACTION_CONTINUE;
        struct Active {
            Registration& registration; unsigned& depth;
            ~Active() { --registration.active; --depth; }
        } active{*registration,self->depth_};
        ++registration->active; ++self->depth_;
        Frame snapshot(*frame,registration->target->definition);
        const auto action = registration->callback(snapshot);
        if (action == -2 || snapshot.retire_) self->Close(*registration);
        if (action < 0 || action > KH_ACTION_SUPERSEDE || (snapshot.Phase() == KH_PHASE_POST && action == KH_ACTION_SUPERSEDE))
            return KH_ACTION_CONTINUE;
        self->CheckBufferEdits(*frame,snapshot);
        snapshot.Commit(*frame,static_cast<unsigned>(action));
        return static_cast<KeelHookAction>(action);
    } catch (...) { return KH_ACTION_CONTINUE; }
}
}
