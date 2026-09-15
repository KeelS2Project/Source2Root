#include "foundation.h"

#include <cstdlib>
#include <fstream>
#include <iostream>

static void Require(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}

class Host final : public sr::GameHost {
public:
    sr::Player player{3, 10, 76561197960265851ULL, true, false, "Ada"};
    std::map<int, sr::Player> peers;
    std::vector<std::string> replies;
    std::set<std::string> commands;
    std::string menu;
    KeelResult Lookup(int slot, sr::Player& result) override {
        if (slot == player.slot) result = player;
        else if (peers.contains(slot)) result = peers.at(slot);
        else return KEEL_RESULT_NOT_FOUND;
        return KEEL_RESULT_OK;
    }
    KeelResult Reply(const sr::Player*, const std::string& text) override {
        replies.push_back(text);
        return KEEL_RESULT_OK;
    }
    void Log(const std::string& text) override { std::cout << text << '\n'; }
    KeelResult RegisterCommand(const std::string& name) override {
        return commands.insert(name).second ? KEEL_RESULT_OK : KEEL_RESULT_ALREADY_EXISTS;
    }
    KeelResult RemoveCommand(const std::string& name) override {
        return commands.erase(name) ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND;
    }
    KeelResult RenderMenu(const sr::Player& target, const std::string& html) override {
        sr::Player current;
        Require(Lookup(target.slot, current) == KEEL_RESULT_OK && target.SameConnection(current), "menu must target the current connection");
        menu = html;
        return KEEL_RESULT_OK;
    }
    KeelResult PlayerHealth(const sr::Player&, std::int32_t& health) override {
        health = 80;
        return KEEL_RESULT_OK;
    }
    KeelResult ListenEvent(const std::string&) override { return KEEL_RESULT_NOT_FOUND; }
    KeelResult RemoveEvent(const std::string&) override { return KEEL_RESULT_NOT_FOUND; }
    KeelResult AcquireProvider(const std::string&, unsigned) override { return KEEL_RESULT_NOT_FOUND; }
    KeelResult ReleaseProvider(const std::string&, unsigned) override { return KEEL_RESULT_NOT_FOUND; }
};

