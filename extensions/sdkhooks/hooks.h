#pragma once
#include "../dhooks/hooks.h"
#include <keels2/entity_hook_data.h>
#include <keels2/entity_construction.h>

namespace source2root::sdkhooks {
using Error = dhooks::Error;

enum class Kind : unsigned { damage = 1, touch, spawn, weapon_can_use, weapon_select, weapon_drop };

enum class Action : int { proceed = 0, changed = 1, block = 2 };

class Service;

struct Lease;

struct State;

class Frame final {
public:
    unsigned Phase() const {
        return phase_;
    }

    unsigned Flags() const {
        return flags_;
    }

    Kind Type() const {
        return kind_;
    }

    std::uint32_t Entity() const {
        return entity_;
    }

    std::uint32_t Other() const {
        return other_;
    }

    const KeelDamageInfo& Damage() const;
    void Damage(const KeelDamageEdit& edit);
    bool Result() const;
    void Result(bool result);

private:
    friend class Service;
    Kind kind_{};
    unsigned phase_ = 0, flags_ = 0;
    std::uint32_t entity_ = UINT32_MAX, other_ = UINT32_MAX;
    KeelDamageInfo damage_{};
    KeelDamageEdit edit_{};
    bool damage_valid_ = false, edited_ = false, result_ = false;
};
// Callback -1 discards staged values; -2 also retires the registration.
using Callback = std::function<int(Frame&)>;

class Hook final {
public:
    ~Hook();
    void Close();
    void Enable(bool enabled);
    bool Active() const;

private:
    friend class Service;
    std::shared_ptr<State> state_;
    std::unique_ptr<dhooks::Hook> native_;
};

class Service final : public std::enable_shared_from_this<Service> {
public:
    Service(KeelPluginHandle owner,
            const KeelHookApi& hooks,
            const KeelNativeRuntimeApi& runtime,
            const KeelEntitiesApi& entities,
            const KeelEntityAccessApi& access,
            const KeelEntityCaptureApi& capture,
            const KeelEntityHookDataApi& data,
            const KeelEntityConstructionApi* construction = nullptr);

    std::unique_ptr<Hook> Attach(const dhooks::Definition& definition, std::uint32_t entity,
        unsigned phases, std::int32_t priority, Callback callback, std::function<void()> retire);

    void Collect(bool end_frame = false);
    bool Empty() const;

private:
    friend struct Lease;
    friend class Hook;
    void Thread() const;
    std::shared_ptr<Lease> Acquire(std::uint32_t source);
    std::shared_ptr<Lease> Capture(const void* pointer);
    KeelResult Describe(const Lease& lease, KeelEntityInfo& info) const;
    KeelResult Visit(const Lease& lease, const char* name, KeelEntityAccessCallback callback, void* data) const;
    bool Valid(const Lease& lease) const;
    bool Matches(const State& state, const void* pointer) const;
    int Dispatch(State& state, dhooks::Frame& native);
    static Kind Validate(const dhooks::Definition& definition);
    KeelPluginHandle owner_;
    KeelNativeRuntimeApi runtime_;
    KeelEntitiesApi entities_;
    KeelEntityAccessApi access_;
    KeelEntityCaptureApi capture_;
    KeelEntityHookDataApi data_;
    KeelEntityConstructionApi construction_;
    std::shared_ptr<dhooks::Service> transport_;
    std::vector<std::weak_ptr<State>> states_;
    unsigned leases_ = 0, active_ = 0;
};
}
