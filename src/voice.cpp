#include "voice.h"
#include "foundation.h"

namespace sr {
namespace {
bool Failure(std::string& error, const char* operation, KeelResult result) {
    if (error.empty())
        error = std::string(operation) + " (KeelResult " + std::to_string(result) + ").";

    return false;
}
}

KeelResult Voice::Current(const Player& player) {
    Player current;
    const auto result = host_.Lookup(player.slot, current);
    return result == KEEL_RESULT_OK && !player.SameConnection(current) ? KEEL_RESULT_NOT_FOUND : result;
}

KeelResult Voice::Current(const Pair& pair) {
    const auto receiver = Current(pair.receiver);
    const auto sender = Current(pair.sender);

    if (receiver == KEEL_RESULT_NOT_FOUND || sender == KEEL_RESULT_NOT_FOUND)
        return KEEL_RESULT_NOT_FOUND;

    return receiver != KEEL_RESULT_OK ? receiver : sender;
}

bool Voice::Muted(const Player& player) const {
    for (const auto& [owner, players] : owners_) {
        const auto found = players.find(player.slot);

        if (found != players.end() && found->second.SameConnection(player))
            return true;
    }

    return false;
}

KeelResult Voice::Filter(int receiver, int sender, bool& listening) {
    if (writing_)
        return KEEL_RESULT_OK;

    Pair pair;
    auto result = host_.Lookup(receiver, pair.receiver);

    if (result != KEEL_RESULT_OK)
        return result;

    result = host_.Lookup(sender, pair.sender);

    if (result != KEEL_RESULT_OK)
        return result;

    const Key key{receiver, sender};
    const auto pending = pairs_.find(key);
    const bool muted = Muted(pair.sender);

    if (!muted && (pending == pairs_.end() || !pending->second.Same(pair)))
        return KEEL_RESULT_OK;

    pair.requested = listening;

    if (pending != pairs_.end() && pending->second.Same(pair))
        pair.restore_owner = pending->second.restore_owner;

    pairs_[key] = pair;

    if (muted)
        listening = false;

    return KEEL_RESULT_OK;
}

KeelResult Voice::Write(const Pair& pair, bool listening) {
    const auto current = Current(pair);

    if (current != KEEL_RESULT_OK)
        return current;

    struct Writing {
        bool& state;
        bool previous;
        explicit Writing(bool& value) : state(value), previous(value) {
            state = true;
        }

        ~Writing() {
            state = previous;
        }
    } scope(writing_);

    try {
        return host_.SetListening(pair.receiver, pair.sender, listening);
    } catch (...) {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

bool Voice::Apply(const Player& sender, std::string& error) {
    bool success = true;
    int after = -1;

    for (unsigned count = 0; count <= 128; ++count) {
        Player receiver;
        const auto next = host_.NextPlayer(after, receiver);

        if (next == KEEL_RESULT_NOT_FOUND)
            return success;

        if (next != KEEL_RESULT_OK)
            return Failure(error, "Voice receiver lookup failed", next);

        if (receiver.slot <= after || count == 128)
            return Failure(error, "Voice receiver list is invalid", KEEL_RESULT_INVALID_ARGUMENT);

        after = receiver.slot;
        Pair pair{receiver, sender};
        auto result = Current(pair);

        if (result == KEEL_RESULT_NOT_FOUND)
            continue;

        if (result != KEEL_RESULT_OK) {
            success = Failure(error, "Voice connection lookup failed", result);
            continue;
        }

        const Key key{receiver.slot, sender.slot};
        const auto existing = pairs_.find(key);

        if (existing != pairs_.end() && existing->second.Same(pair))
            pair = existing->second;
        else {
            result = host_.GetListening(receiver, sender, pair.requested);

            if (result != KEEL_RESULT_OK) {
                success = Failure(error, "Voice state lookup failed", result);
                continue;
            }

            result = Current(pair);

            if (result == KEEL_RESULT_NOT_FOUND)
                continue;

            if (result != KEEL_RESULT_OK) {
                success = Failure(error, "Voice connection lookup failed", result);
                continue;
            }

            pairs_[key] = pair;
        }

        if (!Muted(sender))
            continue;

        result = Write(pair, false);

        if (result != KEEL_RESULT_OK && result != KEEL_RESULT_NOT_FOUND)
            success = Failure(error, "Voice restriction failed", result);
    }

    return success;
}

void Voice::Removed(std::uint64_t owner, const Player& sender) {
    if (Muted(sender))
        return;

    for (auto& [key, pair] : pairs_)
        if (pair.sender.SameConnection(sender))
            pair.restore_owner = owner;
}

bool Voice::Restore(std::uint64_t owner, const Player* sender, std::string& error) {
    bool success = true;
    const auto pending = pairs_;

    for (const auto& [key, pair] : pending) {
        if (Muted(pair.sender) || (owner && pair.restore_owner != owner) ||
            (sender && !pair.sender.SameConnection(*sender)))
            continue;

        const auto found = pairs_.find(key);

        if (found == pairs_.end() || !found->second.Same(pair))
            continue;

        auto result = Current(pair);

        if (result == KEEL_RESULT_OK)
            result = Write(pair, pair.requested);

        if (result == KEEL_RESULT_OK || result == KEEL_RESULT_NOT_FOUND) {
            const auto current = pairs_.find(key);

            if (current != pairs_.end() && current->second.Same(pair) && current->second.requested == pair.requested)
                pairs_.erase(current);
        } else
            success = Failure(error, "Voice restoration failed; retry required", result);
    }

    return success;
}

bool Voice::Set(std::uint64_t owner, const Player& player, bool muted, std::string& error) {
    error.clear();
    const auto current = Current(player);

    if (current == KEEL_RESULT_NOT_FOUND) {
        error = "Player is no longer available.";
        return false;
    }

    if (current != KEEL_RESULT_OK)
        return Failure(error, "Voice connection lookup failed", current);

    if (muted) {
        bool listening;
        const auto result = host_.GetListening(player, player, listening);

        if (result != KEEL_RESULT_OK)
            return Failure(error, "Voice control is unavailable", result);

        const auto checked = Current(player);

        if (checked == KEEL_RESULT_NOT_FOUND) {
            error = "Player is no longer available.";
            return false;
        }

        if (checked != KEEL_RESULT_OK)
            return Failure(error, "Voice connection lookup failed", checked);

        owners_[owner][player.slot] = player;

        if (!Apply(player, error)) {
            error = "Mute recorded; " + error;
            return false;
        }

        return true;
    }

    auto found = owners_.find(owner);

    if (found != owners_.end()) {
        found->second.erase(player.slot);

        if (found->second.empty())
            owners_.erase(found);
    }

    Removed(owner, player);
    return Restore(owner, &player, error);
}

bool Voice::Release(std::uint64_t owner, std::string& error) {
    error.clear();

    if (owner)
        suspended_.erase(owner);
    else {
        suspended_.clear();
        suspended = false;
    }

    std::vector<std::pair<std::uint64_t, Player>> removed;

    for (auto it = owners_.begin(); it != owners_.end();) {
        if (owner && it->first != owner) {
            ++it;
            continue;
        }

        for (const auto& [slot, player] : it->second)
            removed.emplace_back(it->first, player);

        it = owners_.erase(it);
    }

    for (const auto& [id, player] : removed)
        Removed(id, player);

    return Restore(owner, nullptr, error);
}

bool Voice::Refresh(std::string& error) {
    error.clear();
    bool success = true;
    std::map<int, Player> senders;

    for (auto owner = owners_.begin(); owner != owners_.end();) {
        for (auto it = owner->second.begin(); it != owner->second.end();) {
            const auto result = Current(it->second);

            if (result == KEEL_RESULT_NOT_FOUND)
                it = owner->second.erase(it);
            else {
                if (result == KEEL_RESULT_OK)
                    senders[it->first] = it->second;
                else
                    success = Failure(error, "Voice connection lookup failed", result);

                ++it;
            }
        }

        if (owner->second.empty())
            owner = owners_.erase(owner);
        else
            ++owner;
    }

    if (!Restore(0, nullptr, error))
        success = false;

    for (const auto& [slot, sender] : senders)
        if (!Apply(sender, error))
            success = false;

    return success;
}

bool Voice::Suspend(std::string& error) {
    error.clear();

    if (suspended)
        return Restore(0, nullptr, error);

    auto active = std::move(owners_);
    owners_.clear();

    if (!Restore(0, nullptr, error)) {
        owners_ = std::move(active);
        std::string retry;
        Refresh(retry);
        return false;
    }

    suspended_ = std::move(active);
    suspended = true;
    return true;
}

bool Voice::Resume(std::string& error) {
    if (suspended) {
        owners_ = std::move(suspended_);
        suspended_.clear();
        suspended = false;
    }

    return Refresh(error);
}

void Voice::Disconnected(int slot, std::uint64_t connection) {
    for (auto owner = suspended_.begin(); owner != suspended_.end();) {
        auto found = owner->second.find(slot);

        if (found != owner->second.end() && found->second.connection == connection)
            owner->second.erase(found);

        if (owner->second.empty())
            owner = suspended_.erase(owner);
        else
            ++owner;
    }

    for (auto owner = owners_.begin(); owner != owners_.end();) {
        auto found = owner->second.find(slot);

        if (found != owner->second.end() && found->second.connection == connection)
            owner->second.erase(found);

        if (owner->second.empty())
            owner = owners_.erase(owner);
        else
            ++owner;
    }

    for (auto it = pairs_.begin(); it != pairs_.end();) {
        const auto& pair = it->second;

        if ((pair.receiver.slot == slot && pair.receiver.connection == connection) ||
            (pair.sender.slot == slot && pair.sender.connection == connection)) it = pairs_.erase(it);
        else
            ++it;
    }
}
}
