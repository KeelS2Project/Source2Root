#include "foundation.h"

#include <algorithm>
#include <cstring>

namespace sr {

struct Foundation::NativeInvocation {
    Foundation& foundation;
    Script& script;
    Provider& provider;
    const Arguments& arguments;
    char error[512]{};

    template <typename Function>
    static KeelResult Guard(void* raw, Function function) noexcept {
        if (!raw) return KEEL_RESULT_INVALID_ARGUMENT;
        auto& call = *static_cast<NativeInvocation*>(raw);
        try {
            call.foundation.Thread();
            call.error[0] = 0;
            function(call);
            return KEEL_RESULT_OK;
        } catch (const std::exception& failure) {
            const auto length = std::min(std::strlen(failure.what()), sizeof(call.error) - 1);
            std::memcpy(call.error, failure.what(), length);
            call.error[length] = 0;
            return KEEL_RESULT_INVALID_ARGUMENT;
        } catch (...) {
            std::strcpy(call.error, "Native call operation failed.");
            return KEEL_RESULT_ENGINE_FAILURE;
        }
    }

    static void Copy(const std::string& text, char* output, std::uint32_t capacity) {
        if (!output || !capacity || capacity > SR_NATIVE_BUFFER_LIMIT || text.size() >= capacity)
            throw NativeError("Output buffer is too small or invalid.");
        std::memcpy(output, text.c_str(), text.size() + 1);
    }

    ExtensionResource& Resource(Cell handle, std::uint32_t type) {
        auto& resource = std::get<ExtensionResource>(foundation.handles_.Get(handle, script.owner, ExtensionType));
        if (!type || resource.type != type || resource.provider != provider.owner)
            throw NativeError("Foreign extension or wrong resource type.");
        return resource;
    }

