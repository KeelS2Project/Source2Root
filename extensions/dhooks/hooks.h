#pragma once
#include <keels2/keelcall.h>
#include <keels2/entity_access.h>
#include <keels2/players.h>
#include <keels2/native_runtime.h>
#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace source2root::sdkhooks { class Service; }
namespace source2root::dhooks {
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class BufferKind { string, int32, vector3 };

struct BufferSpec {
    unsigned argument = 0, capacity = 0, length_argument = 0;
    BufferKind kind = BufferKind::string;
};

struct EntitySpec {
    unsigned argument = 0;
    std::string class_name;
};

struct EntityHookPolicy {
    std::string kind, class_name, block;
};

struct Definition {
    unsigned source = 0, result = KH_VALUE_VOID;
    bool method = false, allow_calls = false;
    std::string module, symbol, pattern, profile;
    std::int64_t offset = 0;
    unsigned occurrence = 0;
    std::vector<KeelHookValueType> arguments;
    std::vector<BufferSpec> buffers;
    std::vector<EntitySpec> entities;
    EntityHookPolicy entity_hook;
};
Definition ReadDefinition(const std::filesystem::path& file, const std::string& name, const std::string& script);
void Validate(const Definition& definition);

class Service;

class Call;

struct TargetData;

struct Registration;

struct EntityLease {
    EntityLease() = default;
    EntityLease(const EntityLease&) = delete;
    EntityLease& operator=(const EntityLease&) = delete;
    std::shared_ptr<Service> service;
    KeelEntityHandle handle = 0;
    KeelEntityInfo identity{};
    ~EntityLease();
};

// A frame is a private snapshot. Slot zero is the result; arguments are 1-based.
// Its native frame and pointers never escape the backend. Only Commit writes
// back, after a successful script callback with a valid phase/action pair.
class Frame final {
public:
    unsigned Phase() const {
        return phase_;
    }

    unsigned Flags() const {
        return flags_;
    }

    unsigned Count() const {
        return static_cast<unsigned>(arguments_.size());
    }

    unsigned Type(unsigned slot) const;
    std::int32_t Integer(unsigned slot) const;
    std::string IntegerText(unsigned slot) const;
    float Number(unsigned slot) const;
    std::string NumberText(unsigned slot) const;
    bool IsNull(unsigned slot) const;
    void SetInteger(unsigned slot, std::int32_t value);
    void SetIntegerText(unsigned slot, const std::string& value);
    void SetNumber(unsigned slot, float value);
    void SetNumberText(unsigned slot, const std::string& value);
    void SetNull(unsigned slot);
    void Copy(unsigned destination, unsigned source);

private:
    friend class Service;
    friend class Call;
    friend class source2root::sdkhooks::Service;
    const void* native_key_ = nullptr;
    bool retire_ = false;
    explicit Frame(const Definition& definition);
    explicit Frame(KeelHookFrame& frame, const Definition& definition);
    void Commit(KeelHookFrame& frame, unsigned action) const;
    const KeelHookValue& Value(unsigned slot) const;
    KeelHookValue& Writable(unsigned slot);
    unsigned phase_, flags_;
    std::vector<KeelHookValue> arguments_;
    KeelHookValue result_;
};

class Target final {
public:
    ~Target() = default;
    Target(const Target&) = delete;
    Target& operator=(const Target&) = delete;

private:
    friend class Service;
    Target(std::shared_ptr<Service> service, std::shared_ptr<TargetData> data, Definition definition);
    std::shared_ptr<Service> service_;
    std::shared_ptr<TargetData> data_;
    Definition definition_;
};

// Reusable owned call. All arguments must be initialized; buffer lengths are
// supplied by their associated buffer setters.
// Internal shared state survives resource closure from a nested callback.
class Call final {
public:
    Call(const Call&) = delete;
    Call& operator=(const Call&) = delete;
    ~Call();
    unsigned Count() const;
    unsigned Type(unsigned slot) const;
    const Frame& Read(unsigned slot) const;
    bool IsNull(unsigned slot) const;
    void SetEntityReference(unsigned slot, std::uint32_t source);
    void SetPlayer(unsigned slot, const KeelPlayerConnection& player, bool pawn);
    void SetInteger(unsigned slot, std::int32_t value);
    void SetIntegerText(unsigned slot, const std::string& value);
    void SetNumber(unsigned slot, float value);
    void SetNumberText(unsigned slot, const std::string& value);
    void SetNull(unsigned slot);
    void SetString(unsigned slot, const std::string& value, unsigned capacity = 0);
    std::string String(unsigned slot) const;
    void SetArray(unsigned slot, const std::vector<std::int32_t>& value);
    std::vector<std::int32_t> Array(unsigned slot) const;
    void SetVector(unsigned slot, const std::array<float, 3>& value);
    std::array<float, 3> Vector(unsigned slot) const;
    void Reset();
    void Execute(unsigned flags);

private:
    friend class Service;
    Call(std::shared_ptr<Service> service, std::shared_ptr<TargetData> target, const Definition& definition);
    template<class Function> void Edit(unsigned slot, Function function);

