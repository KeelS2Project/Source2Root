#include "foundation.h"
#include <source2root/native.hpp>

#include <fstream>
#include <iostream>
#include <cstring>

static void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
class Host final : public sr::GameHost {
public:
    unsigned leases = 0, completions = 0, destroyed = 0;
    bool release_failure = false, cleanup_order = true;
    KeelResult Lookup(int, sr::Player&) override { return KEEL_RESULT_NOT_FOUND; }
    KeelResult Reply(const sr::Player*, const std::string&) override { return KEEL_RESULT_OK; }
    void Log(const std::string& text) override {
        if (text.find("ASYNC_CALLBACK_OK") != std::string::npos) ++completions;
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
    struct Owned {
        Probe* probe;
        SrCallback token;
        ~Owned() {
            ++probe->host.destroyed;
            probe->host.cleanup_order &= probe->host.leases > 0 &&
                probe->foundation.CancelCallback(100, token) == KEEL_RESULT_NOT_FOUND;
        }
    };
    Host& host;
    sr::Foundation& foundation;
    SrRegistration registration = 0;
    std::vector<SrCallback> tokens;
    bool quota_checked = false;
    bool register_commands = true;
    static KeelResult Call(void* raw, const SrNativeCall* api, int32_t* result, char* error, uint32_t capacity) noexcept {
        try {
            auto& self = *static_cast<Probe*>(raw);
            source2root::NativeCall call(*api);
            *result = 0;
            if (call.Int(2) == 4) { *result = self.register_commands ? 1 : 0; return KEEL_RESULT_OK; }
            if (call.Int(2) == 1) {
                SrCallback token = 999;
                Check(api->capture_callback(api->context, 1, &token) == KEEL_RESULT_INVALID_ARGUMENT && !token,
                    "invalid function rejected without a token");
            } else if (call.Int(2) == 3) {
                std::vector<SrCallback> captured;
                SrCallback token = 0;
                while (api->capture_callback(api->context, 1, &token) == KEEL_RESULT_OK) captured.push_back(token);
                Check(captured.size() > 100 && captured.size() < 128 && !token, "callbacks share per-script resource quota");
                for (const auto id : captured) Check(self.foundation.CancelCallback(100, id) == KEEL_RESULT_OK, "cancel quota token");
                token = call.Callback(1);
                Check(self.foundation.CancelCallback(100, token) == KEEL_RESULT_OK, "cancellation frees quota");
                self.quota_checked = true;
            } else {
                const auto token = call.Callback(1);
                self.tokens.push_back(token);
                Check(self.foundation.DeliverCallback(100, token, nullptr, 0, nullptr) == KEEL_RESULT_BUSY,
                    "no delivery during active VM execution or initialization");
                Check(self.foundation.UnregisterNative(100, self.registration) == KEEL_RESULT_BUSY, "provider retained");
                if (call.Int(2) == 0) call.Own(1, std::make_unique<Owned>(&self, token));
            }
            *result = 1;
            return KEEL_RESULT_OK;
        } catch (const std::exception& failure) {
            if (error && capacity) { std::strncpy(error, failure.what(), capacity - 1); error[capacity - 1] = 0; }
            return KEEL_RESULT_ENGINE_FAILURE;
        }
    }
};

int main(int argc, char** argv) {
    try {
        Check(argc == 4, "extension_callbacks runtime script fixture");
        Host host;
        std::unique_ptr<Probe> probe;
        sr::Foundation foundation(host, argv[1], argv[3]);
        // Destruct after the foundation so resource callbacks retain their probe.
        probe = std::make_unique<Probe>(host, foundation);
        const SrContextNativeSpec spec{sizeof(spec), SR_NATIVE_API_VERSION, "CaptureCallback", 2, 0,
            "test.callbacks", 1, &Probe::Call, probe.get()};
        Check(foundation.RegisterContextNative(100, spec, probe->registration) == KEEL_RESULT_OK, "register callbacks");
        auto install = [&](const char* id) {
            const auto directory = std::filesystem::path(argv[3]) / "plugins" / id;
            std::filesystem::create_directories(directory);
            std::filesystem::copy_file(argv[2], directory / "main.smx", std::filesystem::copy_options::overwrite_existing);
            const auto path = directory / "plugin.json";
            std::ofstream(path) << "{\"schema\":1,\"id\":\"" << id << "\",\"name\":\"callback fixture\",\"author\":\"tests\","
                "\"version\":\"1.0.0\",\"api\":2,\"entry\":\"main.smx\",\"enabled\":true,\"dependencies\":[]}";
            return path;
        };
        Check(foundation.Load(install("first")), "load first script with captured function");
        probe->register_commands = false;
        Check(foundation.Load(install("second")), "load second script with captured function");
        probe->register_commands = true;
        const auto first = probe->tokens[0], second = probe->tokens[1];
        sr::Cell value = 42;
        auto deliver = [&](SrCallback token) { return foundation.DeliverCallback(100, token, &value, 1, "ok"); };
        Check(foundation.DeliverCallback(200, first, &value, 1, "ok") == KEEL_RESULT_INVALID_ARGUMENT &&
            foundation.CancelCallback(200, first) == KEEL_RESULT_INVALID_ARGUMENT, "foreign provider cannot deliver or cancel");
        std::string oversized(4096, 'a');
        Check(foundation.DeliverCallback(100, first, &value, 1, oversized.c_str()) == KEEL_RESULT_INVALID_ARGUMENT &&
            foundation.DeliverCallback(100, first, nullptr, 1, "ok") == KEEL_RESULT_INVALID_ARGUMENT &&
            foundation.DeliverCallback(100, first, &value, 17, "ok") == KEEL_RESULT_INVALID_ARGUMENT,
            "invalid payload preserves token");
        bool wrong_thread = false;
        std::thread foreign([&] { try { deliver(first); } catch (const std::exception&) { wrong_thread = true; } });
        foreign.join();
        Check(wrong_thread, "VM delivery restricted to server thread");
        Check(foundation.Pause("first") && deliver(first) == KEEL_RESULT_BUSY, "paused callback retained");
        Check(deliver(second) == KEEL_RESULT_OK && host.completions == 1, "other script still receives its callback");
        Check(foundation.Resume("first") && deliver(first) == KEEL_RESULT_OK && host.completions == 2 &&
            deliver(first) == KEEL_RESULT_NOT_FOUND, "resume delivers once only");
        Check(foundation.Unload("second"), "unload second script");
        foundation.Dispatch(sr::Origin::ServerConsole, -1, "sr_quota");
        Check(probe->quota_checked, "real VM callback quota checks ran");
        foundation.Dispatch(sr::Origin::ServerConsole, -1, "sr_queue");
        const auto stale = probe->tokens.back();
        Check(foundation.Reload("first"), "reload creates a distinct callback owner");
        Check(deliver(stale) == KEEL_RESULT_NOT_FOUND && probe->tokens.back() != stale, "stale completion cannot enter replacement VM");
        const auto canceled = probe->tokens.back();
        Check(foundation.CancelCallback(100, canceled) == KEEL_RESULT_OK && deliver(canceled) == KEEL_RESULT_NOT_FOUND,
            "explicit cancellation is final");
        foundation.Dispatch(sr::Origin::ServerConsole, -1, "sr_queue");
        const auto retiring = probe->tokens.back();
        value = 0;
        for (int fault = 0; fault < 3; ++fault) {
            foundation.Dispatch(sr::Origin::ServerConsole, -1, "sr_fault");
            const auto token = probe->tokens.back();
            Check(deliver(token) == KEEL_RESULT_ENGINE_FAILURE && deliver(token) == KEEL_RESULT_NOT_FOUND,
                "faulted callback still consumed exactly once");
        }
        Check(deliver(retiring) == KEEL_RESULT_NOT_FOUND, "retired script suppresses other pending callbacks");
        host.release_failure = true;
        const auto refused = !foundation.Unload("first");
        const auto destroyed = host.destroyed;
        host.release_failure = false;
        Check(refused && foundation.Unload("first") && host.destroyed == destroyed, "unload retry does not repeat resource destruction");
        Check(foundation.Shutdown() && !host.leases && host.cleanup_order && host.destroyed == 3,
            "callbacks invalidated before resource destructors and provider release");
        Check(foundation.UnregisterNative(100, probe->registration) == KEEL_RESULT_OK, "provider unloads after script cleanup");
        std::cout << "Callback ownership, pause, reload, limits, faults and unload passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
