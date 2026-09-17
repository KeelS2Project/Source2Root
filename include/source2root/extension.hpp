#pragma once

#include <source2root/extension.h>
#include <source2root/native.hpp>
#include <keels2/authoring.hpp>
#include <keels2/services.hpp>
#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace source2root {

class Extension : public keels2::Plugin {
public:
    bool Load() final {
        if (api_ || native_api_ || publication_ || queried_ || native_queried_) return false;
        ready_ = false;
        bindings_.clear();
        try {
            staging_ = true;
            const bool prepared = OnExtensionStart();
            staging_ = false;
            if (!prepared) { bindings_.clear(); return false; }
            const void* raw = nullptr;
            if (services_.Connect(HostContext()) != KEEL_RESULT_OK ||
                HostContext().QueryService(SR_EXTENSION_SERVICE, SR_EXTENSION_API_VERSION, &raw) != KEEL_RESULT_OK)
                return SetupFailed("Source2Root extension service is unavailable.");
            queried_ = true;
            api_ = static_cast<const SrExtensionApi*>(raw);
            if (!api_ || api_->size != sizeof(*api_) || api_->api_version != SR_EXTENSION_API_VERSION ||
                !api_->register_native || !api_->unregister_native) {
                api_ = nullptr;
                return SetupFailed("Source2Root extension service is incompatible.");
            }
            if (std::any_of(bindings_.begin(), bindings_.end(), [](const auto& binding) { return bool(binding->context_call); })) {
                raw = nullptr;
                if (HostContext().QueryService(SR_NATIVE_SERVICE, SR_NATIVE_API_VERSION, &raw) != KEEL_RESULT_OK)
                    return SetupFailed("Source2Root native call service is unavailable.");
                native_queried_ = true;
                native_api_ = static_cast<const SrNativeApi*>(raw);
                if (!native_api_ || native_api_->size != sizeof(*native_api_) || native_api_->api_version != SR_NATIVE_API_VERSION ||
                    !native_api_->register_native || !native_api_->unregister_native ||
                    !native_api_->deliver_callback || !native_api_->cancel_callback)
                    return SetupFailed("Source2Root native call service is incompatible.");
            }
            if (services_.Publish(service_.c_str(), version_, &marker_, publication_) != KEEL_RESULT_OK)
                return SetupFailed("Could not publish the extension's provider service.");
            for (auto& binding : bindings_) {
                KeelResult registered;
                if (binding->context_call) {
                    const SrContextNativeSpec spec{sizeof(spec), SR_NATIVE_API_VERSION, binding->name.c_str(), binding->count, 0,
                        service_.c_str(), version_, &Binding::InvokeContext, binding.get()};
                    registered = native_api_->register_native(native_api_->context, HostContext().PluginHandle(), &spec, &binding->registration);
                } else {
                    const SrNativeSpec spec{sizeof(spec), SR_EXTENSION_API_VERSION, binding->name.c_str(), binding->count, 0,
                        service_.c_str(), version_, &Binding::Invoke, binding.get()};
                    registered = api_->register_native(api_->context, HostContext().PluginHandle(), &spec, &binding->registration);
                }
                if (registered != KEEL_RESULT_OK) {
                    LogError("Could not register extension native: {}", binding->name);
                    return SetupFailed("Extension native registration failed.");
                }
            }
            ready_ = true;
            return true;
        } catch (const std::exception& error) { return SetupFailed(error.what()); }
        catch (...) { return SetupFailed("Extension initialization threw an exception."); }
    }

    bool PrepareUnload() final { return PrepareExtensionUnload() && Release(); }

protected:
    explicit Extension(const char* provider_service, std::uint32_t version = 1)
        : service_(provider_service ? provider_service : ""), version_(version) {}
    virtual bool OnExtensionStart() = 0;
    virtual bool PrepareExtensionUnload() { return true; }

