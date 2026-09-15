#include "foundation.h"

#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>

static void Require(bool value, const char* reason) {
    if (!value) { std::cerr << "menu input: " << reason << '\n'; std::exit(1); }
}

class Host final : public sr::GameHost {
public:
    std::map<int, sr::Player> players;
    std::map<int, KeelPlayerInput> input;
    std::map<int, std::string> menus;
    std::map<int, int> durations;
    unsigned renders = 0;
    std::vector<std::string> replies;
    std::function<void()> on_read;
    KeelResult input_result = KEEL_RESULT_OK;
    bool throw_input = false, fail_clear = false, fail_render = false, fail_release = false;
    unsigned leases = 0, input_reads = 0;
    KeelResult Lookup(int slot, sr::Player& player) override {
        if (!players.contains(slot)) return KEEL_RESULT_NOT_FOUND;
        player = players.at(slot); return KEEL_RESULT_OK;
    }
    KeelResult Reply(const sr::Player* player, const std::string& text) override {
        Require(player && players.contains(player->slot) && player->SameConnection(players.at(player->slot)), "reply to current connection");
        replies.push_back(text); return KEEL_RESULT_OK;
    }
    void Log(const std::string& text) override { std::cout << text << '\n'; }
    KeelResult RegisterCommand(const std::string&) override { return KEEL_RESULT_OK; }
    KeelResult RemoveCommand(const std::string&) override { return KEEL_RESULT_OK; }
    KeelResult ListenEvent(const std::string&) override { return KEEL_RESULT_OK; }
    KeelResult RemoveEvent(const std::string&) override { return KEEL_RESULT_OK; }
    KeelResult RenderMenu(const sr::Player& player, const std::string& html, int duration_ms) override {
        Require(players.contains(player.slot) && player.SameConnection(players.at(player.slot)), "render cannot follow reused slot");
        if (html.empty() ? fail_clear : fail_render) return KEEL_RESULT_ENGINE_FAILURE;
        ++renders; durations[player.slot] = duration_ms;
        Require(html.empty() ? duration_ms == 0 : duration_ms > 0, "render carries a finite lifetime or explicit close");
        menus[player.slot] = html; return KEEL_RESULT_OK;
    }
    KeelResult ReadPlayerInput(const sr::Player& player, KeelPlayerInput& state) override {
        ++input_reads;
        auto callback = std::move(on_read);
        on_read = {};
        if (callback) callback();
        if (throw_input) throw std::runtime_error("input fixture failure");
        if (!players.contains(player.slot) || !players.at(player.slot).SameConnection(player)) return KEEL_RESULT_NOT_FOUND;
        state = input.at(player.slot);
        return input_result;
    }
    KeelResult AcquireProvider(const std::string&, unsigned) override { ++leases; return KEEL_RESULT_OK; }
    KeelResult ReleaseProvider(const std::string&, unsigned) override {
        if (fail_release) return KEEL_RESULT_BUSY;
        Require(leases > 0, "provider lease underflow"); --leases; return KEEL_RESULT_OK;
    }
};

struct NativeAction {
    unsigned calls = 0;
    std::function<void()> callback;
    static void Selected(void* data, const KeelPlayerConnection* player, std::int32_t item) {
        Require(player && player->generation && item == 0, "native selection arguments");
        auto& self = *static_cast<NativeAction*>(data);
        ++self.calls;
        if (self.callback) self.callback();
    }
};

