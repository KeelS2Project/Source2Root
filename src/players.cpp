#include "foundation.h"

#include <algorithm>
#include <charconv>

namespace sr {

void Foundation::ReconcilePlayers() {
    std::set<std::pair<int, std::uint64_t>> observed, stale;
    for (const auto& [id, entry] : scripts_)
        for (auto* script : {entry.current.get(), entry.replacement.get()}) if (script)
            for (auto handle : handles_.Owned(script->owner, PlayerType)) {
                const auto& expected = std::get<Player>(handles_.Get(handle, script->owner, PlayerType));
                const auto connection = std::make_pair(expected.slot, expected.connection);
                if (!observed.insert(connection).second) continue;
                Player current;
                const auto result = host_.Lookup(expected.slot, current);
                if (result == KEEL_RESULT_NOT_FOUND || (result == KEEL_RESULT_OK && !current.SameConnection(expected)))
                    stale.insert(connection);
            }
    for (const auto& [slot, generation] : stale) Disconnected(slot, generation);
}

bool Foundation::ReadPlayers(Script& script, std::vector<Player>& players) {
    players.clear();
    int after = -1;
    for (;;) {
        Player candidate, current;
        const auto next = host_.NextPlayer(after, candidate);
        if (next == KEEL_RESULT_NOT_FOUND) return true;
        if (next != KEEL_RESULT_OK) { script.error = "Could not list players (KeelResult " + std::to_string(next) + ")."; return false; }
        if (candidate.slot <= after || !candidate.connection || players.size() >= 1024) {
            script.error = "Invalid player enumeration."; return false;
        }
        after = candidate.slot;
        const auto result = host_.Lookup(candidate.slot, current);
        if (result == KEEL_RESULT_NOT_FOUND) continue;
        if (result != KEEL_RESULT_OK) { script.error = "Could not read a current player (KeelResult " + std::to_string(result) + ")."; return false; }
        if (current.SameConnection(candidate)) players.push_back(std::move(current));
    }
}

bool Foundation::MakePlayerHandles(Script& script, const std::vector<Player>& players, int capacity, std::vector<Cell>& output) {
    if (players.size() > static_cast<std::size_t>(capacity)) { script.error = "Player output array is too small."; return false; }
    ReconcilePlayers();
    const auto owned = handles_.Owned(script.owner, PlayerType);
    std::size_t missing = 0;
    for (const auto& player : players)
        if (std::none_of(owned.begin(), owned.end(), [&](auto handle) {
            return std::get<Player>(handles_.Get(handle, script.owner, PlayerType)).SameConnection(player);
        })) ++missing;
    if (handles_.Owned(script.owner).size() + script.commands.size() + script.events.size() + missing > 128) {
        script.error = "Plugin resource limit (128) reached."; return false;
    }
    std::vector<Cell> created;
    try {
        for (const auto& player : players) {
            const auto handle = PlayerHandle(script, player);
            if (std::find(owned.begin(), owned.end(), handle) == owned.end()) created.push_back(handle);
            output.push_back(handle);
        }
    } catch (...) {
        for (auto handle : created) handles_.Remove(handle, script.owner, PlayerType);
        output.clear();
        throw;
    }
    return true;
}

void Foundation::BindPlayers(Script& script) {
    auto bind = [&](const char* name, int count, auto callback) {
        runtime_.Bind(*script.vm, name, count, [&, callback](const Arguments& args) -> Cell { Thread(); return callback(args); });
    };
    const auto action = [&](const Arguments& args, bool slap) {
        if (script.state != PluginState::Running) { script.error = "Player actions require a running plugin callback."; return false; }
        const auto damage = slap ? args.Int(2) : 0;
        if (damage < 0 || damage > 1000) { script.error = "Slap damage must be from 0 to 1000."; return false; }
        Player player;
        if (ResolvePlayer(script, args.Int(1), player) != KEEL_RESULT_OK) return false;
        if (!player.alive) { script.error = "Target is not alive."; return false; }
        const auto result = slap ? host_.SlapPlayer(player, damage) : host_.SlayPlayer(player);
        if (result == KEEL_RESULT_OK) return true;
        script.error = result == KEEL_RESULT_NOT_FOUND ? "Player pawn is no longer available." :
            result == KEEL_RESULT_UNSUPPORTED ? "Player actions are unavailable on this server." :
            "Player action failed (KeelResult " + std::to_string(result) + ").";
        return false;
    };
    bind("SlapPlayer", 2, [action](const Arguments& args) { return action(args, true); });
    bind("SlayPlayer", 1, [action](const Arguments& args) { return action(args, false); });
    bind("KickPlayer", 2, [&](const Arguments& args) {
        if (script.state != PluginState::Running) { script.error = "Player actions require a running plugin callback."; return false; }
        const auto reason = args.String(2, 300);
        if (reason.empty() || std::any_of(reason.begin(), reason.end(), [](unsigned char c) { return c < 32 || c == 127; })) {
            script.error = "Kick reason requires a single line of text."; return false;
        }
        Player player;
        if (ResolvePlayer(script, args.Int(1), player) != KEEL_RESULT_OK) return false;
        const bool self = args.Int(1) == script.callback_player;
        if (self && script.self_kicks.contains(args.Int(1))) { script.error = "A disconnect was already requested for this caller."; return false; }
        if (self) script.self_kicks.emplace(args.Int(1), player);
        struct Pending {
            Script& script; Cell handle; bool armed;
            ~Pending() { if (armed) script.self_kicks.erase(handle); }
        } pending{script, args.Int(1), self};
        const auto result = host_.KickPlayer(player, reason);
        if (result == KEEL_RESULT_OK) { pending.armed = false; return true; }
        script.error = result == KEEL_RESULT_NOT_FOUND ? "Player is no longer available." :
            result == KEEL_RESULT_UNSUPPORTED ? "Player disconnect is unavailable on this server." :
            "Player disconnect failed (KeelResult " + std::to_string(result) + ").";
        return false;
    });
    bind("GetPlayers", 2, [&](const Arguments& args) {
        const auto capacity = args.Int(2);
        args.OutputArray(1, capacity, {});
        std::vector<Player> players;
        std::vector<Cell> output;
        if (!ReadPlayers(script, players) || !MakePlayerHandles(script, players, capacity, output)) return Cell{-1};
        args.OutputArray(1, capacity, output);
        return static_cast<Cell>(output.size());
    });
    bind("FindTargets", 5, [&](const Arguments& args) {
        const auto selector = args.String(2, 1024);
        const auto capacity = args.Int(4);
        args.OutputArray(3, capacity, {});
        if (args.Int(5) != 0 && args.Int(5) != 1) throw NativeError("invalid multiple-target flag");
        Player caller;
        if (args.Int(1) && ResolvePlayer(script, args.Int(1), caller) != KEEL_RESULT_OK) return Cell{-1};
        if (selector.empty() || selector.size() > 256 || std::any_of(selector.begin(), selector.end(), [](unsigned char c) { return c < 32 || c == 127; })) {
            script.error = "A valid target is required."; return Cell{-1};
        }
        const bool group = selector.front() == '@', user = selector.front() == '#';
        if (group && selector != "@me" && selector != "@all" && selector != "@alive" && selector != "@dead" &&
            selector != "@t" && selector != "@ct" && selector != "@bots") {
            script.error = "Unknown target selector \"" + selector + "\"."; return Cell{-1};
        }
        if (group && selector != "@me" && !args.Int(5)) {
            script.error = "This command requires one player."; return Cell{-1};
        }
        if (selector == "@me" && !args.Int(1)) { script.error = "The server console has no player target."; return Cell{-1}; }
        std::uint64_t steam = 0;
        try { steam = ParseSteamIdentity(selector); } catch (const std::exception&) {}
        const bool numeric = std::all_of(selector.begin(), selector.end(), [](unsigned char c) { return c >= '0' && c <= '9'; });
        if (!steam && (selector.starts_with("STEAM_") || selector.front() == '[' || numeric)) {
            script.error = "Invalid Steam identity. Use #userid for numeric player names."; return Cell{-1};
        }
        int user_id = -1;
        if (user) {
            const auto [end, result] = std::from_chars(selector.data() + 1, selector.data() + selector.size(), user_id);
            if (result != std::errc{} || end != selector.data() + selector.size() || user_id < 0) {
                script.error = "Use # followed by a user ID from sr_who."; return Cell{-1};
            }
        }
        std::vector<Player> players, selected;
        if (!ReadPlayers(script, players)) return Cell{-1};
        const auto lower = [](std::string value) {
            for (auto& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            return value;
        };
        const auto name = lower(selector);
        if (!group && !user && !steam)
            for (const auto& player : players) if (lower(player.name) == name) selected.push_back(player);
        if (selected.empty()) for (const auto& player : players) {
            const bool match = steam ? player.authenticated && !player.bot && player.steam_id == steam :
                user ? player.user_id == user_id : selector == "@me" ? player.SameConnection(caller) :
                selector == "@all" ? true : selector == "@alive" ? player.alive : selector == "@dead" ? !player.alive :
                selector == "@t" ? player.team == 2 : selector == "@ct" ? player.team == 3 : selector == "@bots" ? player.bot :
                lower(player.name).find(name) != std::string::npos;
            if (match) selected.push_back(player);
        }
        std::string quoted;
        for (char c : selector) { if (c == '"' || c == '\\') quoted += '\\'; quoted += c; }
        if (selected.empty()) { script.error = "No players match \"" + quoted + "\"."; return Cell{-1}; }
        if ((!group || !args.Int(5)) && selected.size() != 1) {
            script.error = "More than one player matches \"" + quoted + "\". Use a #userid from sr_who."; return Cell{-1};
        }
        std::vector<Cell> output;
        if (!MakePlayerHandles(script, selected, capacity, output)) return Cell{-1};
        args.OutputArray(3, capacity, output);
        return static_cast<Cell>(output.size());
    });
    bind("GetPlayerUserID", 2, [&](const Arguments& args) {
        args.OutputCell(2, -1);
        Player player;
        if (ResolvePlayer(script, args.Int(1), player) != KEEL_RESULT_OK || player.user_id < 0) return 0;
        args.OutputCell(2, player.user_id); return 1;
    });
    bind("GetPlayerTeam", 2, [&](const Arguments& args) {
        args.OutputCell(2, 0);
        Player player;
        if (ResolvePlayer(script, args.Int(1), player) != KEEL_RESULT_OK) return 0;
        args.OutputCell(2, player.team); return 1;
    });
    bind("IsPlayerAlive", 1, [&](const Arguments& args) {
        Player player;
        return ResolvePlayer(script, args.Int(1), player) == KEEL_RESULT_OK && player.alive;
    });
    bind("IsPlayerBot", 1, [&](const Arguments& args) {
        Player player;
        return ResolvePlayer(script, args.Int(1), player) == KEEL_RESULT_OK && player.bot;
    });
    bind("IsPlayerConnected", 1, [&](const Arguments& args) {
        if (!handles_.Contains(args.Int(1), script.owner, PlayerType)) return false;
        Player player;
        return ResolvePlayer(script, args.Int(1), player) == KEEL_RESULT_OK;
    });
}

}