    KeelResult DeliverCallback(SrCallback callback, const std::vector<std::int32_t>& cells = {},
        const char* text = nullptr) {
        if (!native_api_) return KEEL_RESULT_NOT_READY;
        return native_api_->deliver_callback(native_api_->context, HostContext().PluginHandle(), callback,
            cells.data(), static_cast<std::uint32_t>(cells.size()), text);
    }
    KeelResult CancelCallback(SrCallback callback) {
        if (!native_api_) return KEEL_RESULT_NOT_READY;
        return native_api_->cancel_callback(native_api_->context, HostContext().PluginHandle(), callback);
    }
    KeelResult PlayerSnapshot(std::vector<SrPlayerIdentity>& players) {
        players.clear();
        if (!native_api_ || !native_api_->player_snapshot) return KEEL_RESULT_NOT_READY;
        std::array<SrPlayerIdentity, 128> snapshot{};
        std::uint32_t count = 0;
        const auto result = native_api_->player_snapshot(native_api_->context, snapshot.data(), snapshot.size(), &count);
        if (result == KEEL_RESULT_OK && count <= snapshot.size()) players.assign(snapshot.begin(), snapshot.begin() + count);
        else if (result == KEEL_RESULT_OK) return KEEL_RESULT_ENGINE_FAILURE;
        return result;
    }
    KeelResult OpenNativeMenu(const KeelPlayerConnection& player, SrMenuSpec spec, SrMenuSession& session) {
        session = 0;
        if (!api_ || !api_->open_menu) return KEEL_RESULT_NOT_READY;
        spec.provider_service = service_.c_str(); spec.provider_version = version_;
        return api_->open_menu(api_->context, HostContext().PluginHandle(), &player, &spec, &session);
    }
    KeelResult CloseNativeMenu(SrMenuSession session) {
        if (!api_ || !api_->close_menu) return KEEL_RESULT_NOT_READY;
        return api_->close_menu(api_->context, HostContext().PluginHandle(), session);
    }
    KeelResult NativeMenuStatus(SrMenuSession session) {
        if (!native_api_ || !native_api_->menu_status) return KEEL_RESULT_NOT_READY;
        return native_api_->menu_status(native_api_->context, HostContext().PluginHandle(), session);
    }
    KeelResult ConsumerStatus(std::uint64_t owner) {
        if (!native_api_ || !native_api_->consumer_status) return KEEL_RESULT_NOT_READY;
        return native_api_->consumer_status(native_api_->context, HostContext().PluginHandle(), owner);
    }

    template <typename Owner, typename... Arguments>
    bool RegisterNative(const char* name, std::int32_t (Owner::*callback)(Arguments...)) {
        static_assert(std::is_base_of_v<Extension, Owner>, "native callbacks belong to the extension");
        static_assert((std::is_same_v<Arguments, std::int32_t> && ...), "native arguments must be int32_t values");
        static_assert(sizeof...(Arguments) <= 16, "extension API 1 supports at most 16 arguments");
        if (!staging_ || !name || !*name || !callback) return false;
        for (const auto& binding : bindings_) if (binding->name == name) return false;
        auto binding = std::make_unique<Binding>();
        binding->extension = this;
        binding->name = name;
        binding->count = sizeof...(Arguments);
        auto* owner = dynamic_cast<Owner*>(this);
        if (!owner) return false;
        binding->call = [owner, callback](const std::int32_t* values) {
            return [&]<std::size_t... Indices>(std::index_sequence<Indices...>) {
                return (owner->*callback)(values[Indices]...);
            }(std::index_sequence_for<Arguments...>{});
        };
        bindings_.push_back(std::move(binding));
        return true;
    }

    template <typename Owner>
    bool RegisterNative(const char* name, std::uint32_t count, std::int32_t (Owner::*callback)(NativeCall&)) {
        static_assert(std::is_base_of_v<Extension, Owner>);
        if (!staging_ || !name || !*name || !callback || count > 16) return false;
        for (const auto& binding : bindings_) if (binding->name == name) return false;
        auto* owner = dynamic_cast<Owner*>(this);
        if (!owner) return false;
        auto binding = std::make_unique<Binding>();
        binding->extension = this;
        binding->name = name;
        binding->count = count;
        binding->context_call = [owner, callback](NativeCall& call) { return (owner->*callback)(call); };
        bindings_.push_back(std::move(binding));
        return true;
    }

private:
    struct Binding {
        Extension* extension = nullptr;
        std::string name;
        std::uint32_t count = 0;
        SrRegistration registration = 0;
        std::function<std::int32_t(const std::int32_t*)> call;
        std::function<std::int32_t(NativeCall&)> context_call;

