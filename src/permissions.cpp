#include "identity.h"
#include "keyvalues.h"

#include <algorithm>
#include <charconv>
#include <initializer_list>
#include <stdexcept>
#include <string_view>

namespace sr {
namespace {

std::string Lower(std::string text) {
    for (auto& c : text) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return text;
}

std::vector<KeyValue> Root(const std::filesystem::path& file, const char* name) {
    auto entries = ReadKeyValues(file);
    if (entries.size() != 1 || !entries[0].object || Lower(entries[0].name) != Lower(name))
        throw std::runtime_error(file.filename().string() + " must contain one " + name + " object");
    return std::move(entries[0].children);
}

using Fields = std::map<std::string, const KeyValue*>;

Fields Properties(const KeyValue& entry, std::initializer_list<std::string_view> allowed) {
    if (!entry.object) throw std::runtime_error(entry.name + " must be an object");
    Fields fields;
    for (const auto& child : entry.children) {
        const auto name = Lower(child.name);
        if (std::find(allowed.begin(), allowed.end(), name) == allowed.end())
            throw std::runtime_error(entry.name + ": unknown field " + child.name);
        if (!fields.emplace(name, &child).second) throw std::runtime_error(entry.name + ": duplicate field " + child.name);
    }
    return fields;
}

const std::string& Scalar(const Fields& fields, const char* name) {
    const auto found = fields.find(name);
    if (found == fields.end()) throw std::runtime_error(std::string("missing field ") + name);
    if (found->second->object) throw std::runtime_error(std::string(name) + " must be a string value");
    return found->second->value;
}

int ImmunityValue(const Fields& fields, int fallback) {
    if (!fields.contains("immunity")) return fallback;
    const auto& text = Scalar(fields, "immunity");
    int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value < 0)
        throw std::runtime_error("immunity must be an integer from 0 to 2147483647");
    return value;
}

bool Authenticated(const Player& player) {
    return player.slot >= 0 && player.connection && player.authenticated && !player.bot && ValidSteamIdentity(player.steam_id);
}

}

void Permissions::Load(const std::filesystem::path& directory) {
    const auto admin_file = directory / "admins.cfg", group_file = directory / "admin_groups.cfg";
    const bool has_admins = std::filesystem::exists(admin_file), has_groups = std::filesystem::exists(group_file);
    if (!has_admins && !has_groups) {
        if (std::filesystem::exists(directory / "permissions.json") || std::filesystem::exists(directory / "admins.json"))
            throw std::runtime_error("legacy administrator configuration requires migration to admins.cfg and admin_groups.cfg");
        admins_.clear();
        return;
    }
    if (!has_admins || !has_groups) throw std::runtime_error("admins.cfg and admin_groups.cfg must both be present");
    struct Group { std::string name; std::set<std::string> permissions; int immunity; bool root; };
    std::map<std::string, Group> groups;
    for (const auto& entry : Root(group_file, "Groups")) {
        const auto key = Lower(entry.name);
        if (key.empty() || key.size() > 64 || std::any_of(key.begin(), key.end(), [](unsigned char c) { return c < 32 || c == 127; }))
            throw std::runtime_error("group names accept 1..64 printable bytes");
        const auto fields = Properties(entry, {"immunity", "permissions"});
        Group group{entry.name, {}, ImmunityValue(fields, 0), key == "root"};
        if (const auto found = fields.find("permissions"); found != fields.end()) {
            if (!found->second->object) throw std::runtime_error(entry.name + ": permissions must be an object");
            for (const auto& permission : found->second->children) {
                if (permission.object || permission.name.empty() || !ValidPermission(permission.name) || permission.value != "1")
                    throw std::runtime_error(entry.name + ": each named permission must have value 1");
                if (!group.permissions.insert(permission.name).second) throw std::runtime_error(entry.name + ": duplicate permission " + permission.name);
            }
        }
        if (!groups.emplace(key, std::move(group)).second) throw std::runtime_error("duplicate group name: " + entry.name);
        if (groups.size() > 128) throw std::runtime_error("administrator configuration exceeds 128 groups");
    }
    std::map<std::uint64_t, Admin> admins;
    for (const auto& entry : Root(admin_file, "Admins")) {
        if (entry.name.size() > 128 || std::any_of(entry.name.begin(), entry.name.end(), [](unsigned char c) { return c < 32 || c == 127; }))
            throw std::runtime_error("administrator label accepts at most 128 printable bytes");
        const auto fields = Properties(entry, {"identity", "group", "immunity"});
        const auto identity = ParseSteamIdentity(Scalar(fields, "identity"));
        const auto found = groups.find(Lower(Scalar(fields, "group")));
        if (found == groups.end()) throw std::runtime_error(entry.name + ": unknown administrator group");
        const auto& group = found->second;
        if (!admins.emplace(identity, Admin{entry.name, group.name, group.permissions, ImmunityValue(fields, group.immunity), group.root}).second)
            throw std::runtime_error("duplicate administrator identity: " + std::to_string(identity));
        if (admins.size() > 4096) throw std::runtime_error("administrator configuration exceeds 4096 administrators");
    }
    admins_ = std::move(admins);
}

const Permissions::Admin* Permissions::Find(const Player& player) const {
    if (!Authenticated(player)) return nullptr;
    const auto found = admins_.find(player.steam_id);
    return found == admins_.end() ? nullptr : &found->second;
}

int Permissions::Immunity(const Player& player) const {
    const auto* admin = Find(player);
    return admin ? admin->immunity : 0;
}

bool Permissions::Allows(const Player& player, const std::string& permission) const {
    if (!ValidPermission(permission)) return false;
    if (permission.empty()) return true;
    const auto* admin = Find(player);
    return admin && (admin->root || admin->permissions.contains(permission));
}

bool Permissions::CanTarget(const Player* caller, const Player& target, const std::string& permission) const {
    if (!ValidPermission(permission) || target.slot < 0 || !target.connection) return false;
    if (!caller) return true;
    if (!Authenticated(*caller) || !Allows(*caller, permission)) return false;
    if (!target.bot && !Authenticated(target)) return false;
    if (caller->SameConnection(target) && caller->steam_id == target.steam_id) return true;
    return Immunity(*caller) > Immunity(target);
}

bool Permissions::CanTargetIdentity(const Player* caller, std::uint64_t target, const std::string& permission) const {
    if (!ValidPermission(permission) || !ValidSteamIdentity(target)) return false;
    if (!caller) return true;
    if (!Authenticated(*caller) || !Allows(*caller, permission)) return false;
    if (caller->steam_id == target) return true;
    const auto found = admins_.find(target);
    const int target_immunity = found == admins_.end() ? 0 : found->second.immunity;
    return Immunity(*caller) > target_immunity;
}

}
