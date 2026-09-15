#pragma once

#include "identity.h"
#include <keels2/plugin.h>
#include <map>
#include <set>

namespace sr {
class GameHost;

class Voice {
public:
    explicit Voice(GameHost& host) : host_(host) {}
    bool Set(std::uint64_t owner, const Player& player, bool muted, std::string& error);
    bool Muted(const Player& player) const;
    bool Release(std::uint64_t owner, std::string& error);
    bool Refresh(std::string& error);
    bool Suspend(std::string& error);
    bool Resume(std::string& error);
    KeelResult Filter(int receiver, int sender, bool& listening);
    void Disconnected(int slot, std::uint64_t connection);
private:
    struct Pair {
        Player receiver, sender;
        bool requested = false;
        std::uint64_t restore_owner = 0;
        bool Same(const Pair& other) const {
            return receiver.SameConnection(other.receiver) && sender.SameConnection(other.sender);
        }
    };
    using Key = std::pair<int, int>;
    KeelResult Current(const Player& player);
    KeelResult Current(const Pair& pair);
    KeelResult Write(const Pair& pair, bool listening);
    bool Apply(const Player& sender, std::string& error);
    bool Restore(std::uint64_t owner, const Player* sender, std::string& error);
    void Removed(std::uint64_t owner, const Player& sender);
    GameHost& host_;
    std::map<std::uint64_t, std::map<int, Player>> owners_, suspended_;
    bool suspended = false;
    std::map<Key, Pair> pairs_;
    bool writing_ = false;
};
}