int main(int argc, char** argv) {
    Require(argc == 5, "authoring_test runtime script manifest fixture-root");
    const std::filesystem::path root(argv[4]);
    std::filesystem::create_directories(root / "plugins/player_status");
    std::filesystem::create_directories(root / "configs");
    std::filesystem::copy_file(argv[2], root / "plugins/player_status/player_status.smx",
        std::filesystem::copy_options::overwrite_existing);
    const auto manifest = root / "plugins/player_status/plugin.json";
    std::filesystem::copy_file(argv[3], manifest, std::filesystem::copy_options::overwrite_existing);
    std::ofstream(root / "configs/admin_groups.cfg") << R"("Groups" { "fixture" { "immunity" "10" "permissions" { "demo.hello" "1" "demo.status" "1" } } })";
    const auto permissions = root / "configs/admins.cfg";
    auto grant = [&](bool allowed) {
        std::ofstream output(permissions);
        output << (allowed ? R"("Admins" { "Fixture" { "identity" "STEAM_0:1:61" "group" "fixture" } })"
                           : R"("Admins" {})");
    };
    grant(true);
    Host host;
    sr::Foundation app(host, argv[1], root);
    Require(app.Load(manifest), "load the compiled public example");
    Require(app.Dispatch(sr::Origin::ServerConsole, -1, "sr_status extra"), "dispatch arguments");
    Require(host.replies.back() == "Usage: sr_status", "arguments produce usage");
    Require(app.Dispatch(sr::Origin::ServerConsole, -1, "sr_status"), "server invocation");
    Require(host.replies.back() == "Use this command in the game.", "server has no player");
    app.Dispatch(sr::Origin::ClientConsole, 3, "sr_status");
    auto session = app.CurrentMenu(host.player);
    Require(session && host.menu.find("Show name") != std::string::npos, "render example menu");
    Require(app.MenuInput(host.player, session, sr::MenuInput::Select), "execute actual SourcePawn menu callback");
    Require(host.replies.back() == "Ada" && host.menu.empty(), "read player name and close menu");
    app.Dispatch(sr::Origin::SilentChat, 3, "/status");
    session = app.CurrentMenu(host.player);
    app.MenuInput(host.player, session, sr::MenuInput::Down);
    app.MenuInput(host.player, session, sr::MenuInput::Down);
    Require(app.MenuInput(host.player, session, sr::MenuInput::Select), "select health");
    Require(host.replies.back() == "You are alive.", "read health through public API");
    app.Dispatch(sr::Origin::ClientConsole, 3, "sr_status");
    session = app.CurrentMenu(host.player);
    grant(false);
    app.ReloadPermissions();
    const auto before = host.replies.size();
    Require(!app.MenuInput(host.player, session, sr::MenuInput::Select), "recheck revoked permission");
    Require(host.replies.size() == before, "revoked selection never calls script");
    grant(true);
    app.ReloadPermissions();
    app.Dispatch(sr::Origin::ClientConsole, 3, "sr_status");
    session = app.CurrentMenu(host.player);
    const auto old_player = host.player;
    ++host.player.connection;
    Require(!app.MenuInput(host.player, session, sr::MenuInput::Select), "reject reused player slot");
    app.Disconnected(old_player.slot, old_player.connection);
    app.Dispatch(sr::Origin::ClientConsole, 3, "sr_status");
    Require(app.Unload("player_status") && host.menu.empty() && host.commands.empty(), "unload closes menu and releases command");
    Require(app.Load(manifest) && app.Reload("player_status"), "load and reload recover");
    Require(app.Unload("player_status"), "release public example before permission fixture");
    const auto access_dir = root / "plugins/access";
    std::filesystem::create_directories(access_dir);
    std::filesystem::copy_file(std::filesystem::path(argv[2]).parent_path() / "access.smx", access_dir / "access.smx",
        std::filesystem::copy_options::overwrite_existing);
    std::ofstream(access_dir / "plugin.json") << R"({"schema":1,"id":"access","name":"Access fixture","author":"tests","version":"1.0.0","api":2,"entry":"access.smx","enabled":true,"dependencies":[]})";
    const std::string administrators = R"("Admins" { "Ada" { "identity" "[U:1:123]" "group" "root" }
        "Moderator" { "identity" "[U:1:124]" "group" "moderator" } "Equal" { "identity" "[U:1:125]" "group" "root" } })";
    auto groups = [&](int immunity) {
        std::ofstream(root / "configs/admin_groups.cfg") << "\"Groups\" { \"root\" { \"immunity\" \"" << immunity <<
            "\" } \"moderator\" { \"immunity\" \"10\" \"permissions\" { \"admin.kick\" \"1\" } } }";
    };
    groups(100);
    std::ofstream(permissions) << administrators;
    app.ReloadPermissions();
    host.peers.emplace(4, sr::Player{4, 20, 76561197960265852ULL, true, false, "Moderator"});
    host.peers.emplace(5, sr::Player{5, 30, 76561197960265853ULL, true, false, "Equal root"});
    Require(app.Load(access_dir / "plugin.json"), "load actual permission native bytecode");
    auto access = [&](int slot, const std::string& argument) {
        Require(app.Dispatch(slot < 0 ? sr::Origin::ServerConsole : sr::Origin::ClientConsole, slot, "sr_access " + argument), "dispatch access fixture");
    };
    access(3, "group");
    Require(host.replies[host.replies.size() - 2] == "root" && host.replies.back() == "immunity 100", "public group and immunity output natives");
    access(3, "remember");
    access(4, "target");
    Require(host.replies.back() == "denied", "client-console targeting enforces higher immunity");
    app.Dispatch(sr::Origin::PublicChat, 4, "!access target");
    Require(host.replies.back() == "denied", "public chat uses identical immunity policy");
    app.Dispatch(sr::Origin::SilentChat, 4, "/access target");
    Require(host.replies.back() == "denied", "silent chat uses identical immunity policy");
    access(4, "menu");
    Require(app.MenuInput(host.peers.at(4), app.CurrentMenu(host.peers.at(4)), sr::MenuInput::Select) && host.replies.back() == "denied",
        "actual SourcePawn menu selection enforces target immunity");
    access(4, "remember");
    access(4, "target");
    Require(host.replies.back() == "allowed", "self-target allowed with declared permission");
    access(4, "ban");
    Require(host.replies.back() == "denied", "self-target cannot bypass missing command permission");
    access(3, "menu");
    groups(5);
    app.ReloadPermissions();
    Require(app.MenuInput(host.player, app.CurrentMenu(host.player), sr::MenuInput::Select) && !host.replies.empty() && host.replies.back() == "denied",
        "menu callback rechecks changed root immunity before acting");
    groups(100);
    app.ReloadPermissions();
    access(3, "menu");
    ++host.peers.at(4).connection;
    host.replies.clear();
    Require(app.MenuInput(host.player, app.CurrentMenu(host.player), sr::MenuInput::Select) && host.replies.back() == "denied",
        "stale remembered target cannot resolve to a reused player slot");
    access(5, "remember");
    access(3, "target");
    Require(host.replies.back() == "denied", "root script invocation cannot bypass equal immunity");
    access(-1, "target");
    Require(host.replies.back() == "allowed", "console invocation can target root");
    access(4, "offline");
    Require(host.replies.back() == "denied", "script offline-identity native enforces immunity");
    access(-1, "offline");
    Require(host.replies.back() == "allowed", "console offline-identity exception");
    for (int i = 0; i < 4; ++i) access(3, "invalid");
    Require(host.replies.back() == "invalid identity rejected" && app.Status().front().state == sr::PluginState::Running,
        "malformed user identities report failure without retiring the plugin");
    std::ofstream(permissions) << "\"Admins\" {";
    access(3, "load");
    Require(host.replies.back() == "reload denied or invalid", "script reload reports malformed configuration");
    access(3, "group");
    Require(host.replies.back() == "immunity 100", "failed reload keeps prior permissions");
    std::ofstream(permissions) << "\"Admins\" {}";
    access(4, "load");
    Require(host.replies.back() == "reload denied or invalid", "ordinary admin cannot invoke privileged reload native");
    access(3, "load");
    Require(host.replies.back() == "reloaded", "root can reload administrator configuration");
    access(3, "group");
    Require(host.replies.back() == "no administrator", "successful reload immediately revokes removed identity");
    std::ofstream(permissions) << administrators;
    access(-1, "load");
    Require(host.replies.back() == "reloaded", "server can restore administrators through the public native");
    Require(app.Shutdown(), "clean shutdown");
    std::cout << "Public example executed with arguments, permissions, player access, menus and cleanup.\n";
}
