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
    struct Message {
        int slot;
        std::string text;
    };

    struct Action {
        int slot;
        bool slap;
        int damage;
    };
    std::map<int, sr::Player> players;
    std::vector<Message> messages;
    std::vector<Action> actions;
    std::vector<std::string> logs;
    std::string menu;
    int fail_slot = -2;
    std::function<void()> on_action, on_render;
    KeelResult Lookup(int slot, sr::Player& player) override {
        if (!players.contains(slot))
            return KEEL_RESULT_NOT_FOUND;

        player = players.at(slot);
        return KEEL_RESULT_OK;
    }

    KeelResult NextPlayer(int after, sr::Player& player) override {
        const auto next = players.upper_bound(after);

        if (next == players.end())
            return KEEL_RESULT_NOT_FOUND;

        player = next->second;
        return KEEL_RESULT_OK;
    }

    KeelResult Reply(const sr::Player* player, const std::string& text) override {
        Require(!player || (players.contains(player->slot) && players.at(player->slot).SameConnection(*player)),
                "reply cannot follow recycled slot");

        messages.push_back({player ? player->slot : -1, text});
        return KEEL_RESULT_OK;
    }

    KeelResult Act(const sr::Player& player, bool slap, int damage) {
        Require(players.contains(player.slot) && player.SameConnection(players.at(player.slot)) && player.alive,
                "action requires current living player");

        if (player.slot == fail_slot)
            return KEEL_RESULT_ENGINE_FAILURE;

        actions.push_back({player.slot, slap, damage});

        if (on_action)
            on_action();

        return KEEL_RESULT_OK;
    }

    KeelResult SlapPlayer(const sr::Player& player, int damage) override {
        return Act(player, true, damage);
    }

    KeelResult SlayPlayer(const sr::Player& player) override {
        return Act(player, false, 0);
    }

    void Log(const std::string& text) override {
        Require(text.find("FAILED:") == std::string::npos, text.c_str());
        logs.push_back(text);
        std::cout << text << '\n';
    }

    KeelResult RegisterCommand(const std::string&) override {
        return KEEL_RESULT_OK;
    }

    KeelResult RemoveCommand(const std::string&) override {
        return KEEL_RESULT_OK;
    }

    KeelResult ListenEvent(const std::string&) override {
        return KEEL_RESULT_OK;
    }

    KeelResult RemoveEvent(const std::string&) override {
        return KEEL_RESULT_OK;
    }

    KeelResult RenderMenu(const sr::Player&, const std::string& html, int) override {
        menu = html;

        if (on_render)
            on_render();

        return KEEL_RESULT_OK;
    }

    KeelResult AcquireProvider(const std::string&, unsigned) override {
        return KEEL_RESULT_UNSUPPORTED;
    }

    KeelResult ReleaseProvider(const std::string&, unsigned) override {
        return KEEL_RESULT_UNSUPPORTED;
    }

    std::string Text(int slot) const {
        std::string output;

        for (const auto& message : messages)
            if (message.slot == slot)
                output += message.text + "\n";

        return output;
    }

    bool Has(int slot, const std::string& text) const {
        return Text(slot).find(text) != std::string::npos;
    }

    unsigned Count(int slot) const {
        unsigned count = 0;

        for (const auto& message : messages)
            count += message.slot == slot;

        return count;
    }

    void Clear() {
        messages.clear();
        actions.clear();
        logs.clear();
    }
};

