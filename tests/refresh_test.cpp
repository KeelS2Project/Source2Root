#include "foundation.h"

#include <json.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>

namespace {
void Require(bool value, const char* message) {
    if (!value) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}

class Host final : public sr::GameHost {
public:
    std::vector<std::string> logs;
    std::set<std::string> commands;
    bool fail_remove = false;
    KeelResult Lookup(int, sr::Player&) override { return KEEL_RESULT_NOT_FOUND; }
    KeelResult Reply(const sr::Player*, const std::string& text) override { Log(text); return KEEL_RESULT_OK; }
    void Log(const std::string& text) override { logs.push_back(text); }
    KeelResult RegisterCommand(const std::string& name) override {
        return commands.insert(name).second ? KEEL_RESULT_OK : KEEL_RESULT_ALREADY_EXISTS;
    }
    KeelResult RemoveCommand(const std::string& name) override {
        if (fail_remove) return KEEL_RESULT_ENGINE_FAILURE;
        return commands.erase(name) ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND;
    }
    KeelResult ListenEvent(const std::string&) override { return KEEL_RESULT_UNSUPPORTED; }
    KeelResult RemoveEvent(const std::string&) override { return KEEL_RESULT_UNSUPPORTED; }
    KeelResult RenderMenu(const sr::Player&, const std::string&) override { return KEEL_RESULT_UNSUPPORTED; }
    KeelResult AcquireProvider(const std::string&, unsigned) override { return KEEL_RESULT_UNSUPPORTED; }
    KeelResult ReleaseProvider(const std::string&, unsigned) override { return KEEL_RESULT_UNSUPPORTED; }
};

void Write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary);
    output << text;
    Require(static_cast<bool>(output), "write fixture");
}
void Copy(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing);
}
}

