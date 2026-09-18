#include <source2root/extension.hpp>
#include <keels2/unload.h>
#include <array>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

namespace {
void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
struct Host {
    std::map<SrRegistration, SrNativeSpec> natives;
    std::string logs;
    KeelPrepareUnloadCallback prepare = nullptr;
    void* prepare_data = nullptr;
    unsigned registrations = 0, reject_registration = 0, leases = 0, publications = 0;
    bool reject_unregister = false, reject_withdraw = false, reject_release = false;
    bool throw_unregister = false, reject_publish = false, missing_api = false, invalid_api = false;
    bool start_refuses = false, start_throws = false, prepare_refuses = false, prepare_throws = false;
    bool missing_callbacks = false, invalid_callbacks = false, callback_lease = false;
    unsigned callback_queries = 0, callback_retains = 0;
} host;
constexpr KeelPluginHandle Owner = 71;
KeelResult Register(void*, KeelPluginHandle owner, const SrNativeSpec* spec, SrRegistration* id) {
    Check(owner == Owner && spec && id && spec->size == sizeof(*spec), "native ABI envelope");
    Check(spec->api_version == 1 && spec->reserved == 0 && spec->provider_version == 1 &&
        std::string(spec->provider_service) == "test.extension", "native ABI provider metadata");
    *id = 0;
    if (++host.registrations == host.reject_registration) return KEEL_RESULT_ENGINE_FAILURE;
    std::array<std::int32_t, 16> arguments{};
    std::int32_t result = 99;
    Check(spec->invoke(spec->user_data, arguments.data(), spec->argument_count, &result, nullptr, 0) ==
        KEEL_RESULT_NOT_READY && result == 0, "natives remain inert until registration commits");
    *id = host.registrations;
    host.natives.emplace(*id, *spec);
    return KEEL_RESULT_OK;
}
KeelResult Unregister(void*, KeelPluginHandle owner, SrRegistration id) {
    Check(owner == Owner, "native cleanup owner");
    if (host.throw_unregister) throw std::runtime_error("provider cleanup exception");
    if (host.reject_unregister) return KEEL_RESULT_BUSY;
    return host.natives.erase(id) ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND;
}
SrExtensionApi extension_api{sizeof(SrExtensionApi), 1, nullptr, Register, Unregister, nullptr, nullptr};
SrCallbackApi callback_api{sizeof(SrCallbackApi),1,nullptr,
    [](void*, KeelPluginHandle owner, SrCallback token) {
        Check(owner == Owner && token == 9,"persistent callback owner and token");
        ++host.callback_retains; return KEEL_RESULT_OK;
    },
    [](void*, KeelPluginHandle owner, SrCallback token, const SrCallbackArgument*, std::uint32_t count, std::int32_t* result) {
        Check(owner == Owner && token == 9 && !count && result,"persistent invocation envelope");
        *result = 42; return KEEL_RESULT_OK;
    },
    [](void*, KeelPluginHandle, SrCallback) { return KEEL_RESULT_OK; }};
KeelResult Publish(KeelPluginHandle owner, const KeelServiceSpec* spec, KeelServiceHandle* id) {
    Check(owner == Owner && spec && id && spec->service && spec->version == 1 &&
        std::string(spec->name) == "test.extension", "provider publication");
    *id = 0;
    if (host.reject_publish) return KEEL_RESULT_ENGINE_FAILURE;
    Check(!host.publications, "one provider publication");
    ++host.publications; *id = 9; return KEEL_RESULT_OK;
}
KeelResult Withdraw(KeelPluginHandle owner, KeelServiceHandle id) {
    Check(owner == Owner && id == 9 && host.natives.empty(), "natives removed before provider withdrawal");
    if (host.reject_withdraw) return KEEL_RESULT_BUSY;
    Check(host.publications == 1, "provider released exactly once");
    --host.publications; return KEEL_RESULT_OK;
}
KeelResult Release(KeelPluginHandle owner, const char* name, std::uint32_t version) {
    const bool callbacks = std::string(name) == SR_CALLBACK_SERVICE;
    Check(owner == Owner && (callbacks || std::string(name) == SR_EXTENSION_SERVICE) && version == 1 &&
        host.natives.empty() && !host.publications, "lease released after provider cleanup");
    if (host.reject_release) return KEEL_RESULT_BUSY;
    Check(callbacks ? host.callback_lease && host.leases == 2 : !host.callback_lease && host.leases == 1,
        "extension API leases released exactly once");
    if (callbacks) host.callback_lease = false;
    --host.leases; return KEEL_RESULT_OK;
}
KeelServicesApi services{sizeof(services), 1, Publish, Withdraw, Release};
KeelResult Prepare(KeelPluginHandle owner, KeelPrepareUnloadCallback callback, void* data) {
    Check(owner == Owner, "prepare callback owner");
    host.prepare = callback; host.prepare_data = data; return KEEL_RESULT_OK;
}
KeelUnloadApi unload{sizeof(unload), 1, Prepare};
KeelResult Query(KeelPluginHandle owner, const char* name, std::uint32_t version, const void** result) {
    Check(owner == Owner && version == 1, "service query envelope");
    *result = nullptr;
    if (std::string(name) == KEELS2_UNLOAD_SERVICE_NAME) *result = &unload;
    else if (std::string(name) == KEELS2_SERVICES_SERVICE_NAME) *result = &services;
    else if (std::string(name) == SR_EXTENSION_SERVICE) {
        if (host.missing_api) return KEEL_RESULT_NOT_FOUND;
        ++host.leases;
        if (!host.invalid_api) *result = &extension_api;
        return KEEL_RESULT_OK;
    } else if (std::string(name) == SR_CALLBACK_SERVICE) {
        ++host.callback_queries;
        if (host.missing_callbacks) return KEEL_RESULT_NOT_FOUND;
        Check(!host.callback_lease,"one lazy callback service lease");
        host.callback_lease = true; ++host.leases;
        if (!host.invalid_callbacks) *result = &callback_api;
        return KEEL_RESULT_OK;
    }
    return *result ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND;
}
KeelHostApi api{sizeof(api), KEELS2_PLUGIN_ABI_VERSION,
    [](KeelPluginHandle, KeelLogLevel, const char* message) { host.logs += std::string(message) + '\n'; },
    [](KeelPluginHandle, const KeelCommandSpec*, KeelCommandHandle*) { return KEEL_RESULT_UNSUPPORTED; },
    [](KeelPluginHandle, KeelCommandHandle) { return KEEL_RESULT_NOT_FOUND; }, Query};

class Probe;
Probe* active_probe = nullptr;
class Probe final : public source2root::Extension {
public:
    static constexpr keels2::PluginInfo Info{"Test extension", "tests", "1.0.0", "Extension lifecycle tests"};
    Probe() : Extension("test.extension") { active_probe = this; }
    using Extension::RetainCallback;
    using Extension::InvokeCallback;
private:
    bool OnExtensionStart() override {
        Check(RetainCallback(9) == KEEL_RESULT_NOT_READY && !host.callback_queries,"callback query waits for completed load");
        Check(RegisterNative("NoArguments", &Probe::NoArguments), "stage zero argument callback");
        Check(!RegisterNative("NoArguments", &Probe::NoArguments), "reject duplicate staged native");
        Check(!RegisterNative("", &Probe::NoArguments), "reject empty staged native");
        Check(RegisterNative("Subtract", &Probe::Subtract), "stage typed callback");
        Check(RegisterNative("Throw", &Probe::Throw), "stage throwing callback");
        if (host.start_throws) throw std::runtime_error("start rejected");
        return !host.start_refuses;
    }
    bool PrepareExtensionUnload() override {
        if (host.prepare_throws) throw std::runtime_error("prepare rejected");
        return !host.prepare_refuses;
    }
    std::int32_t NoArguments() { return 17; }
    std::int32_t Subtract(std::int32_t a, std::int32_t b) { return a - b; }
    std::int32_t Throw(std::int32_t kind) {
        if (kind == 1) throw std::invalid_argument("Invalid range.");
        if (kind == 2) throw std::runtime_error("Provider failed.");
        throw 42;
    }
};
void Empty() {
    Check(host.natives.empty() && !host.leases && !host.callback_lease && !host.publications && !host.prepare,
        "ABI rollback or unload leaves no callback, publication or lease");
}
void Reset() { Empty(); host = {}; }
SrNativeSpec Native(const char* name) {
    for (const auto& [id, spec] : host.natives) if (std::string(spec.name) == name) return spec;
    throw std::runtime_error("registered native missing");
}
void Finish() {
    Check(host.prepare && host.prepare(host.prepare_data), "cleanup succeeds");
    KeelPlugin_Unload(Owner); Empty();
}
}
KEELS2_PLUGIN(Probe)

