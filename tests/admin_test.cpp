#include "foundation.h"

#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>

static void Require(bool value, const char* message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

class Host final : public sr::GameHost {
public:
    std::map<int, sr::Player> players;
    std::vector<std::string> replies, logs;
    std::set<std::string> commands;
    std::string menu;
    std::function<void(const sr::Player&)> on_next;
    KeelResult enumeration = KEEL_RESULT_OK;
    KeelResult Lookup(int slot, sr::Player& player) override {
        if (!players.contains(slot))
            return KEEL_RESULT_NOT_FOUND;

        player = players.at(slot);
        return KEEL_RESULT_OK;
    }

    KeelResult NextPlayer(int after, sr::Player& player) override {
        if (enumeration != KEEL_RESULT_OK)
            return enumeration;

        const auto next = players.upper_bound(after);

        if (next == players.end())
            return KEEL_RESULT_NOT_FOUND;

        player = next->second;

        if (on_next)
            on_next(player);

        return KEEL_RESULT_OK;
    }

    KeelResult Reply(const sr::Player* player, const std::string& text) override {
        Require(!player || (players.contains(player->slot) && player->SameConnection(players.at(player->slot))),
                "reply targets a current connection");

        replies.push_back(text);
        return KEEL_RESULT_OK;
    }

    void Log(const std::string& text) override {
        logs.push_back(text);
        std::cout << text << '\n';
    }

    KeelResult RegisterCommand(const std::string& name) override {
        return commands.insert(name).second ? KEEL_RESULT_OK : KEEL_RESULT_ALREADY_EXISTS;
    }

    KeelResult RemoveCommand(const std::string& name) override {
        return commands.erase(name) ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND;
    }

    KeelResult ListenEvent(const std::string&) override {
        return KEEL_RESULT_OK;
    }

    KeelResult RemoveEvent(const std::string&) override {
        return KEEL_RESULT_OK;
    }

    KeelResult RenderMenu(const sr::Player&, const std::string& html, int) override {
        menu = html;
        return KEEL_RESULT_OK;
    }

    KeelResult AcquireProvider(const std::string&, unsigned) override {
        return KEEL_RESULT_UNSUPPORTED;
    }

    KeelResult ReleaseProvider(const std::string&, unsigned) override {
        return KEEL_RESULT_UNSUPPORTED;
    }

    bool Contains(const std::string& part) const {
        for (const auto& reply : replies)
            if (reply.find(part) != std::string::npos)
                return true;

        return false;
    }
};

int main(int argc, char** argv) {
    Require(argc == 5, "admin_test runtime tools-bytecode admin-bytecode root");
    const std::filesystem::path root(argv[4]);
    std::filesystem::create_directories(root / "configs");
    const auto admins = root / "configs/admins.cfg";
    const std::string assignments = R"("Admins" { "Root" { "identity" "[U:1:123]" "group" "root" }
        "Moderator" { "identity" "[U:1:124]" "group" "moderator" } })";

    std::ofstream(root / "configs/admin_groups.cfg") << R"("Groups" { "root" { "immunity" "100" }
        "moderator" { "immunity" "10" "permissions" { "admin.help" "1" "admin.menu" "1" "admin.who" "1" } } })";

    std::ofstream(admins) << assignments;
    auto install = [&](const char* id, const char* binary) {
        const auto directory = root / "plugins" / id;
        std::filesystem::create_directories(directory);
        std::filesystem::copy_file(binary, directory / "main.smx", std::filesystem::copy_options::overwrite_existing);
        std::ofstream(directory / "plugin.json") << "{\"schema\":1,\"id\":\"" << id << "\",\"name\":\"" << id <<
            "\",\"author\":\"tests\",\"version\":\"1.0.0\",\"api\":2,\"entry\":\"main.smx\",\"enabled\":true,\"dependencies\":[]}";

        return directory / "plugin.json";
    };
    Host host;
    host.players = {
        {3, {3, 13, 76561197960265851ULL, true, false, "Peter Brev", 70, 3, true}},
        {4, {4, 14, 76561197960265852ULL, true, false, "Alex", 71, 2, true}},
        {5, {5, 15, 76561197960265853ULL, true, false, "Alexandra", 72, 3, false}},
        {6, {6, 16, 76561197960265854ULL, false, false, "Pending", 73, 1, false}},
        {7, {7, 17, 0, false, true, "Bot", 74, 2, true}}
    };
    sr::Foundation app(host, argv[1], root);
    Require(app.Load(install("tools", argv[2])) && app.Load(install("admin", argv[3])),
            "load actual public-API fixture and bundled administration plugin");

    auto run = [&](int slot, const std::string& text, sr::Origin origin = sr::Origin::ClientConsole) {
        host.replies.clear();
        const bool consumed = app.Dispatch(slot == -1 ? sr::Origin::ServerConsole : origin, slot, text);
        Require(consumed == (slot == -1 || origin != sr::Origin::PublicChat), "command origin handling");
    };
    auto find = [&](const std::string& selector, const std::vector<int>& ids, bool multiple = true, int slot = 3) {
        run(slot, std::string(multiple ? "sr_find " : "sr_find_one ") + selector);
        Require(host.replies.size() == ids.size() + 1 && host.replies.front() == "count=" + std::to_string(ids.size()),
                "selector count");

        for (std::size_t i = 0; i < ids.size(); ++i)
            Require(host.replies[i + 1] == "userid=" + std::to_string(ids[i]), "stable ordered target identity");
    };
    const auto legacy_binary = std::filesystem::path(argv[2]).parent_path() / "steam_legacy.smx";
    Require(app.Load(install("legacy", legacy_binary.string().c_str())), "load older three-argument Steam getter bytecode");
    run(3, "sr_legacy_steam");
    Require(host.Contains("76561197960265851"), "existing SteamID64 call remains binary-compatible");
    run(3, "sr_legacy_activity");
    Require(host.Contains("Used older activity bytecode"), "two-argument activity bytecode remains compatible");
    run(3, "sr_legacy_menu");
    Require(app.MenuInput(host.players.at(3), app.CurrentMenu(host.players.at(3)), sr::MenuInput::Select) &&
                host.Contains("legacy index=0") && app.Unload("legacy"),
            "three-argument menu item bytecode retains index callbacks");

    find("#71", {71});
    find("STEAM_0:0:62", {71});
    find("[U:1:124]", {71});
    find("76561197960265852", {71});
    find("aLeX", {71});
    find("xandra", {72});
    find("@me", {70}, false);
    find("@all", {70, 71, 72, 73, 74});
    find("@alive", {70, 71, 74});
    find("@dead", {72, 73});
    find("@t", {71, 74});
    find("@ct", {70, 72});
    find("@bots", {74});

    for (const auto& selector : {"Al", "@unknown", "#-1", "#2147483648", "123", "STEAM_0:2:62", "[U:1:0]", "nobody"}) {
        run(3, std::string("sr_find ") + selector);
        Require(host.replies.size() == 1 && host.replies.front().find("count=") == std::string::npos,
                "invalid or ambiguous selector returns no partial selection");
    }

    run(3, "sr_find Al");
    Require(host.Contains("More than one player matches \"Al\". Use a #userid from sr_who."),
            "approved ambiguous-target wording");

    run(3, "sr_find nobody");
    Require(host.Contains("No players match \"nobody\"."), "approved missing-target wording");
    run(3, "sr_find " + std::string(256, '"'));
    Require(host.replies.size() == 1 && host.Contains("No players match"),
            "large diagnostic remains readable without faulting the script");

    run(-1, "sr_find @me");
    Require(host.Contains("server console has no player"), "console cannot impersonate self target");
    run(3, "sr_find_one @all");
    Require(host.Contains("requires one player"),
            "single-target commands reject group selectors even when a group might contain one player");

    run(3, "sr_find [U:1:126]");
    Require(host.Contains("No players match"), "unauthenticated identity cannot match");
    host.on_next = [&](const sr::Player& player) {
        if (player.slot == 4)
            ++host.players.at(4).connection;
    };
    find("@all", {70, 72, 73, 74});
    host.on_next = {};
    run(3, "sr_info");
    Require(host.Contains("id=70 team=3 alive=1 bot=0") && host.Contains("STEAM_1:1:61") && host.Contains("[U:1:123]"),
            "typed public metadata and identity formats");

    host.players.at(3).team = 2;
    host.players.at(3).alive = false;
    run(3, "sr_info");
    Require(host.Contains("team=2 alive=0"), "getters read current state after changes");
    run(3, "sr_tools");
    Require(host.Contains("tools completed"), "format and parser assertions execute");
    const auto handles = app.Status().back().handles;

    for (int i = 0; i < 10; ++i)
        run(3, "sr_tools");

    Require(app.Status().back().handles == handles, "enumeration reuses owned player handles");
    run(3, "sr_badformat");
    run(3, "sr_tools");
    Require(host.Contains("tools completed"), "bad format faults without overwriting output or preventing recovery");
    run(3, "sr_remember");
    ++host.players.at(3).connection;
    run(-1, "sr_current");
    Require(host.Contains("disconnected"), "stored player handle rejects replaced connection");
    run(3, "sr_remember");
    app.MapChanged();
    run(-1, "sr_current");
    Require(host.Contains("disconnected"), "connection predicate safely rejects retired map handles");
    host.enumeration = KEEL_RESULT_NOT_READY;
    run(3, "sr_who");
    Require(host.replies.size() == 1 && host.Contains("Could not list players"),
            "enumeration failure cannot look like an empty successful list");

    host.enumeration = KEEL_RESULT_OK;
    run(3, "sr_who");
    Require(host.replies.size() == 5 && host.Contains("#70 Peter Brev | 76561197960265851 STEAM_1:1:61 [U:1:123]") &&
                host.Contains("root immunity=100") && host.Contains("#74 Bot | bot") &&
                host.Contains("Steam authentication pending"),
            "bundled who preserves legacy identity and group information");

    for (int i = 0; i < 140; ++i) {
        ++host.players.at(5).connection;
        run(3, "sr_who");
        Require(host.replies.size() == 5 && host.Contains("#72 Alexandra"),
                "repeated reconnects cannot exhaust player handles between frames");
    }

    Require(app.Pause("admin"), "pause admin before a missed disconnect");
    ++host.players.at(5).connection;
    const auto before_sweep = app.Status().front().handles;
    app.Tick(sr::Foundation::Clock::now());
    Require(app.Status().front().handles < before_sweep,
            "game frame reclaims disconnected players even for a paused script");

    Require(app.Resume("admin"), "resume after player cleanup");
    run(4, "sr_help");
    Require(host.Contains("sr_who") && !host.Contains("sr_reloadadmins"), "help filters command permissions");

    for (auto origin : {sr::Origin::ClientConsole, sr::Origin::PublicChat, sr::Origin::SilentChat}) {
        run(5, origin == sr::Origin::ClientConsole ? "sr_who" : origin == sr::Origin::PublicChat ? "!who" : "/who", origin);
        Require(host.replies.size() == 1 && host.Contains("You do not have access to this command."),
                "same command authority in all origins");
    }

    run(3, "sr_who extra");
    Require(host.replies.size() == 1 && host.Contains("Usage: sr_who"), "real usage response");
    run(-1, "sr_admin");
    Require(host.Contains("Use sr_help in the server console."), "server menu invocation has a concrete alternative");
    run(4, "sr_admin");
    Require(host.menu.find("Connected players") != std::string::npos &&
                host.menu.find("#777777'>  Reload administrators") != std::string::npos,
            "menu includes disabled inaccessible reload item");

    auto session = app.CurrentMenu(host.players.at(4));
    app.MenuInput(host.players.at(4), session, sr::MenuInput::Down);
    app.MenuInput(host.players.at(4), session, sr::MenuInput::Down);
    Require(!app.MenuInput(host.players.at(4), session, sr::MenuInput::Select) && host.replies.empty(),
            "disabled menu action cannot execute");

    app.MenuInput(host.players.at(4), session, sr::MenuInput::Up);
    Require(app.MenuInput(host.players.at(4), session, sr::MenuInput::Select) && host.Contains("#70 Peter Brev"),
            "real SourcePawn menu executes who through public APIs");

    run(3, "sr_admin");
    session = app.CurrentMenu(host.players.at(3));
    app.MenuInput(host.players.at(3), session, sr::MenuInput::Down);
    app.MenuInput(host.players.at(3), session, sr::MenuInput::Down);
    std::ofstream(admins) << R"("Admins" { "Root" { "identity" "[U:1:123]" "group" "moderator" } })";
    app.ReloadPermissions();
    host.replies.clear();
    Require(app.MenuInput(host.players.at(3), session, sr::MenuInput::Select) &&
                host.Contains("You do not have access"),
            "menu callback rechecks action permission after role downgrade");

    std::ofstream(admins) << assignments;
    app.ReloadPermissions();
    run(3, "sr_admin");
    session = app.CurrentMenu(host.players.at(3));
    std::ofstream(admins) << "\"Admins\" {}";
    app.ReloadPermissions();
    host.replies.clear();
    Require(!app.MenuInput(host.players.at(3), session, sr::MenuInput::Select) && host.replies.empty(),
            "revoked menu authority prevents callback");

    std::ofstream(admins) << assignments;
    run(-1, "sr_reloadadmins");
    Require(host.replies.size() == 1 && host.Contains("Administrator configuration reloaded."),
            "server reload restores assignments with one confirmation");

    std::ofstream(admins) << "\"Admins\" {";
    run(3, "sr_reloadadmins");
    Require(host.replies.size() == 1 && host.Contains("Could not reload administrators"),
            "malformed reload produces one failure and retains access");

    run(3, "sr_who");
    Require(host.Contains("root immunity=100"), "failed reload preserves old authorization");
    std::ofstream(admins) << "\"Admins\" {}";
    run(3, "sr_reloadadmins");
    Require(host.replies.size() == 1 && host.Contains("Administrator configuration reloaded."),
            "self-revoking reload still confirms once");

    run(3, "sr_who");
    Require(host.Contains("You do not have access"), "successful reload revokes removed administrator");
    std::ofstream(admins) << assignments;
    run(-1, "sr_reloadadmins");
    host.players.at(3).name = "Peter\n\"Brev\\";
    run(3, "sr_who");
    Require(host.logs.back().find("actor=\"Peter \\\"Brev\\\\\"") != std::string::npos,
            "operator audit escapes quotes and controls independently of chat");

    Require(app.Pause("admin"), "pause bundled plugin");
    run(3, "sr_who");
    Require(host.replies.empty(), "paused admin command cannot run");
    Require(app.Resume("admin") && app.Reload("admin"), "resume and reload bundled commands");
    run(3, "sr_admin");
    Require(app.Unload("admin") && host.menu.empty() && !host.commands.contains("sr_reloadadmins"),
            "admin unload closes menu and removes commands");

    for (const auto& log : host.logs)
        Require(log.find("FAILED:") == std::string::npos, "all actual SourcePawn assertions passed");

    Require(app.Shutdown(), "clean test shutdown");
    std::cout << "Public targeting, text helpers and bundled administration command tests passed.\n";
}