        static KeelResult InvokeContext(void* data, const SrNativeCall* call, std::int32_t* result,
            char* error, std::uint32_t capacity) noexcept {
            if (result) *result = 0;
            if (error && capacity) error[0] = 0;
            const auto fail = [&](KeelResult status, const char* message) {
                if (error && capacity) {
                    const auto length = std::min(std::strlen(message), static_cast<std::size_t>(capacity - 1));
                    std::memcpy(error, message, length); error[length] = 0;
                }
                return status;
            };
            auto* binding = static_cast<Binding*>(data);
            if (!binding || !result || !call || call->size != sizeof(*call) || call->api_version != SR_NATIVE_API_VERSION ||
                call->argument_count != binding->count || !binding->context_call)
                return fail(KEEL_RESULT_INVALID_ARGUMENT, "Invalid native call.");
            if (!binding->extension->ready_) return fail(KEEL_RESULT_NOT_READY, "Extension initialization is incomplete.");
            try { NativeCall context(*call); *result = binding->context_call(context); return KEEL_RESULT_OK; }
            catch (const std::invalid_argument& exception) { return fail(KEEL_RESULT_INVALID_ARGUMENT, exception.what()); }
            catch (const std::exception& exception) { return fail(KEEL_RESULT_ENGINE_FAILURE, exception.what()); }
            catch (...) { return fail(KEEL_RESULT_ENGINE_FAILURE, "Native callback threw an exception."); }
        }

        static KeelResult Invoke(void* data, const std::int32_t* arguments, std::uint32_t count,
            std::int32_t* result, char* error, std::uint32_t capacity) noexcept {
            if (result) *result = 0;
            if (error && capacity) error[0] = 0;
            const auto fail = [&](KeelResult status, const char* message) {
                if (error && capacity) {
                    const auto length = std::min(std::strlen(message), static_cast<std::size_t>(capacity - 1));
                    std::memcpy(error, message, length); error[length] = 0;
                }
                return status;
            };
            auto* binding = static_cast<Binding*>(data);
            if (!binding || !result || count != binding->count || (count && !arguments))
                return fail(KEEL_RESULT_INVALID_ARGUMENT, "Invalid native arguments.");
            if (!binding->extension->ready_) return fail(KEEL_RESULT_NOT_READY, "Extension initialization is incomplete.");
            try { *result = binding->call(arguments); return KEEL_RESULT_OK; }
            catch (const std::invalid_argument& exception) { return fail(KEEL_RESULT_INVALID_ARGUMENT, exception.what()); }
            catch (const std::exception& exception) { return fail(KEEL_RESULT_ENGINE_FAILURE, exception.what()); }
            catch (...) { return fail(KEEL_RESULT_ENGINE_FAILURE, "Native callback threw an exception."); }
        }
    };

    bool SetupFailed(const char* message) noexcept {
        staging_ = false;
        ready_ = false;
        LogError("{}", message);
        if (Release()) return false;
        LogError("Extension setup cleanup is incomplete; natives are disabled. Unload the extension to retry cleanup.");
        return true;
    }

    bool Release() noexcept {
        try { return ReleaseResources(); }
        catch (...) { return false; }
    }

    bool ReleaseResources() {
        for (auto& binding : bindings_) if (binding->registration) {
            if (!api_) return false;
            const auto result = api_->unregister_native(api_->context, HostContext().PluginHandle(), binding->registration);
            if (result != KEEL_RESULT_OK && result != KEEL_RESULT_NOT_FOUND) return false;
            binding->registration = 0;
        }
        if (publication_) {
            const auto result = services_.Withdraw(publication_);
            if (result != KEEL_RESULT_OK && result != KEEL_RESULT_NOT_FOUND) return false;
            publication_ = 0;
        }
        if (native_queried_) {
            const auto result = services_.Release(SR_NATIVE_SERVICE, SR_NATIVE_API_VERSION);
            if (result != KEEL_RESULT_OK && result != KEEL_RESULT_NOT_FOUND) return false;
            native_queried_ = false;
        }
        if (queried_) {
            const auto result = services_.Release(SR_EXTENSION_SERVICE, SR_EXTENSION_API_VERSION);
            if (result != KEEL_RESULT_OK && result != KEEL_RESULT_NOT_FOUND) return false;
            queried_ = false;
        }
        ready_ = false;
        api_ = nullptr;
        native_api_ = nullptr;
        bindings_.clear();
        return true;
    }

    const std::string service_;
    const std::uint32_t version_;
    std::uint32_t marker_ = 1;
    bool staging_ = false, ready_ = false, queried_ = false, native_queried_ = false;
    keels2::services::Service services_;
    const SrExtensionApi* api_ = nullptr;
    const SrNativeApi* native_api_ = nullptr;
    KeelServiceHandle publication_ = 0;
    std::vector<std::unique_ptr<Binding>> bindings_;
};

}
