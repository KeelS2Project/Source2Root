#include "foundation.h"

#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>

static void Require(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}

class Host final : public sr::GameHost {
public:
    struct Message { int slot; std::string text; };
    std::map<int, sr::Player> players;
    std::vector<Message> messages, attempts;
    std::vector<std::string> logs;
    std::function<void(const sr::Player&)> on_next;
    int fail_lookup = -1, fail_reply = -2;
    KeelResult enumeration = KEEL_RESULT_OK;
    bool invalid_next = false;
    unsigned enumeration_calls = 0;
    KeelResult Lookup(int slot, sr::Player& player) override {
        if (slot == fail_lookup) return KEEL_RESULT_ENGINE_FAILURE;
        if (!players.contains(slot)) return KEEL_RESULT_NOT_FOUND;
        player = players.at(slot);
        return KEEL_RESULT_OK;
    }
    KeelResult NextPlayer(int after, sr::Player& player) override {
        ++enumeration_calls;
        if (enumeration != KEEL_RESULT_OK) return enumeration;
        const auto next = players.upper_bound(after);
        if (next == players.end()) return KEEL_RESULT_NOT_FOUND;
        player = next->second;
        if (on_next) on_next(player);
        if (invalid_next) player.slot = after;
        return KEEL_RESULT_OK;
    }
    KeelResult Reply(const sr::Player* player, const std::string& text) override {
        const auto slot = player ? player->slot : -1;
        attempts.push_back({slot, text});
        if (slot == fail_reply) return KEEL_RESULT_ENGINE_FAILURE;
        if (player && (!players.contains(slot) || !players.at(slot).SameConnection(*player))) return KEEL_RESULT_NOT_FOUND;
        messages.push_back({slot, text});
        return KEEL_RESULT_OK;
    }
    void Log(const std::string& text) override { logs.push_back(text); }
    KeelResult RegisterCommand(const std::string&) override { return KEEL_RESULT_OK; }
    KeelResult RemoveCommand(const std::string&) override { return KEEL_RESULT_OK; }
    KeelResult ListenEvent(const std::string&) override { return KEEL_RESULT_OK; }
    KeelResult RemoveEvent(const std::string&) override { return KEEL_RESULT_OK; }
    KeelResult RenderMenu(const sr::Player&, const std::string&, int) override { return KEEL_RESULT_OK; }
    KeelResult AcquireProvider(const std::string&, unsigned) override { return KEEL_RESULT_UNSUPPORTED; }
    KeelResult ReleaseProvider(const std::string&, unsigned) override { return KEEL_RESULT_UNSUPPORTED; }
    void Clear() { messages.clear(); attempts.clear(); logs.clear(); enumeration_calls = 0; }
    std::string Text(int slot) const {
        std::string result;
        for (const auto& message : messages) if (message.slot == slot) {
            Require(result.empty(), "each recipient receives at most one message");
            result = message.text;
        }
        return result;
    }
};

