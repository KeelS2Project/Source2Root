#include "foundation.h"
#include <source2root/native.hpp>

#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <thread>

static void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

class Host final : public sr::GameHost {
public:
    unsigned leases = 0, destroyed = 0, stopped = 0;
    bool release_failure = false, destruction_with_lease = true;
    KeelResult Lookup(int, sr::Player&) override { return KEEL_RESULT_NOT_FOUND; }
    KeelResult Reply(const sr::Player*, const std::string&) override { return KEEL_RESULT_OK; }
    void Log(const std::string& text) override {
        if (text.find("resource valid during stop") != std::string::npos) ++stopped;
        std::cout << text << '\n';
    }
    KeelResult RegisterCommand(const std::string&) override { return KEEL_RESULT_OK; }
    KeelResult RemoveCommand(const std::string&) override { return KEEL_RESULT_OK; }
    KeelResult ListenEvent(const std::string&) override { return KEEL_RESULT_OK; }
    KeelResult RemoveEvent(const std::string&) override { return KEEL_RESULT_OK; }
    KeelResult RenderMenu(const sr::Player&, const std::string&, int) override { return KEEL_RESULT_UNSUPPORTED; }
    KeelResult AcquireProvider(const std::string&, unsigned) override { ++leases; return KEEL_RESULT_OK; }
    KeelResult ReleaseProvider(const std::string&, unsigned) override {
        if (release_failure) return KEEL_RESULT_BUSY;
        if (!leases) return KEEL_RESULT_ENGINE_FAILURE;
        --leases;
        return KEEL_RESULT_OK;
    }
};

struct Probe {
    struct Value {
        Host* host;
        ~Value() {
            ++host->destroyed;
            host->destruction_with_lease = host->destruction_with_lease && host->leases > 0;
        }
    };
    Host& host;
    sr::Foundation& foundation;
    SrRegistration registration = 0;
    sr::Cell first = 0;
    std::uint64_t first_owner = 0;
    unsigned creations = 0, foreign_refusals = 0;
    static KeelResult Call(void* raw, const SrNativeCall* api, int32_t* result, char* error, uint32_t capacity) noexcept {
        try {
            auto& probe = *static_cast<Probe*>(raw);
            source2root::NativeCall call(*api);
            const auto operation = call.Int(1);
            if (operation == 0) {
                Check(probe.foundation.UnregisterNative(100, probe.registration) == KEEL_RESULT_BUSY, "active native cannot unregister");
                Check(call.String(3) == "native text", "string input");
                const auto values = call.Array(6, call.Int(7));
                Check(values.size() == 3, "array input");
                call.Output(4, call.Int(5), "returned text");
                call.OutputArray(6, 3, {31});
                call.OutputCell(8, 42);
                Check(!call.DataPath().empty() && !call.DataPath(true).empty(), "owned and shared data paths");
                Check(call.ConfigPath().ends_with("configs/extensions/test.native") &&
                    (call.ScriptId() == "first" || call.ScriptId() == "second"), "extension configuration scope and actual plugin identity");
                *result = call.Own(7, std::make_unique<Value>(&probe.host));
                if (!probe.first) { probe.first = *result; probe.first_owner = call.Owner(); }
                ++probe.creations;
            } else if (operation == 1) {
                void* value = nullptr;
                *result = api->get_resource(api->context, call.Int(2), 7, &value) == KEEL_RESULT_OK ? 42 : -1;
            } else if (operation == 2) {
                call.Close(call.Int(2), 7);
                *result = 1;
            } else {
                void* value = nullptr;
                Check(api->get_resource(api->context, call.Int(2), 999, &value) == KEEL_RESULT_INVALID_ARGUMENT && !value,
                    "wrong-type resource rejected");
                Check(api->write_string(api->context, 4, 4097, "bad") == KEEL_RESULT_INVALID_ARGUMENT,
                    "oversized output rejected");
                Check(api->read_cell(api->context, 99, result) == KEEL_RESULT_INVALID_ARGUMENT,
                    "argument index checked");
                Check(api->read_array(api->context, 6, nullptr, 1025) == KEEL_RESULT_INVALID_ARGUMENT,
                    "array limit checked");
                KeelResult off_thread = KEEL_RESULT_OK;
                std::thread worker([&] { int32_t value = 0; off_thread = api->read_cell(api->context, 1, &value); });
                worker.join();
                Check(off_thread != KEEL_RESULT_OK, "VM operations restricted to game thread");
                if (probe.first_owner != call.Owner()) {
                    Check(api->get_resource(api->context, probe.first, 7, &value) == KEEL_RESULT_INVALID_ARGUMENT,
                        "another script cannot read a foreign resource");
                    ++probe.foreign_refusals;
                }
                *result = 1;
            }
            return KEEL_RESULT_OK;
        } catch (const std::exception& failure) {
            if (error && capacity) { std::strncpy(error, failure.what(), capacity - 1); error[capacity - 1] = 0; }
            return KEEL_RESULT_ENGINE_FAILURE;
        }
    }
};

