#include "foundation.h"

#include <cstring>
#include <limits>

namespace sr {

KeelResult Foundation::OpenNativeMenu(KeelPluginHandle owner, const KeelPlayerConnection& connection,
    const SrMenuSpec& spec, SrMenuSession& session) {
    Thread();
    session = 0;
    if (spec.size != sizeof(spec) || spec.api_version != SR_EXTENSION_API_VERSION) return KEEL_RESULT_INCOMPATIBLE;
    if (!owner || connection.reserved || !connection.generation || !spec.title || !spec.permission ||
        !spec.items || !spec.item_count || spec.item_count > 32 || !spec.selected ||
        !spec.provider_service || !spec.provider_version || std::strlen(spec.title) > 96 ||
        !*spec.title || std::strlen(spec.permission) > 96 || !ValidPermission(spec.permission) ||
        !*spec.provider_service || std::strlen(spec.provider_service) > 96 ||
        spec.timeout_milliseconds < 250 || spec.timeout_milliseconds > 120000)
        return KEEL_RESULT_INVALID_ARGUMENT;
    Player player;
    auto result = host_.Lookup(connection.slot, player);
    if (result != KEEL_RESULT_OK) return result;
    if (player.connection != connection.generation) return KEEL_RESULT_NOT_FOUND;
    if (!permissions_.Allows(player, spec.permission)) return KEEL_RESULT_NOT_READY;
    if (next_session_ == std::numeric_limits<std::uint64_t>::max()) return KEEL_RESULT_BUSY;
    Menu menu{spec.title, spec.permission, {}};
    for (unsigned i = 0; i < spec.item_count; ++i) {
        const auto& item = spec.items[i];
        if (!item.text || !*item.text || std::strlen(item.text) > 96 || item.enabled > 1)
            return KEEL_RESULT_INVALID_ARGUMENT;
        menu.items.push_back({item.text, item.enabled == KEEL_TRUE});
    }
    if (!CloseDisplay(player.slot)) return KEEL_RESULT_BUSY;
    const bool acquired = ServiceUsers(spec.provider_service, spec.provider_version) == 0;
    const auto id = next_session_++;
    auto [it, inserted] = native_displays_.emplace(player.slot, NativeDisplay{id, owner, player,
        std::move(menu), spec.provider_service, spec.provider_version, spec.selected, spec.user_data,
        now_ + std::chrono::milliseconds(spec.timeout_milliseconds), std::chrono::milliseconds(spec.timeout_milliseconds)});
    if (acquired) {
        result = host_.AcquireProvider(spec.provider_service, spec.provider_version);
        if (result != KEEL_RESULT_OK) {
            native_displays_.erase(it);
            return result;
        }
    }
    result = host_.RenderMenu(player, it->second.menu.Html(), static_cast<int>(spec.timeout_milliseconds));
    if (result != KEEL_RESULT_OK) {
        it->second.cleared = true;
        CloseNativeDisplay(player.slot);
        return result;
    }
    session = id;
    const auto controls = InitialMenuControls(player);
    if (auto found = native_displays_.find(player.slot); found != native_displays_.end() && found->second.session == id)
        found->second.controls = controls;
    return KEEL_RESULT_OK;
}

bool Foundation::CloseNativeDisplay(int slot, int selected) {
    auto found = native_displays_.find(slot);
    if (found == native_displays_.end()) return true;
    auto& display = found->second;
    if (display.active) return false;
    display.closing = true;
    if (!display.cleared) {
        Player current;
        auto result = host_.Lookup(slot, current);
        if (result == KEEL_RESULT_NOT_FOUND || (result == KEEL_RESULT_OK && !current.SameConnection(display.player))) {
            selected = -1;
        } else {
            if (result != KEEL_RESULT_OK || host_.RenderMenu(current, "") != KEEL_RESULT_OK) return false;
            if (!permissions_.Allows(current, display.menu.permission)) selected = -1;
        }
        display.cleared = true;
    }
    if (selected >= 0 && display.callback) {
        auto callback = display.callback;
        display.callback = nullptr;
        display.active = true;
        const KeelPlayerConnection connection{slot, 0, display.player.connection};
        try { callback(display.user_data, &connection, selected); }
        catch (...) { host_.Log("native menu callback threw across its ABI boundary"); }
        display.active = false;
    }
    if (ServiceUsers(display.service, display.version) == 1 &&
        host_.ReleaseProvider(display.service, display.version) != KEEL_RESULT_OK) return false;
    native_displays_.erase(found);
    return true;
}

KeelResult Foundation::CloseNativeMenu(KeelPluginHandle owner, SrMenuSession session) {
    Thread();
    for (const auto& [slot, display] : native_displays_) if (display.session == session) {
        if (display.owner != owner) return KEEL_RESULT_INVALID_ARGUMENT;
        return CloseNativeDisplay(slot) ? KEEL_RESULT_OK : KEEL_RESULT_BUSY;
    }
    return KEEL_RESULT_NOT_FOUND;
}

bool Foundation::NativeMenuInput(const Player& player, std::uint64_t session, sr::MenuInput input) {
    auto found = native_displays_.find(player.slot);
    if (found == native_displays_.end()) return false;
    auto& display = found->second;
    if (display.session != session || !display.player.SameConnection(player) || display.active || display.closing)
        return false;
    Player current;
    const auto result = host_.Lookup(player.slot, current);
    if (result != KEEL_RESULT_OK) return false;
    if (!current.SameConnection(display.player) || display.expires <= now_ ||
        !permissions_.Allows(current, display.menu.permission)) {
        CloseNativeDisplay(player.slot);
        return false;
    }
    display.expires = now_ + display.timeout;
    switch (display.menu.Input(input)) {
    case MenuAction::Cancelled: return CloseNativeDisplay(player.slot);
    case MenuAction::Selected: return CloseNativeDisplay(player.slot, static_cast<int>(display.menu.selected));
    case MenuAction::Changed:
        if (host_.RenderMenu(current, display.menu.Html(),
            static_cast<int>(std::chrono::ceil<std::chrono::milliseconds>(display.expires - now_).count())) == KEEL_RESULT_OK) return true;
        if (CurrentMenu(player) == session) CloseNativeDisplay(player.slot);
        return false;
    default: return false;
    }
}

void Foundation::TickNativeMenus() {
    for (auto it = native_displays_.begin(); it != native_displays_.end();) {
        const int slot = it->first;
        auto& display = it->second;
        ++it;
        if (display.active) continue;
        if (display.closing || display.expires <= now_) { CloseNativeDisplay(slot); continue; }
        Player current;
        const auto result = host_.Lookup(slot, current);
        if (result == KEEL_RESULT_NOT_FOUND || (result == KEEL_RESULT_OK && !current.SameConnection(display.player))) {
            display.cleared = true;
            CloseNativeDisplay(slot);
            continue;
        }
        if (result != KEEL_RESULT_OK) continue;
        if (!permissions_.Allows(current, display.menu.permission) ||
            host_.RenderMenu(current, display.menu.Html(),
                static_cast<int>(std::chrono::ceil<std::chrono::milliseconds>(display.expires - now_).count())) != KEEL_RESULT_OK)
            CloseNativeDisplay(slot);
    }
}

}
