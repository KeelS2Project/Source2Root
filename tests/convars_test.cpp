#include "foundation.h"

#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>

static void Require(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}

class Host final : public sr::GameHost {
public:
    struct Variable { sr::ConVarDefinition definition; sr::ConVarValue value; };
    std::map<std::uint64_t, Variable> variables;
    std::map<std::string, sr::ConVarDefinition> definitions;
    std::set<std::string> commands;
    std::vector<std::string> replies;
    std::string log, busy_name;
    bool fail_commands = false, fail_create = false;
    std::uint64_t next = 1;
    std::function<void()> during_write;
    Variable& ByName(const std::string& name) {
        for (auto& [id, variable] : variables) if (variable.definition.name == name) return variable;
        throw std::runtime_error("missing test variable " + name);
    }
    KeelResult CreateConVar(const sr::ConVarDefinition& definition, std::uint64_t& handle) override {
        handle = 0;
        if (fail_create) return KEEL_RESULT_NOT_READY;
        for (const auto& [id, variable] : variables)
            if (variable.definition.name == definition.name) return KEEL_RESULT_ALREADY_EXISTS;
        const auto found = definitions.find(definition.name);
        if (found != definitions.end() && !(found->second == definition)) return KEEL_RESULT_INCOMPATIBLE;
        definitions.insert_or_assign(definition.name, definition);
        handle = next++;
        variables.emplace(handle, Variable{definition, definition.initial});
        return KEEL_RESULT_OK;
    }
    KeelResult ReadConVar(std::uint64_t handle, sr::ConVarValue& value) override {
        if (!variables.contains(handle)) return KEEL_RESULT_NOT_FOUND;
        value = variables.at(handle).value;
        return KEEL_RESULT_OK;
    }
    KeelResult WriteConVar(std::uint64_t handle, const sr::ConVarValue& value) override {
        if (!variables.contains(handle)) return KEEL_RESULT_NOT_FOUND;
        variables.at(handle).value = value;
        if (during_write) { auto callback = std::move(during_write); callback(); }
        return KEEL_RESULT_OK;
    }
    KeelResult ReleaseConVar(std::uint64_t handle) override {
        const auto found = variables.find(handle);
        if (found == variables.end()) return KEEL_RESULT_NOT_FOUND;
        if (found->second.definition.name == busy_name) return KEEL_RESULT_BUSY;
        variables.erase(found);
        return KEEL_RESULT_OK;
    }
    KeelResult Lookup(int, sr::Player&) override { return KEEL_RESULT_NOT_FOUND; }
    KeelResult Reply(const sr::Player*, const std::string& text) override { replies.push_back(text); return KEEL_RESULT_OK; }
    void Log(const std::string& text) override { log += text + '\n'; std::cout << text << '\n'; }
    KeelResult RegisterCommand(const std::string& name) override {
        return commands.insert(name).second ? KEEL_RESULT_OK : KEEL_RESULT_ALREADY_EXISTS;
    }
    KeelResult RemoveCommand(const std::string& name) override {
        return fail_commands ? KEEL_RESULT_BUSY : commands.erase(name) ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND;
    }
    KeelResult ListenEvent(const std::string&) override { return KEEL_RESULT_UNSUPPORTED; }
    KeelResult RemoveEvent(const std::string&) override { return KEEL_RESULT_UNSUPPORTED; }
    KeelResult RenderMenu(const sr::Player&, const std::string&) override { return KEEL_RESULT_UNSUPPORTED; }
    KeelResult AcquireProvider(const std::string&, unsigned) override { return KEEL_RESULT_UNSUPPORTED; }
    KeelResult ReleaseProvider(const std::string&, unsigned) override { return KEEL_RESULT_UNSUPPORTED; }
};