    SrNativeCall Api() {
        return {sizeof(SrNativeCall), SR_NATIVE_API_VERSION, this, script.owner,
            static_cast<std::uint32_t>(arguments.Count()), error,
            [](void* raw, std::uint32_t index, Cell* value) {
                return Guard(raw, [&](auto& call) {
                    if (!value) throw NativeError("Missing cell output.");
                    *value = call.arguments.Int(index);
                });
            },
            [](void* raw, std::uint32_t index, char* output, std::uint32_t capacity) {
                return Guard(raw, [&](auto& call) { Copy(call.arguments.String(index, SR_NATIVE_BUFFER_LIMIT - 1), output, capacity); });
            },
            [](void* raw, std::uint32_t index, Cell* output, std::uint32_t count) {
                return Guard(raw, [&](auto& call) {
                    if (count > 1024 || (count && !output)) throw NativeError("Invalid array output.");
                    auto values = call.arguments.Array(index, count);
                    if (count) std::copy(values.begin(), values.end(), output);
                });
            },
            [](void* raw, std::uint32_t index, std::uint32_t capacity, const char* value) {
                return Guard(raw, [&](auto& call) {
                    if (!value || !capacity || capacity > SR_NATIVE_BUFFER_LIMIT) throw NativeError("Invalid string output.");
                    call.arguments.Output(index, capacity, value);
                });
            },
            [](void* raw, std::uint32_t index, Cell value) {
                return Guard(raw, [&](auto& call) { call.arguments.OutputCell(index, value); });
            },
            [](void* raw, std::uint32_t index, std::uint32_t capacity, const Cell* values, std::uint32_t count) {
                return Guard(raw, [&](auto& call) {
                    if (count > 1024 || capacity > 1024 || (count && !values)) throw NativeError("Invalid array input.");
                    std::vector<Cell> copy;
                    if (count) copy.assign(values, values + count);
                    call.arguments.OutputArray(index, capacity, copy);
                });
            },
            [](void* raw, KeelBool shared, char* output, std::uint32_t capacity) {
                return Guard(raw, [&](auto& call) {
                    if (shared > 1) throw NativeError("Invalid data path scope.");
                    const auto path = shared ? call.foundation.root_ / "data/extensions" / call.provider.service :
                        call.foundation.root_ / "data" / call.script.manifest.id;
                    std::filesystem::create_directories(path);
                    Copy(path.string(), output, capacity);
                });
            },
            [](void* raw, std::uint32_t type, void* value, SrResourceDestroy destroy, Cell* handle) {
                if (handle) *handle = 0;
                return Guard(raw, [&](auto& call) {
                    if (!type || !value || !destroy || !handle) throw NativeError("Invalid resource.");
                    call.foundation.Limit(call.script);
                    auto holder = std::make_shared<ExtensionValue>();
                    holder->data = value;
                    holder->destroy = destroy;
                    *handle = call.foundation.handles_.Add(call.script.owner, ExtensionType,
                        ExtensionResource{call.provider.owner, type, holder});
                    holder->owned = true;
                });
            },
            [](void* raw, Cell handle, std::uint32_t type, void** value) {
                if (value) *value = nullptr;
                return Guard(raw, [&](auto& call) {
                    if (!value) throw NativeError("Missing resource output.");
                    *value = call.Resource(handle, type).value->data;
                });
            },
            [](void* raw, Cell handle, std::uint32_t type) {
                return Guard(raw, [&](auto& call) {
                    call.Resource(handle, type);
                    call.foundation.handles_.Remove(handle, call.script.owner, ExtensionType);
                });
            },
            [](void* raw, const char* message) {
                return Guard(raw, [&](auto& call) {
                    if (!message) throw NativeError("Missing error message.");
                    call.script.error = std::string(message).substr(0, SR_NATIVE_BUFFER_LIMIT - 1);
                });
            },
            [](void* raw, std::uint32_t index, SrCallback* token) {
                if (token) *token = 0;
                return Guard(raw, [&](auto& call) {
                    if (!token) throw NativeError("Missing callback output.");
                    auto* function = call.arguments.Callback(index);
                    call.foundation.Limit(call.script);
                    if (!call.foundation.next_callback_) throw NativeError("Callback token space exhausted.");
                    const auto id = call.foundation.next_callback_++;
                    call.script.callbacks.insert(id);
                    try {
                        call.foundation.callbacks_.emplace(id, ExtensionCallback{&call.script, call.provider.owner, function});
                    } catch (...) { call.script.callbacks.erase(id); throw; }
                    *token = id;
                });
            },
            [](void* raw, char* output, std::uint32_t capacity) {
                return Guard(raw, [&](auto& call) {
                    Copy((call.foundation.root_ / "configs/extensions" / call.provider.service).string(), output, capacity);
                });
            },
            [](void* raw, char* output, std::uint32_t capacity) {
                return Guard(raw, [&](auto& call) { Copy(call.script.manifest.id, output, capacity); });
            },
            [](void* raw, Cell handle, SrPlayerIdentity* output) {
                if (!output || output->size != sizeof(*output)) return KEEL_RESULT_INVALID_ARGUMENT;
                *output = {sizeof(*output), -1, 0, 0, KEEL_FALSE, KEEL_FALSE};
                KeelResult resolved = KEEL_RESULT_NOT_FOUND;
                const auto guarded = Guard(raw, [&](auto& call) {
                    Player player;
                    resolved = call.foundation.ResolvePlayer(call.script, handle, player);
                    if (resolved == KEEL_RESULT_OK) *output = {sizeof(*output), player.slot, player.connection,
                        player.steam_id, player.authenticated ? KEEL_TRUE : KEEL_FALSE, player.bot ? KEEL_TRUE : KEEL_FALSE};
                });
                return guarded == KEEL_RESULT_OK ? resolved : guarded;
            }};
    }
};

KeelResult Foundation::NativePlayerSnapshot(SrPlayerIdentity* output, std::uint32_t capacity, std::uint32_t* count) {
    Thread();
    if (count) *count = 0;
    if (!output || !count || !capacity || capacity > 128) return KEEL_RESULT_INVALID_ARGUMENT;
    std::vector<SrPlayerIdentity> players;
    int after = -1;
    for (;;) {
        Player candidate, current;
        const auto next = host_.NextPlayer(after, candidate);
        if (next == KEEL_RESULT_NOT_FOUND) break;
        if (next != KEEL_RESULT_OK) return next;
        if (candidate.slot <= after || candidate.slot >= 128 || !candidate.connection || players.size() == capacity)
            return KEEL_RESULT_ENGINE_FAILURE;
        after = candidate.slot;
        const auto lookup = host_.Lookup(candidate.slot, current);
        if (lookup == KEEL_RESULT_NOT_FOUND || (lookup == KEEL_RESULT_OK && !current.SameConnection(candidate))) continue;
        if (lookup != KEEL_RESULT_OK) return lookup;
        players.push_back({sizeof(SrPlayerIdentity), current.slot, current.connection, current.steam_id,
            current.authenticated ? KEEL_TRUE : KEEL_FALSE, current.bot ? KEEL_TRUE : KEEL_FALSE});
    }
    std::copy(players.begin(), players.end(), output);
    *count = static_cast<std::uint32_t>(players.size());
    return KEEL_RESULT_OK;
}

Cell Foundation::InvokeContextNative(Script& script, Provider& provider, const Arguments& arguments) {
    NativeInvocation invocation{*this, script, provider, arguments};
    const auto call = invocation.Api();
    Cell result = 0;
    char error[512]{};
    ++provider.active;
    KeelResult status;
    try { status = provider.context_invoke(provider.user_data, &call, &result, error, sizeof(error)); }
    catch (...) {
        --provider.active;
        throw NativeError("Native extension threw across its ABI boundary.");
    }
    --provider.active;
    error[sizeof(error) - 1] = 0;
    if (status != KEEL_RESULT_OK) throw NativeError("Extension native failed: " + std::string(error));
    return result;
}

KeelResult Foundation::RegisterContextNative(KeelPluginHandle owner, const SrContextNativeSpec& spec,
    SrRegistration& registration) {
    Thread();
    registration = 0;
    if (spec.size != sizeof(spec) || spec.api_version != SR_NATIVE_API_VERSION) return KEEL_RESULT_INCOMPATIBLE;
    if (!spec.invoke || !spec.provider_service) return KEEL_RESULT_INVALID_ARGUMENT;
    const std::string_view service(spec.provider_service);
    if (service.empty() || service.size() > 96 || service == "." || service == ".." ||
        !std::all_of(service.begin(), service.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        })) return KEEL_RESULT_INVALID_ARGUMENT;
    for (const auto& [id, provider] : providers_)
        if (provider.service == service && provider.owner != owner) return KEEL_RESULT_INVALID_ARGUMENT;
    const SrNativeSpec legacy{sizeof(legacy), SR_EXTENSION_API_VERSION, spec.name, spec.argument_count, spec.reserved,
        spec.provider_service, spec.provider_version,
        [](void*, const Cell*, std::uint32_t, Cell*, char*, std::uint32_t) { return KEEL_RESULT_UNSUPPORTED; }, spec.user_data};
    const auto result = RegisterNative(owner, legacy, registration);
    if (result == KEEL_RESULT_OK) providers_.at(registration).context_invoke = spec.invoke;
    return result;
}