int main(int argc, char** argv) {
    Require(argc == 4, "player_actions_test runtime scripts root");
    const std::filesystem::path root(argv[3]), scripts(argv[2]);
    std::filesystem::create_directories(root / "configs");
    const auto admins = root / "configs/admins.cfg";
    const std::string assignments = R"("Admins" { "Root" { "identity" "[U:1:123]" "group" "root" }
        "Mod" { "identity" "[U:1:124]" "group" "moderator" } "Equal" { "identity" "[U:1:125]" "group" "root" } })";

    std::ofstream(root / "configs/admin_groups.cfg") << R"("Groups" { "root" { "immunity" "100" }
        "moderator" { "immunity" "10" "permissions" { "admin.help" "1" "admin.menu" "1" "admin.slap" "1" } }
        "child_only" { "permissions" { "admin.slay" "1" } } })";

    std::ofstream(admins) << assignments;
    auto install = [&](const char* id) {
        const auto directory = root / "plugins" / id;
        std::filesystem::create_directories(directory);
        std::filesystem::copy_file(scripts / (std::string(id) + ".smx"),
                                   directory / "main.smx",
                                   std::filesystem::copy_options::overwrite_existing);

        std::ofstream(directory / "plugin.json") << "{\"schema\":1,\"id\":\"" << id << "\",\"name\":\"" << id <<
            "\",\"author\":\"tests\",\"version\":\"1.0.0\",\"api\":2,\"entry\":\"main.smx\",\"enabled\":true,\"dependencies\":[]}";

        return directory / "plugin.json";
    };
    Host host;
    host.players = {
        {3, {3, 13, 76561197960265851ULL, true, false, "Peter Brev", 70, 3, true}},
        {4, {4, 14, 76561197960265852ULL, true, false, "Alex", 71, 2, true}},
        {5, {5, 15, 76561197960265853ULL, true, false, "Alexandra", 72, 3, true}},
        {6, {6, 16, 76561197960265854ULL, false, false, "Pending", 73, 1, true}},
        {7, {7, 17, 0, false, true, "Bot", 74, 2, true}},
        {8, {8, 18, 76561197960265856ULL, true, false, "Dead", 75, 3, false}}
    };
    sr::Foundation app(host, argv[1], root);
    Require(app.Load(install("admin")) && app.Load(install("player_actions")) && app.Load(install("command_menus")),
            "load actual bundled plugins and public API fixture");

    auto run = [&](int slot, const std::string& text, sr::Origin origin = sr::Origin::ClientConsole) {
        host.Clear();
        app.Dispatch(slot == -1 ? sr::Origin::ServerConsole : origin, slot, text);
    };
    auto single = [&](int slot, const std::string& text) {
        Require(host.Count(slot) == 1 && host.Has(slot, text), "exactly one issuer result");
    };
    auto input = [&](int slot, sr::MenuInput key) {
        return app.MenuInput(host.players.at(slot), app.CurrentMenu(host.players.at(slot)), key);
    };
    auto select = [&](int slot, unsigned index) {
        for (unsigned i = 0; i < index; ++i)
            Require(input(slot, sr::MenuInput::Down), "navigate menu");

        Require(input(slot, sr::MenuInput::Select), "select menu callback");
    };
    run(3, "sr_help");
    Require(host.Has(3, "sr_slap <target> [damage]") && host.Has(3, "sr_slay <target>"), "help shows live usage");
    run(4, "sr_help");
    Require(host.Has(4, "sr_slap") && !host.Has(4, "sr_slay"), "help hides inaccessible commands");
    run(3, "sr_slap #71 10");
    single(3, "Slapped Alex (10 damage)");
    Require(host.actions.size() == 1 && host.actions[0].slot == 4 && host.actions[0].damage == 10, "typed slap mechanics");
    Require(host.Has(4, "Peter Brev slapped Alex") && host.Has(6, "ADMIN: Slapped Alex") && !host.Count(7),
            "shared default activity recipients");

    Require(host.logs.size() == 1 && host.logs[0].find("userid=71 damage=10 result=success") != std::string::npos,
            "separate per-target audit");

    for (auto origin : {sr::Origin::ClientConsole, sr::Origin::PublicChat, sr::Origin::SilentChat}) {
        run(4,
            origin == sr::Origin::ClientConsole ? "sr_slay #73"
            : origin == sr::Origin::PublicChat  ? "!slay #73"
                                                : "/slay #73",
            origin);

        single(4, "You do not have access");
        Require(host.actions.empty(), "permission enforced across origins");
    }

    run(3, "sr_slay #72");
    single(3, "immunity");
    Require(host.actions.empty(), "root cannot target equal root immunity");
    run(4, "sr_slap #70");
    single(4, "immunity");
    Require(host.actions.empty(), "lower immunity cannot target root");
    run(4, "sr_slap @me");
    single(4, "Slapped Alex (0 damage)");
    Require(host.actions.size() == 1, "authorized self exception");
    run(-1, "sr_slay #72");
    single(-1, "Killed Alexandra");
    Require(host.actions.size() == 1 && !host.actions[0].slap, "console immunity exception");
    run(-1, "sr_slap \"Peter Brev\" 1000");
    single(-1, "Slapped Peter Brev (1000 damage)");

    for (const auto& command : {"sr_slap",
                                "sr_slap #71 1 extra",
                                "sr_slap #71 -1",
                                "sr_slap #71 1001",
                                "sr_slap #71 1x",
                                "sr_slap #71 +1",
                                "sr_slap Al",
                                "sr_slap @unknown",
                                "sr_slap nobody",
                                "sr_slay #75"}) {
        run(3, command);
        Require(host.Count(3) == 1 && host.actions.empty() && host.Count(4) == 0,
                "bad input and dead target fail privately");
    }

    run(3, "sr_slap @all 5");
    single(3, "Slapped 3 players (5 damage); 3 targets failed:");
    Require(host.actions.size() == 3 && host.logs.size() == 6, "partial group outcome audits every target");

    for (const auto& message : host.messages)
        if (message.slot != 3)
            Require(message.text.find("failed") == std::string::npos, "group failure detail stays private");

    host.fail_slot = 7;
    run(3, "sr_slap @t");
    single(3, "Slapped 1 player (0 damage); 1 target failed:");
    Require(host.actions.size() == 1 && host.logs.size() == 2, "mixed group uses singular failure count");
    host.fail_slot = -2;
    host.players.at(7).alive = false;
    run(3, "sr_slay @dead");
    single(3, "No players were killed. 2 targets failed:");
    Require(host.messages.size() == 1 && host.actions.empty() && host.logs.size() == 2,
            "all-failed group has one private result and target audits");

    host.players.at(7).alive = true;
    host.players.at(3).name = std::string(128, '"');
    host.players.at(4).name = std::string(128, '\\');
    run(3, "sr_slap #71");
    Require(host.Count(3) == 1 && host.actions.size() == 1 && host.logs.size() == 1 && host.logs[0].size() > 512,
            "fully escaped maximum names cannot fault logging after successful mechanics");

    host.players.at(3).name = "Peter Brev";
    host.players.at(4).name = "Alex";
    host.fail_slot = 4;
    run(3, "sr_slap #71");
    single(3, "Player action failed");
    Require(host.actions.empty() && host.Count(4) == 0 && host.logs.size() == 1 &&
                host.logs[0].find("result=failed") != std::string::npos,
            "failed mechanics never announce success");

    host.fail_slot = -2;
    app.SetSetting("sr_show_activity", "0");
    run(3, "sr_slay @me");
    single(3, "Killed Peter Brev");
    Require(host.messages.size() == 1, "disabled activity still confirms");
    app.SetSetting("sr_show_activity", "13");
    host.on_action = [&] {
        std::ofstream(admins) << "\"Admins\" {}";
        app.ReloadPermissions();
    };
    run(3, "sr_slap @all");
    single(3, "Slapped 1 player (0 damage); 5 targets failed:");
    Require(host.actions.size() == 1, "permission rechecked for every target after action callback changes assignments");
    host.on_action = {};
    std::ofstream(admins) << assignments;
    app.ReloadPermissions();
    run(3, "sr_admin");
    select(3, 3);
    Require(host.menu.find("Slap damage") != std::string::npos, "admin routes to other script-owned menu");
    select(3, 2);
    Require(host.menu.find("10 damage") != std::string::npos, "damage selection opens target menu");
    select(3, 1);
    single(3, "Slapped Alex (10 damage)");
    Require(host.actions.size() == 1, "tagged menu target applies selected damage");
    run(3, "sr_admin");
    bool nested = false;
    host.on_render = [&] {
        if (host.menu.find("Slap damage") != std::string::npos) {
            nested = true;
            Require(!app.Unload("admin") && !app.Unload("player_actions"),
                    "both script contexts remain pinned during nested menu invocation");
        }
    };
    select(3, 3);
    host.on_render = {};
    Require(nested, "cross-script callback ownership exercised");
    Require(app.Unload("admin") && app.Unload("player_actions"), "retry cleanup after active nested callbacks return");
    Require(app.Load(install("admin")) && app.Load(install("player_actions")), "reload after orderly nested unload retry");
    run(3, "sr_admin");
    select(3, 4);
    select(3, 1);
    single(3, "Killed Alex");
    run(4, "sr_admin");
    select(4, 3);
    select(4, 0);
    select(4, 1);
    single(4, "Slapped Alex (0 damage)");
    auto open_targets = [&] {
        run(3, "sr_admin");
        select(3, 3);
        select(3, 1);
    };
    open_targets();
    ++host.players.at(4).connection;
    select(3, 1);
    single(3, "Player is no longer available");
    Require(host.actions.empty(), "menu target handle cannot redirect after reconnect");
    open_targets();
    host.players.at(4).alive = false;
    select(3, 1);
    single(3, "Target is not alive");
    Require(host.actions.empty(), "menu checks current alive state");
    host.players.at(4).alive = true;
    open_targets();
    std::ofstream(admins)
        << R"("Admins" { "Root" { "identity" "[U:1:123]" "group" "root" } "Changed" { "identity" "[U:1:124]" "group" "root" } })";

    app.ReloadPermissions();
    select(3, 1);
    single(3, "immunity");
    Require(host.actions.empty(), "menu checks changed target immunity");
    std::ofstream(admins) << assignments;
    app.ReloadPermissions();
    open_targets();
    std::ofstream(admins) << "\"Admins\" {}";
    app.ReloadPermissions();
    Require(!input(3, sr::MenuInput::Select) && host.actions.empty() && !app.CurrentMenu(host.players.at(3)),
            "revoked actor permission cancels target menu");

    std::ofstream(admins) << assignments;
    app.ReloadPermissions();

    for (int mode = 0; mode < 3; ++mode) {
        run(3, "sr_admin");

        if (mode == 0)
            Require(app.Pause("player_actions"), "pause menu provider");
        else
            Require(app.Unload("player_actions"), "unload menu provider");

        if (mode == 2)
            Require(app.Load(install("player_actions")), "load replacement menu provider");

        select(3, 3);

        if (mode == 2)
            Require(host.menu.find("Slap damage") != std::string::npos,
                    "old main menu resolves replacement callback safely");
        else
            single(3, "Command menu is unavailable");

        if (mode == 0) {
            run(3, "sr_help");
            Require(!host.Has(3, "sr_slap"), "paused provider disappears from help");
            Require(app.Resume("player_actions"), "resume provider");
        }

        if (mode == 1)
            Require(app.Load(install("player_actions")), "restore menu provider");
    }

    run(3, "sr_admin");
    select(3, 3);
    select(3, 2);
    Require(input(3, sr::MenuInput::Back) && host.menu.find("Slap damage") != std::string::npos && host.actions.empty(),
        "target selection backs to damage choices without applying a slap");

    Require(input(3, sr::MenuInput::Back) && host.menu.find("Source2Root administration") != std::string::npos &&
                host.actions.empty(),
            "damage choices back across scripts to administration");

    Require(input(3, sr::MenuInput::Back) && host.menu.empty(), "top-level first page closes");
    run(3, "sr_admin");
    select(3, 4);
    Require(app.Unload("admin"), "parent provider can unload while child is open");
    Require(input(3, sr::MenuInput::Back) && host.menu.empty() && host.Has(3, "Command menu is unavailable"),
        "back resolves parent provider at use and does not retain an unloaded callback");

    Require(app.Load(install("admin")), "restore parent provider");
    run(3, "sr_admin");
    select(3, 4);
    std::ofstream(admins) << R"("Admins" { "Root" { "identity" "[U:1:123]" "group" "child_only" } })";
    app.ReloadPermissions();
    Require(input(3, sr::MenuInput::Back) && host.menu.empty() && host.Has(3, "You do not have access"),
        "return to administration rechecks parent permission");

    std::ofstream(admins) << assignments;
    app.ReloadPermissions();
    run(3, "sr_routes recurse");
    Require(host.Has(3, "recursion limit") && host.Has(3, "entered=8"), "nested command menus have bounded recursion");
    run(3, "sr_routes recurse");
    Require(host.Has(3, "entered=8"), "depth guard resets after nested returns");
    run(-1, "sr_routes");
    single(-1, "Use this command in the game");
    run(3, "sr_routes usage");
    Require(!host.logs.empty() && host.logs.front().find("usage must start") != std::string::npos,
            "malformed usage fails before registration");

    run(3, "sr_routes short");
    single(3, "buffer is too small");
    run(3, "sr_routes damage");
    single(3, "from 0 to 1000");
    Require(host.actions.empty(), "native damage bounds are independent of plugin parser");
    run(3, "sr_routes bad");
    single(3, "Player is no longer available");
    Require(host.actions.empty(), "wrong-type handle cannot reach host");

    for (int slot = 9; slot < 67; ++slot)
        host.players.emplace(slot,
                             sr::Player{slot,
                                        static_cast<unsigned>(slot + 20),
                                        0,
                                        false,
                                        true,
                                        "Bot" + std::to_string(slot),
                                        slot + 70,
                                        2,
                                        true});

    run(3, "sr_admin");
    select(3, 4);
    select(3, 40);
    Require(host.actions.size() == 1 && host.actions[0].slot == 43,
            "64-player roster menu can select beyond old 32-item limit");

    run(3, "sr_admin");
    select(3, 3);
    Require(app.Unload("player_actions") && host.menu.empty(), "unload closes action-owned menu");
    Require(app.Shutdown(), "shutdown nested/menu resources");

    for (const auto& log : host.logs)
        Require(log.find("FAILED:") == std::string::npos, "actual script fixture assertions pass");

    std::cout << "Bundled player actions, live command menus, authority and activity tests passed.\n";
}
