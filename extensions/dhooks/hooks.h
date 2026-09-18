#pragma once
#include <keels2/keelcall.h>
#include <keels2/native_runtime.h>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace source2root::dhooks {
class Error : public std::runtime_error { public: using std::runtime_error::runtime_error; };
struct Definition {
    unsigned source = 0, result = KH_VALUE_VOID;
    bool method = false, allow_calls = false;
    std::string module, symbol, pattern, profile;
    std::int64_t offset = 0;
    unsigned occurrence = 0;
    std::vector<KeelHookValueType> arguments;
};
Definition ReadDefinition(const std::filesystem::path& file, const std::string& name, const std::string& script);
void Validate(const Definition& definition);
class Service;
class Call;
struct TargetData;
struct Registration;

// A frame is a private snapshot. Slot zero is the result; arguments are 1-based.
// Its native frame and pointers never escape the backend. Only Commit writes
// back, after a successful script callback with a valid phase/action pair.
class Frame final {
public:
    unsigned Phase() const { return phase_; }
    unsigned Flags() const { return flags_; }
    unsigned Count() const { return static_cast<unsigned>(arguments_.size()); }
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
    Target(std::shared_ptr<Service> service, std::shared_ptr<TargetData> data, bool allow_calls);
    std::shared_ptr<Service> service_;
    std::shared_ptr<TargetData> data_;
    bool allow_calls_;
};

// Reusable owned scalar call. All arguments must be explicitly initialized.
// Internal shared state survives resource closure from a nested callback.
class Call final {
public:
    Call(const Call&) = delete;
    Call& operator=(const Call&) = delete;
    ~Call();
    unsigned Count() const;
    unsigned Type(unsigned slot) const;
    const Frame& Read(unsigned slot) const;
    void SetInteger(unsigned slot, std::int32_t value);
    void SetIntegerText(unsigned slot, const std::string& value);
    void SetNumber(unsigned slot, float value);
    void SetNumberText(unsigned slot, const std::string& value);
    void SetNull(unsigned slot);
    void Reset();
    void Execute(unsigned flags);
private:
    friend class Service;
    Call(std::shared_ptr<Service> service, std::shared_ptr<TargetData> target, const Definition& definition);
    template<class Function> void Edit(unsigned slot, Function function);
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
    std::unique_ptr<Target> Open(const Definition& definition);
    std::unique_ptr<Call> Prepare(const Target& target);
    std::unique_ptr<Hook> Attach(const Target& target, unsigned phases, std::int32_t priority,
        Callback callback, std::function<void()> retire);
    // Retry native removal/restoration failures while retaining callback data.
    // Unload must remain blocked until Empty(), including after script cleanup.
    void Collect();
    bool Empty() const { return registrations_.empty() && targets_.empty(); }
    unsigned TargetCount() const { return static_cast<unsigned>(targets_.size()); }
    unsigned HookCount() const { return static_cast<unsigned>(registrations_.size()); }
private:
    friend class Hook;
    friend class Call;
    void Invoke(const TargetData& target, unsigned flags, const std::vector<KeelHookValue>& arguments, KeelHookValue& result);
    void Thread() const;
    void Close(Registration& registration) noexcept;
    static KeelHookAction Dispatch(KeelHookFrame* frame, void* raw) noexcept;
    KeelPluginHandle owner_;
    KeelHookApi hooks_;
    KeelNativeRuntimeApi runtime_;
    KeelCallApi calls_{};
    // Native user_data must survive a resource destructor or facade close until
    // Collect successfully removes every native registration and target lease.
    std::shared_ptr<Service> keepalive_;
    unsigned depth_ = 0;
    bool collecting_ = false;
    std::vector<std::shared_ptr<TargetData>> targets_;
    std::vector<std::shared_ptr<Registration>> registrations_;
};
}
