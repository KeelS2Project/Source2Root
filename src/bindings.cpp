#include "foundation.h"
#include "core_native_names.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace sr {

void Foundation::Bind(Script& script) {
    BindConVars(script);
    BindPlayers(script);
    BindText(script);
    BindCommandMenus(script);
    BindBans(script);
    BindCommunications(script);
    BindServer(script);
    auto bind = [&](const char* name, int count, auto callback, int maximum = -1) {
        runtime_.Bind(*script.vm, name, count, [&, callback](const Arguments& args) -> Cell {
            Thread();
            return callback(args);
        }, maximum);
    };
    bind("RegisterCommand", 3, [&](const Arguments& args) {
        return RegisterCommand(script, args.String(1, 64), args.String(2, 96), args.Callback(3), args.Count() == 4 ? args.String(4, 256) : "");
    }, 4);
    bind("ListenEvent", 2, [&](const Arguments& args) {
        return ListenEvent(script, args.String(1, 64), args.Callback(2));
    });
    bind("LogMessage", 1, [&](const Arguments& args) {
        host_.Log(script.manifest.id + ": " + args.String(1, 2048));
        return 0;
    });
    bind("GetLastError", 2, [&](const Arguments& args) {
        const auto capacity = args.Int(2);
        if (capacity < 1 || capacity > 4096) throw NativeError("output capacity out of bounds");
        args.Output(1, capacity, script.error.substr(0, capacity - 1));
        return 1;
    });
    bind("GetDataPath", 2, [&](const Arguments& args) {
        const auto path = root_ / "data" / script.manifest.id;
        std::filesystem::create_directories(path);
        args.Output(1, args.Int(2), path.string());
        return 1;
    });
    bind("ReplyToCommand", 2, [&](const Arguments& args) {
        const auto text = args.String(2);
        Player player;
        if (args.Int(1) && ResolvePlayer(script, args.Int(1), player) != KEEL_RESULT_OK) return 0;
        const auto result = host_.Reply(args.Int(1) ? &player : nullptr, text);
        if (result != KEEL_RESULT_OK) script.error = "reply failed (KeelResult " + std::to_string(result) + ")";
        return result == KEEL_RESULT_OK ? 1 : 0;
    });
    bind("ShowActivity", 2, [&](const Arguments& args) {
        return ShowActivity(script, args.Int(1), args.String(2, 256), args.Count() == 3 ? args.String(3, 512) : "");
    }, 3);
    bind("GetPlayerName", 3, [&](const Arguments& args) {
        Player player;
        if (ResolvePlayer(script, args.Int(1), player) != KEEL_RESULT_OK) return 0;
        args.Output(2, args.Int(3), player.name);
        return 1;
    });
    bind("GetPlayerSteamID", 3, [&](const Arguments& args) {
        args.Output(2, args.Int(3), "");
        Player player;
        if (ResolvePlayer(script, args.Int(1), player) != KEEL_RESULT_OK || !player.authenticated ||
            !ValidSteamIdentity(player.steam_id) || player.bot) {
            script.error = "authenticated individual Steam identity unavailable";
            return 0;
        }
        const auto format = args.Count() == 4 ? args.Int(4) : 0;
        const auto account = static_cast<std::uint32_t>(player.steam_id);
        if (format < 0 || format > 2) throw NativeError("invalid Steam identity format");
        const auto identity = format == 1 ? "STEAM_1:" + std::to_string(account % 2) + ":" + std::to_string(account / 2) :
            format == 2 ? "[U:1:" + std::to_string(account) + "]" : std::to_string(player.steam_id);
        args.Output(2, args.Int(3), identity);
        return 1;
    }, 4);
    bind("GetPlayerHealth", 2, [&](const Arguments& args) {
        args.OutputCell(2, 0);
        Player player;
        if (ResolvePlayer(script, args.Int(1), player) != KEEL_RESULT_OK) return 0;
        Cell health = 0;
        const auto result = host_.PlayerHealth(player, health);
        if (result != KEEL_RESULT_OK) {
            script.error = "entity health read failed (KeelResult " + std::to_string(result) + ")";
            return 0;
        }
        args.OutputCell(2, health);
        return 1;
    });
    bind("HasPermission", 2, [&](const Arguments& args) {
        return Allowed(script, args.Int(1), args.String(2, 96));
    });
    bind("IsAdmin", 1, [&](const Arguments& args) {
        Player player;
        return args.Int(1) && ResolvePlayer(script, args.Int(1), player) == KEEL_RESULT_OK && permissions_.Find(player);
    });
    bind("GetAdminGroup", 3, [&](const Arguments& args) {
        args.Output(2, args.Int(3), "");
        Player player;
        if (!args.Int(1) || ResolvePlayer(script, args.Int(1), player) != KEEL_RESULT_OK) return 0;
        const auto* admin = permissions_.Find(player);
        if (!admin) return 0;
        args.Output(2, args.Int(3), admin->group);
        return 1;
    });
    bind("GetAdminImmunity", 2, [&](const Arguments& args) {
        args.OutputCell(2, 0);
        Player player;
        if (!args.Int(1) || ResolvePlayer(script, args.Int(1), player) != KEEL_RESULT_OK) return 0;
        const auto* admin = permissions_.Find(player);
        if (!admin) return 0;
        args.OutputCell(2, admin->immunity);
        return 1;
    });
    bind("CanTarget", 3, [&](const Arguments& args) {
        const auto permission = args.String(3, 96);
        if (!ValidPermission(permission)) throw NativeError("invalid permission name");
        Player caller, target;
        if (!args.Int(2)) { script.error = "A player target is required."; return 0; }
        if ((args.Int(1) && ResolvePlayer(script, args.Int(1), caller) != KEEL_RESULT_OK) ||
            ResolvePlayer(script, args.Int(2), target) != KEEL_RESULT_OK) return 0;
        if (args.Int(1) && !permissions_.Allows(caller, permission)) {
            script.error = "You do not have access to this command."; return 0;
        }
        if (!permissions_.CanTarget(args.Int(1) ? &caller : nullptr, target, permission)) {
            script.error = !target.authenticated && !target.bot ? "Target identity is not ready." :
                "You cannot target a player with equal or higher immunity.";
            return 0;
        }
        return 1;
    });
    bind("CanTargetIdentity", 3, [&](const Arguments& args) {
        const auto permission = args.String(3, 96);
        if (!ValidPermission(permission)) throw NativeError("invalid permission name");
        std::uint64_t target = 0;
        try { target = ParseSteamIdentity(args.String(2, 64)); }
        catch (const std::exception&) { script.error = "Invalid Steam identity."; return 0; }
        Player caller;
        if (args.Int(1) && ResolvePlayer(script, args.Int(1), caller) != KEEL_RESULT_OK) return 0;
        if (args.Int(1) && !permissions_.Allows(caller, permission)) {
            script.error = "You do not have access to this command."; return 0;
        }
        if (!permissions_.CanTargetIdentity(args.Int(1) ? &caller : nullptr, target, permission)) {
            script.error = "You cannot target an account with equal or higher immunity."; return 0;
        }
        return 1;
    });
    bind("ReloadAdmins", 1, [&](const Arguments& args) {
        if (!Allowed(script, args.Int(1), "admin.reloadadmins")) return 0;
        try { ReloadPermissions(); return 1; }
        catch (const std::exception& error) {
            script.error = "Could not reload administrators: " + std::string(error.what());
            host_.Log(script.error);
            return 0;
        }
    });
    bind("CreateTimer", 3, [&](const Arguments& args) {
        Limit(script);
        const auto milliseconds = args.Int(1);
        if (milliseconds < 1 || milliseconds > 3600000) throw NativeError("timer delay must be 1..3600000 milliseconds");
        auto* callback = args.Callback(2);
        Player player;
        if (args.Int(3) && ResolvePlayer(script, args.Int(3), player) != KEEL_RESULT_OK) return Cell{0};
        const auto across_maps = args.Count() == 4 ? args.Int(4) : 0;
        if ((across_maps != 0 && across_maps != 1) || (across_maps && args.Int(3)))
            throw NativeError("timers across maps require NoPlayer and a boolean flag");
        return handles_.Add(script.owner, TimerType, Timer{now_ + std::chrono::milliseconds(milliseconds), callback, args.Int(3), across_maps == 1});
    }, 4);
    bind("CancelTimer", 1, [&](const Arguments& args) {
        handles_.Remove(args.Int(1), script.owner, TimerType);
        return 1;
    });
    bind("CreateMenu", 3, [&](const Arguments& args) {
        Limit(script);
        auto title = args.String(1, 96);
        auto permission = args.String(2, 96);
        if (title.empty() || !ValidPermission(permission)) throw NativeError("invalid menu title or permission");
        return handles_.Add(script.owner, MenuType,
            ScriptMenu{Menu{std::move(title), std::move(permission), {}}, args.Callback(3),
                args.Count() == 4 ? args.Callback(4, true) : nullptr});
    }, 4);
    bind("AddMenuItem", 3, [&](const Arguments& args) {
        auto& menu = std::get<ScriptMenu>(handles_.Get(args.Int(1), script.owner, MenuType));
        if (menu.menu.items.size() >= 128) throw NativeError("menu item limit (128) reached");
        for (const auto& [slot, display] : displays_)
            if (display.script == &script && display.menu == args.Int(1)) throw NativeError("cannot edit a displayed menu");
        auto text = args.String(2, 96);
        if (text.empty() || (args.Int(3) != 0 && args.Int(3) != 1)) throw NativeError("invalid menu item");
        const auto value = args.Count() == 4 ? args.Int(4) : -1;
        if (value < -1) throw NativeError("menu item value must be -1 or nonnegative");
        menu.menu.items.push_back({std::move(text), args.Int(3) == 1, value});
        return 1;
    }, 4);
    bind("ShowMenu", 3, [&](const Arguments& args) {
        Limit(script);
        auto& menu = std::get<ScriptMenu>(handles_.Get(args.Int(1), script.owner, MenuType));
        Player player;
        if (ResolvePlayer(script, args.Int(2), player) != KEEL_RESULT_OK || !Allowed(script, args.Int(2), menu.menu.permission)) return 0;
        if (args.Int(3) < 250 || args.Int(3) > 120000 || menu.menu.items.empty()) throw NativeError("menu requires items and a 250..120000 ms timeout");
        for (const auto& [slot, display] : displays_)
            if (display.script == &script && display.menu == args.Int(1))
                throw NativeError("menu is already displayed");
        if (script.state != PluginState::Running) {
            script.error = "display menus after initialization, in a callback"; return 0;
        }
        if (!CloseDisplay(player.slot)) { script.error = "previous menu clear failed"; return 0; }
        if (next_session_ == std::numeric_limits<std::uint64_t>::max()) throw NativeError("menu session IDs exhausted");
        menu.menu.selected = 0;
        const auto result = host_.RenderMenu(player, menu.menu.Html(menu.back != nullptr), args.Int(3));
        if (result != KEEL_RESULT_OK) {
            script.error = "menu renderer unavailable (KeelResult " + std::to_string(result) + ")"; return 0;
        }
        const auto session = next_session_++;
        displays_.emplace(player.slot, Display{session, &script, args.Int(1), args.Int(2), player,
            now_ + std::chrono::milliseconds(args.Int(3)), std::chrono::milliseconds(args.Int(3)), {}});
        const auto controls = InitialMenuControls(player);
        if (auto found = displays_.find(player.slot); found != displays_.end() && found->second.session == session)
            found->second.controls = controls;
        return 1;
    });
    bind("CloseMenu", 1, [&](const Arguments& args) {
        handles_.Get(args.Int(1), script.owner, MenuType);
        for (auto it = displays_.begin(); it != displays_.end();) {
            const auto slot = it->first;
            const bool owned = it->second.script == &script && it->second.menu == args.Int(1);
            ++it;
            if (owned && !CloseDisplay(slot)) { script.error = "menu clear failed; retry close"; return 0; }
        }
        if (handles_.Contains(args.Int(1), script.owner, MenuType)) handles_.Remove(args.Int(1), script.owner, MenuType);
        return 1;
    });
    for (auto& [registration, provider] : providers_) {
        unsigned index;
        if (script.vm->FindNativeByName(provider.name.c_str(), &index) != SP_ERROR_NONE) continue;
        if (!ProviderUsers(provider) && host_.AcquireProvider(provider.service, provider.version) != KEEL_RESULT_OK)
            throw NativeError("native provider unavailable or incompatible: " + provider.service);
        ++provider.users;
        script.providers.insert(registration);
        runtime_.Bind(*script.vm, provider.name.c_str(), static_cast<int>(provider.argc),
            [this, &provider](const Arguments& args) -> Cell {
                Thread();
                std::vector<Cell> arguments;
                for (unsigned i = 1; i <= provider.argc; ++i) arguments.push_back(args.Int(static_cast<int>(i)));
                char error[512]{};
                Cell result = 0;
                ++provider.active;
                KeelResult status;
                try {
                    status = provider.invoke(provider.user_data, arguments.data(), provider.argc, &result, error, sizeof(error));
                } catch (...) {
                    --provider.active;
                    throw NativeError("native extension threw across its ABI boundary");
                }
                --provider.active;
                error[sizeof(error) - 1] = 0;
                if (status != KEEL_RESULT_OK) throw NativeError("extension native failed: " + std::string(error));
                return result;
            });
    }
}