    struct Entity;
    Entity& WritableEntity(unsigned slot);

    struct Buffer;
    Buffer& WritableBuffer(unsigned slot, BufferKind kind);
    const Buffer& ReadBuffer(unsigned slot, BufferKind kind) const;
    void CommitBuffer(Buffer& buffer, Buffer value);

    struct State;
    std::shared_ptr<State> state_;
};

class Hook final {
public:
    ~Hook();
    Hook(const Hook&) = delete;
    Hook& operator=(const Hook&) = delete;
    void Close();
    void Enable(bool enabled);
    bool Active() const;

private:
    friend class Service;
    Hook(std::shared_ptr<Service> service, std::shared_ptr<Registration> registration);
    std::shared_ptr<Service> service_;
    std::shared_ptr<Registration> registration_;
};
// Return -1 to discard all edits, -2 to also retire the hook. Callback
// providers retain their own VM tokens and cancel them in the retire function.
using Callback = std::function<int(Frame&)>;

class Service final : public std::enable_shared_from_this<Service> {
public:
    Service(KeelPluginHandle owner, const KeelHookApi& hooks, const KeelNativeRuntimeApi& runtime,
        const KeelCallApi* calls = nullptr);

    void EntityServices(const KeelEntityAccessApi& access, const KeelEntitiesApi& entities, const KeelPlayersApi& players);
    std::unique_ptr<Target> Open(const Definition& definition);
    std::unique_ptr<Call> Prepare(const Target& target);
    std::unique_ptr<Hook> Attach(const Target& target, unsigned phases, std::int32_t priority,
        Callback callback, std::function<void()> retire);
    // Retry native removal/restoration failures while retaining callback data.
    // Unload must remain blocked until Empty(), including after script cleanup.
    void Collect();
    bool Empty() const {
        return registrations_.empty() && targets_.empty() && !entity_count_;
    }

    unsigned TargetCount() const {
        return static_cast<unsigned>(targets_.size());
    }

    unsigned HookCount() const {
        return static_cast<unsigned>(registrations_.size());
    }

private:
    friend class Hook;
    friend class Call;
    friend struct EntityLease;
    std::shared_ptr<EntityLease> AcquireEntity(std::uint32_t source);
    void ValidateEntity(const EntityLease& entity) const;
    KeelPlayerInfo Player(const KeelPlayerConnection& player) const;
    void InvokeEntities(const TargetData& target, const std::vector<KeelHookValue>& arguments,
        const std::vector<BufferSpec>& bounds, const std::vector<KeelEntityAccessSpec>& entities,
        const std::vector<unsigned>& slots, KeelHookValue& result);

    void Invoke(const TargetData& target, unsigned flags, const std::vector<KeelHookValue>& arguments,
        const std::vector<BufferSpec>& bounds, KeelHookValue& result);

    void CheckBufferEdits(const KeelHookFrame& before, const Frame& after) const;
    void Thread() const;
    void Close(Registration& registration) noexcept;
    static KeelHookAction Dispatch(KeelHookFrame* frame, void* raw) noexcept;
    KeelPluginHandle owner_;
    KeelHookApi hooks_;
    KeelNativeRuntimeApi runtime_;
    KeelCallApi calls_{};
    KeelEntityAccessApi access_{};
    KeelEntitiesApi entities_{};
    KeelPlayersApi players_{};
    unsigned entity_count_ = 0;

    struct BufferScope {
        KeelHookTargetHandle target;
        const std::vector<KeelHookValue>* arguments;
        const std::vector<BufferSpec>* bounds;
    };
    std::vector<BufferScope> active_buffers_;
    // Native user_data must survive a resource destructor or facade close until
    // Collect successfully removes every native registration and target lease.
    std::shared_ptr<Service> keepalive_;
    unsigned depth_ = 0;
    bool collecting_ = false;
    std::vector<std::shared_ptr<TargetData>> targets_;
    std::vector<std::shared_ptr<Registration>> registrations_;
};
}
