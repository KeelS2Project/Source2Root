#include <keels2/round_control.h>
#include <keels2/player_management.h>
#include <keels2/player_actions.h>
#include <keels2/player_input.h>
#include <cmath>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <keels2/bootstrap_api.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#if defined(SR_PREFS_TEST)
#include "store.h"
#endif
#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {
bool (*during_command)(const char*, int) = nullptr;
bool during_ok = false;
void AttemptRetire() noexcept {
    during_ok = during_command("sr plugins unload hello", -1) && during_command("keel plugins unload 2", -1);
}
class Library {
public:
    explicit Library(const std::filesystem::path& path) {
#if defined(_WIN32)
        handle = LoadLibraryW(path.c_str());
#else
        handle = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
#endif
        if (!handle) throw std::runtime_error("cannot load " + path.string()
#if !defined(_WIN32)
            + ": " + dlerror()
#endif
        );
    }
    ~Library() = default;
    void Close() {
#if defined(_WIN32)
        FreeLibrary(handle);
#else
        dlclose(handle);
#endif
        handle = nullptr;
    }
    template <typename T> T Get(const char* name) {
#if defined(_WIN32)
        auto value = GetProcAddress(handle, name);
#else
        auto value = dlsym(handle, name);
#endif
        if (!value) throw std::runtime_error(std::string("missing export ") + name);
        return reinterpret_cast<T>(value);
    }
private:
#if defined(_WIN32)
    HMODULE handle;
#else
    void* handle;
#endif
};
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Copy(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::filesystem::create_directories(to.parent_path());
    std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing);
}
}