KeelResult Foundation::RegisterNative(KeelPluginHandle owner, const SrNativeSpec& spec, SrRegistration& registration) {
    Thread();
    registration = 0;
    if (spec.size != sizeof(spec) || spec.api_version != SR_EXTENSION_API_VERSION) return KEEL_RESULT_INCOMPATIBLE;
    if (!owner || !spec.name || !spec.provider_service || !spec.invoke || !spec.provider_version ||
        spec.argument_count > 16 || spec.reserved || std::strlen(spec.name) > 96 ||
        std::strlen(spec.provider_service) > 96 || !*spec.provider_service) return KEEL_RESULT_INVALID_ARGUMENT;
    for (const auto& [id, provider] : providers_)
        if (provider.name == spec.name) return KEEL_RESULT_ALREADY_EXISTS;
    const std::string_view name(spec.name);
    const auto first = [](unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
    if (name.empty() || !first(name.front()) || !std::all_of(name.begin(), name.end(), [&](unsigned char c) {
            return first(c) || (c >= '0' && c <= '9');
        }) || std::find(CoreNativeNames.begin(), CoreNativeNames.end(), name) != CoreNativeNames.end() ||
        next_provider_ == std::numeric_limits<std::uint64_t>::max()) return KEEL_RESULT_INVALID_ARGUMENT;
    registration = next_provider_++;
    providers_.emplace(registration, Provider{owner, spec.name, spec.provider_service, spec.provider_version,
        spec.argument_count, 0, 0, spec.invoke, spec.user_data});
    return KEEL_RESULT_OK;
}

KeelResult Foundation::UnregisterNative(KeelPluginHandle owner, SrRegistration registration) {
    Thread();
    const auto found = providers_.find(registration);
    if (found == providers_.end()) return KEEL_RESULT_NOT_FOUND;
    if (found->second.owner != owner) return KEEL_RESULT_INVALID_ARGUMENT;
    if (found->second.users || found->second.active) return KEEL_RESULT_BUSY;
    providers_.erase(found);
    return KEEL_RESULT_OK;
}

}
