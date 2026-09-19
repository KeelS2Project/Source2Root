#include "foundation.h"
#include <source2root/native.hpp>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>

namespace {
void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

class Host final : public sr::GameHost {
public:
    unsigned leases = 0, destroyed = 0;
    bool cleanup_order = true, failed = false;
    KeelResult Lookup(int, sr::Player&) override {
        return KEEL_RESULT_NOT_FOUND;
    }

    KeelResult Reply(const sr::Player*, const std::string&) override {
        return KEEL_RESULT_OK;
    }

    void Log(const std::string& text) override {
        failed |= text.find("PERSISTENT_FAILED") != std::string::npos;
    }

    KeelResult RegisterCommand(const std::string&) override {
        return KEEL_RESULT_OK;
    }

    KeelResult RemoveCommand(const std::string&) override {
        return KEEL_RESULT_OK;
    }

    KeelResult ListenEvent(const std::string&) override {
        return KEEL_RESULT_OK;
    }

    KeelResult RemoveEvent(const std::string&) override {
        return KEEL_RESULT_OK;
    }

    KeelResult RenderMenu(const sr::Player&, const std::string&, int) override {
        return KEEL_RESULT_UNSUPPORTED;
    }

    KeelResult AcquireProvider(const std::string&, unsigned) override {
        ++leases;
        return KEEL_RESULT_OK;
    }

    KeelResult ReleaseProvider(const std::string&, unsigned) override {
        if (!leases)
            return KEEL_RESULT_ENGINE_FAILURE;

        --leases;
        return KEEL_RESULT_OK;
    }
};

struct Payload {
    std::int32_t integer = 3;
    float real = 1.25f;
    std::int32_t integers[2]{4,5}, readonly[2]{9,10};
    float reals[2]{1,2};
    std::array<SrCallbackArgument,8> args{};
    explicit Payload(std::int32_t mode = 0) {
        for (auto& a : args)
            a.size = sizeof(a);

        args[0].type = SR_CALLBACK_INT32;
        args[0].value.integer = mode;
        args[1].type = SR_CALLBACK_FLOAT32;
        args[1].value.real = 2.5f;
        args[2].type = SR_CALLBACK_STRING;
        args[2].value.string = "ok";
        args[3].type = SR_CALLBACK_INT32_REF;
        args[3].count = 1;
        args[3].value.integers = &integer;
        args[4].type = SR_CALLBACK_FLOAT32_REF;
        args[4].count = 1;
        args[4].value.reals = &real;
        args[5].type = SR_CALLBACK_INT32_ARRAY;
        args[5].count = 2;
        args[5].flags = SR_CALLBACK_COPYBACK;
        args[5].value.integers = integers;
        args[6].type = SR_CALLBACK_FLOAT32_ARRAY;
        args[6].count = 2;
        args[6].flags = SR_CALLBACK_COPYBACK;
        args[6].value.reals = reals;
        args[7].type = SR_CALLBACK_INT32_ARRAY;
        args[7].count = 2;
        args[7].value.integers = readonly;
    }

    bool Changed() const { return integer == 10 && real == 3.5f && integers[0] == 40 && integers[1] == 50 &&
        reals[0] == 4 && reals[1] == 5 && readonly[0] == 9 && readonly[1] == 10; }
    bool Unchanged() const { return integer == 3 && real == 1.25f && integers[0] == 4 && integers[1] == 5 &&
        reals[0] == 1 && reals[1] == 2 && readonly[0] == 9 && readonly[1] == 10; }
};

struct Probe {
    Host& host;
    sr::Foundation& foundation;
    SrRegistration capture = 0, control = 0;
    SrCallback token = 0;
    unsigned nested = 0, recursion_busy = 0;

    struct Owned {
        Probe* probe;
        SrCallback token;
        ~Owned() {
            ++probe->host.destroyed;
            probe->host.cleanup_order &= probe->host.leases > 0 &&
                probe->foundation.CancelCallback(100,token) == KEEL_RESULT_NOT_FOUND;
        }
    };
    KeelResult Invoke(Payload& payload, std::int32_t& result) {
        return foundation.InvokeCallback(100,token,payload.args.data(),payload.args.size(),&result);
    }