int main(int argc, char** argv) {
    try {
        Check(argc == 4, "native_calls_test runtime script fixture");
        const std::filesystem::path root(argv[3]);
        Host host;
        sr::Foundation foundation(host, argv[1], root);
        Probe probe{host, foundation};
        SrContextNativeSpec spec{sizeof(spec), SR_NATIVE_API_VERSION, "ExtensionProbe", 8, 0,
            "test.native", 1, &Probe::Call, &probe};
        Check(foundation.RegisterContextNative(100, spec, probe.registration) == KEEL_RESULT_OK, "register rich native");
        spec.name = "OtherExtensionProbe";
        spec.provider_service = "test.other";
        SrRegistration other = 0;
        Check(foundation.RegisterContextNative(200, spec, other) == KEEL_RESULT_OK, "register foreign extension");
        auto install = [&](const char* id) {
            const auto directory = root / "plugins" / id;
            std::filesystem::create_directories(directory);
            std::filesystem::copy_file(argv[2], directory / "main.smx", std::filesystem::copy_options::overwrite_existing);
            const auto manifest = directory / "plugin.json";
            std::ofstream(manifest) << "{\"schema\":1,\"id\":\"" << id << "\",\"name\":\"native fixture\",\"author\":\"tests\","
                "\"version\":\"1.0.0\",\"api\":2,\"entry\":\"main.smx\",\"enabled\":true,\"dependencies\":[]}";
            return manifest;
        };
        Check(foundation.Load(install("first")) && foundation.Load(install("second")), "real scripts use native memory and owned resources");
        Check(probe.foreign_refusals == 1 && host.destroyed == 2, "foreign ownership and explicit close verified");
        Check(foundation.Reload("first"), "staged reload owns distinct resources");
        Check(host.stopped == 1 && host.destroyed == 4, "retired script cleanup");
        Check(foundation.UnregisterNative(100, probe.registration) == KEEL_RESULT_BUSY, "retained providers refuse unload");
        Check(foundation.Unload("second"), "release second script before last-consumer failure");
        host.release_failure = true;
        Check(!foundation.Unload("first"), "failed provider release retained for retry");
        const auto destroyed = host.destroyed;
        host.release_failure = false;
        Check(foundation.Unload("first") && host.destroyed == destroyed, "retry does not double destroy");
        Check(foundation.Shutdown() && host.leases == 0 && host.destroyed == probe.creations && host.destruction_with_lease,
            "all resources destroyed while provider code remains leased");
        Check(foundation.UnregisterNative(100, probe.registration) == KEEL_RESULT_OK &&
            foundation.UnregisterNative(200, other) == KEEL_RESULT_OK, "providers retire after scripts");
        std::cout << "Native memory, ownership, unload ordering and retry passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