int main() {
    try {
        for (int failure = 0; failure < 7; ++failure) {
            Reset();
            if (failure == 0) host.start_refuses = true;
            if (failure == 1) host.start_throws = true;
            if (failure == 2) host.missing_api = true;
            if (failure == 3) host.invalid_api = true;
            if (failure == 4) host.reject_publish = true;
            if (failure == 5) host.reject_registration = 1;
            if (failure == 6) host.reject_registration = 2;
            Check(!KeelPlugin_Load(&api, Owner), "failed setup rolls back before ABI load refusal");
            Empty();
            if (failure < 2) Check(!host.registrations, "failed start never publishes staged callbacks");
        }
        Reset();
        Check(KeelPlugin_Load(&api, Owner), "load through actual Keel authoring bridge");
        Check(!KeelPlugin_Load(&api, Owner), "bridge rejects duplicate load without discarding bindings");
        auto zero = Native("NoArguments");
        auto subtract = Native("Subtract");
        auto throwing = Native("Throw");
        std::int32_t result = 99;
        char error[128] = "old error";
        Check(zero.invoke(zero.user_data, nullptr, 0, &result, error, sizeof(error)) == KEEL_RESULT_OK &&
            result == 17 && !*error, "zero argument invocation and cleared diagnostic");
        const std::int32_t arguments[]{29, 11};
        Check(subtract.invoke(subtract.user_data, arguments, 2, &result, nullptr, 0) == KEEL_RESULT_OK &&
            result == 18, "typed argument ordering");
        for (int invalid = 0; invalid < 4; ++invalid) {
            result = 99;
            Check(subtract.invoke(invalid == 0 ? nullptr : subtract.user_data,
                invalid == 1 ? nullptr : arguments, invalid == 2 ? 1 : 2,
                invalid == 3 ? nullptr : &result, error, sizeof(error)) == KEEL_RESULT_INVALID_ARGUMENT &&
                (invalid == 3 || result == 0) && std::string(error) == "Invalid native arguments.", "ABI input rejection");
        }
        for (std::int32_t kind = 1; kind <= 3; ++kind) {
            result = 99;
            Check(throwing.invoke(throwing.user_data, &kind, 1, &result, error, sizeof(error)) ==
                (kind == 1 ? KEEL_RESULT_INVALID_ARGUMENT : KEEL_RESULT_ENGINE_FAILURE) && result == 0,
                "native exceptions contained at C boundary");
            Check(std::string(error) == (kind == 1 ? "Invalid range." : kind == 2 ? "Provider failed." :
                "Native callback threw an exception."), "native diagnostic preserved");
            char bounded[]{'a', 'b', 'c', 'd', 'e'};
            throwing.invoke(throwing.user_data, &kind, 1, &result, bounded + 1, 3);
            Check(bounded[0] == 'a' && bounded[3] == 0 && bounded[4] == 'e', "bounded NUL diagnostic");
            throwing.invoke(throwing.user_data, &kind, 1, &result, bounded + 1, 1);
            Check(bounded[1] == 0 && bounded[0] == 'a', "one byte diagnostic");
        }
        for (int refusal = 0; refusal < 3; ++refusal) {
            host.prepare_refuses = refusal == 0;
            host.prepare_throws = refusal == 1;
            host.reject_unregister = refusal == 2;
            Check(!host.prepare(host.prepare_data), "unload refusal retains image");
            Check(zero.invoke(zero.user_data, nullptr, 0, &result, nullptr, 0) == KEEL_RESULT_OK && result == 17,
                "callback remains valid after preparation refusal");
        }
        host.reject_unregister = false;
        host.reject_withdraw = true;
        Check(!host.prepare(host.prepare_data) && host.natives.empty() && host.publications == 1 && host.leases == 1,
            "withdrawal refusal retains provider and API lease");
        host.reject_withdraw = false; host.reject_release = true;
        Check(!host.prepare(host.prepare_data) && !host.publications && host.leases == 1,
            "lease release refusal remains retryable");
        host.reject_release = false; Finish();
        for (int refusal = 0; refusal < 4; ++refusal) {
            Reset(); host.reject_registration = 2;
            host.reject_unregister = refusal == 0;
            host.throw_unregister = refusal == 1;
            host.reject_withdraw = refusal == 2;
            host.reject_release = refusal == 3;
            Check(KeelPlugin_Load(&api, Owner) && host.prepare,
                "failed setup cleanup retains module instead of returning unsafe ABI load failure");
            for (const auto& [id, spec] : host.natives) {
                result = 99;
                Check(spec.invoke(spec.user_data, nullptr, 0, &result, error, sizeof(error)) == KEEL_RESULT_NOT_READY &&
                    result == 0, "retained setup failure exposes inert callbacks only");
            }
            Check(host.logs.find("natives are disabled") != std::string::npos, "operator told how to retry cleanup");
            host.reject_unregister = host.throw_unregister = host.reject_withdraw = host.reject_release = false;
            Finish();
        }
        Reset(); Check(KeelPlugin_Load(&api, Owner), "static extension instance reloads after cleanup"); Finish();
        for (int mode = 0; mode < 3; ++mode) {
            Reset(); host.missing_callbacks = mode == 1; host.invalid_callbacks = mode == 2;
            Check(KeelPlugin_Load(&api, Owner) && !host.callback_queries,"legacy extension needs no persistent callback service");
            result = 99;
            Check(active_probe->InvokeCallback(9,{},result) == KEEL_RESULT_NOT_READY && !result,"invocation waits for lazy service acquisition");
            const auto expected = mode == 1 ? KEEL_RESULT_NOT_FOUND : mode == 2 ? KEEL_RESULT_INCOMPATIBLE : KEEL_RESULT_OK;
            Check(active_probe->RetainCallback(9) == expected && active_probe->RetainCallback(9) == expected,
                "optional and incompatible callback service results preserved");
            Check(host.callback_queries == (mode == 1 ? 2u : 1u) && host.leases == (mode == 1 ? 1u : 2u),
                "lazy service lookup retains at most one lease including incompatible table");
            if (!mode) {
                Check(host.callback_retains == 2 && active_probe->InvokeCallback(9,{},result) == KEEL_RESULT_OK && result == 42,
                    "persistent service wrappers preserve ownership and scalar result");
                host.reject_release = true;
                Check(!host.prepare(host.prepare_data) && host.callback_lease,"failed callback lease release can be retried");
                host.reject_release = false;
            }
            Finish();
        }
        std::cout << "extension ABI, typed invocation and recoverable cleanup passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
