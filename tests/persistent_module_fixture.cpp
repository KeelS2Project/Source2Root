#include <source2root/extension.hpp>
#include <map>

namespace {
using namespace keels2::authoring;
class Persistent final : public source2root::Extension {
public:
    static constexpr PluginInfo Info{"Persistent callback fixture", "tests", "1.0.0", "Callback service lifecycle"};
    static constexpr PluginRequirement Requirements[]{{"Source2Root", "1.0.0", DependencyRequirement::exact}};
    Persistent() : Extension("test.persistent") {}
    void OnGameFrame(bool, bool, bool) override {
        for (const auto& [owner, token] : callbacks_) {
            std::int32_t value = 0;
            const auto status = Invoke(token,20,value);
            if (status == KEEL_RESULT_BUSY && !value) LogMessage("PERSISTENT_PAUSED_OK");
            else if (status == KEEL_RESULT_OK && value == 42) LogMessage("PERSISTENT_FRAME_OK");
            else LogError("PERSISTENT_FAILED frame invocation");
        }
    }
private:
    struct Owned {
        Persistent* extension;
        std::uint64_t owner;
        SrCallback token;
        ~Owned() {
            extension->callbacks_.erase(owner);
            const auto canceled = extension->CancelCallback(token);
            if (canceled == KEEL_RESULT_NOT_FOUND) extension->LogMessage("PERSISTENT_CLEANUP_OK");
            else extension->LogError("PERSISTENT_FAILED cleanup ordering");
        }
    };
    std::map<std::uint64_t,SrCallback> callbacks_;
    bool OnExtensionStart() override {
        return RegisterNative("Persistent_Store",1,&Persistent::Store)
            && RegisterNative("Persistent_Invoke",0,&Persistent::Run);
    }
    KeelResult Invoke(SrCallback token, std::int32_t input, std::int32_t& result) {
        std::int32_t output = 0;
        std::vector<SrCallbackArgument> arguments(2);
        arguments[0] = {sizeof(SrCallbackArgument),SR_CALLBACK_INT32,0,0,{}};
        arguments[0].value.integer = input;
        arguments[1] = {sizeof(SrCallbackArgument),SR_CALLBACK_INT32_REF,1,0,{}};
        arguments[1].value.integers = &output;
        const auto status = InvokeCallback(token,arguments,result);
        if ((status == KEEL_RESULT_OK && output != input + 1) || (status != KEEL_RESULT_OK && output))
            LogError("PERSISTENT_FAILED copyback");
        return status;
    }
    std::int32_t Store(source2root::NativeCall& call) {
        if (callbacks_.contains(call.Owner())) throw std::runtime_error("Duplicate callback owner.");
        const auto token = call.Callback(1);
        if (RetainCallback(token) != KEEL_RESULT_OK) {
            CancelCallback(token); throw std::runtime_error("Retain failed.");
        }
        try {
            std::int32_t value = 99;
            if (Invoke(token,20,value) != KEEL_RESULT_BUSY || value)
                throw std::runtime_error("Loading callback executed.");
            callbacks_.emplace(call.Owner(),token);
            call.Own(1,std::make_unique<Owned>(this,call.Owner(),token));
        } catch (...) {
            callbacks_.erase(call.Owner()); CancelCallback(token); throw;
        }
        LogMessage("PERSISTENT_STORED_OK");
        return 1;
    }
    std::int32_t Run(source2root::NativeCall& call) {
        std::int32_t result = 99;
        if (Invoke(callbacks_.at(call.Owner()),40,result) != KEEL_RESULT_OK || result != 42)
            throw std::runtime_error("Nested callback failed.");
        return result;
    }
};
}
KEELS2_PLUGIN(Persistent)
