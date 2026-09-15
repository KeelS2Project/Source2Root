#include "foundation.h"
#include "core_native_names.h"

#include <json.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <functional>

static void Require(bool value, const char* message) {
    if (!value) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}

class Host final : public sr::GameHost {
public:
    sr::Player player{3, 10, 76561197960265851ULL, true, false, "Fixture player"};
    KeelResult lookup = KEEL_RESULT_OK;
    bool fail_remove = false, fail_render = false, provider_available = true;
    unsigned replies = 0, renders = 0, leases = 0;
    std::function<void(const std::string&)> on_log;
    std::vector<std::string> logs;
    std::set<std::string> commands, events;
    KeelResult Lookup(int slot, sr::Player& output) override {
        if (lookup != KEEL_RESULT_OK) return lookup;
        if (slot != player.slot) return KEEL_RESULT_NOT_FOUND;
        output = player; return KEEL_RESULT_OK;
    }
    KeelResult Reply(const sr::Player*, const std::string& text) override {
        ++replies; logs.push_back(text); return KEEL_RESULT_OK;
    }
    void Log(const std::string& text) override {
        logs.push_back(text);
        if (on_log) on_log(text);
    }
    KeelResult RegisterCommand(const std::string& name) override {
        return commands.insert(name).second ? KEEL_RESULT_OK : KEEL_RESULT_ALREADY_EXISTS;
    }
    KeelResult RemoveCommand(const std::string& name) override {
        if (fail_remove) return KEEL_RESULT_ENGINE_FAILURE;
        return commands.erase(name) ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND;
    }
    KeelResult ListenEvent(const std::string& name) override {
        return events.insert(name).second ? KEEL_RESULT_OK : KEEL_RESULT_ALREADY_EXISTS;
    }
    KeelResult RemoveEvent(const std::string& name) override {
        return events.erase(name) ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND;
    }
    KeelResult RenderMenu(const sr::Player& recipient, const std::string&) override {
        if (fail_render) return KEEL_RESULT_ENGINE_FAILURE;
        Require(recipient.SameConnection(player), "render targets actual connection");
        ++renders; return KEEL_RESULT_OK;
    }
    KeelResult AcquireProvider(const std::string&, unsigned) override {
        if (!provider_available) return KEEL_RESULT_NOT_FOUND;
        ++leases; return KEEL_RESULT_OK;
    }
    KeelResult ReleaseProvider(const std::string&, unsigned) override {
        Require(leases != 0, "no extra provider release"); --leases; return KEEL_RESULT_OK;
    }
};

static KeelResult Add(void* context, const int32_t* arguments, uint32_t count, int32_t* result, char*, uint32_t) {
    if (count != 2) return KEEL_RESULT_INVALID_ARGUMENT;
    if (context) (*static_cast<std::function<void()>*>(context))();
    *result = arguments[0] + arguments[1];
    return KEEL_RESULT_OK;
}

static void Write(const std::filesystem::path& file, const std::string& text) {
    std::ofstream output(file); output << text; Require(static_cast<bool>(output), "write fixture");
}