    static KeelResult
    Capture(void* raw, const SrNativeCall* api, std::int32_t* result, char* error, unsigned capacity) noexcept {
        try {
            auto& p = *static_cast<Probe*>(raw);
            source2root::NativeCall call(*api);
            p.token = call.Callback(1);
            std::int32_t value = 99;
            Check(p.foundation.InvokeCallback(100, p.token, nullptr, 0, &value) == KEEL_RESULT_INVALID_ARGUMENT &&
                      !value,
                  "one-shot token cannot use persistent API");

            Check(p.foundation.RetainCallback(200,p.token) == KEEL_RESULT_INVALID_ARGUMENT,"foreign provider cannot retain");
            Check(p.foundation.RetainCallback(100,p.token) == KEEL_RESULT_OK &&
                p.foundation.RetainCallback(100,p.token) == KEEL_RESULT_OK,"retaining is idempotent");

            Check(p.foundation.DeliverCallback(100, p.token, nullptr, 0, nullptr) == KEEL_RESULT_INVALID_ARGUMENT,
                  "persistent token cannot use one-shot API");

            Check(p.foundation.InvokeCallback(100,p.token,nullptr,0,&value) == KEEL_RESULT_BUSY,"loading callback cannot execute");
            call.Own(1,std::make_unique<Owned>(&p,p.token));
            *result = 1;
            return KEEL_RESULT_OK;
        } catch (const std::exception& failure) {
            if (capacity) {
                std::strncpy(error, failure.what(), capacity - 1);
                error[capacity - 1] = 0;
            }

            return KEEL_RESULT_ENGINE_FAILURE;
        }
    }

    static KeelResult
    Control(void* raw, const SrNativeCall* api, std::int32_t* result, char* error, unsigned capacity) noexcept {
        try {
            auto& p = *static_cast<Probe*>(raw);
            source2root::NativeCall call(*api);
            const auto op = call.Int(1);

            if (op == 0 || op == 3) {
                Payload payload(op == 3 ? 1 : 0);
                std::int32_t value = 99;
                const auto status = p.Invoke(payload,value);

                if (status == KEEL_RESULT_BUSY) {
                    Check(op == 3 && value == 0 && payload.Unchanged(),"recursion limit has no effects");
                    ++p.recursion_busy;
                    *result = -1;
                } else {
                    Check(status == KEEL_RESULT_OK && payload.Changed(),"nested callback marshals independent buffers");
                    ++p.nested;
                    *result = value;
                }
            } else if (op == 1) {
                Check(!p.foundation.Pause("first") && !p.foundation.Reload("first"),"active callback prevents pause/reload");
                Check(p.foundation.UnregisterNative(100,p.capture) == KEEL_RESULT_BUSY,"active callback retains provider");
                *result = 1;
            } else if (op == 2) {
                *result = p.foundation.CancelCallback(100,p.token) == KEEL_RESULT_OK;
            } else if (op == 4)
                *result = 0;
            else if (op == 5)
                *result = !p.foundation.Unload("first");
            else
                throw std::runtime_error("unknown fixture operation");

            return KEEL_RESULT_OK;
        } catch (const std::exception& failure) {
            if (capacity) {
                std::strncpy(error, failure.what(), capacity - 1);
                error[capacity - 1] = 0;
            }

            return KEEL_RESULT_ENGINE_FAILURE;
        }
    }
};
}