int main(int argc, char** argv) {
#if defined(_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    try {
        Check(argc == 10 || argc == 11 || argc == 12 || argc == 15 || argc == 16,
            "module_integration host adapter tier0 module extension pawn sample manifest fixture [mode descriptors [random roll roll-manifest [extensions-directory]]]");
        const auto fixture = std::filesystem::absolute(argv[9]);
#if defined(_WIN32)
        const std::string platform = "win64", host_name = "keels2_host.dll", adapter_name = "keels2_game_cs2.dll";
        const std::string pawn_name = "libsourcepawn.dll", extension = ".dll";
#else
        const std::string platform = "linuxsteamrt64", host_name = "libkeels2_host.so", adapter_name = "libkeels2_game_cs2.so";
        const std::string pawn_name = "libsourcepawn.so", extension = ".so";
#endif
        const auto native = fixture / "addons/keels2";
        const auto script = fixture / "addons/source2root";
        const auto bin = native / "bin" / platform;
        const auto plugins = native / "plugins" / platform;
        if (argc == 16) std::filesystem::remove_all(plugins);
        std::filesystem::remove_all(plugins / ".runtime");
        if (argc >= 15) {
            std::filesystem::remove(plugins / ("source2root_random" + extension));
            std::filesystem::remove(script / "plugins/roll/plugin.json");
            std::filesystem::remove(script / "plugins/roll/roll.smx");
            std::filesystem::remove(script / "plugins/roll");
        }
        Copy(argv[1], bin / host_name);
        Copy(argv[2], bin / adapter_name);
        Copy(argv[3], bin / std::filesystem::path(argv[3]).filename());
        Copy(argv[4], plugins / ("source2root" + extension));
        Copy(argv[5], plugins / ("sr_example" + extension));
        Copy(argv[6], script / "bin" / platform / pawn_name);
        Copy(argv[7], script / "plugins/hello/hello.smx");
        Copy(argv[8], script / "plugins/hello/plugin.json");
        std::filesystem::remove(script / "logs/source2root.log");
        std::filesystem::create_directories(script / "configs");
        if (argc == 12 && std::string(argv[10]) == "topmenus") {
            Copy(std::filesystem::path(argv[7]).parent_path() / "topmenus_contributor.smx", script / "plugins/top_other/main.smx");
            std::ofstream(script / "plugins/top_other/plugin.json") << R"({"schema":1,"id":"top_other","name":"Top menu contributor","author":"tests","version":"1.0.0","api":2,"entry":"main.smx","enabled":true,"dependencies":[]})";
        }
        if (argc == 12 && (std::string(argv[10]) == "dhooks" || std::string(argv[10]) == "sdkcall")) {
            const auto directory = script / "configs/extensions/source2root.dhooks";
            std::filesystem::create_directories(directory);
            std::ofstream(directory / "targets.json") << R"({"schema":1,"targets":{"scalar":{"allow_calls":true,"allow_plugins":["hello"],"source":"symbol","module":")"
                << adapter_name << R"(","symbol":"SrFixtureHookScalar","return":"int32","arguments":["int32","float32"]},"observe_only":{"allow_plugins":["hello"],"source":"symbol","module":")"
                << adapter_name << R"(","symbol":"SrFixtureHookScalar","return":"int32","arguments":["int32","float32"]}}})";
        }
        if (argc == 12 && std::string(argv[10]) == "http") {
            const auto* url = std::getenv("SR_HTTP_URL"), *tls = std::getenv("SR_HTTP_TLS_URL");
            const auto* ca = std::getenv("SR_HTTP_CA"), *bad_ca = std::getenv("SR_HTTP_BAD_CA");
            const auto* files = std::getenv("SR_HTTP_FIXTURE_FILES"), *consumer = std::getenv("SR_HTTP_NATIVE_MODULE");
            Check(url && tls && ca && bad_ca && files && consumer, "HTTP fixture environment is required");
            std::ofstream(script / "configs/http-fixture.txt") << url << '\n' << tls << '\n';
            Copy(ca, script / "configs/extensions/source2root.http/ca.pem");
            Copy(bad_ca, script / "configs/extensions/source2root.http/bad-ca.pem");
            Copy(std::filesystem::path(files) / "upload.bin", script / "data/hello/upload.bin");
            Copy(consumer, plugins / ("zz_http_native" + extension));
        }
        if (argc == 12 && std::string(argv[10]) == "geoip") {
            const auto* data = std::getenv("SR_GEOIP_TEST_DATA");
            Check(data && *data, "GeoIP fixture needs upstream sample database");
            const auto root = script / "data/extensions/source2root.geoip";
            std::filesystem::remove_all(root);
            Copy(std::filesystem::path(data) / "GeoIP2-City-Test.mmdb", root / "city.mmdb");
        }
        if (argc == 12 && std::string(argv[10]) == "clientprefs_mysql") {
            const auto* config = std::getenv("SR_DATABASE_TEST_CONFIG");
            Check(config && *config, "shared preferences fixture requires private configuration");
            const auto file = script / "configs/extensions/source2root.clientprefs/databases.json";
            Copy(config, file);
            std::filesystem::permissions(file, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
            const auto sql = script / "configs/extensions/source2root.database/databases.json";
            Copy(config, sql);
            std::filesystem::permissions(sql, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
            const auto* module = std::getenv("SR_PREFS_SQL_MODULE");
            Check(module && *module, "shared preferences fixture requires the database module");
            Copy(module, plugins / ("zz_database" + extension));
        }
        if (argc == 12 && std::string(argv[10]) == "database_configured") {
            const auto file = script / "configs/extensions/source2root.database/databases.json";
            if (const auto* config = std::getenv("SR_DATABASE_TEST_CONFIG"); config && *config) {
                Copy(config, file);
                std::filesystem::permissions(file, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
            } else {
                std::filesystem::create_directories(file.parent_path());
                std::ofstream(file) << R"({"schema":1,"connections":{"acceptance":{"driver":"sqlite","database":"shared","allow_plugins":["hello"]}}})";
            }
        }
        std::ofstream(script / "configs/admin_groups.cfg") << R"("Groups" { "fixture" { "immunity" "10" "permissions" { "demo.hello" "1" "demo.status" "1" "admin.kick" "1" "admin.changemap" "1" "admin.restart" "1" } } })";
        const std::string permissions = R"("Admins" { "Fixture" { "identity" "STEAM_0:1:61" "group" "fixture" } })";
        std::ofstream(script / "configs/admins.cfg") << permissions;
        const auto core_cfg = fixture / "cfg/source2root/source2root.cfg";
        std::filesystem::create_directories(core_cfg.parent_path());
        const std::string core_values = "sr_show_activity 5\nsr_chat_public_trigger \"!\"\nsr_chat_silent_trigger \"/\"\n";
        std::ofstream(core_cfg) << core_values;
        Library tier0(bin / std::filesystem::path(argv[3]).filename());
        Library adapter(bin / adapter_name);
        Library host(bin / host_name);
        auto network_init = adapter.Get<bool (*)(const char*)>("SrFixtureNetworkInitialize");
        auto menu_text = adapter.Get<const char* (*)()>("SrFixtureMenuText");
        auto network_stop = adapter.Get<bool (*)()>("SrFixtureNetworkStop");
        auto input_state = adapter.Get<void (*)(std::uint64_t, std::uint64_t, KeelResult)>("SrFixtureInput");
        if (argc >= 12) Check(network_init(argv[11]), "initialize native protocol fixture");
        auto factory = adapter.Get<KeelCreateInterfaceFn>("SrFixtureFactory");
        auto command = adapter.Get<bool (*)(const char*, int)>("SrFixtureCommand");
        auto frame = adapter.Get<void (*)()>("SrFixtureFrame");
        auto count = adapter.Get<unsigned (*)()>("SrFixtureCommands");
        auto cvars = adapter.Get<unsigned (*)()>("SrFixtureConVars");
        auto set_cvar = adapter.Get<bool (*)(const char*, const char*)>("SrFixtureSetConVar");
        auto cvar_equals = adapter.Get<bool (*)(const char*, const char*)>("SrFixtureConVarEquals");
        auto chat = adapter.Get<const char* (*)()>("SrFixtureChatOutput");
        auto say = adapter.Get<bool (*)(const char*, int)>("SrFixtureChat");
        auto chat_command = adapter.Get<bool (*)(const char*, const char*, int)>("SrFixtureChatCommand");
        auto reconnect = adapter.Get<void (*)()>("SrFixtureReconnect");
        auto actions = adapter.Get<unsigned (*)(KeelPlayerAction*)>("SrFixturePlayerActions");
        auto action_state = adapter.Get<void (*)(unsigned, unsigned, bool)>("SrFixtureActionState");
        auto kick_state = adapter.Get<void (*)(bool, bool, bool)>("SrFixtureKickState");
        auto kicks = adapter.Get<unsigned (*)()>("SrFixtureKicks");
        auto kick_reason = adapter.Get<const char* (*)()>("SrFixtureKickReason");
        auto read_listening = adapter.Get<bool (*)(int, int)>("SrFixtureReadListening");
        auto write_listening = adapter.Get<bool (*)(int, int, bool)>("SrFixtureWriteListening");
        auto fail_listening = adapter.Get<void (*)(bool)>("SrFixtureFailListening");
        auto listening_calls = adapter.Get<unsigned (*)()>("SrFixtureListeningCalls");
        auto map_changes = adapter.Get<unsigned (*)()>("SrFixtureMapChanges");
        auto changed_map = adapter.Get<const char* (*)()>("SrFixtureChangedMap");
        auto restart_variable = adapter.Get<void (*)(unsigned, bool)>("SrFixtureRestartVariable");
        auto entity_error = adapter.Get<void (*)(unsigned)>("SrFixtureEntityError");
        auto entity_reads = adapter.Get<unsigned (*)()>("SrFixtureEntityReads");
        auto messages = tier0.Get<const char* (*)()>("KeelTest_Messages");
        auto start = host.Get<KeelHostStartFn>("KeelHost_Start");
        auto complete = host.Get<KeelHostCompleteStartupFn>("KeelHost_CompleteStartup");
        auto stop = host.Get<KeelHostStopFn>("KeelHost_Stop");
        KeelHostCompatibilityInfo compatibility{};
        compatibility.size = sizeof(compatibility);
        compatibility.profile = "source2root-headless-fixture";
        compatibility.game_version = "fixture";
        const auto path = bin.string();
        const KeelHostStartInfo info{sizeof(info), KEELS2_HOST_ABI_VERSION, factory, factory, path.c_str(),
                                    "cs2", platform.c_str(), &compatibility};
        Check(start(&info) == KEELS2_HOST_START_RUNNING && complete(), "real host startup");
        auto contains = [&](const char* value) { return std::string(messages()).find(value) != std::string::npos; };
        std::size_t printed = 0;
        auto run = [&](const std::string& text) {
            Check(command(text.c_str(), -1), text.c_str());
            const std::string log = messages();
            std::cout << log.substr(printed) << std::flush;
            printed = log.size();
        };
        std::cout << messages();
        if (argc == 11 && std::string(argv[10]) == "stock") {
            Check(contains("required unload preparation service is unavailable") && count() == 1,
                  "stock baseline must refuse platform before initialization");
            Check(stop(), "stock host stops after rejected module");
            std::cout << messages() << "stock KeelS2 missing-unload-service limitation reproduced\n";
            return 0;
        }
        if (argc == 12 && std::string(argv[10]) == "topmenus") {
            auto player_lookup = adapter.Get<void (*)(KeelResult)>("SrFixturePlayerLookup");
            const auto occurrences = [&](const char* text) {
                const std::string log = messages(); unsigned total = 0; std::size_t offset = 0;
                while ((offset = log.find(text, offset)) != std::string::npos) { ++total; offset += std::strlen(text); }
                return total;
            };
            auto press = [&](std::uint64_t button) {
                input_state(0, 1, KEEL_RESULT_OK); frame();
                input_state(button, 1, KEEL_RESULT_OK); frame();
            };
            Check(occurrences("TOP_PRIMARY_LOADED") == 1 && occurrences("TOP_CONTRIBUTOR_LOADED") == 1,
                "independent scripts contribute to a shared menu");
            run("sr_top_show");
            Check(std::string(menu_text()).find("Shared tools") != std::string::npos &&
                std::string(menu_text()).find("Player actions") != std::string::npos &&
                std::string(menu_text()).find("Disabled action") != std::string::npos &&
                std::string(menu_text()).find("Denied action") == std::string::npos &&
                std::string(menu_text()).find("Hidden action") == std::string::npos, "core and callback access filter shared display");
            press(KEELS2_BUTTON_USE);
            Check(std::string(menu_text()).find("Contributed action") != std::string::npos &&
                std::string(menu_text()).find("Local action") != std::string::npos, "category contains both plugins' entries");
            press(KEELS2_BUTTON_USE);
            Check(occurrences("TOP_OTHER_SELECTED") == 1 && !*menu_text(), "selection invokes contributor with its own player handle");
            run("sr_top_show"); press(KEELS2_BUTTON_USE);
            std::ofstream(script / "configs/admins.cfg") << "\"Admins\" {}";
            run("sr_top_permissions"); frame();
            Check(!*menu_text(), "revoking category permission closes the active submenu");
            std::ofstream(script / "configs/admins.cfg") << permissions;
            run("sr_top_permissions");
            run("sr_top_show");
            player_lookup(KEEL_RESULT_ENGINE_FAILURE); frame(); run("sr_top_close"); frame();
            player_lookup(KEEL_RESULT_OK); frame();
            Check(!*menu_text(), "failed renderer cleanup retains context through resource destruction and retries");
            run("sr_top_show"); run("sr plugins pause hello"); frame();
            Check(!*menu_text(), "pausing display owner closes the view");
            run("sr plugins resume hello");
            run("sr_top_show"); run("sr_top_title"); frame();
            Check(std::string(menu_text()).find("Changed title") != std::string::npos, "title invalidation refreshes open display");
            press(KEELS2_BUTTON_USE);
            run("sr plugins pause top_other"); frame();
            Check(std::string(menu_text()).find("Contributed action") == std::string::npos &&
                std::string(menu_text()).find("Local action") != std::string::npos, "paused contributor disappears before selection");
            press(KEELS2_BUTTON_USE);
            Check(occurrences("TOP_LOCAL_SELECTED") == 1 && occurrences("TOP_OTHER_SELECTED") == 1, "remaining owner still selects normally");
            run("sr plugins resume top_other"); run("sr_top_show"); press(KEELS2_BUTTON_USE);
            run("sr plugins reload top_other"); frame();
            Check(occurrences("TOP_CONTRIBUTOR_LOADED") == 2, "staged reload accepts matching contribution key");
            press(KEELS2_BUTTON_USE);
            Check(occurrences("TOP_OTHER_SELECTED") == 2, "reloaded generation owns the new callback");
            run("sr_top_show"); press(KEELS2_BUTTON_USE);
            reconnect(); frame();
            run("sr_top_closed"); press(KEELS2_BUTTON_USE);
            Check(occurrences("TOP_DISPLAY_CLOSED") == 1 && occurrences("TOP_OTHER_SELECTED") == 2,
                "reused slot closes old connection's display without invoking a stale action");
            run("sr_top_show"); press(KEELS2_BUTTON_USE);
            run("sr plugins unload top_other"); frame();
            run("sr_top_hide"); press(KEELS2_BUTTON_USE);
            Check(occurrences("TOP_LOCAL_SELECTED") == 1, "dynamic access is rechecked before selection");
            run("sr plugins reload hello");
            run("sr_top_mutate"); frame();
            Check(occurrences("TOP_MUTATION_REJECTED") == 1 && !*menu_text(), "access callback mutation rejects display atomically");
            run("sr_top_show"); press(KEELS2_BUTTON_USE); run("sr_top_selfclose"); press(KEELS2_BUTTON_USE);
            Check(occurrences("TOP_SELF_CLOSED") == 1 && std::string(menu_text()).find("Changed title") != std::string::npos,
                "selection may remove itself and show another menu without stale context");
            run("sr plugins load top_other"); run("sr_top_fault");
            run("sr_top_show"); frame();
            Check(!*menu_text(), "script fault retires contribution and discards an invalidated display");
            run("sr_top_show");
            Check(std::string(menu_text()).find("Player actions") == std::string::npos, "faulted callback cannot leave an active category");
            run("keel plugins unload 2");
            Check(contains("plugin unload is blocked"), "consumer resources retain native provider");
            run("sr plugins unload top_other"); run("sr plugins unload hello"); frame();
            Check(!*menu_text(), "script unload closes display before callback storage is reclaimed");
            run("keel plugins unload 2"); run("keel plugins load sr_example");
            run("sr plugins load hello"); run("sr plugins load top_other"); run("sr_top_show");
            press(KEELS2_BUTTON_USE); press(KEELS2_BUTTON_USE);
            Check(occurrences("TOP_OTHER_SELECTED") == 3, "provider reload reacquires consumer and callback services");
            const bool stopped = stop();
            Check(stopped || stop(), "top menu provider releases all services after script cleanup and unload retry");
            Check(network_stop(), "top menu renderer fixture teardown");
            Check(!contains("TOP_FAILED_CALLBACK_IDENTITY") && !contains("TOP_FAILED_CONTRIBUTOR_IDENTITY") &&
                !contains("TOP_FAILED_REOPEN") && !contains("TOP_FAILED_MUTATION") && !contains("TOP_FAILED_TITLE") &&
                !contains("TOP_FAILED_CLOSED") && !contains("TOP_FAILED_PERMISSIONS"), "all ownership and mutation assertions passed");
            std::cout << messages() << "Shared top menu actual-host lifecycle passed\n";
            return 0;
        }
        if (argc == 12 && std::string(argv[10]) == "persistent") {
            const auto occurrences = [&](const char* value) {
                const std::string log = messages(); unsigned found = 0; std::size_t offset = 0;
                while ((offset = log.find(value, offset)) != std::string::npos) { ++found; offset += std::strlen(value); }
                return found;
            };
            Check(occurrences("PERSISTENT_STORED_OK") == 1,"persistent service acquired at script startup");
            run("sr_persistent_module"); frame(); frame();
            Check(occurrences("PERSISTENT_NESTED_OK") == 1 && occurrences("PERSISTENT_FRAME_OK") == 2,
                "callbacks repeat from native reentry and idle host frames");
            run("keel plugins unload 2");
            Check(contains("plugin unload is blocked"),"script lease retains callback provider");
            run("sr plugins pause hello"); frame();
            Check(occurrences("PERSISTENT_FRAME_OK") == 2 && occurrences("PERSISTENT_PAUSED_OK") == 1,
                "paused callback is retained without execution");
            run("sr plugins resume hello"); frame();
            Check(occurrences("PERSISTENT_FRAME_OK") == 3,"resume reuses persistent token");
            run("sr plugins reload hello"); run("sr_persistent_module"); frame();
            Check(occurrences("PERSISTENT_STORED_OK") == 2 && occurrences("PERSISTENT_CLEANUP_OK") == 1 &&
                occurrences("PERSISTENT_NESTED_OK") == 2 && occurrences("PERSISTENT_FRAME_OK") == 4,
                "staged reload replaces token and cleans old generation before provider release");
            run("sr plugins unload hello"); frame();
            Check(occurrences("PERSISTENT_CLEANUP_OK") == 2 && occurrences("PERSISTENT_FRAME_OK") == 4,
                "unloaded script stops frame callbacks");
            run("keel plugins unload 2"); run("keel plugins load sr_example");
            run("sr plugins load hello"); run("sr_persistent_module"); frame();
            Check(occurrences("PERSISTENT_STORED_OK") == 3 && occurrences("PERSISTENT_NESTED_OK") == 3 &&
                occurrences("PERSISTENT_FRAME_OK") == 5,"provider reload reacquires callback service and rebinds natives");
            run("sr plugins unload hello");
            Check(stop(),"persistent callback service lease released on host shutdown");
            Check(network_stop(),"persistent callback fixture teardown");
            Check(occurrences("PERSISTENT_CLEANUP_OK") == 3 && !contains("PERSISTENT_FAILED"),"persistent lifecycle has no failures");
            std::cout << messages() << "Persistent callback module lifecycle passed\n";
            return 0;
        }
        if (argc == 12 && std::string(argv[10]) == "sdkcall") {
            auto scalar = adapter.Get<std::int32_t (*)(std::int32_t,float)>("SrFixtureHookScalar");
            auto calls = adapter.Get<unsigned (*)()>("SrFixtureHookCalls");
            auto original = adapter.Get<std::int32_t (*)()>("SrFixtureHookOriginal");
            Check(contains("SDKCALL_READY"),"SDKCall module and script loaded with explicit call permission");
            run("sr_sdkcall");
            Check(contains("SDKCALL_CHECK_OK") && calls() == 2 && original() == 24,
                "direct calls map arguments, bypass or run hooks and reject same-resource recursion");
            run("keel plugins unload 2"); Check(contains("plugin unload is blocked"),"prepared call retains provider");
            run("sr plugins pause hello"); Check(scalar(3,2) == 8,"paused script hook bypasses callback");
            run("sr plugins resume hello");
            run("sr_sdkcall_close"); Check(contains("SDKCALL_CLOSE_OK") && original() == 24,"call and hook can close inside callback");
            frame(); Check(scalar(3,2) == 8,"deferred cleanup restores original");
            run("sr_sdkcall_stale"); Check(contains("stale, foreign or wrong-type handle"),"closed call cannot be reused");
            run("sr plugins reload hello"); frame();
            const auto before = calls(); run("sr_sdkcall"); Check(calls() == before + 2,"new script generation owns fresh calls");
            run("sr plugins unload hello"); frame(); Check(scalar(3,2) == 8,"script cleanup releases hook and prepared call");
            run("keel plugins unload 2"); run("keel plugins load sr_example"); run("sr plugins load hello"); frame();
            const auto reloaded = calls(); run("sr_sdkcall"); Check(calls() == reloaded + 2,"provider reload reacquires optional direct-call service");
            run("sr plugins unload hello"); frame();
            Check(!contains("SDKCALL_FAILED"),"SDKCall script reports no failure");
            Check(stop(),"SDKCall host stops after owned resource cleanup"); Check(network_stop(),"SDKCall fixture teardown");
            std::cout << messages() << "SDKCall native module lifecycle passed\n";
            return 0;
        }
        if (argc == 12 && std::string(argv[10]) == "dhooks") {
            auto scalar = adapter.Get<std::int32_t (*)(std::int32_t,float)>("SrFixtureHookScalar");
            auto calls = adapter.Get<unsigned (*)()>("SrFixtureHookCalls");
            auto original = adapter.Get<std::int32_t (*)()>("SrFixtureHookOriginal");
            const auto occurrences = [&](const char* value) {
                const std::string log = messages(); unsigned found = 0; std::size_t offset = 0;
                while ((offset = log.find(value,offset)) != std::string::npos) { ++found; offset += std::strlen(value); }
                return found;
            };
            Check(occurrences("DHOOKS_READY") == 1,"DHooks module and compiled script initialized");
            Check(scalar(3,2) == 90 && calls() == 1 && original() == 24,"pre changes original arguments and post overrides return");
            run("sr_dhook_disable"); Check(scalar(3,2) == 8,"disabled hook bypasses script");
            run("sr_dhook_enable"); run("sr plugins pause hello");
            Check(scalar(3,2) == 8,"paused script cannot change native call");
            run("sr plugins resume hello"); Check(scalar(3,2) == 90,"resume reuses hook callback");
            const auto pre = occurrences("DHOOKS_PRE_OK"); std::int32_t worker_result = 0;
            std::thread worker([&] { worker_result = scalar(3,2); }); worker.join();
            Check(worker_result == 8 && occurrences("DHOOKS_PRE_OK") == pre,"engine worker does not enter SourcePawn");
            run("keel plugins unload 2"); Check(contains("plugin unload is blocked"),"script keeps detour provider loaded");
            run("sr plugins reload hello"); frame();
            Check(occurrences("DHOOKS_READY") == 2 && scalar(3,2) == 90 && occurrences("DHOOKS_PRE_OK") == pre + 1,
                "staged replacement shares target lease and retires old callback");
            run("sr_dhook_mode"); const auto originals = calls();
            Check(scalar(3,2) == 70 && calls() == originals,"pre supercede prevents original execution");
            run("sr_dhook_mode");
            Check(scalar(3,2) == 60 && calls() == originals && contains("DHOOKS_SELF_CLOSE_OK"),"self-removal retains callback until return");
            frame(); Check(scalar(3,2) == 8,"last hook removal restores original");
            run("sr_dhook_stale"); Check(contains("Hook frame is stale, inactive or belongs to another script."),"saved frame expires after callback");
            run("sr plugins reload hello"); frame();
            run("sr_dhook_mode"); run("sr_dhook_mode"); run("sr_dhook_mode");
            Check(scalar(3,2) == 8 && original() == 8,"script fault discards staged argument mutation and calls original");
            run("sr_dhook_faulted"); Check(contains("DHOOKS_FAULT_RETIRED_OK"),"fault invalidates token and hook");
            frame(); run("sr plugins reload hello"); frame();
            Check(scalar(3,2) == 90 && occurrences("DHOOKS_READY") == 4,"reload replaces failed hook generation");
            run("sr plugins unload hello"); frame(); Check(scalar(3,2) == 8,"unload restores original before provider removal");
            run("keel plugins unload 2"); run("keel plugins load sr_example");
            run("sr plugins load hello"); frame();
            Check(scalar(3,2) == 90 && occurrences("DHOOKS_READY") == 5,"native provider reload reacquires hook and callback services");
            run("sr plugins unload hello"); frame();
            Check(stop(),"DHooks host stops after deferred hook cleanup"); Check(network_stop(),"DHooks fixture teardown");
            Check(!contains("DHOOKS_FAILED"),"DHooks fixture has no reported failures");
            std::cout << messages() << "DHooks native module lifecycle passed\n";
            return 0;
        }
        if (argc == 12 && std::string(argv[10]) == "http") {
            const auto occurrences = [&](const char* value) {
                const std::string log = messages(); unsigned found = 0; std::size_t offset = 0;
                while ((offset = log.find(value, offset)) != std::string::npos) { ++found; offset += std::strlen(value); }
                return found;
            };
            auto await = [&](auto ready) {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                while (!ready()) {
                    if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("HTTP fixture completion timeout");
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
            };
            Check(occurrences("HTTP_SCRIPT_START") == 1, "HTTP script startup and request construction");
            run("sr plugins pause hello");
            await([&] { frame(); return contains("HTTP_NATIVE_OK") || contains("HTTP_NATIVE_FAILED"); });
            for (unsigned i = 0; i < 25; ++i) { frame(); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
            Check(!contains("HTTP_SCRIPT_HELLO") && !contains("HTTP_SCRIPT_BINARY") && !contains("HTTP_NATIVE_FAILED"), "script callbacks wait while paused; native service works on worker");
            run("sr plugins resume hello");
            auto completed = [&](unsigned count) {
                frame();
                return occurrences("HTTP_SCRIPT_HELLO") == count && occurrences("HTTP_SCRIPT_BINARY") == count &&
                    occurrences("HTTP_SCRIPT_FORM") == count && occurrences("HTTP_SCRIPT_TLS") == count && occurrences("HTTP_SCRIPT_ERROR") == count;
            };
            await([&] { return completed(1); });
            run("sr_http_check"); run("sr_http_stale");
            Check(contains("stale, foreign or wrong-type handle") && !contains("HTTP_SCRIPT_FAILED"), "HTTP retained response and stale handle rejection");
            run("sr plugins reload hello");
            Check(occurrences("HTTP_SCRIPT_START") == 2, "HTTP staged script reload succeeds");
            await([&] { return completed(2); });
            run("sr_http_pending");
            run("sr plugins unload hello");
            run("keel plugins unload 2");
            Check(contains("plugin unload is blocked") && contains("HTTP Native Fixture"), "native consumer service lease retains HTTP provider");
            run("keel plugins unload 3");
            await([&] { frame(); return stop(); });
            Check(!contains("HTTP_SCRIPT_FAILED") && !contains("HTTP_NATIVE_FAILED"), "HTTP close/unload cancels outstanding callbacks and drains workers");
            Check(network_stop(), "HTTP fixture teardown");
            std::cout << messages() << "HTTP native and script module lifecycle passed\n";
            return 0;
        }
        if (argc == 12 && std::string(argv[10]) == "cstrike") {
            auto occurrences = [&](const char* value) {
                const std::string log = messages(); unsigned count = 0; std::size_t at = 0;
                while ((at = log.find(value, at)) != std::string::npos) { ++count; at += std::strlen(value); }
                return count;
            };
            const auto action_count = adapter.Get<unsigned (*)(KeelPlayerManagementAction*)>("SrFixtureManagementCount");
            const auto action_state = adapter.Get<void (*)(unsigned,unsigned,unsigned)>("SrFixtureManagementState");
            const auto write_count = adapter.Get<unsigned (*)()>("SrFixtureWriteCount");
            const auto write_state = adapter.Get<void (*)(unsigned,unsigned,bool)>("SrFixtureWriteState");
            const auto round_count = adapter.Get<unsigned (*)(KeelRoundTermination*)>("SrFixtureRoundCount");
            const auto round_state = adapter.Get<void (*)(unsigned,unsigned,unsigned)>("SrFixtureRoundState");
            const auto stat_writes = adapter.Get<unsigned (*)()>("SrFixtureStatisticsWrites");
            const auto stat_state = adapter.Get<void (*)(unsigned,unsigned,KeelResult,KeelResult,KeelResult,unsigned)>("SrFixtureStatisticsState");
            Check(stat_writes() == 0,"initialization does not change component statistics");
            run("sr_cs_components");
            Check(occurrences("CSTRIKE_COMPONENTS_OK") == 1 && stat_writes() == 8,"compiled script component mappings and readback");
            stat_state(15,0,KEEL_RESULT_OK,KEEL_RESULT_OK,KEEL_RESULT_OK,0); run("sr_cs_components_readonly");
            Check(contains("CSTRIKE_COMPONENTS_READONLY_OK") && stat_writes() == 8,"read-only component capabilities block every setter");
            stat_state(15,15,KEEL_RESULT_ENGINE_FAILURE,KEEL_RESULT_OK,KEEL_RESULT_OK,0); run("sr_cs_components_caps_error");
            Check(contains("CSTRIKE_COMPONENTS_CAPS_ERROR_OK") && stat_writes() == 8,"component capability failure clears both script outputs");
            stat_state(15,15,KEEL_RESULT_OK,KEEL_RESULT_ENGINE_FAILURE,KEEL_RESULT_OK,0); run("sr_cs_components_read_error");
            Check(occurrences("CSTRIKE_COMPONENTS_READ_ERROR_OK") == 1,"component read failure suppresses partial host result");
            stat_state(15,15,KEEL_RESULT_OK,KEEL_RESULT_OK,KEEL_RESULT_OK,1); run("sr_cs_components_read_error");
            Check(occurrences("CSTRIKE_COMPONENTS_READ_ERROR_OK") == 2,"component read revalidates map epoch");
            stat_state(15,15,KEEL_RESULT_OK,KEEL_RESULT_OK,KEEL_RESULT_ENGINE_FAILURE,0); run("sr_cs_components_write_error");
            Check(contains("CSTRIKE_COMPONENTS_WRITE_ERROR_OK") && stat_writes() == 9,"component error after mutation is visible to script");
            stat_state(15,15,KEEL_RESULT_OK,KEEL_RESULT_OK,KEEL_RESULT_OK,0); run("sr_cs_components_restore");
            Check(contains("CSTRIKE_COMPONENTS_RESTORE_OK") && stat_writes() == 10,"component setter recovers after notification failure");
            Check(round_count(nullptr) == 0,"initialization does not terminate rounds");
            run("sr_cs_round");
            KeelRoundTermination last_round{};
            Check(occurrences("CSTRIKE_ROUND_OK") == 1 && round_count(&last_round) == 3 && last_round.reason == 9 &&
                last_round.delay == 3600 && last_round.team == 2 && !last_round.reserved,"compiled script round parameter mapping");
            round_state(0,KEEL_RESULT_OK,KEEL_RESULT_OK); run("sr_cs_round_unavailable");
            Check(contains("CSTRIKE_ROUND_UNAVAILABLE_OK") && round_count(nullptr) == 3,"unsupported round cannot dispatch");
            round_state(1,KEEL_RESULT_UNSUPPORTED,KEEL_RESULT_OK); run("sr_cs_round_caps_error");
            Check(contains("CSTRIKE_ROUND_CAPS_ERROR_OK") && round_count(nullptr) == 3,"round capability error clears output");
            round_state(1,KEEL_RESULT_OK,KEEL_RESULT_NOT_READY); run("sr_cs_round_error");
            Check(contains("CSTRIKE_ROUND_ERROR_OK") && round_count(nullptr) == 3,"round engine error propagated");
            round_state(1,KEEL_RESULT_OK,KEEL_RESULT_OK);
            Check(occurrences("CSTRIKE_SCRIPT_OK") == 1 && action_count(nullptr) == 0, "Counter-Strike initialization has no player actions");
            Check(write_count() == 0, "Counter-Strike initialization has no statistic writes");
            run("sr_cs_stats");
            Check(occurrences("CSTRIKE_STATS_OK") == 1 && write_count() == 4, "score/MVP mapping and readback through actual script");
            write_state(0,KEEL_RESULT_OK,false); run("sr_cs_stats_unavailable");
            Check(contains("CSTRIKE_STATS_UNAVAILABLE_OK") && write_count() == 4, "unavailable writer preserves statistic reads");
            write_state(1,KEEL_RESULT_ENGINE_FAILURE,false); run("sr_cs_stats_error");
            Check(contains("CSTRIKE_STATS_ERROR_OK") && write_count() == 4, "statistic engine failure reaches script");
            write_state(1,KEEL_RESULT_OK,false);
            run("sr_cs_check");
            KeelPlayerManagementAction last{};
            Check(occurrences("CSTRIKE_ACTIONS_OK") == 1 && action_count(&last) == 3 &&
                last.kind == KEELS2_PLAYER_MANAGEMENT_SWITCH_TEAM && last.team == 3, "three script actions reach the owned controller");
            run("sr_cs_invalid");
            Check(contains("CSTRIKE_INVALID_OK") && action_count(nullptr) == 3 && write_count() == 4, "invalid team/player/statistic cannot dispatch");
            Check(round_count(nullptr) == 3,"invalid round cannot dispatch");
            Check(stat_writes() == 10,"invalid component input cannot dispatch");
            action_state(0,KEEL_RESULT_OK,KEEL_RESULT_OK);
            run("sr_cs_unavailable");
            Check(contains("CSTRIKE_UNAVAILABLE_OK") && action_count(nullptr) == 3, "unsupported actions cannot dispatch");
            action_state(7,KEEL_RESULT_UNSUPPORTED,KEEL_RESULT_OK);
            run("sr_cs_caps_error");
            Check(contains("CSTRIKE_CAPS_ERROR_OK") && action_count(nullptr) == 3, "capability failure clears output and records error");
            action_state(7,KEEL_RESULT_OK,KEEL_RESULT_ENGINE_FAILURE);
            run("sr_cs_action_error");
            Check(contains("CSTRIKE_ACTION_ERROR_OK") && action_count(nullptr) == 3, "engine rejection reaches script");
            action_state(7,KEEL_RESULT_OK,KEEL_RESULT_OK);
            reconnect(); run("sr_cs_reconnected");
            Check(contains("CSTRIKE_RECONNECTED_OK") && action_count(nullptr) == 3 && write_count() == 4, "saved player cannot target replacement connection");
            Check(stat_writes() == 10,"saved player cannot change replacement connection statistics");
            run("sr_cs_check");
            run("sr_cs_round");
            Check(round_count(nullptr) == 6,"round API still available after player reconnect");
            Check(action_count(nullptr) == 6 && occurrences("CSTRIKE_ACTIONS_OK") == 2, "new player handle dispatches after reconnect");
            run("keel plugins unload 2"); Check(contains("plugin unload is blocked"), "script retains Counter-Strike provider");
            run("sr plugins pause hello"); run("sr_cs_check"); run("sr_cs_stats"); run("sr_cs_round"); run("sr_cs_components");
            Check(round_count(nullptr) == 6,"paused script cannot terminate rounds");
            Check(stat_writes() == 10 && occurrences("CSTRIKE_COMPONENTS_OK") == 1,"paused script cannot change component statistics");
            Check(action_count(nullptr) == 6 && write_count() == 4, "paused script cannot dispatch its command");
            run("sr plugins resume hello"); run("sr plugins reload hello"); run("sr_cs_check"); run("sr_cs_stats"); run("sr_cs_round"); run("sr_cs_components");
            Check(stat_writes() == 18 && occurrences("CSTRIKE_COMPONENTS_OK") == 2,"script reload rebinds component statistics");
            Check(round_count(nullptr) == 9 && occurrences("CSTRIKE_ROUND_OK") == 3,"script reload rebinds round natives");
            Check(occurrences("CSTRIKE_SCRIPT_OK") == 2 && action_count(nullptr) == 9, "script reload rebinds actions");
            Check(occurrences("CSTRIKE_STATS_OK") == 2 && write_count() == 8, "script reload rebinds statistics");
            run("sr plugins unload hello"); run("keel plugins unload 2"); run("keel plugins load sr_example" + extension);
            run("sr plugins load hello"); run("sr_cs_check"); run("sr_cs_stats"); run("sr_cs_round"); run("sr_cs_components");
            Check(stat_writes() == 26 && occurrences("CSTRIKE_COMPONENTS_OK") == 3,"provider reload rebinds component statistics");
            Check(round_count(nullptr) == 12 && occurrences("CSTRIKE_ROUND_OK") == 4,"provider reload rebinds round natives");
            Check(occurrences("CSTRIKE_SCRIPT_OK") == 3 && action_count(nullptr) == 12 && !contains("CSTRIKE_FAILED"), "provider reload rebinds actions");
            Check(occurrences("CSTRIKE_STATS_OK") == 3 && write_count() == 12, "provider reload rebinds statistics");
            run("sr plugins unload hello");
            Check(stop(), "Counter-Strike host stop"); Check(network_stop(), "Counter-Strike fixture teardown");
            std::cout << messages() << "Counter-Strike native module lifecycle passed\n";
            return 0;
        }
        if (argc == 12 && std::string(argv[10]) == "sdktools") {
            auto occurrences = [&](const char* value) {
                const std::string log = messages(); unsigned count = 0; std::size_t at = 0;
                while ((at = log.find(value, at)) != std::string::npos) { ++count; at += std::strlen(value); }
                return count;
            };
            Check(occurrences("SDKTOOLS_SCRIPT_OK") == 1, "SDKTools compiled script initialization");
            run("sr_sdk_check");
            Check(contains("SDKTOOLS_READ_OK"), "typed schema read via actual script and host");
            const auto write_count = adapter.Get<unsigned (*)()>("SrFixtureWriteCount");
            const auto write_state = adapter.Get<void (*)(unsigned,unsigned,bool)>("SrFixtureWriteState");
            Check(write_count() == 0,"SDKTools initialization has no field writes");
            run("sr_sdk_write");
            Check(contains("SDKTOOLS_WRITE_OK") && write_count() == 9,"typed field writes through actual script and host");
            write_state(0,KEEL_RESULT_OK,false); run("sr_sdk_write_unavailable");
            Check(contains("SDKTOOLS_WRITE_UNAVAILABLE_OK") && write_count() == 9,"missing capability prevents write");
            write_state(1,KEEL_RESULT_ENGINE_FAILURE,false); run("sr_sdk_write_error");
            Check(contains("SDKTOOLS_WRITE_ERROR_OK") && write_count() == 9,"write failure reaches script");
            write_state(1,KEEL_RESULT_OK,true); run("sr_sdk_write_callback");
            Check(contains("SDKTOOLS_WRITE_CALLBACK_OK") && write_count() == 11,"nested callback closes active Entity/Field without invalid access");
            run("sr_sdk_wrong");
            Check(contains("Foreign extension or wrong resource type."), "wrong entity resource type raises native error");
            run("sr_sdk_stale");
            Check(contains("stale, foreign or wrong-type handle"), "closed entity resource raises native error");
            adapter.Get<void (*)()>("SrFixtureEntityEpoch")();
            run("sr_sdk_epoch");
            Check(contains("SDKTOOLS_EPOCH_OK"), "old entity epoch refused and new lookup succeeds");
            reconnect();
            run("sr_sdk_player");
            Check(contains("SDKTOOLS_PLAYER_CHANGED_OK"), "stale player handle cannot select new connection pawn");
            run("keel plugins unload 2");
            Check(contains("plugin unload is blocked"), "script retains SDKTools provider");
            run("sr plugins pause hello");
            run("sr plugins resume hello");
            run("sr plugins reload hello");
            run("sr_sdk_check");
            Check(occurrences("SDKTOOLS_SCRIPT_OK") == 2 && !contains("SDKTOOLS_FAILED"), "script reload succeeds with new generation and releases old resources");
            run("sr plugins unload hello");
            run("keel plugins unload 2");
            run("keel plugins load sr_example");
            run("sr plugins load hello");
            run("sr_sdk_check");
            Check(occurrences("SDKTOOLS_SCRIPT_OK") == 3 && !contains("SDKTOOLS_FAILED"), "SDKTools provider reload rebinds script natives");
            run("sr plugins unload hello");
            Check(stop(), "SDKTools host stop after script cleanup");
            Check(network_stop(), "SDKTools fixture teardown");
            std::cout << messages() << "SDKTools native module lifecycle passed\n";
            return 0;
        }
        if (argc == 12 && std::string(argv[10]) == "regex") {
            auto occurrences = [&](const char* value) {
                const std::string log = messages(); unsigned count = 0; std::size_t at = 0;
                while ((at = log.find(value, at)) != std::string::npos) { ++count; at += std::strlen(value); }
                return count;
            };
            Check(contains("REGEX_SCRIPT_OK"), "regex compiled script initialization");
            run("sr_regex_check");
            Check(contains("REGEX_RETAINED_OK"), "owned captures survive pattern close");
            run("sr_regex_wrong");
            Check(contains("Foreign extension or wrong resource type."), "wrong regex resource type raises native error");
            run("sr_regex_stale");
            Check(contains("stale, foreign or wrong-type handle") && !contains("REGEX_FAILED"), "stale regex handle raises native error");
            run("sr_regex_quota");
            Check(occurrences("REGEX_QUOTA_OK") == 1 && !contains("REGEX_FAILED"), "provider pattern and result quotas");
            run("keel plugins unload 2");
            Check(contains("plugin unload is blocked"), "script lease retains regex provider");
            run("sr plugins reload hello");
            run("sr_regex_check");
            Check(occurrences("REGEX_SCRIPT_OK") == 2 && !contains("REGEX_FAILED"), "staged regex script reload succeeds");
            run("sr_regex_quota");
            Check(occurrences("REGEX_QUOTA_OK") == 2 && !contains("REGEX_FAILED"), "reload releases prior resources and quota");
            run("sr plugins unload hello");
            run("keel plugins unload 2");
            run("keel plugins load sr_example" + extension);
            run("sr plugins load hello");
            run("sr_regex_check");
            Check(occurrences("REGEX_SCRIPT_OK") == 3 && !contains("REGEX_FAILED"), "regex extension reload rebinds native functions");
            run("sr plugins unload hello");
            Check(stop(), "regex fixture host stop after script resources release");
            Check(network_stop(), "regex fixture teardown");
            std::cout << messages() << "Regex module lifecycle passed\n";
            return 0;
        }
        if (argc == 12 && std::string(argv[10]) == "geoip") {
            auto await = [&](auto ready) {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                while (!ready()) {
                    Check(std::chrono::steady_clock::now() < deadline, "GeoIP module deadline");
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            };
            run("sr plugins pause hello");
            for (int i = 0; i < 10; ++i) { frame(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
            Check(!contains("GEO_SCRIPT_OK"), "paused script retains async GeoIP completion");
            run("sr plugins resume hello");
            await([&] { frame(); return contains("GEO_SCRIPT_OK") && contains("GEO_MISSING_OK"); });
            run("keel plugins unload 2");
            Check(contains("plugin unload is blocked"), "GeoIP provider remains leased by database/record resources");
            const auto root = script / "data/extensions/source2root.geoip";
            std::ofstream(root / "city.mmdb", std::ios::trunc) << "malformed replacement";
            run("sr_geo_reload 0");
            await([&] { frame(); return contains("GEO_RELOAD_ERROR_OK"); });
            Copy(std::filesystem::path(std::getenv("SR_GEOIP_TEST_DATA")) / "GeoIP2-Country-Test.mmdb", root / "city.mmdb");
            run("sr_geo_reload 1");
            await([&] { frame(); return contains("GEO_RELOAD_CHAIN_OK"); });
            run("sr_geo_wrong");
            Check(contains("Foreign extension or wrong resource type."), "wrong GeoIP handle type raises a native error");
            run("sr_geo_pending"); run("sr_geo_close");
            run("sr plugins unload hello");
            await([&] { frame(); return stop(); });
            Check(!contains("GEO_FAILED") && !contains("GEO_UNEXPECTED"), "GeoIP ownership, snapshot replacement and cancellation");
            Check(std::filesystem::is_empty(root / ".snapshots"), "canceled and live snapshots released before extension unload");
            Check(network_stop(), "GeoIP fixture teardown");
            std::cout << messages() << "GeoIP module lifecycle passed\n";
            return 0;
        }
#if defined(SR_PREFS_TEST)
        if (argc == 12 && (std::string(argv[10]) == "clientprefs" || std::string(argv[10]) == "clientprefs_mysql")) {
            auto authentication = adapter.Get<void (*)(bool, bool)>("SrFixtureAuthentication");
            auto player_lookup = adapter.Get<void (*)(KeelResult)>("SrFixturePlayerLookup");
            auto occurrences = [&](const char* text) {
                const std::string log = messages();
                std::size_t offset = 0;
                unsigned found = 0;
                while ((offset = log.find(text, offset)) != std::string::npos) { ++found; offset += std::strlen(text); }
                return found;
            };
            auto await = [&](auto ready) {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                while (!ready()) {
                    Check(std::chrono::steady_clock::now() < deadline, "preferences module deadline");
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            };
            run("sr plugins pause hello");
            for (int i = 0; i < 10; ++i) { frame(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
            Check(!contains("PREFS_SCRIPT_OK"), "paused VM retains preferences callbacks");
            run("sr plugins resume hello");
            await([&] { frame(); return occurrences("PREFS_SCRIPT_OK") == 1; });
            if (std::string(argv[10]) == "clientprefs_mysql")
                await([&] { frame(); return occurrences("PREFS_NETWORK_OK") >= 1; });
            run("sr_prefs_offline");
            authentication(false, false); frame();
            await([&] { frame(); return occurrences("PREFS_OFFLINE_SAVED") == 1; });
            authentication(true, false); frame();
            run("sr plugins reload hello");
            await([&] { frame(); return occurrences("PREFS_SCRIPT_OK") == 2; });
            run("keel plugins unload 2");
            Check(contains("plugin unload is blocked"), "preferences provider retained while scripts use cookies");
            auto press = [&](std::uint64_t button) {
                input_state(0, 1, KEEL_RESULT_OK); frame();
                input_state(button, 1, KEEL_RESULT_OK); frame();
            };
            unsigned menu_saved = 0;
            for (int type = 0; type < 4; ++type) for (int enabled = 1; enabled >= 0; --enabled) {
                run("sr_prefs_type " + std::to_string(type));
                run("sr_prefs_menu");
                const std::string root_menu = menu_text();
                Check(root_menu.find("Music setting") != std::string::npos && root_menu.find("[read only]") != std::string::npos &&
                    root_menu.find("internal") == std::string::npos && root_menu.find("script-private") == std::string::npos,
                    "shared menu shows public/protected preferences and hides private values");
                press(KEELS2_BUTTON_USE);
                Check(std::string(menu_text()).find(type < 2 ? "Yes" : "On") != std::string::npos &&
                    std::string(menu_text()).find("Back to settings") != std::string::npos, "prefab opens correct choice submenu");
                if (!enabled) press(KEELS2_BUTTON_BACK);
                press(KEELS2_BUTTON_USE);
                Check(std::string(menu_text()).find("Player preferences") != std::string::npos, "selection returns to shared menu");
                const auto network_before = occurrences("PREFS_NETWORK_OK");
                run("sr_prefs_observe");
                ++menu_saved;
                await([&] { frame(); return occurrences("PREFS_MENU_SAVED") == menu_saved; });
                if (std::string(argv[10]) == "clientprefs_mysql")
                    await([&] { frame(); return occurrences("PREFS_NETWORK_OK") > network_before; });
                const auto expected = type == 0 ? (enabled ? "yes" : "no") : type == 2 ? (enabled ? "on" : "off") : (enabled ? "1" : "0");
                Check(contains(("PREFS_MENU_VALUE_" + std::to_string(type) + ":" + expected).c_str()), "selected prefab value is committed and read by compiled script");
            }
            run("sr_prefs_menu"); press(KEELS2_BUTTON_USE);
            press(KEELS2_BUTTON_BACK); press(KEELS2_BUTTON_BACK); press(KEELS2_BUTTON_USE);
            Check(std::string(menu_text()).find("Player preferences") != std::string::npos, "explicit submenu Back returns without changing value");
            press(KEELS2_BUTTON_RELOAD);
            Check(!*menu_text(), "shared settings menu closes on Reload");
            run("sr_prefs_menu"); press(KEELS2_BUTTON_USE); run("sr_prefs_drop"); press(KEELS2_BUTTON_USE);
            Check(!*menu_text(), "closed prefab cancels an existing submenu before a stale selection writes");
            run("sr_prefs_type 0"); run("sr_prefs_menu");
            player_lookup(KEEL_RESULT_ENGINE_FAILURE); frame(); run("sr_prefs_close"); frame();
            player_lookup(KEEL_RESULT_OK); frame();
            Check(!*menu_text(), "menu callback storage survives resource close while host cleanup retries");
            run("sr_prefs_observe"); ++menu_saved;
            await([&] { frame(); return occurrences("PREFS_MENU_SAVED") == menu_saved; });
            run("sr_prefs_type 0"); run("sr_prefs_menu"); run("sr plugins pause hello"); frame();
            Check(!*menu_text(), "pausing the display owner closes its native preferences menu");
            run("sr plugins resume hello"); run("sr_prefs_menu");
            run("sr_prefs_pending");
            authentication(false, false); frame();
            Check(!contains("PREFS_UNEXPECTED_CALLBACK") && !*menu_text(), "lost authentication cancels player waits and menu");
            authentication(true, false); frame();
            run("sr_prefs_pending");
            authentication(true, true); frame();
            Check(!contains("PREFS_UNEXPECTED_CALLBACK"), "bot identities cannot receive player waits");
            authentication(true, false); frame();
            run("sr_prefs_pending");
            reconnect(); frame();
            Check(!contains("PREFS_UNEXPECTED_CALLBACK"), "reused player slot cannot receive the old connection's callback");
            run("sr plugins reload hello");
            await([&] { frame(); return occurrences("PREFS_SCRIPT_OK") == 3; });
            run("sr_prefs_pending");
            run("sr plugins reload hello");
            await([&] { frame(); return occurrences("PREFS_SCRIPT_OK") == 4; });
            if (std::string(argv[10]) == "clientprefs_mysql")
                await([&] { frame(); return occurrences("PREFS_NETWORK_OK") >= 9; });
            run("sr_prefs_final");
            run("sr plugins unload hello");
            await([&] { return stop(); });
            Check(!contains("PREFS_FAILED") && !contains("PREFS_UNEXPECTED_CALLBACK"), "preferences ownership, cache and save callbacks isolated by script generation");
            const auto storage = source2root::prefs::ConfiguredStorage(script / "data/extensions/source2root.clientprefs",
                script / "configs/extensions/source2root.clientprefs/databases.json");
            const auto values = storage()->Load(76561197960265851ULL);
            Check(values.at("music").text == "on-unload", "accepted preference write committed after script unload before host stop");
            Check(storage()->Load(76561198000000001ULL).at("music").text == "offline-latest" &&
                storage()->Load(76561198000000002ULL).at("music").text == "offline-on-unload",
                "offline callbacks survive authentication loss and accepted writes survive script unload");
            Check(network_stop(), "preferences fixture teardown");
            std::cout << messages() << "Client preferences module lifecycle passed\n";
            return 0;
        }
#endif
        if (argc == 12 && (std::string(argv[10]) == "database_async" || std::string(argv[10]) == "database_configured")) {
            auto occurrences = [&](const char* text) {
                const std::string log = messages();
                std::size_t offset = 0;
                unsigned found = 0;
                while ((offset = log.find(text, offset)) != std::string::npos) { ++found; offset += std::strlen(text); }
                return found;
            };
            auto await = [&](auto ready) {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
                while (!ready()) {
                    Check(std::chrono::steady_clock::now() < deadline, "async module deadline");
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            };
            run("sr plugins pause hello");
            for (int i = 0; i < 10; ++i) { frame(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
            Check(!contains("SQL_ASYNC_OK") && !contains("SQL_ASYNC_ERROR_OK"), "paused VM cannot receive completed database work");
            run("sr plugins resume hello");
            await([&] { frame(); return occurrences("SQL_ASYNC_OK") == 1 && occurrences("SQL_ASYNC_ERROR_OK") == 1; });
            run("keel plugins unload 2");
            Check(contains("plugin unload is blocked"), "provider remains usable after unload refusal");
            run("sr_async");
            run("sr plugins reload hello");
            await([&] { frame(); return occurrences("SQL_ASYNC_OK") == 2 && occurrences("SQL_ASYNC_ERROR_OK") == 2; });
            Check(!contains("SQL_ASYNC_FAILED") && !contains("SQL_ASYNC_UNEXPECTED"),
                "typed results, recoverable SQL error, cancellation and generation isolation");
            run("sr_async_slow");
            run("sr plugins unload hello");
            // Global stop is retryable while canceled workers leave their images.
            await([&] { return stop(); });
            Check(!contains("SQL_ASYNC_UNEXPECTED"), "unload suppresses in-flight completion");
            Check(network_stop(), "async database fixture teardown");
            std::cout << messages() << "Async database module lifecycle passed\n";
            return 0;
        }
        if (argc == 12 && std::string(argv[10]) == "database") {
            Check(contains("SQLITE_SCRIPT_OK"), "database module executes actual script and bound SQL values");
            run("keel plugins unload 2");
            run("sr plugins list");
            Check(contains("plugin unload is blocked"), "provider remains while used by a script");
            run("sr plugins reload hello");
            const std::string reloaded = messages();
            const auto first = reloaded.find("SQLITE_SCRIPT_OK");
            Check(first != std::string::npos && reloaded.find("SQLITE_SCRIPT_OK", first + 1) != std::string::npos,
                "staged reload executes database script and cleans retired resources");
            run("sr plugins unload hello");
            run("keel plugins unload 2");
            frame();
            run("keel plugins load sr_example" + extension);
            frame();
            run("sr plugins load hello");
            Check(!stop(), "global stop first releases script leases and retains extension images");
            Check(stop(), "global stop retry destroys database resources before releasing extension images");
            Check(network_stop(), "database fixture network teardown");
            std::cout << messages() << "Database native module lifecycle passed\n";
            return 0;
        }
        Check(contains("SR_ExampleAdd registered"), "real example module registration");
        if (argc == 12 && std::string(argv[10]) == "late") {
            const auto registrations = count();
            run("sr plugins unload hello");
            run("keel plugins unload 2");
            run("keel plugins unload 1");
            Check(count() == 1 && cvars() == 0, "late-load fixture starts with only the host command");
            for (const auto* operation : {"load", "reload"}) {
                run(std::string("keel plugins ") + operation + " source2root" + extension);
                run("keel plugins load sr_example" + extension);
                frame();
                const auto before = std::string(messages()).size();
                run("sr plugins list");
                Check(std::string(messages()).substr(before).find("hello                 running") != std::string::npos,
                      "late-loaded and reloaded native core discovers enabled scripts on its first frame");
                const auto replies = std::string(chat()).size();
                Check(command("sr_hello", 3) && std::string(chat()).size() > replies,
                      "automatically restored script can call its native provider");
                frame();
                Check(count() == registrations, "later frames do not duplicate script registrations");
                run("sr plugins unload hello");
                run("keel plugins unload 2");
            }
            Check(stop(), "late-load fixture stops without retained resources");
            Check(network_stop(), "late-load fixture releases native message allocations");
            std::cout << "late native loading and reload discovery passed\n";
            return 0;
        }
        run("sr");
        run("sr help plugins");
        Check(contains("Source2Root Menu:") && contains("Usage: sr plugins <command>"), "approved management menus are real commands");
        run("sr plugins list");
        run("sr plugins cmds hello");
        Check(contains("sr_hello | hello | running | permission: demo.hello"), "script command inventory has owner and required permission");
        const auto extension_start = std::string(messages()).size();
        run("sr extensions list");
        const auto extension_list = std::string(messages()).substr(extension_start);
        Check(extension_list.find("sr_example" + extension) != std::string::npos && extension_list.find("source2root" + extension) == std::string::npos,
              "extension inventory excludes unrelated native framework plugins");
        run("sr config");
        Check(cvars() == 4 && contains("sr_show_activity = \"5\""), "core settings read specified cfg path and register real KeelS2 ConVars");
        run("sr config sr_show_activity 13");
        Check(contains("sr_show_activity = \"13\""), "management setting uses actual ConVar callback");
        Check(command("sr config sr_show_activity 0", 3), "fixture delivers unauthorized management invocation");
        const auto config_start = std::string(messages()).size();
        run("sr config sr_show_activity");
        Check(std::string(messages()).substr(config_start).find("sr_show_activity = \"13\"") != std::string::npos,
              "client console cannot change management settings");
        run("sr plugins unload hello");
        run("keel plugins unload 2");
        run("keel plugins pause 1");
        Check(contains("plugin paused: [01] Source2Root"), "native Source2Root really enters paused state");
        const auto paused_chat = std::string(chat());
        Check(say("/unknown", 3) && std::string(chat()) == paused_chat,
              "native pause disables the engine chat hook without retaining callbacks");
        Check(set_cvar("sr_show_activity", "9") && set_cvar("sr_chat_public_trigger", "/") && set_cvar("sr_chat_silent_trigger", "!"),
              "operator changes core settings while native plugin callbacks are paused");
        run("keel plugins resume 1");
        const auto resumed_config = std::string(messages()).size();
        run("sr config");
        const auto resumed_values = std::string(messages()).substr(resumed_config);
        Check(resumed_values.find("sr_show_activity = \"9\"") != std::string::npos &&
            resumed_values.find("sr_chat_public_trigger = \"/\"") != std::string::npos &&
            resumed_values.find("sr_chat_silent_trigger = \"!\"") != std::string::npos,
            "native resume reconciles a complete changed core snapshot, including swapped prefixes");
        run("keel plugins pause 1");
        Check(set_cvar("sr_show_activity", "1") && set_cvar("sr_chat_public_trigger", "??") && set_cvar("sr_chat_silent_trigger", "??"),
              "operator can produce an invalid pair while callbacks are paused");
        run("keel plugins resume 1");
        const auto rejected_config = std::string(messages()).size();
        run("sr config");
        const auto rejected_values = std::string(messages()).substr(rejected_config);
        Check(rejected_values.find("sr_show_activity = \"9\"") != std::string::npos &&
            rejected_values.find("sr_chat_public_trigger = \"/\"") != std::string::npos &&
            rejected_values.find("sr_chat_silent_trigger = \"!\"") != std::string::npos,
            "invalid resumed settings retain the entire previous configuration");
        Check(cvar_equals("sr_show_activity", "9") && cvar_equals("sr_chat_public_trigger", "/") &&
            cvar_equals("sr_chat_silent_trigger", "!"), "invalid resumed values are restored in the engine as well as the cache");
        run("sr config sr_chat_public_trigger \"\"");
        run("sr config sr_chat_silent_trigger /");
        run("sr config sr_chat_public_trigger !");
        run("sr config sr_show_activity 13");
        run("keel plugins load sr_example" + extension);
        run("sr plugins load hello");
        run("sr version");
        Check(contains("Source2Root Version 1.0.0") && contains("Revision:") && contains("Script API: 2"), "version reports product, build and API metadata");
        run("sr plugins info hello");
        Check(contains("hello | running"), "real compiled script initialized");
        const bool packaged = argc >= 11 && std::string(argv[10]) == "package";
        auto install_script = [&](const std::string& id, const std::string& bytecode) {
            Copy(std::filesystem::path(argv[7]).parent_path() / (bytecode + ".smx"), script / "plugins" / id / "main.smx");
            std::ofstream(script / "plugins" / id / "plugin.json") << "{\"schema\":1,\"id\":\"" << id <<
                "\",\"name\":\"ConVar fixture\",\"author\":\"tests\",\"version\":\"1.0.0\",\"api\":2,\"entry\":\"main.smx\",\"enabled\":false,\"dependencies\":[]}";
        };
        install_script("admin", "admin");
        run("sr plugins load admin");
        Check(!command("sr_permissions_reload", -1), "temporary native reload command is removed");
        run("sr_who");
        Check(contains("#70 Module fixture player | 76561197960265851 STEAM_1:1:61 [U:1:123]"),
            "bundled administration uses actual native player metadata and formatting");
        for (int i = 0; i < 140; ++i) {
            reconnect();
            const auto start = std::string(messages()).size();
            run("sr_who");
            Check(std::string(messages()).substr(start).find("#" + std::to_string(71 + i) + " Module fixture player") != std::string::npos,
                "actual host reconnects do not exhaust script player handles");
        }
        run("sr_help");
        Check(contains("sr_reloadadmins - Reload administrators and groups"), "real bundled command help");
        install_script("player_actions", "player_actions");
        run("sr plugins load player_actions");
        KeelPlayerAction last{};
        run("sr_slap [U:1:123] 10");
        Check(actions(&last) == 1 && last.kind == KEELS2_PLAYER_ACTION_IMPULSE && last.damage == 10
            && std::fabs(last.impulse[0]) == 200 && std::fabs(last.impulse[1]) == 200 && last.impulse[2] == 300,
            "SourcePawn slap reaches actual Keel action service with damage and impulse");
        run("sr_slay [U:1:123]");
        Check(actions(&last) == 2 && last.kind == KEELS2_PLAYER_ACTION_KILL, "SourcePawn slay reaches actual Keel kill service");
        for (unsigned mutation : {1u, 2u, 3u}) {
            action_state(KEEL_RESULT_OK, mutation, true);
            const auto begin = std::string(messages()).size();
            run("sr_slap [U:1:123]");
            Check(actions(nullptr) == 2 && std::string(messages()).substr(begin).find("no longer available") != std::string::npos,
                "changed pawn, connection or alive state during entity acquisition prevents action");
        }
        action_state(KEEL_RESULT_ENGINE_FAILURE, 0, true);
        run("sr_slap [U:1:123]");
        Check(actions(nullptr) == 2 && contains("Player action failed"), "native action failure is not reported as success");
        action_state(KEEL_RESULT_OK, 0, false);
        run("sr_slay [U:1:123]");
        Check(actions(nullptr) == 2 && contains("Target is not alive."), "dead player never reaches action service");
        action_state(KEEL_RESULT_OK, 0, true);
        run("sr_slap [U:1:123] 1000");
        Check(actions(&last) == 3 && last.damage == 1000, "native resources release after refused actions and allow recovery");
        run("sr plugins unload player_actions");
        install_script("moderation", "moderation");
        run("sr plugins load moderation");
        kick_state(true, false, false);
        run("sr_kick [U:1:123] Native reason");
        Check(kicks() == 1 && std::string(kick_reason()) == "Native reason", "public kick reaches typed engine disconnect with full reason");
        kick_state(true, false, true);
        run("sr_kick [U:1:123]");
        Check(kicks() == 1 && contains("Player is no longer available."), "connection changed while acquiring engine interface cannot be kicked");
        kick_state(false, false, false);
        run("sr_kick [U:1:123]");
        Check(kicks() == 1 && contains("Player disconnect is unavailable"), "missing engine interface is a useful failed action");
        kick_state(true, true, false);
        const auto self_chat = std::string(chat()).size();
        Check(command("sr_kick @me", 3), "self kick command through native caller bridge");
        Check(kicks() == 2 && std::string(kick_reason()) == "Kicked Module fixture player" && std::string(chat()).size() == self_chat,
            "immediate native self-disconnect provides one disconnect result without a duplicate chat reply");
        kick_state(true, false, false); reconnect();
        run("sr_ban [U:1:999] 1 Native persisted reason");
        Check(contains("Banned 76561197960266727 for 1 minute") && kicks() == 2, "offline account ban persists through actual public runtime");
        run("sr_unban STEAM_0:1:499");
        Check(contains("Removed the ban for 76561197960266727"), "stored ban removed using another identity format");
        run("sr plugins unload moderation");
        install_script("communications", "communications");
        run("sr plugins load communications");
        run("sr_mute [U:1:123]");
        Check(!read_listening(3, 3) && listening_calls() > 0, "public mute reaches actual typed engine interface");
        Check(write_listening(3, 3, true) && !read_listening(3, 3), "actual KeelHook filters later engine listening request");
        run("sr_unmute [U:1:123]");
        Check(read_listening(3, 3), "unmute restores the latest intercepted request");
        run("sr_gag [U:1:123]");
        Check(!say("ordinary gagged chat", 3), "engine ConCommand route suppresses ordinary gagged chat");
        run("sr_ungag [U:1:123]");
        Check(say("ordinary visible chat", 3), "engine ConCommand route allows ungagged chat");
        run("sr_mute [U:1:123]");
        fail_listening(true);
        run("sr plugins unload communications");
        Check(contains("native resource release failed; retained for retry") && !read_listening(3, 3),
            "failed typed engine restoration retains script cleanup");
        fail_listening(false);
        run("sr plugins unload communications");
        Check(read_listening(3, 3), "script unload retry restores typed engine state");
        if (!packaged) {
        install_script("variables", "convars");
        run("sr plugins load variables");
        Check(cvars() == 7, "scripts create typed ConVars through the actual KeelS2 C service");
        run("sr_variables write");
        run("sr_variables mismatch");
        Check(contains("typed writes and bounds passed") && contains("type mismatch rejected"), "native bridge preserves integer, float and string types");
        run("sr plugins pause variables");
        Check(set_cvar("sr_test_text", "native operator edit") && set_cvar("sr_test_fraction", "0.75"), "real engine adapter changes paused script settings");
        run("sr plugins reload variables");
        const auto variable_info = std::string(messages()).size();
        run("sr plugins cvars variables");
        Check(cvars() == 7 && std::string(messages()).substr(variable_info).find("sr_test_text = \"native operator edit\" | variables | paused") != std::string::npos,
              "actual service keeps one definition and current value during paused reload");
        run("sr plugins resume variables");
        run("sr_variables");
        Check(contains("native operator edit"), "resumed script reads the current ConVar value");
        install_script("variables", "convars_changed");
        run("sr plugins reload variables");
        Check(contains("definition changed; use a new name or restart the server") && cvars() == 7,
              "changed script definition fails without disturbing existing engine values");
        run("sr plugins unload variables");
        Check(cvars() == 4, "script unload releases typed ConVars through actual host");
        Copy(std::filesystem::path(argv[7]).parent_path() / "entity.smx", script / "plugins/entity/entity.smx");
        {
            std::ofstream metadata(script / "plugins/entity/plugin.json");
            metadata << R"({"schema":1,"id":"entity","name":"Entity fixture","author":"tests","version":"1.0.0","api":2,"entry":"entity.smx","enabled":false,"dependencies":[]})";
        }
        run("sr plugins load entity");
        Check(command("sr_health", 3) && entity_reads() == 1 && std::string(chat()).find("entity health 73") != std::string::npos,
              "actual player, typed entity/schema service and SourcePawn reference output");
        entity_error(5);
        Check(command("sr_health", 3) && entity_reads() == 1 && std::string(chat()).find("KeelResult 5") != std::string::npos,
              "transient entity error is preserved through native/script bridge");
        entity_error(0);
        Check(command("sr_health", 3) && entity_reads() == 2, "entity read recovers after transient error");
        run("sr plugins unload entity");
        }
        install_script("greeting", "greeting");
        run("sr plugins load greeting");
        run("sr_greeting");
        Check(contains("Welcome to the server!") && set_cvar("sr_greeting_message", "Welcome back!"), "public greeting example uses real ConVars");
        run("sr plugins reload greeting");
        run("sr_greeting");
        Check(contains("Welcome back!"), "public example keeps the administrator's edited message");
        run("sr plugins unload greeting");
        Check(cvars() == 4, "public example cleans up without explicit script stop handler");
        std::ofstream(script / "configs/allowed_maps.txt") << "\n";
        std::ofstream(script / "configs/map_menu.txt") << "de_dust2\nde_mirage\n";
        install_script("server", "server");
        run("sr plugins load server");
        run("sr_map de_missing"); frame();
        Check(map_changes() == 0, "actual typed engine rejects an unavailable map");
        kick_state(false, false, false);
        auto begin_server = std::string(messages()).size(); run("sr_map de_dust2"); frame();
        Check(map_changes() == 0 && std::string(messages()).substr(begin_server).find("unavailable on this game host") != std::string::npos,
            "missing engine fails before map request acceptance");
        kick_state(true, false, false);
        Check(command("sr_map de_dust2", 3), "client requests map through actual module");
        Check(map_changes() == 0 && std::string(chat()).find("Requested a map change to de_dust2") != std::string::npos,
            "issuer confirmation precedes next-frame ChangeLevel");
        frame(); Check(map_changes() == 1 && std::string(changed_map()) == "de_dust2", "typed ChangeLevel receives checked map and null landmark");
        restart_variable(0, false); begin_server = std::string(messages()).size(); run("sr_restart");
        Check(std::string(messages()).substr(begin_server).find("Round restart is unavailable") != std::string::npos,
            "missing engine restart ConVar fails without constructing a replacement");
        restart_variable(4, false); begin_server = std::string(messages()).size(); run("sr_restart 5");
        Check(std::string(messages()).substr(begin_server).find("Round restart") != std::string::npos && cvar_equals("mp_restartgame", "0"),
            "wrong-type restart variable is unchanged");
        restart_variable(2, false); run("sr_restart");
        Check(cvar_equals("mp_restartgame", "1"), "native restart writes typed engine mp_restartgame");
        run("sr_restart 60"); Check(cvar_equals("mp_restartgame", "60"), "restart accepts upper bound");
        run("sr_restart 0"); run("sr_restart 61"); Check(cvar_equals("mp_restartgame", "60"), "invalid delays never reach engine");
        restart_variable(2, true); begin_server = std::string(messages()).size(); run("sr_restart 5");
        Check(cvar_equals("mp_restartgame", "60") && std::string(messages()).substr(begin_server).find("request failed") != std::string::npos,
            "native restart surfaces rejected engine write");
        restart_variable(2, false);
        run("sr plugins unload server");
        const auto audience = adapter.Get<void (*)(bool)>("SrFixtureActivityAudience");
        const auto player_chat = adapter.Get<const char* (*)(int)>("SrFixturePlayerChat");
        audience(true);
        install_script("activity", "activity");
        run("sr plugins load activity");
        Check(set_cvar("sr_show_activity", "13"), "set default activity mask in actual engine");
        auto admin_chat = std::string(player_chat(3));
        auto ordinary_chat = std::string(player_chat(4));
        run("sr_wave");
        Check(std::string(player_chat(3)).substr(admin_chat.size()) == "[S2R] Server waved.\n" &&
            std::string(player_chat(4)).substr(ordinary_chat.size()) == "[S2R] ADMIN: Waved.\n",
            "actual player enumeration, permission audience and private chat formatting");
        admin_chat = player_chat(3); ordinary_chat = player_chat(4);
        Check(command("sr_wave", 3), "execute activity example as player");
        Check(std::string(player_chat(3)).substr(admin_chat.size()) == "[S2R] Waved.\n" &&
            std::string(player_chat(4)).substr(ordinary_chat.size()) == "[S2R] ADMIN: Waved.\n",
            "actual issuer receives exactly one private confirmation");
        Check(set_cvar("sr_show_activity", "0"), "disable activity announcements in actual engine");
        admin_chat = player_chat(3); ordinary_chat = player_chat(4);
        Check(command("sr_wave", 3) && std::string(player_chat(3)).substr(admin_chat.size()) == "[S2R] Waved.\n" &&
            std::string(player_chat(4)) == ordinary_chat, "disabled activity retains only issuer confirmation");
        run("sr plugins unload activity");
        audience(false);
        Check(set_cvar("sr_show_activity", "13"), "restore default after activity example");
        const auto baseline = count();
        run("sr_hello");
        Check(contains("SourcePawn and the C++ extension returned 42."), "real extension callback result");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        frame();
        Check(contains("The delayed SourcePawn callback ran."), "real timer through host frame");
        auto occurrences = [](const std::string& text, const std::string& part) {
            unsigned count = 0;
            for (std::size_t pos = 0; (pos = text.find(part, pos)) != std::string::npos; pos += part.size()) ++count;
            return count;
        };
        auto answers = [&] { return occurrences(chat(), "SourcePawn and the C++ extension returned 42."); };
        if (argc >= 12) {
            input_state(KEELS2_BUTTON_USE, 1, KEEL_RESULT_OK);
            Check(command("sr_hello", 3), "client opens actual module menu");
            Check(std::string(menu_text()).find("Forward/Back: move") != std::string::npos, "module renders action hints through native serialization without an advertisement");
            const auto selected = [&] { return occurrences(chat(), "The menu selection ran in SourcePawn."); };
            const auto before_selection = selected();
            frame(); frame();
            Check(selected() == before_selection && *menu_text(), "native module baselines held input at opening");
            input_state(0, 1, KEEL_RESULT_OK); frame();
            input_state(KEELS2_BUTTON_USE, 1, KEEL_RESULT_OK); frame();
            Check(selected() == before_selection + 1 && !*menu_text(), "native module frame reads Keel input and invokes actual script menu once");
            frame(); Check(selected() == before_selection + 1, "native module does not repeat held selection");
            input_state(0, 1, KEEL_RESULT_OK);
            Check(command("sr_hello", 3), "reopen native module menu");
            input_state(KEELS2_BUTTON_USE, 2, KEEL_RESULT_OK); frame(); frame();
            Check(selected() == before_selection + 1 && *menu_text(), "native module resets context baseline");
            const auto input_errors = [&] { return occurrences(messages(), "menu input unavailable for slot 3 (KeelResult "); };
            const auto before_errors = input_errors();
            input_state(0, 2, KEEL_RESULT_NOT_READY); frame(); frame(); frame();
            Check(input_errors() == before_errors + 1, "persistent input failure logs once without per-frame spam");
            input_state(KEELS2_BUTTON_USE, 2, KEEL_RESULT_OK); frame();
            input_state(0, 2, KEEL_RESULT_NOT_READY); frame(); frame();
            Check(input_errors() == before_errors + 2, "new input failure after recovery is reported");
            input_state(KEELS2_BUTTON_USE, 2, KEEL_RESULT_OK); frame();
            Check(selected() == before_selection + 1, "native module resets after adapter read failure");
            input_state(0, 2, KEEL_RESULT_OK); frame();
            input_state(KEELS2_BUTTON_RELOAD, 2, KEEL_RESULT_OK); frame();
            Check(!*menu_text() && selected() == before_selection + 1, "native module closes through reload without selection");
            input_state(0, 2, KEEL_RESULT_OK);
            std::cout << "native input service -> module frame -> menu packet -> SourcePawn callback passed\n";
        }
        run("sr plugins pause hello");
        const auto paused_start = std::string(messages()).size();
        run("sr_hello");
        Check(std::string(messages()).substr(paused_start).find("SourcePawn and the C++ extension returned 42.") == std::string::npos,
              "real management pause stops script command callbacks");
        Check(command("sr plugins resume hello", 3), "fixture delivers unauthorized resume invocation");
        run("sr plugins refresh");
        const auto paused_info = std::string(messages()).size();
        run("sr plugins info hello");
        Check(std::string(messages()).substr(paused_info).find("hello | paused") != std::string::npos,
              "client cannot resume plugin and refresh preserves paused state");
        run("sr plugins resume hello.smx");
        const auto chat_answers = answers();
        Check(say("!hello", 3) && answers() == chat_answers + 1, "public chat executes once and remains publishable");
        Check(!say("/hello", 3) && answers() == chat_answers + 2, "silent chat executes once before publication is suppressed");
        Check(!say("/hello \"unterminated", 3) && answers() == chat_answers + 2, "malformed known silent command is suppressed without execution");
        std::ofstream(script / "configs/admins.cfg") << R"("Admins" {})";
        run("sr_reloadadmins");
        Check(!say("/hello", 3) && answers() == chat_answers + 2 && occurrences(chat(), "You do not have access to this command.") == 1,
              "denied silent command cannot publish or execute");
        Check(!say("/slpa @me", 3) && answers() == chat_answers + 2 && occurrences(chat(), "Unknown command \"slpa\".") == 1,
              "unknown silent command is suppressed and replied to privately");
        std::ofstream(script / "configs/admins.cfg") << permissions;
        run("sr_reloadadmins");
        const auto route_answers = answers();
        Check(!chat_command("say_team", "/hello", 3) && answers() == route_answers + 1,
              "team chat uses the engine dispatch hook and suppresses silent commands");
        Check(!chat_command("SAY", "/hello", 3) && answers() == route_answers + 2,
              "case-insensitive engine chat command executes once");
        const auto passthrough_chat = std::string(chat());
        Check(chat_command("say", "/hello", -1) && chat_command("echo", "/hello", 3) &&
              std::string(chat()) == passthrough_chat && answers() == route_answers + 2,
              "server-origin chat and unrelated engine commands are not script invocations");
        const auto custom_answers = answers();
        Check(set_cvar("sr_chat_public_trigger", "!!"), "direct engine ConVar update");
        run("sr config sr_chat_silent_trigger ??");
        Check(say("!!hello", 3) && answers() == custom_answers + 1, "configured public trigger remains visible");
        Check(!say("??hello", 3) && answers() == custom_answers + 2, "configured silent trigger dispatches privately");
        Check(!say("??slpa", 3) && occurrences(chat(), "Unknown command \"slpa\".") == 2,
              "custom silent trigger hides unknown commands");
        run("sr config sr_chat_public_trigger ?");
        Check(!say("??hello", 3) && answers() == custom_answers + 3, "longer silent prefix takes precedence over public prefix");
        Check(set_cvar("sr_chat_public_trigger", "??"), "fixture submits an invalid equal trigger through the engine");
        Check(say("?hello", 3) && answers() == custom_answers + 4, "invalid ConVar change restores previous effective trigger");
        run("sr config sr_chat_public_trigger \"\"");
        Check(say("?hello", 3) && answers() == custom_answers + 4, "empty public trigger disables shortcuts");
        run("sr config sr_chat_public_trigger !");
        run("sr config sr_chat_silent_trigger /");
        std::ifstream preserved_input(core_cfg);
        const std::string preserved((std::istreambuf_iterator<char>(preserved_input)), std::istreambuf_iterator<char>());
        Check(preserved == core_values, "runtime settings do not overwrite startup configuration");
        const auto before_reconnect = std::string(chat());
        reconnect();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        frame();
        Check(std::string(chat()) == before_reconnect, "delayed callbacks cannot follow a recycled player slot");
        const auto provider_refusal = std::string(messages()).size();
        run("keel plugins unload 2");
        Check(std::string(messages()).substr(provider_refusal).find(
            "plugin unload is blocked by dependent Source2Root: Source2Root Example") != std::string::npos &&
            count() == baseline, "provider lease blocks unload and retains commands");
        run("sr_hello");
        Check(std::string(messages()).substr(provider_refusal).find(
            "SourcePawn and the C++ extension returned 42.") != std::string::npos,
            "retained provider still executes its native through SourcePawn");
        run("sr plugins unload hello");
        run("keel plugins unload 2");
        run("sr plugins reload hello");
        Check(contains("missing native: SR_ExampleAdd"), "missing provider rejected");
        for (unsigned i = 0; i < 10; ++i) {
            run("keel plugins load sr_example" + extension);
            run("sr plugins reload hello");
            Check(count() == baseline, "registration count after native/script reload");
            run("sr_hello");
            run("sr plugins unload hello");
            run("keel plugins unload 2");
        }
        if (!packaged) {
        const auto example_path = std::filesystem::path(argv[5]);
        const auto test_example = example_path.parent_path() / (example_path.stem().string() + "_test" + extension);
        Copy(test_example, plugins / ("sr_example" + extension));
        run("keel plugins load sr_example" + extension);
        run("sr plugins reload hello");
        std::filesystem::path loaded_test;
        for (const auto& file : std::filesystem::recursive_directory_iterator(plugins / ".runtime"))
            if (file.path().filename() == "sr_example" + extension) loaded_test = file.path();
        Check(!loaded_test.empty(), "actual loader retained extension image");
        Library extension_control(loaded_test);
        auto arm = extension_control.Get<void (*)(void (*)())>("SrFixtureDuringNative");
        during_command = command;
        arm(&AttemptRetire);
        run("sr_hello");
        arm(nullptr);
        extension_control.Close();
        Check(during_ok && contains("callback active; retained for unload retry"), "active real native call cannot destroy its script VM");
        run("sr plugins info hello");
        Check(contains("hello | retiring"), "active unload stops new script dispatch");
        run("sr plugins unload hello");
        run("keel plugins unload 2");
        }
        run("sr plugins load greeting");
        Check(cvars() == 6, "script settings active before native core unload");
        run("sr plugins load communications");
        run("sr_mute [U:1:123]");
        fail_listening(true);
        run("keel plugins pause 1");
        Check(contains("plugin pause preparation is incomplete; plugin remains running: Source2Root"),
            "native pause refuses failed listening restoration");
        fail_listening(false);
        Check(write_listening(3, 3, true) && !read_listening(3, 3), "refused pause restores active voice filtering");
        run("keel plugins pause 1");
        Check(read_listening(3, 3), "successful native pause restores the original listening value");
        Check(write_listening(3, 3, false), "external listening request while the native platform is paused");
        run("keel plugins resume 1");
        run("sr_unmute [U:1:123]");
        Check(!read_listening(3, 3), "native pause must not lose external voice intent before later restoration");
        Check(write_listening(3, 3, true), "reset external listening baseline");
        run("sr_mute [U:1:123]");
        fail_listening(true);
        run("keel plugins unload 1");
        Check(contains("plugin unload preparation is incomplete; plugin retained: Source2Root") && !read_listening(3, 3),
            "native unload retains the platform when voice restoration fails");
        fail_listening(false);
        run("keel plugins unload 1");
        Check(read_listening(3, 3), "native unload retry restores voice during unload preparation");
        const auto unloaded_chat = std::string(chat());
        Check(say("/unknown", 3) && std::string(chat()) == unloaded_chat,
              "native unload removes the engine chat hook");
        Check(count() == 1 && cvars() == 0, "module cleanup removes its commands and ConVars");
        std::ofstream(core_cfg) << "sr_show_activity 9\nsr_chat_public_trigger +\nsr_chat_silent_trigger ##\n";
        Copy(argv[5], plugins / ("sr_example" + extension));
        run("keel plugins load source2root" + extension);
        Check(cvars() == 4, "changed cfg values do not conflict with persistent KeelS2 ConVar definitions");
        const auto reloaded_config = std::string(messages()).size();
        run("sr config");
        Check(std::string(messages()).substr(reloaded_config).find("sr_show_activity = \"9\"") != std::string::npos,
              "native core reload reads changed startup settings");
        run("keel plugins load sr_example" + extension);
        run("sr plugins load hello/hello.smx");
        run("sr plugins load admin");
        Check(count() == baseline, "live scripts restored before global host shutdown");
        const auto reloaded_answers = answers();
        Check(say("+hello", 3) && answers() == reloaded_answers + 1 && !say("##unknown", 3),
              "reloaded core uses new public and silent triggers");
        run("sr plugins load greeting");
        if (argc >= 15) {
            Copy(argv[13], script / "plugins/roll/roll.smx");
            Copy(argv[14], script / "plugins/roll/plugin.json");
            run("sr plugins load roll");
            Check(contains("missing native: RandomInt") && !command("sr_roll", -1), "optional script needs its native provider");
            Copy(argv[12], plugins / ("source2root_random" + extension));
            run("keel plugins load source2root_random" + extension);
            run("sr plugins retry roll");
            const auto before_roll = std::string(messages()).size();
            run("sr_roll 1");
            Check(std::string(messages()).substr(before_roll).find("Rolled 1 of 1.") != std::string::npos,
                "public script calls the typed RandomInt extension through the VM");
            const auto before_usage = std::string(messages()).size();
            for (const auto* invalid : {"0", "1000001", "x", "2 3"}) run(std::string("sr_roll ") + invalid);
            const auto usage = std::string(messages()).substr(before_usage);
            Check(usage.find("Usage: sr_roll [sides: 1-1000000]") != std::string::npos && usage.find("Rolled ") == std::string::npos,
                "invalid roll arguments return usage without rolling");
            const auto before_chat = std::string(chat()).size();
            Check(command("sr_roll 1", 3) && std::string(chat()).substr(before_chat).find("Rolled 1 of 1.") != std::string::npos,
                "public extension command replies to its player caller");
            install_script("random_native", "random_native");
            run("sr plugins load random_native");
            Check(contains("Random native bounds passed."), "real extension covers full int32 range and deterministic endpoints");
            run("sr_random_error");
            Check(contains("Minimum must not exceed maximum.") && contains("random_native.sp"),
                "native exception becomes a script diagnostic with source location");
            run("sr plugins unload random_native");
            const auto random_refusal = std::string(messages()).size();
            run("keel plugins unload \"Source2Root Random\"");
            run("sr_roll 1");
            const auto retained_random = std::string(messages()).substr(random_refusal);
            Check(retained_random.find("plugin unload is blocked by dependent Source2Root: Source2Root Random") != std::string::npos &&
                retained_random.find("Rolled 1 of 1.") != std::string::npos,
                "script lease keeps the native extension callable after refused unload");
            run("sr plugins unload roll");
            run("keel plugins unload \"Source2Root Random\"");
            run("sr plugins load roll");
            Check(!command("sr_roll", -1), "extension unload removes native registration");
            run("keel plugins load source2root_random" + extension);
            run("sr plugins retry roll");
            run("sr_roll 1");
        }
        if (argc == 16) {
            const auto packaged_extensions = std::filesystem::absolute(argv[15]);
            const auto libraries = packaged_extensions / "lib";
            if (std::filesystem::exists(libraries)) {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(libraries)) {
                    if (entry.is_regular_file())
                        Copy(entry.path(), plugins / "lib" / std::filesystem::relative(entry.path(), libraries));
                }
            }
            for (const auto& entry : std::filesystem::directory_iterator(packaged_extensions)) {
                if (!entry.is_regular_file() || entry.path().extension() != extension ||
                    entry.path().filename() == "source2root_random" + extension) continue;
                Copy(entry.path(), plugins / entry.path().filename());
                const auto before_load = std::string(messages()).size();
                run("keel plugins load " + entry.path().filename().string());
                Check(std::string(messages()).substr(before_load).find("plugin loaded:") != std::string::npos,
                    "packaged optional extension loads through host shadow staging");
            }
        }
        Check(!stop(), "first global stop retains modules while platform releases script provider leases");
        Check(stop(), "second global stop completes after every plugin can prepare");
        std::cout << messages() << "real KeelS2 module, SourcePawn, native lease and reload tests passed\n";
        if (argc >= 12) Check(network_stop(), "native menu message allocations released");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        std::quick_exit(1);
    }
}
