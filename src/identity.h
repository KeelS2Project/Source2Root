#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>

namespace sr {

struct Player {
    int slot = -1;
    std::uint64_t connection = 0;
    std::uint64_t steam_id = 0;
    bool authenticated = false;
    bool bot = false;
    std::string name;
    int user_id = -1, team = 0;
    bool alive = false;
    bool SameConnection(const Player& other) const {
        return slot == other.slot && connection != 0 && connection == other.connection;
    }
};

std::uint64_t ParseSteamIdentity(const std::string& text);
bool ValidSteamIdentity(std::uint64_t value);
bool ValidIdentifier(const std::string& text);
bool ValidPermission(const std::string& text);

class Permissions {
public:
    struct Admin {
        std::string label, group;
        std::set<std::string> permissions;
        int immunity = 0;
        bool root = false;
    };
    void Load(const std::filesystem::path& directory);
    bool Allows(const Player& player, const std::string& permission) const;
    const Admin* Find(const Player& player) const;
    int Immunity(const Player& player) const;
    bool CanTarget(const Player* caller, const Player& target, const std::string& permission) const;
    bool CanTargetIdentity(const Player* caller, std::uint64_t target, const std::string& permission) const;
private:
    std::map<std::uint64_t, Admin> admins_;
};

}