int main(int argc, char** argv) {
    Require(argc == 4, "runtime script root arguments");
    const std::filesystem::path root(argv[3]);
    std::filesystem::create_directories(root / "configs");
    std::filesystem::create_directories(root / "plugins/input");
    const auto admins = root / "configs/admins.cfg";
    const auto manifest = root / "plugins/input/plugin.json";
    const std::string allowed = R"("Admins" { "Tester" { "identity" "[U:1:123]" "group" "root" } "Peer" { "identity" "[U:1:124]" "group" "root" } })";
    std::ofstream(admins) << allowed;
    std::ofstream(root / "configs/admin_groups.cfg") << R"("Groups" { "root" { "immunity" "100" } })";
    std::ofstream(manifest) << R"({"schema":1,"id":"input","name":"Input test","author":"tests","version":"1.0.0","api":2,"entry":"main.smx","enabled":true,"dependencies":[]})";
    std::filesystem::copy_file(argv[2], root / "plugins/input/main.smx", std::filesystem::copy_options::overwrite_existing);
    Host host;
    host.players = {{3, {3, 13, 76561197960265851ULL, true, false, "Tester"}},
                    {4, {4, 14, 76561197960265852ULL, true, false, "Peer"}}};
    for (const auto& [slot, player] : host.players) host.input[slot] = {sizeof(KeelPlayerInput), 0, 0, 1};
    sr::Foundation app(host, argv[1], root);
    Require(app.Load(manifest), "load actual script");
    auto now = sr::Foundation::Clock::now();
    const auto tick = [&] { now += std::chrono::milliseconds(16); app.Tick(now); };
    const auto open = [&](int slot = 3) {
        app.Dispatch(sr::Origin::ClientConsole, slot, "sr_inputmenu");
        Require(app.CurrentMenu(host.players.at(slot)) != 0, "script menu opened");
        Require(host.menus.at(slot).find("Forward/Back: move") != std::string::npos &&
            host.menus.at(slot).find("Use: select") != std::string::npos && host.menus.at(slot).find("Reload: back/close") != std::string::npos,
            "menu explains actual action controls");
    };
    const auto press = [&](std::uint64_t button, int slot = 3) {
        host.input[slot].buttons = 0; tick();
        host.input[slot].buttons = button; tick();
    };
    tick();
    open();
    Require(host.durations.at(3) == 10000, "script initial packet uses requested lifetime");
    auto renders = host.renders;
    for (int i = 0; i < 80; ++i) tick();
    Require(host.renders == renders + 80 && host.durations.at(3) == 8720 && app.CurrentMenu(host.players.at(3)),
        "idle script menu refreshes every frame without extending its lifetime");
    press(KEELS2_BUTTON_BACK);
    Require(host.renders == renders + 83 && host.durations.at(3) == 10000,
        "fresh script navigation restarts the requested inactivity timeout");
    now += std::chrono::milliseconds(8688); app.Tick(now);
    Require(app.CurrentMenu(host.players.at(3)) && host.durations.at(3) == 1312,
        "held navigation does not renew script timeout; menu survives its original deadline");
    press(KEELS2_BUTTON_ATTACK | KEELS2_BUTTON_JUMP);
    Require(host.durations.at(3) == 1280, "unrelated gameplay input does not renew script timeout");
    now += std::chrono::milliseconds(1279); app.Tick(now);
    Require(app.CurrentMenu(host.players.at(3)) && host.durations.at(3) == 1,
        "script menu remains available until the inactivity deadline");
    host.input[3].buttons = KEELS2_BUTTON_USE;
    now += std::chrono::milliseconds(1); app.Tick(now);
    Require(!app.CurrentMenu(host.players.at(3)) && host.menus.at(3).empty() && host.durations.at(3) == 0,
        "script expiry clears the display before late input can revive it");
    host.input[3].buttons = KEELS2_BUTTON_USE;
    open(); const auto first = app.CurrentMenu(host.players.at(3));
    tick(); tick();
    Require(app.CurrentMenu(host.players.at(3)) == first && host.replies.empty(), "held before opening cannot select");
    press(KEELS2_BUTTON_USE);
    const auto submenu = app.CurrentMenu(host.players.at(3));
    Require(submenu != first && submenu && host.menus.at(3).find("Submenu test") != std::string::npos, "key selects real SourcePawn submenu");
    Require(host.durations.at(3) == 20000, "script default is twenty seconds of inactivity");
    for (int i = 0; i < 40; ++i) tick();
    Require(app.CurrentMenu(host.players.at(3)) == submenu && host.replies.empty(), "held select cannot cascade or repeat");
    press(KEELS2_BUTTON_USE);
    Require(!app.CurrentMenu(host.players.at(3)) && host.replies == std::vector<std::string>{"selected=20"}, "fresh press selects submenu once");
    host.replies.clear(); host.input[3].buttons = 0; open();
    press(KEELS2_BUTTON_BACK); press(KEELS2_BUTTON_USE);
    Require(app.CurrentMenu(host.players.at(3)) && host.replies.empty(), "disabled item ignores key selection");
    press(KEELS2_BUTTON_BACK); press(KEELS2_BUTTON_USE);
    Require(host.replies == std::vector<std::string>{"selected=12"}, "navigation uses stored item value");
    host.replies.clear(); host.input[3].buttons = 0; open();
    press(KEELS2_BUTTON_FORWARD);
    Require(host.menus.at(3).find("Page 2/2") != std::string::npos, "up wraps to second page");
    press(KEELS2_BUTTON_RELOAD);
    Require(app.CurrentMenu(host.players.at(3)) && host.menus.at(3).find("Page 1/2") != std::string::npos, "reload goes back one page");
    press(KEELS2_BUTTON_RELOAD);
    Require(!app.CurrentMenu(host.players.at(3)), "reload closes first page");
    host.input[3].buttons = 0; open();
    auto session = app.CurrentMenu(host.players.at(3));
    host.input[3].buttons = KEELS2_BUTTON_USE; ++host.input[3].context; tick(); tick();
    Require(app.CurrentMenu(host.players.at(3)) == session, "changed pawn or map context establishes baseline");
    host.input_result = KEEL_RESULT_NOT_READY; tick();
    host.input_result = KEEL_RESULT_OK; tick();
    Require(app.CurrentMenu(host.players.at(3)) == session, "held input after failure stays a baseline");
    host.throw_input = true; tick(); host.throw_input = false; tick();
    Require(app.CurrentMenu(host.players.at(3)) == session, "read exception resets input without action");
    press(KEELS2_BUTTON_RELOAD);
    host.input[3].buttons = 0; open(); session = app.CurrentMenu(host.players.at(3));
    host.input[3].buttons = KEELS2_BUTTON_USE;
    host.on_read = [&] { open(); };
    tick();
    Require(app.CurrentMenu(host.players.at(3)) != session && host.menus.at(3).find("Submenu test") == std::string::npos,
        "replacement during read cannot receive old session input");
    tick(); Require(host.menus.at(3).find("Submenu test") == std::string::npos, "replacement retains held-key baseline");
    std::ofstream(admins) << R"("Admins" {})"; app.ReloadPermissions();
    press(KEELS2_BUTTON_USE);
    Require(!app.CurrentMenu(host.players.at(3)) && host.replies.empty(), "key selection rechecks revoked permission");
    std::ofstream(admins) << allowed; app.ReloadPermissions();
    host.input[3].buttons = 0; open();
    host.on_read = [&] { Require(app.Unload("input"), "unload between sample and dispatch"); };
    host.input[3].buttons = KEELS2_BUTTON_USE; tick();
    Require(!app.CurrentMenu(host.players.at(3)) && host.replies.empty(), "unloaded callback cannot receive pending input");
    Require(app.Load(manifest), "reload input script");
    host.input[3].buttons = 0; open();
    Require(app.Pause("input"), "pause closes script menu");
    host.input[3].buttons = KEELS2_BUTTON_USE; tick();
    Require(app.Resume("input"), "resume input script"); open(); session = app.CurrentMenu(host.players.at(3)); tick();
    Require(app.CurrentMenu(host.players.at(3)) == session, "resume/open while held cannot select");
    app.MapChanged(); tick();
    Require(!app.CurrentMenu(host.players.at(3)) && host.replies.empty(), "map removes key state and callbacks");
    host.input[3].buttons = 0; open();
    const auto old_player = host.players.at(3);
    host.on_read = [&] { ++host.players.at(3).connection; };
    host.input[3].buttons = KEELS2_BUTTON_USE; tick(); tick();
    Require(!app.CurrentMenu(old_player) && !app.CurrentMenu(host.players.at(3)) && host.replies.empty(), "reused slot cannot select old menu");
    host.input[3].buttons = 0; open();
    app.Disconnected(3, host.players.at(3).connection); tick();
    Require(!app.CurrentMenu(host.players.at(3)), "disconnect retires input session");
    host.input[3].buttons = 0; open();
    host.fail_clear = true; press(KEELS2_BUTTON_USE);
    Require(app.CurrentMenu(host.players.at(3)) && host.replies.empty(), "script clear failure prevents callback");
    host.fail_clear = false; tick();
    Require(host.menus.at(3).find("Submenu test") == std::string::npos, "held select after clear failure cannot retry action");
    press(KEELS2_BUTTON_RELOAD);
    host.input[3].buttons = 0; open();
    now += std::chrono::seconds(11); tick();
    Require(!app.CurrentMenu(host.players.at(3)), "expired script menu stops input");

    host.input[3].buttons = 0; open();
    host.fail_render = host.fail_clear = true;
    tick();
    host.fail_render = false;
    renders = host.renders;
    press(KEELS2_BUTTON_USE);
    Require(host.renders == renders && host.replies.empty(),
        "failed idle script refresh waits for cleanup without redrawing or executing a selection");
    host.fail_clear = false; tick();
    Require(!app.CurrentMenu(host.players.at(3)) && host.menus.at(3).empty(), "failed script update retries cleanup after transport recovery");

    NativeAction native, peer;
    const SrMenuItem items[] = {{"Native action", KEEL_TRUE}};
    const auto native_open = [&](int slot, NativeAction& action) {
        const SrMenuSpec spec{sizeof(spec), 1, "Native input", "test.menu", items, 1, 10000,
            &NativeAction::Selected, &action, "test.input.provider", 1};
        const auto& player = host.players.at(slot);
        const KeelPlayerConnection connection{slot, 0, player.connection};
        SrMenuSession id = 0;
        Require(app.OpenNativeMenu(50, connection, spec, id) == KEEL_RESULT_OK, "open native input menu");
        return id;
    };
    host.input[3].buttons = host.input[4].buttons = 0;
    native_open(3, native);
    Require(host.durations.at(3) == 10000, "native initial packet uses requested lifetime");
    renders = host.renders;
    for (int i = 0; i < 80; ++i) tick();
    Require(host.renders == renders + 80 && host.durations.at(3) == 8720 && app.CurrentMenu(host.players.at(3)),
        "idle native menu refreshes every frame without extending its lifetime");
    press(KEELS2_BUTTON_BACK);
    Require(host.renders == renders + 83 && host.durations.at(3) == 10000,
        "fresh native navigation restarts the requested inactivity timeout");
    now += std::chrono::milliseconds(8688); app.Tick(now);
    Require(app.CurrentMenu(host.players.at(3)) && host.durations.at(3) == 1312,
        "held navigation does not renew native timeout; menu survives its original deadline");
    press(KEELS2_BUTTON_ATTACK | KEELS2_BUTTON_JUMP);
    Require(host.durations.at(3) == 1280, "unrelated gameplay input does not renew native timeout");
    now += std::chrono::milliseconds(1279); app.Tick(now);
    Require(app.CurrentMenu(host.players.at(3)) && host.durations.at(3) == 1,
        "native menu remains available until the inactivity deadline");
    host.input[3].buttons = KEELS2_BUTTON_USE;
    now += std::chrono::milliseconds(1); app.Tick(now);
    Require(!app.CurrentMenu(host.players.at(3)) && host.menus.at(3).empty() && host.durations.at(3) == 0 && !host.leases,
        "native expiry clears the display before late input can revive it");
    host.input[3].buttons = 0;
    native_open(3, native); const auto peer_session = native_open(4, peer);
    Require(host.leases == 1, "native menus share one provider lease");
    native.callback = [&] { Require(app.CloseNativeMenu(50, peer_session) == KEEL_RESULT_OK, "native callback closes another sampled session"); native_open(4, peer); };
    host.input[3].buttons = host.input[4].buttons = KEELS2_BUTTON_USE; tick();
    Require(native.calls == 1 && !peer.calls && app.CurrentMenu(host.players.at(4)) != peer_session, "callback replacement invalidates later input snapshot");
    tick(); Require(!peer.calls, "replacement native menu cannot reuse held select");
    peer.callback = [&] { app.MapChanged(); };
    press(KEELS2_BUTTON_USE, 4);
    Require(peer.calls == 1 && !host.leases, "native callback can invalidate map without corrupting poll iteration");
    native.callback = {};
    host.on_read = [&] { app.MapChanged(); };
    native_open(3, native);
    Require(!app.CurrentMenu(host.players.at(3)) && !host.leases, "opening baseline does not retain display erased by map callback");
    host.input[3].buttons = 0; native_open(3, native);
    host.fail_clear = true; press(KEELS2_BUTTON_USE);
    Require(native.calls == 1 && host.leases == 1, "failed native clear prevents action and retains cleanup");
    host.fail_clear = false; tick();
    Require(native.calls == 1 && !host.leases, "native cleanup retry does not invent a selection");
    host.input[3].buttons = 0; native_open(3, native);
    std::ofstream(admins) << R"("Admins" {})"; app.ReloadPermissions(); press(KEELS2_BUTTON_USE);
    Require(native.calls == 1 && !host.leases, "native key selection rechecks permission");
    std::ofstream(admins) << allowed; app.ReloadPermissions();
    host.input[3].buttons = 0; native_open(3, native);
    Require(app.PreparePause() && !host.leases, "native platform pause closes native input menu");
    app.NativeResumed(); tick();
    Require(!app.CurrentMenu(host.players.at(3)), "resume cannot revive old native menu input");
    host.input[3].buttons = 0; native_open(3, native);
    host.fail_render = host.fail_clear = true;
    tick();
    host.fail_render = false;
    renders = host.renders;
    press(KEELS2_BUTTON_USE);
    Require(host.renders == renders && native.calls == 1 && host.leases == 1,
        "failed idle native refresh retains cleanup and provider without redrawing or selecting");
    host.fail_clear = false; tick();
    Require(!app.CurrentMenu(host.players.at(3)) && host.menus.at(3).empty() && !host.leases, "native update failure retries cleanup after transport recovery");
    Require(app.Shutdown(), "clean input shutdown");
    const auto reads = host.input_reads; tick();
    Require(host.input_reads == reads, "no polling after all menu sessions close");
    std::cout << "actual script/native menu input, holds, replacement, permissions and cleanup passed\n";
}
