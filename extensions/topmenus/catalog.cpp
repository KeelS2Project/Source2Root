#include "catalog.h"

#include <algorithm>
#include <limits>
#include <map>
#include <utility>

namespace source2root::topmenus {
namespace {
void Key(const std::string& value, bool empty = false) {
    if ((!empty && value.empty()) || value.size() > 64 || std::any_of(value.begin(), value.end(), [](unsigned char c) {
        return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.');
    })) throw Error("Top menu names accept 1..64 letters, digits, dots, underscores or hyphens.");
}
void Label(const std::string& value) {
    if (value.empty() || value.size() > 96 || std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        throw Error("Top menu labels require 1..96 printable bytes.");
}
void Permission(const std::string& value) {
    if (value.empty()) return;
    if (value.size() > 96 || value.front() == '.' || value.back() == '.' || value.find("..") != std::string::npos ||
        std::any_of(value.begin(), value.end(), [](unsigned char c) {
            return !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.');
        })) throw Error("Invalid top menu permission name.");
}
}

Object::~Object() { Close(); }
void Object::Close() {
    if (!live) return;
    live = false;
    ++menu->revision;
    if (auto cleanup = std::exchange(release, {})) cleanup();
}
void Object::SetLabel(std::string value) {
    Label(value);
    if (!live) throw Error("Top menu object is closed.");
    if (label != value) { label = std::move(value); ++menu->revision; }
}
void Object::SetOrder(std::int32_t value) {
    if (!live) throw Error("Top menu object is closed.");
    if (order != value) { order = value; ++menu->revision; }
}
void Menu::SetTitle(std::string value) {
    Label(value);
    if (title != value) { title = std::move(value); ++revision; }
}
std::shared_ptr<Object> Menu::Add(Kind kind, std::string key, std::string category, std::string label,
    std::string permission, std::string plugin, std::uint64_t owner, std::int32_t order) {
    Key(key); Key(category, true); Label(label); Permission(permission);
    if (!owner || plugin.empty() || (kind == Kind::Category && !category.empty())) throw Error("Invalid top menu registration.");
    std::erase_if(objects, [](const auto& weak) { const auto object = weak.lock(); return !object || !object->live; });
    if (objects.size() >= 256 || next_sequence == std::numeric_limits<std::uint64_t>::max()) throw Error("Top menu object limit (256) reached.");
    for (const auto& weak : objects) if (auto object = weak.lock(); object && object->key == key) {
        if (object->plugin != plugin || object->owner == owner || object->kind != kind || object->category != category)
            throw Error("Top menu object name is already registered.");
    }
    auto object = std::make_shared<Object>();
    object->menu = shared_from_this(); object->kind = kind; object->key = std::move(key); object->category = std::move(category);
    object->label = std::move(label); object->permission = std::move(permission); object->plugin = std::move(plugin);
    object->owner = owner; object->order = order; object->sequence = next_sequence++;
    objects.push_back(object); ++revision;
    return object;
}
std::vector<std::shared_ptr<Object>> Menu::Active(const std::function<bool(std::uint64_t)>& running) {
    std::erase_if(objects, [](const auto& weak) { const auto object = weak.lock(); return !object || !object->live; });
    std::map<std::string, std::shared_ptr<Object>> latest;
    for (const auto& weak : objects) if (auto object = weak.lock(); object && object->live && running(object->owner)) {
        auto& current = latest[object->key];
        if (!current || current->sequence < object->sequence) current = std::move(object);
    }
    std::vector<std::shared_ptr<Object>> result;
    for (auto& [key, object] : latest) result.push_back(std::move(object));
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        if (a->order != b->order) return a->order < b->order;
        if (a->label != b->label) return a->label < b->label;
        return a->key < b->key;
    });
    return result;
}
Page Menu::Build(const std::string& category, std::size_t requested,
    const std::function<bool(std::uint64_t)>& running,
    const std::function<Access(const std::shared_ptr<Object>&)>& access) {
    Key(category, true);
    Page page{title, category, {}, 0, revision};
    const auto active = Active(running);
    std::map<std::string, Access> visibility;
    for (const auto& object : active) if (object->kind == Kind::Category) {
        visibility.emplace(object->key, access(object));
        if (revision != page.revision) throw Error("Top menu changed during its access callback.");
    }
    for (const auto& object : active) if (object->kind == Kind::Item) {
        const auto category_access = visibility.find(object->category);
        const bool allowed_category = object->category.empty() ||
            (category_access != visibility.end() && category_access->second == Access::Enabled);
        visibility.emplace(object->key, allowed_category ? access(object) : Access::Hidden);
        if (revision != page.revision) throw Error("Top menu changed during its access callback.");
    }
    auto visible = [&](const auto& object) { return visibility.at(object->key) != Access::Hidden; };
    if (!category.empty()) {
        const auto found = std::find_if(active.begin(), active.end(), [&](const auto& object) {
            return object->key == category && object->kind == Kind::Category && visibility.at(object->key) == Access::Enabled;
        });
        if (found == active.end()) throw Error("Top menu category is no longer available.");
        page.title = (*found)->label;
    }
    std::vector<std::shared_ptr<Object>> shown;
    for (const auto& object : active) {
        if (!visible(object)) continue;
        if (object->kind == Kind::Item && object->category == category) shown.push_back(object);
        if (object->kind == Kind::Category && category.empty() && std::any_of(active.begin(), active.end(), [&](const auto& child) {
            return child->kind == Kind::Item && child->category == object->key && visible(child);
        })) shown.push_back(object);
    }
    constexpr std::size_t per_page = 28;
    page.page = std::min(requested, shown.empty() ? 0 : (shown.size() - 1) / per_page);
    const auto first = page.page * per_page;
    for (auto i = first; i < std::min(shown.size(), first + per_page); ++i)
        page.rows.push_back({shown[i]->label, visibility.at(shown[i]->key) == Access::Enabled, shown[i]});
    if (shown.empty()) page.rows.push_back({"No available actions", false, {}, -1, false, true});
    if (page.page) page.rows.push_back({"Previous page", true, {}, static_cast<int>(page.page - 1)});
    if (first + per_page < shown.size()) page.rows.push_back({"Next page", true, {}, static_cast<int>(page.page + 1)});
    if (!category.empty()) page.rows.push_back({"Back to categories", true, {}, -1, true});
    return page;
}

std::shared_ptr<Menu> Catalog::Open(const std::string& name, const std::string& title) {
    Key(name); Label(title);
    std::erase_if(menus_, [](const auto& weak) { return weak.expired(); });
    for (const auto& weak : menus_) if (auto menu = weak.lock(); menu && menu->name == name) return menu;
    if (menus_.size() >= 32) throw Error("Shared top menu limit (32) reached.");
    auto menu = std::make_shared<Menu>(); menu->name = name; menu->title = title;
    menus_.push_back(menu);
    return menu;
}
}
