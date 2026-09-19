#include "menu.h"

namespace source2root::prefs {
namespace {
std::string Clip(const std::string& text, std::size_t limit) {
    auto size = std::min(text.size(), limit);

    while (size && size < text.size() && (static_cast<unsigned char>(text[size]) & 0xc0) == 0x80)
        --size;

    return text.substr(0, size);
}
}

void ValidatePrefab(const Prefab& prefab) {
    if (!prefab.cookie || prefab.cookie->definition.access != Access::Public)
        throw Error("Only public cookies can have editable prefab menus.");

    if (prefab.type < PrefabType::YesNo || prefab.type > PrefabType::OnOffInteger)
        throw Error("Invalid preferences prefab type.");

    if (prefab.label.empty() || prefab.label.size() > 64 || prefab.label.find('\0') != std::string::npos)
        throw Error("Preference menu labels must contain 1..64 bytes without NUL.");
}

std::string ChoiceLabel(PrefabType type, bool enabled) {
    if (type == PrefabType::YesNo || type == PrefabType::YesNoInteger)
        return enabled ? "Yes" : "No";

    if (type == PrefabType::OnOff || type == PrefabType::OnOffInteger)
        return enabled ? "On" : "Off";

    throw Error("Invalid preferences prefab type.");
}

std::string ChoiceValue(PrefabType type, bool enabled) {
    if (type == PrefabType::YesNoInteger || type == PrefabType::OnOffInteger)
        return enabled ? "1" : "0";

    if (type == PrefabType::YesNo)
        return enabled ? "yes" : "no";

    if (type == PrefabType::OnOff)
        return enabled ? "on" : "off";

    throw Error("Invalid preferences prefab type.");
}

MenuPage BuildSettings(Service& service, const Identity& player,
    const std::vector<std::weak_ptr<Prefab>>& prefabs, std::size_t requested) {
    if (!service.Ready() || service.Status(player) != State::Ready)
        throw Error("Client preferences are not cached.");

    const auto cookies = service.UserCookies();
    MenuPage menu;
    menu.page = std::min(requested, cookies.empty() ? 0 : (cookies.size() - 1) / SettingsPerPage);
    menu.title = "Player preferences";
    std::map<std::string, std::shared_ptr<Prefab>> options;
    // A staged reload may briefly have old/new registrations. The latest live
    // registration wins; failed/unloaded replacements disappear with their owner.
    for (const auto& weak : prefabs) if (auto prefab = weak.lock()) {
            if (prefab->cookie->state == State::Ready)
                options[prefab->cookie->definition.name] = std::move(prefab);
    }

    const auto begin = menu.page * SettingsPerPage;

    for (std::size_t i = begin; i < std::min(cookies.size(), begin + SettingsPerPage); ++i) {
        const auto& definition = cookies[i];
        const auto value = service.Get(player, service.Find(definition.name));
        auto option = options.find(definition.name);
        const bool editable = definition.access == Access::Public && option != options.end();
        const auto label = editable ? option->second->label : definition.name;
        MenuRow row{Clip(label, 48) + ": " + Clip(value.text.empty() ? "(unset)" : value.text, 32), editable, {}, -1};

        if (definition.access == Access::Protected)
            row.text += " [read only]";

        if (editable)
            row.prefab = option->second;

        menu.rows.push_back(std::move(row));
    }

    if (cookies.empty())
        menu.rows.push_back({"No visible preferences", false, {}, -1});

    if (menu.page)
        menu.rows.push_back({"Previous settings", true, {}, static_cast<int>(menu.page - 1)});

    if (begin + SettingsPerPage < cookies.size())
        menu.rows.push_back({"More settings", true, {}, static_cast<int>(menu.page + 1)});

    return menu;
}

}
