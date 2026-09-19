#include <source2root/extension.h>
#include <keels2/authoring.hpp>
#include <keels2/services.hpp>
#include <limits>

namespace {
using namespace keels2::authoring;

void (*during_native)() = nullptr;

class ExtensionFixture final : public Plugin {
public:
    static constexpr PluginInfo Info{"Source2Root Example", "tests", "1.0.0", "Test-only active native fixture"};
    std::vector<PluginDependency> Dependencies() const override {
        return {{"Source2Root", "1.0.0", DependencyRequirement::exact}};
    }

    bool Load() override {
        if (services.Connect(HostContext()) != KEEL_RESULT_OK)
            return false;

        const void* value = nullptr;

        if (HostContext().QueryService(SR_EXTENSION_SERVICE, 999, &value) != KEEL_RESULT_INCOMPATIBLE || value)
            return false;

        if (HostContext().QueryService(SR_EXTENSION_SERVICE, 1, &value) != KEEL_RESULT_OK)
            return false;

        api = static_cast<const SrExtensionApi*>(value);

        if (!api || api->size != sizeof(*api) || api->api_version != 1 || !api->register_native || !api->unregister_native)
            return false;

        SrNativeSpec wrong{};
        wrong.size = sizeof(wrong);
        wrong.api_version = 999;
        SrRegistration rejected = 0;

        if (api->register_native(api->context, HostContext().PluginHandle(), &wrong, &rejected) !=
                KEEL_RESULT_INCOMPATIBLE ||
            rejected)
            return false;

        if (services.Publish("source2root.example", 1, &marker, publication) != KEEL_RESULT_OK)
            return false;

        const SrNativeSpec spec{sizeof(spec), 1, "SR_ExampleAdd", 2, 0, "source2root.example", 1, &Add, this};
        return api->register_native(api->context, HostContext().PluginHandle(), &spec, &registration) == KEEL_RESULT_OK;
    }

    bool PrepareUnload() override {
        if (registration) {
            auto result = api->unregister_native(api->context, HostContext().PluginHandle(), registration);

            if (result != KEEL_RESULT_OK && result != KEEL_RESULT_NOT_FOUND)
                return false;

            registration = 0;
        }

        if (publication) {
            auto result = services.Withdraw(publication);

            if (result != KEEL_RESULT_OK && result != KEEL_RESULT_NOT_FOUND)
                return false;

            publication = 0;
        }

        const auto result = services.Release(SR_EXTENSION_SERVICE, 1);
        return result == KEEL_RESULT_OK || result == KEEL_RESULT_NOT_FOUND;
    }

private:
    keels2::services::Service services;
    const SrExtensionApi* api = nullptr;
    SrRegistration registration = 0;
    KeelServiceHandle publication = 0;
    unsigned marker = 1;
    static KeelResult Add(void*, const int32_t* arguments, uint32_t count, int32_t* result, char*, uint32_t) noexcept {
        if (count != 2 || !arguments || !result)
            return KEEL_RESULT_INVALID_ARGUMENT;

        if (during_native)
            during_native();

        const int64_t sum = static_cast<int64_t>(arguments[0]) + arguments[1];

        if (sum < std::numeric_limits<int32_t>::min() || sum > std::numeric_limits<int32_t>::max())
            return KEEL_RESULT_INVALID_ARGUMENT;

        *result = static_cast<int32_t>(sum);
        return KEEL_RESULT_OK;
    }
};
}

KEELS2_PLUGIN(ExtensionFixture)
extern "C" KEELS2_PLUGIN_EXPORT void SrFixtureDuringNative(void (*callback)()) {
    during_native = callback;
}