int main(int argc, char** argv) {
    try {
        Check(argc == 4,"persistent_callbacks runtime script fixture");
        Host host;
        std::unique_ptr<Probe> probe;
        sr::Foundation foundation(host,argv[1],argv[3]);
        probe = std::make_unique<Probe>(host,foundation);
        const SrContextNativeSpec capture{
            sizeof(capture), 1, "CapturePersistent", 1, 0, "test.persistent", 1, Probe::Capture, probe.get()};

        const SrContextNativeSpec control{
            sizeof(control), 1, "PersistentControl", 1, 0, "test.persistent", 1, Probe::Control, probe.get()};

        Check(foundation.RegisterContextNative(100,capture,probe->capture) == KEEL_RESULT_OK &&
            foundation.RegisterContextNative(100,control,probe->control) == KEEL_RESULT_OK,"register fixture natives");

        const auto directory = std::filesystem::path(argv[3])/"plugins/first";
        std::filesystem::create_directories(directory);
        std::filesystem::copy_file(argv[2],directory/"main.smx",std::filesystem::copy_options::overwrite_existing);
        const auto path = directory/"plugin.json";
        std::ofstream(path)
            << R"({"schema":1,"id":"first","name":"persistent fixture","author":"tests","version":"1.0.0","api":2,"entry":"main.smx","enabled":true,"dependencies":[]})";

        Check(foundation.Load(path),"load persistent fixture");

        for (unsigned i = 0; i < 3; ++i) {
            Payload payload;
            std::int32_t value = 99;
            Check(probe->Invoke(payload, value) == KEEL_RESULT_OK && value == 43 && payload.Changed(),
                  "persistent callback repeats with typed copyback");
        }

        Payload unchanged;
        std::int32_t value = 99;
        Check(foundation.InvokeCallback(200, probe->token, unchanged.args.data(), 8, &value) ==
                      KEEL_RESULT_INVALID_ARGUMENT &&
                  !value && unchanged.Unchanged(),
              "foreign invocation has no effect");

        Check(foundation.InvokeCallback(100, probe->token, nullptr, 1, &value) == KEEL_RESULT_INVALID_ARGUMENT &&
                  !value,
              "invalid payload preserves token");

        for (unsigned fault = 0; fault < 10; ++fault) {
            Payload payload;

            if (fault == 0)
                payload.args[0].size = 0;

            if (fault == 1)
                payload.args[0].type = 99;

            if (fault == 2)
                payload.args[0].flags = 1;

            if (fault == 3)
                payload.args[0].count = 1;

            if (fault == 4)
                payload.args[2].value.string = nullptr;

            if (fault == 5)
                payload.args[3].count = 0;

            if (fault == 6)
                payload.args[4].value.reals = nullptr;

            if (fault == 7)
                payload.args[5].flags = 2;

            if (fault == 8)
                payload.args[5].count = 1025;

            if (fault == 9)
                payload.args[6].count = 0;

            value = 99;
            Check(probe->Invoke(payload, value) == KEEL_RESULT_INVALID_ARGUMENT && !value && payload.Unchanged(),
                  "invalid descriptor is atomic");
        }

        std::string large(4096, 'x');
        Payload too_large;
        too_large.args[2].value.string = large.c_str();
        Check(probe->Invoke(too_large, value) == KEEL_RESULT_INVALID_ARGUMENT && too_large.Unchanged(),
              "oversized string refused");

        std::array<SrCallbackArgument,17> many{};
        Check(foundation.InvokeCallback(100, probe->token, many.data(), 17, &value) == KEEL_RESULT_INVALID_ARGUMENT,
              "argument count bounded");

        std::array<std::int32_t,1024> block{};

        for (auto& argument : many)
            argument = {sizeof(argument), SR_CALLBACK_INT32_ARRAY, 1024, 0, {}};

        for (auto& argument : many)
            argument.value.integers = block.data();

        Check(foundation.InvokeCallback(100, probe->token, many.data(), 5, &value) == KEEL_RESULT_INVALID_ARGUMENT,
              "combined payload bounded");

        Check(foundation.Pause("first"),"pause fixture");
        Check(probe->Invoke(unchanged,value) == KEEL_RESULT_BUSY && !value && unchanged.Unchanged(),"paused token preserved");
        Check(foundation.Resume("first"),"resume fixture");
        bool refused = false;
        std::thread worker([&] {
            try {
                probe->Invoke(unchanged, value);
            } catch (...) {
                refused = true;
            }
        });
        worker.join();
        Check(refused && !value && unchanged.Unchanged(), "worker cannot enter VM");
        foundation.Dispatch(sr::Origin::ServerConsole,-1,"sr_persistent");
        Check(probe->nested == 1 && !host.failed,"persistent call supports synchronous VM reentry");
        Payload recursive(1);
        Check(probe->Invoke(recursive, value) == KEEL_RESULT_OK && value == 7 && recursive.Changed() &&
                  probe->recursion_busy == 1,
              "eight nested callback frames unwind safely");

        const auto old = probe->token;
        Check(foundation.Reload("first") && probe->token != old,"reload assigns new token");
        Check(foundation.InvokeCallback(100, old, nullptr, 0, &value) == KEEL_RESULT_NOT_FOUND && !value,
              "old generation cannot reenter replacement");

        Payload canceled(2);
        Check(probe->Invoke(canceled, value) == KEEL_RESULT_OK && value == 43 && canceled.Changed(),
              "callback may cancel itself");

        Check(probe->Invoke(unchanged,value) == KEEL_RESULT_NOT_FOUND && !value,"self cancellation final");
        Check(foundation.Reload("first"),"reload before fault");
        Payload faulted(3);
        Check(probe->Invoke(faulted, value) == KEEL_RESULT_ENGINE_FAILURE && !value && faulted.Unchanged(),
              "fault discards all staged copyback");

        Check(probe->Invoke(unchanged,value) == KEEL_RESULT_NOT_FOUND,"fault invalidates persistent token");
        Check(foundation.Reload("first"),"reload after fault");
        Payload retirement(4);
        Check(probe->Invoke(retirement, value) == KEEL_RESULT_OK && value == 43 && retirement.Changed(),
              "active script retained during requested unload");

        Check(probe->Invoke(unchanged,value) == KEEL_RESULT_NOT_FOUND,"retiring script cannot be called again");
        Check(foundation.Unload("first") && foundation.Shutdown() && !host.leases && host.cleanup_order &&
                  host.destroyed == 4 && !host.failed,
              "callbacks canceled before owned cleanup and provider release");

        Check(foundation.UnregisterNative(100, probe->capture) == KEEL_RESULT_OK &&
                  foundation.UnregisterNative(100, probe->control) == KEEL_RESULT_OK,
              "provider unregisters after retirement");

        std::cout << "Persistent callback fixtures passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