KeelResult Foundation::CancelCallback(KeelPluginHandle owner, SrCallback token) {
    Thread();
    if (!owner || !token) return KEEL_RESULT_INVALID_ARGUMENT;
    const auto found = callbacks_.find(token);
    if (found == callbacks_.end()) return KEEL_RESULT_NOT_FOUND;
    if (found->second.provider != owner) return KEEL_RESULT_INVALID_ARGUMENT;
    found->second.script->callbacks.erase(token);
    callbacks_.erase(found);
    return KEEL_RESULT_OK;
}

KeelResult Foundation::DeliverCallback(KeelPluginHandle owner, SrCallback token,
    const Cell* cells, std::uint32_t count, const char* text) {
    Thread();
    if (!owner || !token || count > 16 || (count && !cells) || (text && count == 16))
        return KEEL_RESULT_INVALID_ARGUMENT;
    if (text) {
        std::size_t length = 0;
        while (length < SR_NATIVE_BUFFER_LIMIT && text[length]) ++length;
        if (length == SR_NATIVE_BUFFER_LIMIT) return KEEL_RESULT_INVALID_ARGUMENT;
    }
    const auto found = callbacks_.find(token);
    if (found == callbacks_.end()) return KEEL_RESULT_NOT_FOUND;
    const auto callback = found->second;
    if (callback.provider != owner) return KEEL_RESULT_INVALID_ARGUMENT;
    auto& script = *callback.script;
    if (script.state != PluginState::Running && script.state != PluginState::Loading && script.state != PluginState::Paused) {
        CancelCallback(owner, token);
        return KEEL_RESULT_NOT_FOUND;
    }
    if (managing_ || !runtime_.Idle() || script.state != PluginState::Running) return KEEL_RESULT_BUSY;
    std::vector<Cell> arguments;
    if (count) arguments.assign(cells, cells + count);
    CancelCallback(owner, token);
    return Invoke(script, callback.function, arguments, text) ? KEEL_RESULT_OK : KEEL_RESULT_ENGINE_FAILURE;
}

}