int main(int argc, char** argv) {
    Require(argc == 4, "convars_test runtime scripts fixture-root");
    const std::filesystem::path scripts(argv[2]), root(argv[3]);
    auto install = [&](const std::string& id, const std::string& bytecode) {
        const auto dir = root / "plugins" / id;
        std::filesystem::create_directories(dir);
        std::filesystem::copy_file(scripts / (bytecode + ".smx"), dir / "main.smx", std::filesystem::copy_options::overwrite_existing);
        std::ofstream(dir / "plugin.json") << "{\"schema\":1,\"id\":\"" << id <<
            "\",\"name\":\"ConVar fixture\",\"author\":\"tests\",\"version\":\"1.0.0\",\"api\":2,\"entry\":\"main.smx\",\"enabled\":true,\"dependencies\":[]}";
        return dir / "plugin.json";
    };
    const auto manifest = install("variables", "convars");
    Host host;
    sr::Foundation app(host, argv[1], root);
    auto state = [&](const std::string& id) {
        for (const auto& status : app.Status()) if (status.id == id) return status;
        throw std::runtime_error("missing status " + id);
    };
    auto run = [&](const char* text) { Require(app.Dispatch(sr::Origin::ServerConsole, -1, text), text); };
    Require(app.Load(manifest) && host.variables.size() == 3 && state("variables").convars == 3, "create typed, owned ConVars");
    Require(std::get<std::int32_t>(host.definitions.at("sr_test_unbounded_int").minimum) == std::numeric_limits<std::int32_t>::min() &&
        std::get<float>(host.definitions.at("sr_test_unbounded_float").maximum) == std::numeric_limits<float>::max(),
        "SourcePawn default bounds preserve full integer and finite float ranges");
    Require(std::get<std::int32_t>(host.ByName("sr_test_count").value) == 1, "initialization cannot mutate shared values");
    run("sr_variables");
    Require(host.replies.back() == "initial", "read string from actual bytecode");
    run("sr_variables write");
    Require(host.replies.back() == "typed writes and bounds passed" && std::get<std::string>(host.ByName("sr_test_text").value) == "changed",
        "typed access clamps numeric bounds and writes strings");
    run("sr_variables mismatch");
    Require(host.replies.back() == "type mismatch rejected" && std::get<std::int32_t>(host.ByName("sr_test_count").value) == 10,
        "wrong-type operations clear outputs and preserve engine value");
    Require(!app.Load(install("foreign", "convars")) && host.variables.size() == 3, "another script cannot adopt existing definitions");
    app.MapChanged();
    run("sr_variables");
    Require(host.replies.back() == "changed" && state("variables").handles == 3, "ConVar handles survive map transitions");
    Require(app.Pause("variables"), "pause owner");
    host.ByName("sr_test_text").value = std::string("edited while paused");
    host.log.clear();
    app.ManagePlugins({"cvars", "variables"});
    Require(host.log.find("sr_test_count = 10 | variables | paused") < host.log.find("sr_test_fraction = 0 | variables | paused") &&
        host.log.find("sr_test_text = \"edited while paused\" | variables | paused") != std::string::npos,
        "ordered inventory reports live values, ownership and pause state");
    Require(app.Reload("variables") && state("variables").state == sr::PluginState::Paused && host.variables.size() == 3,
        "paused reload shares the exact definition");
    Require(app.Resume("variables"), "resume owner");
    run("sr_variables");
    Require(host.replies.back() == "edited while paused", "reload keeps operator values");
    install("variables", "convars_changed");
    Require(!app.Reload("variables") && state("variables").state == sr::PluginState::Running && host.variables.size() == 3,
        "changed definitions cannot replace a running script");
    run("sr_variables");
    Require(host.replies.back() == "edited while paused", "failed staged replacement preserves old VM and value");
    install("variables", "convars");
    host.fail_commands = true;
    Require(!app.Unload("variables"), "command cleanup failure retains VM after ConVar release");
    host.fail_commands = false;
    Require(app.Unload("variables") && host.variables.empty(), "retry releases remaining command without stale ConVar access");
    Require(app.Load(manifest), "reload after complete unload");
    host.busy_name = "sr_test_count";
    Require(!app.Unload("variables") && state("variables").convars == 1 && host.variables.size() == 1,
        "busy ConVar retains physical ownership and retiring VM");
    Require(!app.Unload("variables") && host.variables.size() == 1, "repeated refusal never loses ownership");
    host.busy_name.clear();
    Require(app.Unload("variables") && host.variables.empty(), "retry closes retained ConVar after refusal clears");
    Require(app.Load(manifest), "load before active callback cleanup");
    host.during_write = [&] { Require(!app.Unload("variables"), "active callback prevents immediate destruction"); };
    run("sr_variables write");
    Require(state("variables").state == sr::PluginState::Retiring && host.variables.size() == 3, "active write retains VM and resources");
    Require(app.Unload("variables") && host.variables.empty(), "explicit retry after active write succeeds");
    Require(app.Load(manifest), "load for invalid-handle tests");
    run("sr_variables close");
    Require(host.replies.back() == "closed" && state("variables").convars == 2, "explicit close releases owned handle");
    auto replies = host.replies.size();
    run("sr_variables");
    Require(host.replies.size() == replies && !state("variables").error.empty(), "closed handle cannot read a reused resource");
    run("sr_variables nan");
    Require(host.replies.size() == replies && std::get<float>(host.ByName("sr_test_fraction").value) == 0.5f,
        "non-finite writes fail without modifying the ConVar");
    Require(app.Unload("variables"), "cleanup after native errors");
    host.fail_create = true;
    Require(!app.Load(manifest) && host.variables.empty() && state("variables").convars == 0, "failed backend registration leaves no owned resource");
    host.fail_create = false;
    Require(app.Retry("variables") && app.Unload("variables"), "backend recovery permits explicit retry");
    Require(!app.Load(install("partial", "convars_failed")) && host.variables.empty(), "failed initialization closes its ConVars");
    const auto greeting = install("greeting", "greeting");
    Require(app.Load(greeting), "compile and load public greeting example");
    run("sr_greeting");
    Require(host.replies.back() == "Welcome to the server!", "public example reads configured message");
    host.ByName("sr_greeting_message").value = std::string("operator message");
    Require(app.Reload("greeting"), "reload public example");
    run("sr_greeting");
    Require(host.replies.back() == "operator message", "public example preserves operator edits on reload");
    host.ByName("sr_greeting_enabled").value = std::int32_t{0};
    run("sr_greeting");
    Require(host.replies.back() == "The greeting command is disabled.", "public example observes integer setting");
    Require(app.Shutdown() && host.variables.empty() && host.commands.empty(), "shutdown releases all script resources");
    std::cout << "Script ConVar ownership, typed values, bounds, map lifetime, reload, refusal, retry and greeting example passed.\n";
}
