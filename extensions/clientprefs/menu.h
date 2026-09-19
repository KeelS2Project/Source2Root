#pragma once

#include "service.h"

namespace source2root::prefs {

enum class PrefabType { YesNo, YesNoInteger, OnOff, OnOffInteger };

struct Prefab {
    std::shared_ptr<Cookie> cookie;
    PrefabType type = PrefabType::YesNo;
    std::string label;
    std::uint64_t owner = 0;
};

struct MenuRow {
    std::string text;
    bool enabled = false;
    std::weak_ptr<Prefab> prefab;
    int page = -1; // Navigation if nonnegative.
};

struct MenuPage {
    std::string title;
    std::vector<MenuRow> rows;
    std::size_t page = 0;
};
constexpr std::size_t SettingsPerPage = 28;
void ValidatePrefab(const Prefab& prefab);
std::string ChoiceLabel(PrefabType type, bool enabled);
std::string ChoiceValue(PrefabType type, bool enabled);
MenuPage BuildSettings(Service& service, const Identity& player,
    const std::vector<std::weak_ptr<Prefab>>& prefabs, std::size_t page);

}
