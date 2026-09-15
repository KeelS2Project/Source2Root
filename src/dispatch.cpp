#include "foundation.h"

#include <algorithm>

namespace sr {

bool Foundation::Dispatch(Origin origin, int slot, const std::string& text) {
    Thread();
    const bool chat = origin == Origin::PublicChat || origin == Origin::SilentChat;
    Player caller;
    const bool server = origin == Origin::ServerConsole;
    const bool valid_caller = server ? slot == -1 : slot >= 0 && host_.Lookup(slot, caller) == KEEL_RESULT_OK;
    if (chat && !valid_caller) return true;
    const bool gagged = chat && Gagged(caller);
    if (text.empty()) return gagged;
    const bool public_match = chat && !settings_.public_trigger.empty() && text.starts_with(settings_.public_trigger);
    const bool silent_match = chat && !settings_.silent_trigger.empty() && text.starts_with(settings_.silent_trigger);
    if (chat && !public_match && !silent_match) return gagged;
    const bool silent = silent_match && (!public_match || settings_.silent_trigger.size() > settings_.public_trigger.size());
    const auto prefix = chat ? (silent ? settings_.silent_trigger.size() : settings_.public_trigger.size()) : 0;
    bool quoted = false, escaped = false, malformed = text.size() > 1024;
    for (unsigned char c : text) {
        if (c < 32 || c == 127 || c == ';') malformed = true;
        if (c == '"' && !escaped) quoted = !quoted;
        escaped = c == '\\' && !escaped;
    }
    if (quoted) malformed = true;
    auto split = text.find(' ');
    auto name = text.substr(prefix, split == std::string::npos ? std::string::npos : split - prefix);
    if (chat) name = "sr_" + name;
    if (!valid_caller)
        return gagged || silent || !chat;
    ReconcilePlayers();
    const auto found = commands_.find(name);
    if (found == commands_.end()) {
        if (silent) host_.Reply(&caller, malformed ? "Invalid command syntax." :
            "Unknown command \"" + name.substr(3) + "\".");
        return gagged || silent;
    }
    if (malformed) {
        host_.Reply(server ? nullptr : &caller, "Invalid command syntax.");
        return gagged || silent || !chat;
    }
    for (auto* script : found->second) {
        if (script->state != PluginState::Running) continue;
        const auto command = script->commands.at(name);
        if (!server && !permissions_.Allows(caller, command.permission)) {
            script->error = "You do not have access to this command.";
            host_.Reply(&caller, script->error);
            break;
        }
        const auto arguments = split == std::string::npos ? "" : text.substr(split + 1);
        try {
            auto handle = server ? Cell{0} : PlayerHandle(*script, caller);
            Invoke(*script, command.callback, {handle}, arguments.c_str());
        } catch (const std::exception& error) { Fail(script->manifest.id + ": " + error.what()); }
        break;
    }
    return gagged || silent || !chat;
}

void Foundation::Event(const std::string& name) {
    Thread();
    auto found = events_.find(name);
    if (found == events_.end()) return;
    std::vector<std::pair<std::string, std::uint64_t>> owners;
    for (auto* script : found->second) owners.emplace_back(script->manifest.id, script->owner);
    for (const auto& [id, owner] : owners) {
        auto* script = scripts_.at(id).current.get();
        if (script && script->owner == owner && script->state == PluginState::Running)
            Invoke(*script, script->events.at(name), {}, name.c_str());
    }
}

bool Foundation::CloseDisplay(int slot) {
    if (!CloseNativeDisplay(slot)) return false;
    auto found = displays_.find(slot);
    if (found == displays_.end()) return true;
    const auto retire = [&] {
        handles_.Remove(found->second.menu, found->second.script->owner, MenuType);
        displays_.erase(found);
    };
    Player current;
    auto result = host_.Lookup(slot, current);
    if (result == KEEL_RESULT_NOT_FOUND || (result == KEEL_RESULT_OK && !current.SameConnection(found->second.player))) {
        retire();
        return true;
    }
    if (result != KEEL_RESULT_OK || host_.RenderMenu(current, "") != KEEL_RESULT_OK) return false;
    retire();
    return true;
}

void Foundation::CloseOwnedMenus(Script& script) {
    for (auto it = displays_.begin(); it != displays_.end();) {
        const auto slot = it->first;
        const bool owned = it->second.script == &script;
        ++it;
        if (owned) CloseDisplay(slot);
    }
}

void Foundation::Tick(Clock::time_point now) {
    Thread();
    if (!runtime_.Idle()) return;
    now_ = std::max(now_, now);
    if (RunPendingMap()) return;
    ReconcilePlayers();
    if (now_ >= next_voice_) {
        next_voice_ = now_ + std::chrono::seconds(1);
        std::string error;
        if (!voice_.Refresh(error) && error != voice_error_) Fail(error);
        voice_error_ = std::move(error);
    }
    TickNativeMenus();
    for (auto& [id, slot] : scripts_) {
        auto* script = slot.current.get();
        if (!script || script->state != PluginState::Running) continue;
        for (auto handle : handles_.Owned(script->owner, TimerType)) {
            if (!handles_.Contains(handle, script->owner, TimerType)) continue;
            auto timer = std::get<Timer>(handles_.Get(handle, script->owner, TimerType));
            if (timer.due > now_) continue;
            if (timer.target) {
                Player player;
                const auto result = ResolvePlayer(*script, timer.target, player);
                if (result == KEEL_RESULT_NOT_FOUND) { handles_.Remove(handle, script->owner, TimerType); continue; }
                if (result != KEEL_RESULT_OK) continue;
            }
            handles_.Remove(handle, script->owner, TimerType);
            Invoke(*script, timer.callback, {timer.target});
            if (script->state != PluginState::Running) break;
        }
    }
    for (auto it = displays_.begin(); it != displays_.end();) {
        const auto slot = it->first;
        auto& display = it->second;
        ++it;
        if (display.expires <= now_ || display.script->state != PluginState::Running) { CloseDisplay(slot); continue; }
        Player current;
        const auto result = host_.Lookup(slot, current);
        if (result == KEEL_RESULT_NOT_FOUND || (result == KEEL_RESULT_OK && !current.SameConnection(display.player))) {
            handles_.Remove(display.menu, display.script->owner, MenuType);
            displays_.erase(slot); continue;
        }
        if (result != KEEL_RESULT_OK) continue;
        const auto& menu = std::get<ScriptMenu>(handles_.Get(display.menu, display.script->owner, MenuType));
        if (!permissions_.Allows(current, menu.menu.permission) ||
            host_.RenderMenu(current, menu.menu.Html(menu.back != nullptr),
                static_cast<int>(std::chrono::ceil<std::chrono::milliseconds>(display.expires - now_).count())) != KEEL_RESULT_OK) {
            display.expires = now_;
            CloseDisplay(slot);
        }
    }
    PollMenuInput();
}

MenuControls Foundation::InitialMenuControls(const Player& player) {
    MenuControls controls;
    KeelPlayerInput input{sizeof(input), 0, 0, 0};
    try {
        if (host_.ReadPlayerInput(player, input) == KEEL_RESULT_OK) controls.Baseline(input);
    } catch (...) {}
    return controls;
}

void Foundation::PollMenuInput() {
    std::vector<std::pair<Player, std::uint64_t>> sessions;
    for (const auto& [slot, display] : displays_)
        if (display.script->state == PluginState::Running) sessions.emplace_back(display.player, display.session);
    for (const auto& [slot, display] : native_displays_)
        if (!display.active && !display.closing) sessions.emplace_back(display.player, display.session);
    for (const auto& [player, session] : sessions) {
        if (CurrentMenu(player) != session) continue;
        KeelPlayerInput input{sizeof(input), 0, 0, 0};
        try {
            if (host_.ReadPlayerInput(player, input) != KEEL_RESULT_OK) input = {};
        } catch (...) { input = {}; }
        if (CurrentMenu(player) != session) continue;
        MenuControls* controls = nullptr;
        if (auto found = displays_.find(player.slot); found != displays_.end() && found->second.session == session)
            controls = &found->second.controls;
        else if (auto found = native_displays_.find(player.slot); found != native_displays_.end() && found->second.session == session)
            controls = &found->second.controls;
        if (controls) {
            const auto action = controls->Read(input);
            if (action) MenuInput(player, session, *action);
        }
    }
}

void Foundation::MapChanged() {
    Thread();
    CancelMap();
    if (!voice_.Release(0, voice_error_)) Fail(voice_error_);
    for (auto it = native_displays_.begin(); it != native_displays_.end();) {
        int slot = it->first;
        it->second.cleared = true;
        ++it;
        CloseNativeDisplay(slot);
    }
    displays_.clear();
    for (auto& [id, slot] : scripts_)
        for (auto* script : {slot.current.get(), slot.replacement.get()}) if (script) {
            for (auto handle : handles_.Owned(script->owner, TimerType))
                if (!std::get<Timer>(handles_.Get(handle, script->owner, TimerType)).across_maps)
                    handles_.Remove(handle, script->owner, TimerType);
            for (auto type : {PlayerType, MenuType}) handles_.Retire(script->owner, type);
            script->self_kicks.clear();
            script->gags.clear();
        }
}

void Foundation::Disconnected(int slot, std::uint64_t generation) {
    Thread();
    voice_.Disconnected(slot, generation);
    auto native = native_displays_.find(slot);
    if (native != native_displays_.end() && native->second.player.connection == generation) {
        native->second.cleared = true;
        CloseNativeDisplay(slot);
    }
    auto found = displays_.find(slot);
    if (found != displays_.end() && found->second.player.connection == generation) {
        handles_.Remove(found->second.menu, found->second.script->owner, MenuType);
        displays_.erase(found);
    }
    for (auto& [id, entry] : scripts_) for (auto* script : {entry.current.get(), entry.replacement.get()}) if (script) {
        const auto gag = script->gags.find(slot);
        if (gag != script->gags.end() && gag->second.connection == generation) script->gags.erase(gag);
        for (auto handle : handles_.Owned(script->owner, PlayerType)) {
            auto player = std::get<Player>(handles_.Get(handle, script->owner, PlayerType));
            if (player.slot != slot || player.connection != generation) continue;
            for (auto timer_handle : handles_.Owned(script->owner, TimerType))
                if (std::get<Timer>(handles_.Get(timer_handle, script->owner, TimerType)).target == handle)
                    handles_.Remove(timer_handle, script->owner, TimerType);
            handles_.Remove(handle, script->owner, PlayerType);
        }
    }
}

bool Foundation::MenuInput(const Player& player, std::uint64_t session, sr::MenuInput input) {
    Thread();
    if (native_displays_.contains(player.slot)) return NativeMenuInput(player, session, input);
    auto found = displays_.find(player.slot);
    if (found == displays_.end() || found->second.session != session || !found->second.player.SameConnection(player)) return false;
    auto display = found->second;
    if (display.script->state != PluginState::Running || display.expires <= now_) { CloseDisplay(player.slot); return false; }
    auto& menu = std::get<ScriptMenu>(handles_.Get(display.menu, display.script->owner, MenuType));
    if (!Allowed(*display.script, display.player_handle, menu.menu.permission)) { CloseDisplay(player.slot); return false; }
    found->second.expires = display.expires = now_ + display.timeout;
    if (input == sr::MenuInput::Back && menu.back) {
        auto* callback = menu.back;
        found->second.expires = now_;
        if (!CloseDisplay(player.slot)) return false;
        return Invoke(*display.script, callback, {display.player_handle});
    }
    const auto action = menu.menu.Input(input);
    if (action == MenuAction::Cancelled) return CloseDisplay(player.slot);
    if (action == MenuAction::Selected) {
        const auto value = menu.menu.items[menu.menu.selected].value;
        const auto selected = value < 0 ? static_cast<Cell>(menu.menu.selected) : value;
        auto* callback = menu.callback;
        if (!CloseDisplay(player.slot)) return false;
        return Invoke(*display.script, callback, {display.player_handle, selected});
    }
    if (action == MenuAction::Changed) {
        if (host_.RenderMenu(player, menu.menu.Html(menu.back != nullptr),
            static_cast<int>(std::chrono::ceil<std::chrono::milliseconds>(display.expires - now_).count())) == KEEL_RESULT_OK) return true;
        if (CurrentMenu(player) == session) {
            displays_.at(player.slot).expires = now_;
            CloseDisplay(player.slot);
        }
    }
    return false;
}

std::uint64_t Foundation::CurrentMenu(const Player& player) const {
    Thread();
    const auto native = native_displays_.find(player.slot);
    if (native != native_displays_.end() && native->second.player.SameConnection(player) && !native->second.closing)
        return native->second.session;
    const auto found = displays_.find(player.slot);
    return found != displays_.end() && found->second.player.SameConnection(player) ? found->second.session : 0;
}

}