int main(int argc, char** argv) {
    Require(argc == 5, "foundation_test runtime sample.smx manifest.json fixture-root");
    const std::filesystem::path root(argv[4]);
    std::filesystem::create_directories(root / "plugins/hello");
    std::filesystem::create_directories(root / "configs");
    const auto manifest = root / "plugins/hello/plugin.json";
    std::filesystem::copy_file(argv[2], root / "plugins/hello/hello.smx", std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(argv[3], manifest, std::filesystem::copy_options::overwrite_existing);
    const std::string permissions = R"("Admins" { "Fixture" { "identity" "STEAM_0:1:61" "group" "fixture" } })";
    Write(root / "configs/admin_groups.cfg", R"("Groups" { "fixture" { "immunity" "10" "permissions" { "demo.hello" "1" "demo.status" "1" } } })");
    Write(root / "configs/admins.cfg", permissions);
    Require(sr::ParseSteamIdentity("STEAM_0:1:61") == sr::ParseSteamIdentity("[U:1:123]") &&
        sr::ParseSteamIdentity("[U:1:123]") == sr::ParseSteamIdentity("76561197960265851"), "identity normalization");
    for (const auto* invalid : {"STEAM_0:2:61", "[U:2:123]", "76561197960265728", "-1", "18446744073709551616", "76561197960265851x"}) {
        bool rejected = false;
        try { sr::ParseSteamIdentity(invalid); } catch (...) { rejected = true; }
        Require(rejected, "reject invalid identity");
    }
    sr::Handles<int> handles;
    auto first = handles.Add(1, 1, 42);
    Require(handles.Get(first, 1, 1) == 42, "typed handle value");
    Require(!handles.Contains(first, 2, 1) && !handles.Contains(first, 1, 2), "handle owner/type enforcement");
    handles.Remove(first, 1, 1);
    auto second = handles.Add(1, 1, 99);
    Require(first != second && !handles.Contains(first, 1, 1), "generation rejects stale handle");
    Host host;
    sr::Foundation app(host, argv[1], root);
    Require(!app.Load(manifest) && host.commands.empty() && !host.leases, "missing native fails cleanly");
    std::function<void()> during_native;
    SrNativeSpec spec{sizeof(spec), SR_EXTENSION_API_VERSION, "SR_ExampleAdd", 2, 0,
        "source2root.example", 1, &Add, &during_native};
    during_native = [] {};
    SrRegistration registration = 0;
    auto named = spec;
    for (const auto name : sr::CoreNativeNames) {
        named.name = name.data();
        registration = 99;
        Require(app.RegisterNative(50, named, registration) == KEEL_RESULT_INVALID_ARGUMENT && !registration,
                "every public core native is reserved");
    }
    for (const auto* name : {"", "1Native", "Bad Name", "Bad-Name", "Bad.Name", "Native\xc3\xa9"}) {
        named.name = name;
        Require(app.RegisterNative(50, named, registration) == KEEL_RESULT_INVALID_ARGUMENT && !registration,
                "invalid extension identifiers rejected");
    }
    const std::string long_name(97, 'N');
    named.name = long_name.c_str();
    Require(app.RegisterNative(50, named, registration) == KEEL_RESULT_INVALID_ARGUMENT, "native name length bound");
    for (const auto* name : {"RandomInt", "_Private2", "SR_Legacy"}) {
        named.name = name;
        Require(app.RegisterNative(50, named, registration) == KEEL_RESULT_OK, "concise and existing names accepted");
        Require(app.UnregisterNative(51, registration) == KEEL_RESULT_INVALID_ARGUMENT, "only native owner can unregister");
        Require(app.UnregisterNative(50, registration) == KEEL_RESULT_OK, "native owner unregisters");
    }
    named = spec; named.provider_service = "";
    Require(app.RegisterNative(50, named, registration) == KEEL_RESULT_INVALID_ARGUMENT, "provider service required");
    Require(app.RegisterNative(0, spec, registration) == KEEL_RESULT_INVALID_ARGUMENT, "native owner required");
    auto incompatible = spec; incompatible.api_version = 999;
    Require(app.RegisterNative(50, incompatible, registration) == KEEL_RESULT_INCOMPATIBLE, "extension version rejection");
    Require(app.RegisterNative(50, spec, registration) == KEEL_RESULT_OK, "extension registration");
    SrRegistration duplicate;
    Require(app.RegisterNative(51, spec, duplicate) == KEEL_RESULT_ALREADY_EXISTS, "duplicate native rejection");
    host.provider_available = false;
    Require(!app.Load(manifest) && !host.leases, "unavailable provider fails before initialization");
    host.provider_available = true;
    Require(app.Load(manifest), "load actual sample");
    Require(host.commands.contains("sr_hello") && host.events.contains("round_start") && host.leases == 1, "owned registrations and lease");
    Require(app.UnregisterNative(50, registration) == KEEL_RESULT_BUSY, "provider held by script");
    Require(!app.Load(manifest), "duplicate plugin ID rejected");
    std::ifstream initial_input(manifest);
    const auto initial_metadata = nlohmann::json::parse(initial_input);
    initial_input.close();
    const auto dependent_directory = root / "plugins/dependent";
    std::filesystem::create_directories(dependent_directory);
    auto dependent = initial_metadata;
    dependent["id"] = "dependent";
    dependent["entry"] = "empty.smx";
    dependent["dependencies"] = nlohmann::json::array({{{"id", "hello"}, {"minimum_version", "1.0.0"}}});
    Write(dependent_directory / "plugin.json", dependent.dump());
    std::filesystem::copy_file(std::filesystem::path(argv[2]).parent_path() / "empty.smx",
        dependent_directory / "empty.smx", std::filesystem::copy_options::overwrite_existing);
    Require(app.Load(dependent_directory / "plugin.json"), "declared script dependency starts after provider");
    auto downgraded = initial_metadata;
    downgraded["version"] = "0.9.9";
    Write(manifest, downgraded.dump());
    Require(!app.Reload("hello") && app.Error().find("requirement") != std::string::npos,
            "replacement cannot break existing dependent version requirement");
    Write(manifest, initial_metadata.dump());
    Require(!app.Unload("hello"), "running dependent prevents script provider unload");
    Require(app.Unload("dependent"), "dependent retires before provider");
    auto fixture = [&](const std::string& id, bool malformed = false) {
        const auto directory = root / "plugins" / id;
        std::filesystem::create_directories(directory);
        auto metadata = initial_metadata;
        metadata["id"] = id;
        metadata["entry"] = id + ".smx";
        Write(directory / "plugin.json", metadata.dump());
        if (malformed) Write(directory / (id + ".smx"), "malformed SMX bytes");
        else std::filesystem::copy_file(std::filesystem::path(argv[2]).parent_path() / (id + ".smx"),
            directory / (id + ".smx"), std::filesystem::copy_options::overwrite_existing);
        return directory / "plugin.json";
    };
    Require(!app.Load(fixture("failed_init")) && !host.commands.contains("sr_partial") && host.commands.contains("sr_hello"),
            "failed initialization removes partial duplicate registration and preserves peer");
    Require(!app.Load(fixture("malformed", true)) && host.commands.contains("sr_hello"), "malformed bytecode cannot harm running plugin");
    Require(!app.Load(fixture("runtime_limit")) && app.Error().find("16 MiB") != std::string::npos,
            "declared VM memory rejected before allocation");
    Require(app.Dispatch(sr::Origin::ServerConsole, -1, "sr_hello"), "server command dispatch");
    Require(host.replies == 1, "real VM replied once");
    app.Tick(sr::Foundation::Clock::now() + std::chrono::seconds(1));
    Require(host.replies == 2, "real timer fired");
    Require(!app.Dispatch(sr::Origin::PublicChat, 3, "!hello"), "public chat remains visible");
    Require(host.replies == 3 && app.CurrentMenu(host.player), "public command executes once and opens menu");
    auto old_session = app.CurrentMenu(host.player);
    Require(app.MenuInput(host.player, old_session, sr::MenuInput::Down), "navigate menu");
    Require(!app.MenuInput(host.player, old_session, sr::MenuInput::Select), "disabled item cannot execute");
    Require(app.MenuInput(host.player, old_session, sr::MenuInput::Down), "navigate to identity");
    Require(app.MenuInput(host.player, old_session, sr::MenuInput::Select), "real menu VM callback");
    Require(host.logs.back() == "76561197960265851", "SteamID64 survives cells as a string");
    Require(!app.MenuInput(host.player, old_session, sr::MenuInput::Select), "late selection rejected");
    Require(app.Dispatch(sr::Origin::SilentChat, 3, "/hello"), "silent chat suppressed");
    auto session = app.CurrentMenu(host.player);
    Require(session && session != old_session, "menu generation changed");
    Write(root / "configs/admins.cfg", R"("Admins" {})");
    app.ReloadPermissions();
    const auto before = host.replies;
    Require(!app.MenuInput(host.player, session, sr::MenuInput::Select) && host.replies == before, "menu permissions rechecked at execution");
    Require(app.Dispatch(sr::Origin::SilentChat, 3, "/hello"), "denied silent command still suppressed");
    Require(host.replies == before + 1 && host.logs.back() == "You do not have access to this command.", "permission denial reply");
    Write(root / "configs/admins.cfg", permissions);
    app.ReloadPermissions();
    const auto malformed_before = host.replies;
    Require(app.Dispatch(sr::Origin::SilentChat, 3, "/hello \"unterminated"), "malformed silent invocation suppressed");
    Require(host.replies == malformed_before + 1 && host.logs.back() == "Invalid command syntax.",
            "malformed command gets one private error without executing the script");
    const auto unknown_before = host.replies;
    Require(app.Dispatch(sr::Origin::SilentChat, 3, "/slpa @me"), "unknown silent command stays hidden");
    Require(host.replies == unknown_before + 1 && host.logs.back() == "Unknown command \"slpa\".",
            "unknown silent command replies privately once");
    Require(!app.Dispatch(sr::Origin::PublicChat, 3, "!slpa @me"), "unknown public command remains visible");
    Require(host.replies == unknown_before + 1, "unhandled public command is left for other plugins");
    host.player.authenticated = false;
    app.Dispatch(sr::Origin::ClientConsole, 3, "sr_hello");
    Require(host.logs.back() == "You do not have access to this command.", "unauthenticated identity cannot gain permissions");
    host.player.authenticated = true;
    app.MapChanged();
    const auto cancel_before = host.replies;
    app.Tick(sr::Foundation::Clock::now() + std::chrono::seconds(10));
    Require(host.replies == cancel_before, "map change retires timers and menus");
    app.Event("round_start");
    Require(host.logs.back() == "hello: hello received round_start", "real game-event callback survives map change");
    Require(app.Reload("hello") && host.commands.size() == 1 && host.leases == 1, "staged replacement transfers command and provider ownership");
    std::ifstream input(manifest); auto metadata = nlohmann::json::parse(input); input.close();
    auto invalid_api = metadata; invalid_api["api"] = 999;
    Write(manifest, invalid_api.dump());
    auto status = [&](const std::string& id) {
        for (const auto& item : app.Status()) if (item.id == id) return item;
        std::abort();
    };
    Require(!app.Reload("hello") && status("hello").state == sr::PluginState::Running, "invalid replacement preserves running VM");
    invalid_api["api"] = 1;
    Write(manifest, invalid_api.dump());
    Require(!app.Reload("hello") && status("hello").state == sr::PluginState::Running,
            "previous script API is explicitly rejected without replacing the running VM");
    Write(manifest, metadata.dump());
    during_native = [&] { Require(!app.Unload("hello"), "in-flight script unload refused"); };
    app.Dispatch(sr::Origin::ServerConsole, -1, "sr_hello");
    Require(status("hello").state == sr::PluginState::Retiring, "retiring context stops new dispatch");
    during_native = [] {};
    host.fail_remove = true;
    Require(!app.Unload("hello") && !host.commands.empty(), "failed removal retains registrations for retry");
    host.fail_remove = false;
    Require(app.Unload("hello") && host.commands.empty() && host.events.empty() && !host.leases, "retry completes cleanup");
    Require(app.Load(manifest), "reload for lookup recovery");
    app.Dispatch(sr::Origin::ClientConsole, 3, "sr_hello");
    const auto pending_before = host.replies;
    const auto pending_session = app.CurrentMenu(host.player);
    host.lookup = KEEL_RESULT_ENGINE_FAILURE;
    app.Tick(sr::Foundation::Clock::now() + std::chrono::seconds(12));
    Require(host.replies == pending_before, "transient lookup defers timer");
    Require(!app.Unload("hello") && status("hello").state == sr::PluginState::Retiring,
            "transient lookup cannot discard pending menu cleanup and permit unload");
    Require(app.CurrentMenu(host.player) == pending_session, "pending cleanup retains menu session");
    host.lookup = KEEL_RESULT_OK;
    ++host.player.connection;
    Require(app.Unload("hello") && !app.CurrentMenu(host.player), "confirmed replacement retires old connection cleanup");
    Require(app.Load(fixture("resource_fault")), "load invalid resource argument fixture");
    app.Dispatch(sr::Origin::ServerConsole, -1, "sr_bad_handle");
    app.Dispatch(sr::Origin::ServerConsole, -1, "sr_bad_timer");
    app.Dispatch(sr::Origin::ServerConsole, -1, "sr_bad_array");
    Require(status("resource_fault").state == sr::PluginState::Retiring, "repeated actual VM native faults retire dispatch");
    Require(app.Unload("resource_fault"), "faulted script cleanup");
    for (int i = 0; i < 30; ++i) {
        Require(app.Load(manifest), "repeat load");
        Require(app.Reload("hello"), "repeat reload");
        Require(app.Unload("hello"), "repeat unload");
        Require(host.commands.empty() && host.events.empty() && host.leases == 0 && status("hello").handles == 0, "resource counts return to zero");
    }
    Require(app.Load(manifest), "load pause fixture");
    const auto pause_time = sr::Foundation::Clock::now() + std::chrono::minutes(1);
    app.Tick(pause_time);
    app.Dispatch(sr::Origin::ClientConsole, 3, "sr_hello");
    const auto paused_session = app.CurrentMenu(host.player);
    app.Tick(pause_time + std::chrono::milliseconds(100));
    Require(app.Pause("hello") && status("hello").state == sr::PluginState::Paused && !app.CurrentMenu(host.player),
            "pause closes owned menu while retaining VM");
    Require(host.commands.contains("sr_hello") && host.events.contains("round_start") && host.leases == 1,
            "pause keeps registrations and provider ownership");
    const auto paused_replies = host.replies;
    const auto paused_logs = host.logs.size();
    app.Tick(pause_time + std::chrono::seconds(5));
    app.Event("round_start");
    app.Dispatch(sr::Origin::ServerConsole, -1, "sr_hello");
    Require(!app.MenuInput(host.player, paused_session, sr::MenuInput::Select) &&
            host.replies == paused_replies && host.logs.size() == paused_logs,
            "paused commands, events, timers and stale menu selections do not execute");
    Require(app.Resume("hello"), "resume paused VM");
    app.Tick(pause_time + std::chrono::milliseconds(5149));
    Require(host.replies == paused_replies, "resume preserves remaining timer delay");
    app.Tick(pause_time + std::chrono::milliseconds(5150));
    Require(host.replies == paused_replies + 1, "resumed timer fires once at remaining delay");
    Require(app.Pause("hello"), "pause before staged replacement");
    Write(manifest, invalid_api.dump());
    Require(!app.Reload("hello") && status("hello").state == sr::PluginState::Paused,
            "invalid replacement preserves paused VM");
    Write(manifest, metadata.dump());
    Require(app.Reload("hello") && status("hello").state == sr::PluginState::Paused,
            "valid replacement remains paused");
    Require(app.Resume("hello"), "resume replacement");
    app.Dispatch(sr::Origin::ClientConsole, 3, "sr_hello");
    host.fail_render = true;
    Require(!app.Pause("hello") && status("hello").state == sr::PluginState::Paused && app.CurrentMenu(host.player),
            "failed menu clear keeps paused dispatch and cleanup ownership");
    Require(!app.Resume("hello"), "resume waits for owned menu cleanup");
    std::filesystem::copy_file(std::filesystem::path(argv[2]).parent_path() / "startup_timer.smx",
        root / "plugins/hello/hello.smx", std::filesystem::copy_options::overwrite_existing);
    Require(!app.Reload("hello") && status("hello").state == sr::PluginState::Retiring,
            "staged paused replacement retains old menu for cleanup retry");
    const auto staged_logs = host.logs.size();
    app.Tick(pause_time + std::chrono::seconds(15));
    Require(host.logs.size() == staged_logs, "staged initialization timer waits while cleanup is pending");
    host.fail_render = false;
    Require(app.Reload("hello") && status("hello").state == sr::PluginState::Paused && !app.CurrentMenu(host.player),
            "reload cleanup retry remembers original paused state");
    Require(app.Resume("hello"), "resume after retained replacement");
    app.Tick(pause_time + std::chrono::milliseconds(15249));
    Require(host.logs.size() == staged_logs, "staged timer delay begins when replacement can activate");
    app.Tick(pause_time + std::chrono::milliseconds(15250));
    Require(host.logs.size() == staged_logs + 1 && host.logs.back() == "hello: startup timer",
            "replacement startup timer runs once after resume");
    std::filesystem::copy_file(argv[2], root / "plugins/hello/hello.smx", std::filesystem::copy_options::overwrite_existing);
    Require(app.Reload("hello"), "restore command and extension fixture after staged timer test");
    Require(app.Load(dependent_directory / "plugin.json"), "load pause dependency");
    Require(!app.Pause("hello") && status("hello").state == sr::PluginState::Running,
            "running dependent blocks provider pause");
    Require(app.Pause("dependent") && app.Pause("hello"), "pause consumers before provider");
    Require(!app.Resume("dependent") && !app.Reload("dependent") && status("dependent").state == sr::PluginState::Paused,
            "paused dependency blocks resume and replacement without destroying consumer");
    Require(!app.Unload("hello"), "paused consumer still prevents provider unload");
    Require(app.Resume("hello") && app.Resume("dependent"), "resume provider before consumer");
    auto cyclic = metadata;
    cyclic["dependencies"] = nlohmann::json::array({{{"id", "dependent"}, {"minimum_version", "1.0.0"}}});
    Write(manifest, cyclic.dump());
    Require(!app.Reload("hello") && app.Error().find("cycle") != std::string::npos &&
            status("hello").state == sr::PluginState::Running, "replacement cannot introduce dependency cycle");
    Write(manifest, metadata.dump());
    during_native = [&] {
        Require(!app.UnloadAll() && !app.Pause("hello") && !app.Refresh(), "active callback refuses bulk unload, pause and refresh");
    };
    app.Dispatch(sr::Origin::ServerConsole, -1, "sr_hello");
    during_native = [] {};
    Require(status("hello").state == sr::PluginState::Running && status("dependent").state == sr::PluginState::Running,
            "refused bulk action leaves unrelated consumers unchanged");
    unsigned nested_attempts = 0;
    host.on_log = [&](const std::string& text) {
        if (text != "hello: hello stopped") return;
        ++nested_attempts;
        Require(!app.Load(manifest) && !app.Reload("hello") && !app.Unload("hello") && !app.Shutdown(),
                "stop callback cannot reenter plugin management");
    };
    host.fail_remove = true;
    Require(!app.UnloadAll() && status("dependent").state == sr::PluginState::Disabled &&
            status("hello").state == sr::PluginState::Retiring && host.leases == 0,
            "bulk unload retires dependent first and reports retained provider cleanup");
    Require(app.Error().find("Plugins remain loaded: hello") != std::string::npos && nested_attempts == 1,
            "bulk result lists remaining VM without repeating stop callback");
    host.on_log = {};
    host.fail_remove = false;
    Require(app.UnloadAll() && !host.leases && host.commands.empty() && host.events.empty(),
            "bulk retry completes cleanup without bypassing refusal");
    Require(app.Load(manifest), "load provider for retained dependent cleanup");
    std::filesystem::copy_file(std::filesystem::path(argv[2]).parent_path() / "resource_fault.smx",
        dependent_directory / "empty.smx", std::filesystem::copy_options::overwrite_existing);
    Require(app.Load(dependent_directory / "plugin.json") && app.Pause("dependent") && app.Pause("hello"),
            "pause consumer with owned commands and its provider");
    host.fail_remove = true;
    Require(!app.UnloadAll() && status("dependent").state == sr::PluginState::Retiring &&
            status("hello").state == sr::PluginState::Paused && host.leases == 1,
            "retained dependent prevents bulk unload from touching its paused provider");
    Require(app.Error().find("dependent") != std::string::npos && app.Error().find("hello") != std::string::npos,
            "bulk result reports every remaining dependency owner");
    host.fail_remove = false;
    Require(app.Shutdown() && !host.leases && host.commands.empty() && host.events.empty(),
            "shutdown retries dependency cleanup in order");
    Require(!app.Retry("hello"), "retry does not re-enable disabled plugin");
    Require(!app.Retry("malformed"), "failed plugin retry still validates bytecode");
    std::filesystem::copy_file(std::filesystem::path(argv[2]).parent_path() / "empty.smx",
        root / "plugins/malformed/malformed.smx", std::filesystem::copy_options::overwrite_existing);
    Require(app.Retry("malformed") && status("malformed").state == sr::PluginState::Running,
            "retry starts corrected failed bytecode");
    Require(app.UnloadAll(), "unload retried fixture");
    const auto discovery_logs = host.logs.size();
    app.Discover();
    app.Discover();
    Require(host.logs.size() == discovery_logs && host.commands.empty() && !host.leases,
            "repeated discovery keeps known disabled and failed records unchanged");
    unsigned selected = 0;
    auto selected_callback = [](void* data, const KeelPlayerConnection* connection, int32_t item) {
        Require(connection->generation != 0 && item == 0, "native callback receives full connection and selection");
        ++*static_cast<unsigned*>(data);
    };
    const SrMenuItem native_items[] = {{"Native action", KEEL_TRUE}, {"Disabled", KEEL_FALSE}};
    const SrMenuSpec native_spec{sizeof(native_spec), 1, "Native menu", "demo.hello", native_items, 2,
        1000, selected_callback, &selected, "source2root.example", 1};
    const KeelPlayerConnection connection{host.player.slot, 0, host.player.connection};
    SrMenuSession native_session = 0;
    Require(app.OpenNativeMenu(50, connection, native_spec, native_session) == KEEL_RESULT_OK && host.leases == 1,
            "native menu owns actual provider lease");
    Require(app.CloseNativeMenu(51, native_session) == KEEL_RESULT_INVALID_ARGUMENT, "foreign native menu owner rejected");
    Require(app.MenuInput(host.player, native_session, sr::MenuInput::Down) &&
            !app.MenuInput(host.player, native_session, sr::MenuInput::Select) && !selected, "native disabled action blocked");
    app.MenuInput(host.player, native_session, sr::MenuInput::Up);
    Require(app.MenuInput(host.player, native_session, sr::MenuInput::Select) && selected == 1 && !host.leases,
            "native selection uses shared permission and renderer service");
    Require(!app.MenuInput(host.player, native_session, sr::MenuInput::Select), "retired native callback cannot execute");
    Require(app.OpenNativeMenu(50, connection, native_spec, native_session) == KEEL_RESULT_OK, "native cleanup fixture");
    host.lookup = KEEL_RESULT_ENGINE_FAILURE;
    Require(app.CloseNativeMenu(50, native_session) == KEEL_RESULT_BUSY && host.leases == 1,
            "transient native lookup retains provider and retry state");
    host.lookup = KEEL_RESULT_OK;
    Require(app.CloseNativeMenu(50, native_session) == KEEL_RESULT_OK && !host.leases, "native cleanup retry");
    Require(app.OpenNativeMenu(50, connection, native_spec, native_session) == KEEL_RESULT_OK, "native permission fixture");
    Write(root / "configs/admins.cfg", R"("Admins" {})");
    app.ReloadPermissions();
    Require(!app.MenuInput(host.player, native_session, sr::MenuInput::Select) && selected == 1 && !host.leases,
            "native selection rechecks revoked permission");
    Write(root / "configs/admins.cfg", permissions);
    app.ReloadPermissions();
    Require(app.OpenNativeMenu(50, connection, native_spec, native_session) == KEEL_RESULT_OK, "native map fixture");
    app.MapChanged();
    Require(!host.leases && !app.CurrentMenu(host.player), "map change releases native menu lease");
    Require(app.UnregisterNative(99, registration) == KEEL_RESULT_INVALID_ARGUMENT, "foreign extension cannot unregister");
    Require(app.UnregisterNative(50, registration) == KEEL_RESULT_OK, "provider unregister after consumers retire");
    Require(app.Shutdown(), "clean shutdown");
    std::cout << "real VM service ownership, permissions/chat, menu sessions, timers/events and lifecycle recovery passed\n";
}
