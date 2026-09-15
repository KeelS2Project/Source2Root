#pragma once

#include <source2root/extension.h>
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
        if (api_ || publication_ || queried_) return false;
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
            if (services_.Publish(service_.c_str(), version_, &marker_, publication_) != KEEL_RESULT_OK)
                return SetupFailed("Could not publish the extension's provider service.");
            for (auto& binding : bindings_) {
                const SrNativeSpec spec{sizeof(spec), SR_EXTENSION_API_VERSION, binding->name.c_str(), binding->count, 0,
                    service_.c_str(), version_, &Binding::Invoke, binding.get()};
                if (api_->register_native(api_->context, HostContext().PluginHandle(), &spec, &binding->registration) != KEEL_RESULT_OK) {
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

private:
    struct Binding {
        Extension* extension = nullptr;
        std::string name;
        std::uint32_t count = 0;
        SrRegistration registration = 0;
        std::function<std::int32_t(const std::int32_t*)> call;

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
        if (queried_) {
            const auto result = services_.Release(SR_EXTENSION_SERVICE, SR_EXTENSION_API_VERSION);
            if (result != KEEL_RESULT_OK && result != KEEL_RESULT_NOT_FOUND) return false;
            queried_ = false;
        }
        ready_ = false;
        api_ = nullptr;
        bindings_.clear();
        return true;
    }

    const std::string service_;
    const std::uint32_t version_;
    std::uint32_t marker_ = 1;
    bool staging_ = false, ready_ = false, queried_ = false;
    keels2::services::Service services_;
    const SrExtensionApi* api_ = nullptr;
    KeelServiceHandle publication_ = 0;
    std::vector<std::unique_ptr<Binding>> bindings_;
};

}
