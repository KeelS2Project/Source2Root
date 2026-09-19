#include <source2root/extension.h>
#include <keels2/authoring.hpp>
#include <keels2/services.hpp>

#include <limits>

namespace {
using namespace keels2::authoring;

class ExampleExtension final : public Plugin {
public:
    static constexpr PluginInfo Info{"Source2Root Example", "KeelS2Project", "1.0.0", "SourcePawn native example"};
    std::vector<PluginDependency> Dependencies() const override {
        return {{"Source2Root", "1.0.0", DependencyRequirement::exact}};
    }

    bool Load() override {
        if (services_.Connect(HostContext()) != KEEL_RESULT_OK)
            return false;

        const void* value = nullptr;

        if (HostContext().QueryService(SR_EXTENSION_SERVICE, SR_EXTENSION_API_VERSION, &value) != KEEL_RESULT_OK)
            return false;

        api_ = static_cast<const SrExtensionApi*>(value);

        if (!api_ || api_->size != sizeof(*api_) || api_->api_version != SR_EXTENSION_API_VERSION ||
            !api_->register_native || !api_->unregister_native || !api_->open_menu || !api_->close_menu)
            return false;

        if (services_.Publish("source2root.example", 1, &marker_, publication_) != KEEL_RESULT_OK)
            return false;

        const SrNativeSpec spec{sizeof(spec), SR_EXTENSION_API_VERSION, "SR_ExampleAdd", 2, 0,
                               "source2root.example", 1, &Add, this};

        if (!CreateCommand("sr_native_menu",
                           "Open the native menu example",
                           &ExampleExtension::OpenMenu,
                           FCVAR_GAMEDLL | FCVAR_CLIENT_CAN_EXECUTE))
            return false;

        if (api_->register_native(api_->context, HostContext().PluginHandle(), &spec, &registration_) != KEEL_RESULT_OK)
            return false;

        LogMessage("SR_ExampleAdd registered through Source2Root API 1");
        return true;
    }

    bool PrepareUnload() override {
        if (registration_) {
            auto result = api_->unregister_native(api_->context, HostContext().PluginHandle(), registration_);

            if (result != KEEL_RESULT_OK && result != KEEL_RESULT_NOT_FOUND)
                return false;

            registration_ = 0;
        }

        if (publication_) {
            auto result = services_.Withdraw(publication_);

            if (result != KEEL_RESULT_OK && result != KEEL_RESULT_NOT_FOUND)
                return false;

            publication_ = 0;
        }

        const auto result = services_.Release(SR_EXTENSION_SERVICE, SR_EXTENSION_API_VERSION);
        return result == KEEL_RESULT_OK || result == KEEL_RESULT_NOT_FOUND;
    }

    void Unload() override {
        api_ = nullptr;
    }

private:
    keels2::services::Service services_;
    const SrExtensionApi* api_ = nullptr;
    KeelServiceHandle publication_ = 0;
    SrRegistration registration_ = 0;
    unsigned marker_ = 1;
    void OpenMenu(const CCommandContext& context, const CCommand&) {
        PlayerInfo player;

        if (!GetPlayer(context.GetPlayerSlot(), player))
            return;

        const SrMenuItem items[] = {{"Native selection", KEEL_TRUE}, {"Disabled action", KEEL_FALSE},
            {"Third item", KEEL_TRUE}, {"Fourth item", KEEL_TRUE}, {"Second page", KEEL_TRUE}};

        const SrMenuSpec spec{sizeof(spec), SR_EXTENSION_API_VERSION, "Native Source2Root menu", "demo.hello",
            items, 5, 30000, &Selected, this, "source2root.example", 1};

        const KeelPlayerConnection connection{player.slot.Get(), 0, player.connection};
        SrMenuSession session = 0;
        auto result = api_->open_menu(api_->context, HostContext().PluginHandle(), &connection, &spec, &session);

        if (result != KEEL_RESULT_OK)
            PrintToConsole(player.slot, "Source2Root menu unavailable or permission denied.");
    }

    static void Selected(void* data, const KeelPlayerConnection* connection, int32_t) noexcept {
        try {
            auto& self = *static_cast<ExampleExtension*>(data);
            PlayerInfo player;

            if (self.GetPlayer(PlayerConnection{CPlayerSlot(connection->slot), connection->generation}, player))
                self.PrintToChat(player.slot, "The menu selection ran in the C++ extension.");
        } catch (...) {}
    }

    static KeelResult Add(void*, const int32_t* arguments, uint32_t count, int32_t* result, char*, uint32_t) noexcept {
        if (!arguments || count != 2 || !result)
            return KEEL_RESULT_INVALID_ARGUMENT;

        const auto sum = static_cast<int64_t>(arguments[0]) + arguments[1];

        if (sum < std::numeric_limits<int32_t>::min() || sum > std::numeric_limits<int32_t>::max())
            return KEEL_RESULT_INVALID_ARGUMENT;

        *result = static_cast<int32_t>(sum);
        return KEEL_RESULT_OK;
    }
};
}

KEELS2_PLUGIN(ExampleExtension)