int main(int argc, char** argv) {
    Require(argc == 4, "activity_test runtime bytecode fixture-root");
    const std::filesystem::path root(argv[3]);
    const auto plugin = root / "plugins/activity";
    std::filesystem::create_directories(plugin);
    std::filesystem::create_directories(root / "configs");
    std::filesystem::copy_file(argv[2], plugin / "activity.smx", std::filesystem::copy_options::overwrite_existing);
    std::ofstream(plugin / "plugin.json") << R"({"schema":1,"id":"activity","name":"Activity fixture","author":"tests","version":"1.0.0","api":2,"entry":"activity.smx","enabled":true,"dependencies":[]})";
    std::ofstream(root / "configs/admin_groups.cfg") << R"("Groups" { "root" {} "observer" {} })";
    const auto admins = root / "configs/admins.cfg";
    const std::string assignments = R"("Admins" { "Issuer" { "identity" "[U:1:123]" "group" "root" }
        "Other" { "identity" "[U:1:124]" "group" "root" } "Unverified" { "identity" "[U:1:126]" "group" "root" }
        "Observer" { "identity" "[U:1:128]" "group" "observer" } })";
    std::ofstream(admins) << assignments;
    Host host;
    for (int slot = 3; slot <= 9; ++slot)
        host.players.emplace(slot, sr::Player{slot, static_cast<unsigned>(slot + 10),
            76561197960265851ULL + slot - 3, true, slot == 7 || slot == 9, slot == 3 ? "Peter Brev" : "Recipient"});
    host.players.at(6).authenticated = false;
    sr::Foundation app(host, argv[1], root);
    Require(app.Load(plugin / "plugin.json"), "load compiled activity fixture and refuse startup announcements");
    Require(host.messages.empty(), "initialization never sends activity");
    auto dispatch = [&](sr::Origin origin, int slot, const std::string& argument = "") {
        const bool chat = origin == sr::Origin::PublicChat || origin == sr::Origin::SilentChat;
        const auto command = chat ? (origin == sr::Origin::SilentChat ? "/activity" : "!activity") : "sr_activity";
        const bool consumed = app.Dispatch(origin, slot, std::string(command) + (argument.empty() ? "" : " " + argument));
        Require(consumed == (origin != sr::Origin::PublicChat), "public chat remains visible; other invocations are consumed");
    };
    const std::string confirmation = "Slapped Kiddo (10 damage)", anonymous = "ADMIN: " + confirmation;
    auto verify = [&](int mask, int issuer, const std::string& name) {
        Require(host.Text(issuer) == confirmation, "issuer always receives exactly one private confirmation");
        std::size_t expected = 1;
        for (const auto& [slot, player] : host.players) {
            if (slot == issuer) continue;
            const bool admin = slot == 3 || slot == 4 || slot == 8;
            const bool visible = !player.bot && (mask & (admin ? 4 : 1));
            Require(host.Text(slot) == (visible ? (mask & (admin ? 8 : 2) ? name + " slapped Kiddo (10 damage)" : anonymous) : ""),
                "activity mask respects admin, ordinary, unauthenticated and bot recipients");
            expected += visible;
        }
        Require(host.messages.size() == expected, "no extra broadcast or issuer duplicate");
        Require(host.logs.size() == 1 && host.logs.back() == "activity: activity delivered", "shared API creates no separate successful audit log");
    };
    for (int mask = 0; mask <= 15; ++mask) {
        Require(app.SetSetting("sr_show_activity", std::to_string(mask)), "set each accepted activity mask");
        for (auto origin : {sr::Origin::ClientConsole, sr::Origin::PublicChat, sr::Origin::SilentChat}) {
            host.Clear(); dispatch(origin, 3); verify(mask, 3, "Peter Brev");
        }
        host.Clear(); dispatch(sr::Origin::ServerConsole, -1); verify(mask, -1, "Server");
        host.Clear(); dispatch(sr::Origin::ClientConsole, 5); verify(mask, 5, "Recipient");
        if (!(mask & 5)) Require(host.enumeration_calls == 0, "disabled audience flags do not query recipients");
    }
    Require(app.SetSetting("sr_show_activity", "13"), "restore approved default");
    for (int mask = 0; mask <= 15; ++mask) {
        app.SetSetting("sr_show_activity", std::to_string(mask));
        host.Clear(); dispatch(sr::Origin::ClientConsole, 3, "override");
        Require(host.Text(3) == "Slapped Kiddo (10 damage); 1 target failed: private detail", "custom confirmation reaches issuer once");
        for (const auto& message : host.messages) if (message.slot != 3)
            Require(message.text.find("failed") == std::string::npos && message.text.find("private") == std::string::npos,
                "failure details never enter named or anonymous activity");
    }
    host.Clear(); dispatch(sr::Origin::ClientConsole, 3, "validate");
    Require(host.messages.empty(), "invalid confirmation is rejected before any delivery");
    app.SetSetting("sr_show_activity", "13");
    host.Clear(); dispatch(sr::Origin::ClientConsole, 3, "menu");
    Require(host.messages.empty(), "opening a menu does not announce");
    app.SetSetting("sr_show_activity", "0");
    Require(app.MenuInput(host.players.at(3), app.CurrentMenu(host.players.at(3)), sr::MenuInput::Select), "actual SourcePawn menu callback");
    verify(0, 3, "Peter Brev");
    host.Clear(); dispatch(sr::Origin::ClientConsole, 3, "menu");
    std::ofstream(admins) << "\"Admins\" {}";
    app.ReloadPermissions();
    Require(!app.MenuInput(host.players.at(3), app.CurrentMenu(host.players.at(3)), sr::MenuInput::Select), "revoked menu permission prevents announcement");
    Require(host.messages.empty(), "no activity from revoked selection");
    std::ofstream(admins) << assignments; app.ReloadPermissions();
    app.SetSetting("sr_show_activity", "13");
    host.Clear(); dispatch(sr::Origin::ClientConsole, 3, "remember");
    ++host.players.at(3).connection;
    dispatch(sr::Origin::ServerConsole, -1, "stored");
    Require(host.messages.empty() && host.logs.back().find("Player is no longer available.") != std::string::npos,
        "remembered actor cannot become a replacement connection or server caller");
    host.Clear();
    host.on_next = [&](const sr::Player& player) { if (player.slot == 5) ++host.players.at(5).connection; };
    dispatch(sr::Origin::ClientConsole, 3);
    Require(host.Text(5).empty() && host.Text(4) == "Peter Brev slapped Kiddo (10 damage)", "skip recycled recipient while continuing other recipients");
    host.on_next = {};
    host.Clear();
    std::ofstream(admins) << R"("Admins" { "Issuer" { "identity" "[U:1:123]" "group" "root" } })";
    app.ReloadPermissions(); dispatch(sr::Origin::ClientConsole, 3);
    Require(host.Text(4) == anonymous && host.Text(8) == anonymous, "recipient classification follows current administrator assignments");
    std::ofstream(admins) << assignments; app.ReloadPermissions();
    host.Clear(); host.fail_reply = 4; dispatch(sr::Origin::ClientConsole, 3);
    Require(host.Text(3) == confirmation && host.Text(4).empty() && host.Text(5) == anonymous,
        "one recipient failure neither repeats issuer confirmation nor stops other recipients");
    Require(host.logs.back().find("Activity announcement failed") != std::string::npos, "partial delivery returns failure with a diagnostic");
    host.fail_reply = 3; host.Clear(); dispatch(sr::Origin::ClientConsole, 3);
    Require(host.Text(3).empty() && host.Text(4) == "Peter Brev slapped Kiddo (10 damage)", "failed confirmation is attempted once and other recipients continue");
    unsigned attempts = 0;
    for (const auto& message : host.attempts) if (message.slot == 3) ++attempts;
    Require(attempts == 1 && host.logs.back().find("Activity confirmation failed") != std::string::npos, "no automatic retry of a failed confirmation");
    host.fail_reply = -2; host.fail_lookup = 4; host.Clear(); dispatch(sr::Origin::ClientConsole, 3);
    Require(host.Text(4).empty() && host.Text(5) == anonymous && host.logs.back().find("recipient lookup failed") != std::string::npos,
        "recipient lookup failure continues other recipients and reports partial delivery");
    host.fail_lookup = -1; host.enumeration = KEEL_RESULT_NOT_READY; host.Clear(); dispatch(sr::Origin::ClientConsole, 3);
    Require(host.messages.size() == 1 && host.Text(3) == confirmation && host.logs.back().find("recipient lookup failed") != std::string::npos,
        "unavailable enumeration still permits the single issuer confirmation");
    host.enumeration = KEEL_RESULT_OK; host.invalid_next = true; host.Clear(); dispatch(sr::Origin::ClientConsole, 3);
    Require(host.enumeration_calls == 1 && host.logs.back().find("Invalid activity recipient") != std::string::npos, "invalid enumeration cannot loop forever");
    host.invalid_next = false;
    for (const auto& argument : {"invalid", "empty"}) {
        host.Clear(); dispatch(sr::Origin::ClientConsole, 3, argument);
        Require(host.messages.empty() && host.logs.back().find("single line") != std::string::npos, "invalid action text sends nothing without retiring the script");
    }
    host.Clear(); dispatch(sr::Origin::ClientConsole, 3, "percent");
    Require(host.Text(3) == "Set accuracy to 100% for %s.", "action text is not a format string");
    host.players.at(3).name = "Peter\n\x02"; host.Clear(); dispatch(sr::Origin::ClientConsole, 3);
    Require(host.Text(4) == "Peter   slapped Kiddo (10 damage)", "actor control bytes cannot inject extra lines or chat colors");
    Require(app.Status().front().state == sr::PluginState::Running, "ordinary activity failures preserve the plugin");
    host.Clear(); Require(app.Pause("activity"), "pause script"); dispatch(sr::Origin::ClientConsole, 3);
    Require(host.messages.empty(), "paused command cannot announce");
    Require(app.Resume("activity"), "resume script");
    host.Clear(); Require(app.Shutdown(), "clean activity shutdown");
    Require(host.messages.empty() && host.logs.front() == "activity: stop activity refused", "retiring plugin cannot announce during cleanup");
    std::cout << "All activity masks, origins, menus, current connections and delivery failures passed.\n";
}