int main(int argc, char** argv) {
    Require(argc == 6, "refresh_test runtime empty.smx commands.smx timer.smx fixture-root");
    const std::filesystem::path root(argv[5]);
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "configs");
    Write(root / "configs/admin_groups.cfg", R"("Groups" { "fixture" { "immunity" "10" "permissions" { "demo.hello" "1" "demo.status" "1" } } })");
    Write(root / "configs/admins.cfg", R"("Admins" {})");
    auto manifest_path = [&](const std::string& id) { return root / "plugins" / id / "plugin.json"; };
    auto bytecode_path = [&](const std::string& id) { return root / "plugins" / id / (id + ".smx"); };
    auto make = [&](const std::string& id, const std::filesystem::path& code, bool enabled = true) {
        std::filesystem::create_directories(root / "plugins" / id);
        nlohmann::json metadata{{"schema", 1}, {"api", 2}, {"id", id}, {"name", id}, {"author", "tests"},
            {"version", "1.0.0"}, {"entry", id + ".smx"}, {"enabled", enabled}, {"dependencies", nlohmann::json::array()}};
        Write(manifest_path(id), metadata.dump());
        Copy(code, bytecode_path(id));
        return metadata;
    };
    Host host;
    sr::Foundation app(host, argv[1], root);
    auto status = [&](const std::string& id) {
        for (const auto& item : app.Status()) if (item.id == id) return item;
        std::abort();
    };
    const auto now = sr::Foundation::Clock::now();
    app.Tick(now);
    auto timer = make("timer", argv[4]);
    Require(app.Refresh() && status("timer").state == sr::PluginState::Running, "refresh discovers enabled script");
    app.Tick(now + std::chrono::milliseconds(100));
    std::filesystem::last_write_time(manifest_path("timer"), std::filesystem::file_time_type::clock::now() + std::chrono::hours(1));
    std::filesystem::last_write_time(bytecode_path("timer"), std::filesystem::file_time_type::clock::now() + std::chrono::hours(1));
    Require(app.Refresh(), "timestamp changes alone leave loaded bytes unchanged");
    const auto before_timer = host.logs.size();
    app.Tick(now + std::chrono::milliseconds(250));
    Require(host.logs.size() == before_timer + 1 && host.logs.back() == "timer: startup timer",
            "unchanged refresh preserves original VM timer deadline");
    const auto manifest_time = std::filesystem::last_write_time(manifest_path("timer"));
    timer["version"] = "1.0.1";
    Write(manifest_path("timer"), timer.dump());
    std::filesystem::last_write_time(manifest_path("timer"), manifest_time);
    Require(app.Pause("timer") && app.Refresh() && status("timer").state == sr::PluginState::Paused,
            "changed manifest reloads even with identical file size and timestamp, preserving pause");
    app.Tick(now + std::chrono::seconds(2));
    const auto paused_logs = host.logs.size();
    Require(app.Resume("timer"), "resume refreshed VM");
    app.Tick(now + std::chrono::milliseconds(2250));
    Require(host.logs.size() == paused_logs + 1 && host.logs.back() == "timer: startup timer",
            "paused replacement starts its own timer after resume");
    std::filesystem::remove(bytecode_path("timer"));
    Require(!app.Refresh() && status("timer").state == sr::PluginState::Running,
            "missing bytecode is reported without unloading running VM");
    Write(bytecode_path("timer"), "invalid SMX");
    Require(!app.Refresh() && status("timer").state == sr::PluginState::Running,
            "invalid replacement keeps old VM");
    Copy(argv[4], bytecode_path("timer"));
    Require(app.Refresh(), "restoring original bytes permits unchanged refresh");
    auto disabled = make("disabled", argv[2], false);
    Require(app.Refresh() && status("disabled").state == sr::PluginState::Disabled && !status("disabled").handles,
            "discovered disabled record owns no runtime resources");
    disabled["enabled"] = true;
    Write(manifest_path("disabled"), disabled.dump());
    Copy(argv[3], bytecode_path("disabled"));
    Require(app.Refresh() && status("disabled").state == sr::PluginState::Disabled && host.commands.empty(),
            "refresh cannot re-enable disabled plugin when its files change");
    Require(app.Load(manifest_path("disabled")) && host.commands.size() == 3, "explicit load enables disabled bytecode");
    Copy(argv[4], bytecode_path("disabled"));
    host.fail_remove = true;
    Require(!app.Refresh() && status("disabled").state == sr::PluginState::Retiring,
            "changed bytecode uses recoverable cleanup instead of bypassing removal failure");
    Require(!app.Refresh() && host.commands.size() == 3, "refresh reports pending lifecycle cleanup without destroying ownership");
    host.fail_remove = false;
    Require(app.Reload("disabled") && host.commands.empty(), "explicit reload retry completes retained replacement");
    Require(app.Unload("disabled") && app.Refresh() && status("disabled").state == sr::PluginState::Disabled,
            "explicit unload remains disabled on later refresh");
    auto provider = make("z_provider", argv[2]);
    Require(app.Refresh(), "initial dependency provider load");
    auto consumer = make("a_consumer", argv[2]);
    consumer["dependencies"] = nlohmann::json::array({{{"id", "z_provider"}, {"minimum_version", "2.0.0"}}});
    Write(manifest_path("a_consumer"), consumer.dump());
    provider["version"] = "2.0.0";
    Write(manifest_path("z_provider"), provider.dump());
    Require(app.Refresh() && status("a_consumer").state == sr::PluginState::Running,
            "new consumer waits for changed provider regardless of directory order");
    make("b_provider", argv[2]);
    consumer["dependencies"].push_back({{"id", "b_provider"}, {"minimum_version", "1.0.0"}});
    Write(manifest_path("a_consumer"), consumer.dump());
    Require(app.Pause("a_consumer") && app.Refresh() && status("a_consumer").state == sr::PluginState::Paused,
            "changed paused consumer waits for newly discovered provider");
    auto cycle_a = make("cycle_a", argv[2]);
    auto cycle_b = make("cycle_b", argv[2]);
    cycle_a["dependencies"] = nlohmann::json::array({{{"id", "cycle_b"}, {"minimum_version", "1.0.0"}}});
    cycle_b["dependencies"] = nlohmann::json::array({{{"id", "cycle_a"}, {"minimum_version", "1.0.0"}}});
    Write(manifest_path("cycle_a"), cycle_a.dump());
    Write(manifest_path("cycle_b"), cycle_b.dump());
    Require(!app.Refresh() && status("cycle_a").state == sr::PluginState::Failed &&
            status("cycle_b").state == sr::PluginState::Failed && !status("cycle_a").handles,
            "dependency cycle produces known failed records without VMs or false resource counts");
    auto broken = make("broken", argv[2]);
    Write(manifest_path("broken"), "{");
    Require(!app.Refresh() && status("broken").state == sr::PluginState::Failed && !status("broken").handles,
            "invalid new manifest is retained in operator inventory");
    Write(manifest_path("broken"), broken.dump());
    Require(app.Refresh() && status("broken").state == sr::PluginState::Failed,
            "refresh leaves failed records for explicit retry");
    Require(app.Retry("broken") && status("broken").state == sr::PluginState::Running, "corrected failed manifest can be retried");
    Require(app.UnloadAll(), "refresh fixture unload respects paused dependencies");
    Require(host.commands.empty(), "no registrations remain after refresh cleanup");
    std::cout << "real bytecode refresh, unchanged state, paused recovery, disabled authority and discovery dependencies passed\n";
}
