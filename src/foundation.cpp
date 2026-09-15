#include "foundation.h"

#include <json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <tuple>

namespace sr {
namespace {

class ManagementScope {
public:
    explicit ManagementScope(bool& busy) : busy_(busy), acquired_(!busy) {
        if (acquired_) busy_ = true;
    }
    ~ManagementScope() { if (acquired_) busy_ = false; }
    explicit operator bool() const { return acquired_; }
private:
    bool& busy_;
    bool acquired_;
};

std::array<unsigned, 3> Version(const std::string& text) {
    std::array<unsigned, 3> version{};
    const char* begin = text.data();
    const char* end = begin + text.size();
    for (unsigned i = 0; i < 3; ++i) {
        auto [next, error] = std::from_chars(begin, end, version[i]);
        if (error != std::errc{} || version[i] > 65535 ||
            (i == 2 ? next != end : next == end || *next != '.'))
            throw std::runtime_error("version must be major.minor.patch (0..65535 each)");
        begin = next + (i != 2);
    }
    return version;
}

}

Manifest Manifest::Read(const std::filesystem::path& path) {
    if (std::filesystem::is_symlink(path) || std::filesystem::is_symlink(path.parent_path()))
        throw std::runtime_error("manifest and plugin directory symlinks are not accepted");
    const auto size = std::filesystem::file_size(path);
    if (size > 16384) throw std::runtime_error("manifest exceeds 16 KiB");
    std::ifstream input(path, std::ios::binary);
    std::string source(static_cast<std::size_t>(size), '\0');
    if (!input.read(source.data(), static_cast<std::streamsize>(size))) throw std::runtime_error("cannot read complete manifest");
    if (input.peek() != std::char_traits<char>::eof()) throw std::runtime_error("manifest changed while reading");
    auto json = nlohmann::json::parse(source);
    if (json.at("schema") != 1 || json.at("api") != 2)
        throw std::runtime_error("manifest schema or required Source2Root API is incompatible");
    Manifest manifest;
    manifest.source = std::move(source);
    manifest.id = json.at("id").get<std::string>();
    manifest.name = json.at("name").get<std::string>();
    manifest.author = json.at("author").get<std::string>();
    manifest.version = json.at("version").get<std::string>();
    manifest.entry = json.at("entry").get<std::string>();
    manifest.enabled = json.at("enabled").get<bool>();
    if (!ValidIdentifier(manifest.id) || manifest.name.empty() || manifest.name.size() > 128 ||
        manifest.author.size() > 128 || manifest.entry.size() > 128 ||
        std::filesystem::path(manifest.entry).filename().string() != manifest.entry ||
        manifest.entry.find('\\') != std::string::npos || !manifest.entry.ends_with(".smx"))
        throw std::runtime_error("invalid plugin identity, metadata or entry basename");
    if (path.parent_path().filename().string() != manifest.id)
        throw std::runtime_error("plugin directory basename must equal its ID");
    Version(manifest.version);
    if (!json.at("dependencies").is_array() || json.at("dependencies").size() > 32)
        throw std::runtime_error("dependencies must be an array with at most 32 entries");
    for (const auto& dependency : json.at("dependencies")) {
        auto id = dependency.at("id").get<std::string>();
        auto minimum = dependency.at("minimum_version").get<std::string>();
        Version(minimum);
        if (!ValidIdentifier(id) || id == manifest.id || !manifest.dependencies.emplace(id, minimum).second)
            throw std::runtime_error("invalid, duplicate or self dependency");
    }
    return manifest;
}

Foundation::Foundation(GameHost& host, const std::filesystem::path& runtime_library,
                       std::filesystem::path root, CoreSettings settings)
    : host_(host), runtime_(runtime_library, [&](const auto& message) { host_.Log(message); }),
      root_(std::move(root)), bans_(root_ / "data/bans.json"), voice_(host_), thread_(std::this_thread::get_id()), settings_(std::move(settings)) {
    ReloadPermissions();
}

Foundation::~Foundation() {
    if (!Shutdown()) std::terminate();
}

void Foundation::Thread() const {
    if (std::this_thread::get_id() != thread_)
        throw std::runtime_error("Source2Root operation requires the server thread");
}

bool Foundation::Fail(std::string message) {
    error_ = std::move(message);
    host_.Log(error_);
    return false;
}

void Foundation::ReloadPermissions() {
    Thread();
    permissions_.Load(root_ / "configs");
}

bool Foundation::SetSetting(const std::string& name, const std::string& value) {
    Thread();
    try { settings_.Set(name, value); return true; }
    catch (const std::exception& error) { return Fail(error.what()); }
}

bool Foundation::SetSettings(const std::array<std::string, 4>& values) {
    Thread();
    try { settings_.SetAll(values); return true; }
    catch (const std::exception& error) { return Fail(error.what()); }
}

bool Foundation::CheckDependencies(const Manifest& manifest) {
    for (const auto& [id, slot] : scripts_) {
        if (id == manifest.id) continue;
        for (const auto* script : {slot.current.get(), slot.replacement.get()}) {
            if (!script || !script->vm) continue;
            const auto required = script->manifest.dependencies.find(manifest.id);
            if (required != script->manifest.dependencies.end() && Version(manifest.version) < Version(required->second))
                return Fail(manifest.id + ": replacement version is below " + id + " requirement " + required->second);
        }
    }
    for (const auto& [id, version] : manifest.dependencies) {
        const auto found = scripts_.find(id);
        if (found == scripts_.end() || !found->second.current ||
            found->second.current->state != PluginState::Running ||
            Version(found->second.current->manifest.version) < Version(version))
            return Fail(manifest.id + ": missing running dependency " + id + " >= " + version);
    }
    std::vector<std::string> pending;
    for (const auto& [id, version] : manifest.dependencies) pending.push_back(id);
    std::set<std::string> visited;
    while (!pending.empty()) {
        auto id = std::move(pending.back());
        pending.pop_back();
        if (id == manifest.id) return Fail(manifest.id + ": dependency cycle; replacement refused");
        if (!visited.insert(id).second) continue;
        const auto found = scripts_.find(id);
        if (found == scripts_.end()) continue;
        for (const auto* script : {found->second.current.get(), found->second.replacement.get()}) {
            if (!script || !script->vm) continue;
            for (const auto& [dependency, version] : script->manifest.dependencies) pending.push_back(dependency);
        }
    }
    return true;
}

std::string Foundation::Dependent(const std::string& id, bool running_only) const {
    for (const auto& [other_id, slot] : scripts_) {
        if (other_id == id) continue;
        for (const auto* script : {slot.current.get(), slot.replacement.get()}) {
            if (!script || !script->vm || !script->manifest.dependencies.contains(id)) continue;
            if (!running_only || script->state != PluginState::Paused)
                return other_id;
        }
    }
    return {};
}

std::unique_ptr<Foundation::Script> Foundation::Prepare(const std::filesystem::path& manifest_path,
                                                       bool replacement) {
    auto candidate = std::make_unique<Script>();
    candidate->manifest_path = manifest_path;
    candidate->manifest = Manifest::Read(manifest_path);
    const auto& id = candidate->manifest.id;
    if (!replacement && scripts_.contains(id) && scripts_.at(id).current &&
        scripts_.at(id).current->state != PluginState::Disabled && scripts_.at(id).current->state != PluginState::Failed)
        throw std::runtime_error(id + ": already loaded");
    if (!CheckDependencies(candidate->manifest)) throw std::runtime_error(error_);
    if (next_owner_ == std::numeric_limits<std::uint64_t>::max()) throw std::runtime_error("owner IDs exhausted");
    candidate->owner = next_owner_++;
    candidate->suspended_at = now_;
    if (settings_.debug) host_.Log(id + ": preparing " + manifest_path.filename().string());
    try {
        const auto entry = manifest_path.parent_path() / candidate->manifest.entry;
        if (std::filesystem::is_symlink(entry)) throw std::runtime_error("SMX symlinks are not accepted");
        candidate->vm = runtime_.Load(entry, &candidate->bytecode);
        Bind(*candidate);
        for (unsigned i = 0; i < candidate->vm->GetNativesNum(); ++i) {
            const auto* native = candidate->vm->GetNative(i);
            if (!native || native->status != SP_NATIVE_BOUND)
                throw std::runtime_error("missing native: " + std::string(native ? native->name : "<invalid>"));
        }
        auto* start = candidate->vm->GetFunctionByName("OnPluginStart");
        Cell result = 0;
        if (!start || !Invoke(*candidate, start, {}, nullptr, &result) || result != 1)
            throw std::runtime_error(candidate->error.empty() ? "OnPluginStart must return true" : candidate->error);
        return candidate;
    } catch (const std::exception& error) {
        candidate->error = id + ": " + error.what();
        candidate->state = PluginState::Retiring;
        candidate->stop_notified = true;
        if (Cleanup(*candidate)) candidate->state = PluginState::Failed;
        Fail(candidate->error);
        return candidate;
    }
}

bool Foundation::Load(const std::filesystem::path& manifest) {
    Thread();
    ManagementScope scope(managing_);
    if (!scope) return Fail("plugin management is already in progress");
    return LoadScript(manifest);
}

bool Foundation::LoadScript(const std::filesystem::path& manifest) {
    error_.clear();
    try {
        auto script = Prepare(manifest, false);
        const auto id = script->manifest.id;
        const bool ready = script->state == PluginState::Loading;
        if (ready) {
            script->state = PluginState::Running;
            script->load_order = next_load_++;
        }
        scripts_[id].current = std::move(script);
        return ready;
    } catch (const std::exception& error) {
        RecordFailure(manifest, error.what());
        return error_ == error.what() ? false : Fail(error.what());
    }
}

bool Foundation::Unload(const std::string& id) {
    Thread();
    ManagementScope scope(managing_);
    if (!scope) return Fail("plugin management is already in progress");
    return UnloadScript(id);
}

bool Foundation::UnloadScript(const std::string& id) {
    const auto found = scripts_.find(id);
    if (found == scripts_.end() || !found->second.current) return Fail("unknown plugin ID: " + id);
    const auto dependent = Dependent(id);
    if (!dependent.empty()) return Fail(id + ": in use by " + dependent);
    auto& slot = found->second;
    if (slot.replacement) {
        slot.replacement->state = PluginState::Retiring;
        if (!Cleanup(*slot.replacement)) return false;
        slot.replacement.reset();
    }
    slot.current->state = PluginState::Retiring;
    if (!Cleanup(*slot.current)) return false;
    slot.current->state = PluginState::Disabled;
    return true;
}

bool Foundation::Reload(const std::string& id) {
    Thread();
    ManagementScope scope(managing_);
    if (!scope) return Fail("plugin management is already in progress");
    return ReloadScript(id);
}

bool Foundation::ReloadScript(const std::string& id) {
    error_.clear();
    auto found = scripts_.find(id);
    if (found == scripts_.end() || !found->second.current) return Fail("unknown plugin ID: " + id);
    auto& slot = found->second;
    if (slot.current->active) return Fail(id + ": callback active; replacement refused");
    try {
        if (!slot.replacement) {
            slot.replacement_state = slot.current->state == PluginState::Paused ? PluginState::Paused : PluginState::Running;
            auto candidate = Prepare(slot.current->manifest_path, true);
            if (candidate->manifest.id != id) {
                candidate->state = PluginState::Retiring;
                candidate->error = "replacement changed the plugin ID";
            }
            if (candidate->state != PluginState::Loading) {
                if (!Cleanup(*candidate)) slot.replacement = std::move(candidate);
                return false;
            }
            slot.replacement = std::move(candidate);
        }
        if (slot.replacement->state != PluginState::Loading) {
            if (Cleanup(*slot.replacement)) slot.replacement.reset();
            return Fail(id + ": failed replacement cleaned; retry to prepare new bytecode");
        }
        if (!CheckDependencies(slot.replacement->manifest)) return false;
        slot.current->state = PluginState::Retiring;
        if (!Cleanup(*slot.current)) return false;
        slot.current = std::move(slot.replacement);
        ResumeTimers(*slot.current);
        slot.current->suspended_at = now_;
        slot.current->state = slot.replacement_state;
        slot.current->load_order = next_load_++;
        return true;
    } catch (const std::exception& error) { return error_ == error.what() ? false : Fail(id + ": " + error.what()); }
}

bool Foundation::ClearMenus(Script& script) {
    CloseOwnedMenus(script);
    for (const auto& [slot, display] : displays_)
        if (display.script == &script) return Fail(script.manifest.id + ": menu clear pending; retry required");
    return true;
}

void Foundation::ResumeTimers(Script& script) {
    const auto elapsed = now_ - script.suspended_at;
    for (auto handle : handles_.Owned(script.owner, TimerType))
        std::get<Timer>(handles_.Get(handle, script.owner, TimerType)).due += elapsed;
}

bool Foundation::Pause(const std::string& id) {
    Thread();
    ManagementScope scope(managing_);
    if (!scope) return Fail("plugin management is already in progress");
    const auto found = scripts_.find(id);
    if (found == scripts_.end() || !found->second.current) return Fail("unknown plugin ID: " + id);
    auto& script = *found->second.current;
    if (found->second.replacement) return Fail(id + ": replacement pending; finish reload or unload first");
    if (script.state == PluginState::Paused) return ClearMenus(script);
    if (script.state != PluginState::Running) return Fail(id + ": only running plugins can be paused");
    if (script.active) return Fail(id + ": callback active; pause refused");
    const auto dependent = Dependent(id, true);
    if (!dependent.empty()) return Fail(id + ": in use by " + dependent);
    CancelMap(script.owner);
    script.state = PluginState::Paused;
    script.suspended_at = now_;
    return ClearMenus(script);
}

bool Foundation::Resume(const std::string& id) {
    Thread();
    ManagementScope scope(managing_);
    if (!scope) return Fail("plugin management is already in progress");
    const auto found = scripts_.find(id);
    if (found == scripts_.end() || !found->second.current) return Fail("unknown plugin ID: " + id);
    auto& script = *found->second.current;
    if (found->second.replacement) return Fail(id + ": replacement pending; finish reload or unload first");
    if (script.state != PluginState::Paused) return Fail(id + ": only paused plugins can be resumed");
    if (!CheckDependencies(script.manifest) || !ClearMenus(script)) return false;
    ResumeTimers(script);
    script.state = PluginState::Running;
    return true;
}

bool Foundation::Retry(const std::string& id) {
    Thread();
    ManagementScope scope(managing_);
    if (!scope) return Fail("plugin management is already in progress");
    const auto found = scripts_.find(id);
    if (found == scripts_.end() || !found->second.current) return Fail("unknown plugin ID: " + id);
    if (found->second.current->state != PluginState::Failed || found->second.replacement)
        return Fail(id + ": retry requires a failed plugin; use load for a disabled plugin");
    const auto path = found->second.current->manifest_path;
    return LoadScript(path);
}

bool Foundation::UnloadAll() {
    Thread();
    ManagementScope scope(managing_);
    if (!scope) return Fail("plugin management is already in progress");
    if (!runtime_.Idle()) return Fail("cannot unload all plugins while a script callback is active");
    return UnloadAllScripts();
}

bool Foundation::UnloadAllScripts() {
    std::vector<std::pair<std::uint64_t, std::string>> order;
    for (const auto& [id, slot] : scripts_)
        if (slot.current) order.emplace_back(slot.current->load_order, id);
    std::sort(order.rbegin(), order.rend());
    std::set<std::string> attempted;
    bool progress = true;
    while (progress) {
        progress = false;
        for (const auto& [sequence, id] : order) {
            auto& slot = scripts_.at(id);
            if ((!slot.current->vm && (!slot.replacement || !slot.replacement->vm)) || attempted.contains(id) ||
                !Dependent(id).empty()) continue;
            attempted.insert(id);
            if (UnloadScript(id)) progress = true;
        }
    }
    std::string remaining;
    for (const auto& [id, slot] : scripts_) {
        if ((!slot.current || !slot.current->vm) && (!slot.replacement || !slot.replacement->vm)) continue;
        if (!remaining.empty()) remaining += ", ";
        remaining += id;
        const auto dependent = Dependent(id);
        if (!dependent.empty()) remaining += " (in use by " + dependent + ")";
    }
    return remaining.empty() || Fail("Plugins remain loaded: " + remaining + ".");
}

bool Foundation::Shutdown() {
    Thread();
    ManagementScope scope(managing_);
    if (!scope) return Fail("plugin management is already in progress");
    if (!runtime_.Idle()) return Fail("cannot shut down SourcePawn while a callback is active");
    bool success = true;
    for (auto it = native_displays_.begin(); it != native_displays_.end();) {
        int slot = it->first;
        ++it;
        if (!CloseNativeDisplay(slot)) success = false;
    }
    if (!UnloadAllScripts()) success = false;
    if (!voice_.Release(0, voice_error_)) { Fail(voice_error_); success = false; }
    return success;
}

std::size_t Foundation::ExtensionCount() const {
    return Extensions().size();
}

std::vector<KeelPluginHandle> Foundation::Extensions() const {
    Thread();
    std::set<KeelPluginHandle> owners;
    for (const auto& [registration, provider] : providers_) owners.insert(provider.owner);
    return {owners.begin(), owners.end()};
}

bool Foundation::Cleanup(Script& script) {
    CancelMap(script.owner);
    if (script.active) return Fail(script.manifest.id + ": callback active; retained for unload retry");
    if (script.vm && !script.stop_notified) {
        script.stop_notified = true;
        if (auto* stop = script.vm->GetFunctionByName("OnPluginStop")) Invoke(script, stop);
    }
    CloseOwnedMenus(script);
    for (const auto& [slot, display] : displays_)
        if (display.script == &script) return Fail(script.manifest.id + ": menu clear pending; retained for retry");
    script.gags.clear();
    std::string voice_error;
    bool success = voice_.Release(script.owner, voice_error);
    if (!success) script.error = voice_error;
    for (auto it = script.convars.begin(); it != script.convars.end();) {
        const auto name = it->first;
        ++it;
        if (!CloseConVar(script, name)) success = false;
    }
    for (auto it = script.commands.begin(); it != script.commands.end();) {
        auto& owners = commands_.at(it->first);
        if (owners.size() == 1 && host_.RemoveCommand(it->first) != KEEL_RESULT_OK) {
            success = false; ++it; continue;
        }
        owners.erase(&script);
        if (owners.empty()) commands_.erase(it->first);
        it = script.commands.erase(it);
    }
    for (auto it = script.events.begin(); it != script.events.end();) {
        auto& owners = events_.at(it->first);
        if (owners.size() == 1 && host_.RemoveEvent(it->first) != KEEL_RESULT_OK) {
            success = false; ++it; continue;
        }
        owners.erase(&script);
        if (owners.empty()) events_.erase(it->first);
        it = script.events.erase(it);
    }
    for (auto it = script.providers.begin(); it != script.providers.end();) {
        auto& provider = providers_.at(*it);
        if (provider.active || (ProviderUsers(provider) == 1 &&
            host_.ReleaseProvider(provider.service, provider.version) != KEEL_RESULT_OK)) {
            success = false; ++it; continue;
        }
        --provider.users;
        it = script.providers.erase(it);
    }
    handles_.Retire(script.owner);
    if (!success) return Fail(script.manifest.id + ": native resource release failed; retained for retry");
    script.vm.reset();
    script.bytecode = std::vector<std::uint8_t>{};
    return true;
}

unsigned Foundation::ProviderUsers(const Provider& provider) const {
    return ServiceUsers(provider.service, provider.version);
}

unsigned Foundation::ServiceUsers(const std::string& service, unsigned version) const {
    unsigned users = 0;
    for (const auto& [id, other] : providers_)
        if (other.service == service && other.version == version) users += other.users;
    for (const auto& [slot, display] : native_displays_)
        if (display.service == service && display.version == version) ++users;
    return users;
}

bool Foundation::Invoke(Script& script, SourcePawn::IPluginFunction* function,
                        const std::vector<Cell>& cells, const char* text, Cell* output) {
    ++script.active;
    const auto previous_player = script.callback_player;
    script.callback_player = cells.empty() ? 0 : cells.front();
    Cell result = 0;
    const bool success = runtime_.Invoke(script.manifest.id, function, cells, text, result);
    script.callback_player = previous_player;
    if (!--script.active) script.self_kicks.clear();
    if (!success) {
        CancelMap(script.owner);
        script.error = "script callback failed; see stack in platform log";
        if (++script.faults >= 3) script.state = PluginState::Retiring;
    }
    if (output) *output = result;
    return success;
}

std::vector<PluginStatus> Foundation::Status() const {
    Thread();
    std::vector<PluginStatus> status;
    for (const auto& [id, slot] : scripts_) if (slot.current)
        status.push_back({id, slot.current->state, slot.current->error,
            slot.current->commands.size(), slot.current->events.size(), handles_.Owned(slot.current->owner).size(), slot.current->convars.size()});
    return status;
}

const char* Foundation::StateName(PluginState state) {
    constexpr const char* names[] = {"loading", "running", "retiring", "disabled", "failed", "paused"};
    return names[static_cast<unsigned>(state)];
}

std::string Foundation::SelectPlugin(const std::string& selector) {
    if (scripts_.contains(selector)) return selector;
    std::string match;
    for (const auto& [id, slot] : scripts_) if (slot.current) {
        const auto& entry = slot.current->manifest.entry;
        if (entry.empty() || (selector != entry && selector != id + "/" + entry)) continue;
        if (!match.empty()) { Fail("More than one script uses \"" + selector + "\". Use a plugin ID."); return {}; }
        match = id;
    }
    if (match.empty()) Fail("Unknown script plugin \"" + selector + "\". Use sr plugins list.");
    return match;
}

std::filesystem::path Foundation::SelectFile(const std::string& selector) {
    if (ValidIdentifier(selector)) return root_ / "plugins" / selector / "plugin.json";
    const std::filesystem::path file(selector);
    if (selector.find('\\') != std::string::npos || file.is_absolute() || file.extension() != ".smx")
        throw std::runtime_error("use a plugin ID or <plugin>/<file>.smx");
    if (!file.parent_path().empty()) {
        if (!ValidIdentifier(file.parent_path().string())) throw std::runtime_error("invalid script directory ID");
        const auto manifest = root_ / "plugins" / file.parent_path() / "plugin.json";
        if (Manifest::Read(manifest).entry != file.filename().string())
            throw std::runtime_error("file does not match the plugin manifest entry");
        return manifest;
    }
    std::filesystem::path match;
    const auto directory = root_ / "plugins";
    if (std::filesystem::exists(directory)) for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_directory() || entry.is_symlink()) continue;
        const auto path = entry.path() / "plugin.json";
        Manifest manifest;
        try { manifest = Manifest::Read(path); }
        catch (const std::exception&) { continue; }
        if (manifest.entry != selector) continue;
        if (!match.empty()) throw std::runtime_error("more than one plugin uses this file name; include the plugin directory");
        match = path;
    }
    if (match.empty()) throw std::runtime_error("no plugin manifest names this file");
    return match;
}

void Foundation::ManagePlugins(const std::vector<std::string>& arguments) {
    Thread();
    const auto help = [&] {
        host_.Log("Source2Root Plugin Commands:\nUsage: sr plugins <command> [arguments]\n\n"
            "  list              - List known script plugins\n"
            "  info <plugin>     - Show plugin information\n"
            "  load <file>       - Load a script plugin\n"
            "  unload <plugin>   - Unload and disable a plugin for this session\n"
            "  unload_all        - Unload plugins in dependency order\n"
            "  reload <plugin>   - Replace a plugin, preserving paused state\n"
            "  refresh           - Discover new plugins and reload changed files\n"
            "  pause <plugin>    - Pause script execution\n"
            "  resume <plugin>   - Resume a paused plugin\n"
            "  retry <plugin>    - Retry a failed plugin load\n"
            "  cvars [plugin]    - List registered script ConVars\n"
            "  cmds [plugin]     - List registered script commands\n\n"
            "Select a known plugin by ID or file name. Load accepts an ID or <plugin>/<file>.smx.");
    };
    if (arguments.empty() || (arguments.size() == 1 && arguments[0] == "help")) { help(); return; }
    const auto& operation = arguments[0];
    if (operation == "list" && arguments.size() == 1) {
        std::ostringstream output;
        output << "Source2Root Script Plugins:\n  " << std::left << std::setw(22) << "ID" << std::setw(11) << "Status" << "File\n";
        for (const auto& [id, slot] : scripts_) if (slot.current)
            output << "  " << std::setw(22) << id << std::setw(11) << StateName(slot.current->state) << slot.current->manifest.entry << '\n';
        if (scripts_.empty()) output << "  No known script plugins.\n";
        host_.Log(output.str());
        return;
    }
    if (operation == "unload_all" && arguments.size() == 1) {
        if (UnloadAll()) host_.Log("Unloaded all script plugins.");
        return;
    }
    if (operation == "refresh" && arguments.size() == 1) { Refresh(); return; }
    if (operation == "cvars" && arguments.size() <= 2) {
        const auto selected = arguments.size() == 2 ? SelectPlugin(arguments[1]) : "";
        if (arguments.size() == 2 && selected.empty()) return;
        std::ostringstream output;
        output << "Source2Root Script ConVars:\n";
        unsigned count = 0;
        for (const auto& [name, variable] : convars_) {
            std::vector<Script*> ordered(variable.owners.begin(), variable.owners.end());
            std::sort(ordered.begin(), ordered.end(), [](const Script* a, const Script* b) {
                return std::tie(a->manifest.id, a->state) < std::tie(b->manifest.id, b->state);
            });
            ConVarValue value;
            const auto result = host_.ReadConVar(variable.native, value);
            for (const auto* script : ordered) {
                if (!selected.empty() && script->manifest.id != selected) continue;
                output << "  " << name << " = " << (result == KEEL_RESULT_OK ? ConVarText(value) : "<unavailable>")
                    << " | " << script->manifest.id << " | " << StateName(script->state) << '\n';
                ++count;
            }
        }
        if (!count) output << "  No registered ConVars.\n";
        host_.Log(output.str());
        return;
    }
    if (operation == "cmds" && arguments.size() <= 2) {
        const auto selected = arguments.size() == 2 ? SelectPlugin(arguments[1]) : "";
        if (arguments.size() == 2 && selected.empty()) return;
        std::ostringstream output;
        output << "Source2Root Script Commands:\n";
        unsigned count = 0;
        for (const auto& [name, owners] : commands_) {
            std::vector<Script*> ordered(owners.begin(), owners.end());
            std::sort(ordered.begin(), ordered.end(), [](const Script* a, const Script* b) {
                return std::tie(a->manifest.id, a->state) < std::tie(b->manifest.id, b->state);
            });
            for (const auto* script : ordered) {
                if (!selected.empty() && script->manifest.id != selected) continue;
                const auto& permission = script->commands.at(name).permission;
                output << "  " << name << " | " << script->manifest.id << " | " << StateName(script->state)
                       << " | permission: " << (permission.empty() ? "none" : permission) << '\n';
                ++count;
            }
        }
        if (!count) output << "  No registered commands.\n";
        host_.Log(output.str());
        return;
    }
    if (arguments.size() != 2) { help(); return; }
    if (operation == "load") {
        try {
            const auto file = SelectFile(arguments[1]);
            if (Load(file)) host_.Log("Loaded " + scripts_.at(file.parent_path().filename().string()).current->manifest.entry + ".");
        } catch (const std::exception& error) { Fail("Could not load " + arguments[1] + ": " + error.what() + "."); }
        return;
    }
    if (operation != "info" && operation != "unload" && operation != "reload" && operation != "pause" &&
        operation != "resume" && operation != "retry") { help(); return; }
    const auto id = SelectPlugin(arguments[1]);
    if (id.empty()) return;
    const auto& script = *scripts_.at(id).current;
    const auto file = script.manifest.entry.empty() ? id : script.manifest.entry;
    if (operation == "info") {
        const auto& manifest = script.manifest;
        std::ostringstream output;
        output << manifest.id << " | " << StateName(script.state) << "\nName: " << manifest.name
               << "\nAuthor: " << manifest.author << "\nVersion: " << manifest.version << "\nFile: " << manifest.entry
               << "\nCommands: " << script.commands.size() << "\nEvents: " << script.events.size()
               << "\nConVars: " << script.convars.size()
               << "\nHandles: " << handles_.Owned(script.owner).size() << "\nDependencies:";
        if (manifest.dependencies.empty()) output << " none";
        for (const auto& [dependency, version] : manifest.dependencies) output << "\n  " << dependency << " >= " << version;
        if (scripts_.at(id).replacement) output << "\nReplacement cleanup or activation is pending.";
        if (!script.error.empty()) output << "\nLast error: " << script.error;
        host_.Log(output.str());
        return;
    }
    bool success = false;
    std::string action;
    if (operation == "unload") { success = Unload(id); action = "Unloaded"; }
    else if (operation == "reload") { success = Reload(id); action = "Reloaded"; }
    else if (operation == "pause") { success = Pause(id); action = "Paused"; }
    else if (operation == "resume") { success = Resume(id); action = "Resumed"; }
    else if (operation == "retry") { success = Retry(id); action = "Loaded"; }
    if (success) host_.Log(action + " " + file + ".");
}

void Foundation::Discover() {
    Thread();
    ManagementScope scope(managing_);
    if (!scope) { Fail("plugin management is already in progress"); return; }
    DiscoverScripts();
}

bool Foundation::Refresh() {
    Thread();
    ManagementScope scope(managing_);
    if (!scope) return Fail("plugin management is already in progress");
    if (!runtime_.Idle()) return Fail("cannot refresh plugins while a script callback is active");
    return DiscoverScripts(true);
}

void Foundation::RecordFailure(const std::filesystem::path& path, const std::string& error) {
    const auto id = path.parent_path().filename().string();
    if (!ValidIdentifier(id)) return;
    auto& slot = scripts_[id];
    if ((slot.current && slot.current->vm) || slot.replacement) return;
    if (!slot.current) slot.current = std::make_unique<Script>();
    auto& script = *slot.current;
    script.manifest.id = id;
    script.manifest_path = path;
    script.state = PluginState::Failed;
    script.error = error;
    try { script.manifest = Manifest::Read(path); }
    catch (const std::exception&) {}
}

bool Foundation::SameBytecode(const std::filesystem::path& path, const std::vector<std::uint8_t>& expected) {
    if (std::filesystem::is_symlink(path)) throw std::runtime_error("SMX symlinks are not accepted");
    if (!std::filesystem::is_regular_file(path)) throw std::runtime_error("bytecode file is missing or is not a regular file");
    if (std::filesystem::file_size(path) != expected.size()) return false;
    std::ifstream input(path, std::ios::binary);
    std::array<std::uint8_t, 65536> buffer;
    for (std::size_t offset = 0; offset < expected.size();) {
        const auto count = std::min(buffer.size(), expected.size() - offset);
        if (!input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(count)))
            throw std::runtime_error("cannot read complete bytecode file");
        if (!std::equal(buffer.begin(), buffer.begin() + count, expected.begin() + offset)) return false;
        offset += count;
    }
    const bool complete = input.peek() == std::char_traits<char>::eof();
    if (input.bad()) throw std::runtime_error("cannot read bytecode file");
    return complete;
}

bool Foundation::DiscoverScripts(bool refresh) {
    struct Pending { Manifest manifest; std::filesystem::path path; bool replacement; };
    std::map<std::string, Pending> pending;
    unsigned loaded = 0, replaced = 0, unchanged = 0, failed = 0;
    bool success = true;
    if (refresh) for (auto& [id, slot] : scripts_) {
        if (!slot.current) continue;
        auto& script = *slot.current;
        if (script.state == PluginState::Disabled || script.state == PluginState::Failed) continue;
        if (slot.replacement || (script.state != PluginState::Running && script.state != PluginState::Paused)) {
            Fail(id + ": lifecycle cleanup pending; use reload or unload to retry");
            success = false;
            ++failed;
            continue;
        }
        try {
            auto manifest = Manifest::Read(script.manifest_path);
            const bool same = SameBytecode(script.manifest_path.parent_path() / manifest.entry, script.bytecode);
            if (manifest.source == script.manifest.source && same) ++unchanged;
            else pending.emplace(id, Pending{std::move(manifest), script.manifest_path, true});
        } catch (const std::exception& error) {
            script.error = id + ": refresh retained the loaded script: " + error.what();
            Fail(script.error);
            success = false;
            ++failed;
        }
    }
    try {
        const auto directory = root_ / "plugins";
        if (std::filesystem::exists(directory)) for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (!entry.is_directory() || entry.is_symlink()) continue;
            const auto id = entry.path().filename().string();
            if (scripts_.contains(id)) continue;
            const auto path = entry.path() / "plugin.json";
            try {
                auto manifest = Manifest::Read(path);
                if (!manifest.enabled) {
                    auto script = std::make_unique<Script>();
                    script->manifest = std::move(manifest);
                    script->manifest_path = path;
                    script->state = PluginState::Disabled;
                    scripts_[id].current = std::move(script);
                } else pending.emplace(id, Pending{std::move(manifest), path, false});
            } catch (const std::exception& error) {
                const auto message = id + ": could not discover plugin: " + error.what();
                RecordFailure(path, message);
                Fail(message);
                success = false;
                ++failed;
            }
        }
    } catch (const std::exception& error) {
        Fail("could not scan script directory: " + std::string(error.what()));
        success = false;
        ++failed;
    }
    bool progress = true;
    while (progress && !pending.empty()) {
        progress = false;
        for (auto it = pending.begin(); it != pending.end();) {
            auto& item = it->second;
            if (std::any_of(item.manifest.dependencies.begin(), item.manifest.dependencies.end(),
                    [&](const auto& dependency) { return pending.contains(dependency.first); })) {
                ++it;
                continue;
            }
            const bool ready = item.replacement ? ReloadScript(it->first) : LoadScript(item.path);
            if (ready) {
                if (item.replacement) ++replaced;
                else ++loaded;
            } else {
                if (item.replacement) scripts_.at(it->first).current->error = error_;
                success = false;
                ++failed;
            }
            it = pending.erase(it);
            progress = true;
        }
    }
    for (const auto& [id, item] : pending) {
        const auto message = id + ": dependency cycle in pending plugins; " +
            (item.replacement ? "loaded script retained" : "not started");
        if (item.replacement) scripts_.at(id).current->error = message;
        else RecordFailure(item.path, message);
        Fail(message);
        success = false;
        ++failed;
    }
    if (refresh) host_.Log("Refreshed plugins: " + std::to_string(loaded) + " loaded, " + std::to_string(replaced) +
        " reloaded, " + std::to_string(unchanged) + " unchanged, " + std::to_string(failed) + " failed.");
    return success;
}

void Foundation::Limit(Script& script) {
    if (script.state != PluginState::Loading && script.state != PluginState::Running)
        throw NativeError("plugin is retiring; new resources are refused");
    if (handles_.Owned(script.owner).size() + script.commands.size() + script.events.size() >= 128)
        throw NativeError("plugin resource limit (128) reached");
}

bool Foundation::RegisterCommand(Script& script, const std::string& name, const std::string& permission,
                                 SourcePawn::IPluginFunction* callback, const std::string& usage) {
    Limit(script);
    if (!name.starts_with("sr_") || !ValidIdentifier(name) || name.size() < 4 || !ValidPermission(permission))
        throw NativeError("invalid sr_ command name or permission");
    if ((!usage.empty() && usage != name && !usage.starts_with(name + " ")) ||
        std::any_of(usage.begin(), usage.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        throw NativeError("usage must start with the command name and stay on one line");
    if (script.commands.contains(name)) { script.error = "duplicate command registration: " + name; return false; }
    auto existing = commands_.find(name);
    if (existing != commands_.end()) {
        for (const auto* owner : existing->second)
            if (owner->manifest.id != script.manifest.id) { script.error = "command owned by another plugin: " + name; return false; }
    } else if (host_.RegisterCommand(name) != KEEL_RESULT_OK) {
        script.error = "native command registration failed: " + name; return false;
    }
    commands_[name].insert(&script);
    script.commands.emplace(name, Command{permission, callback, usage.empty() ? name : usage});
    return true;
}

bool Foundation::ListenEvent(Script& script, const std::string& name, SourcePawn::IPluginFunction* callback) {
    Limit(script);
    if (name != "round_start") throw NativeError("foundation supports the round_start game event");
    if (script.events.contains(name)) { script.error = "duplicate event registration"; return false; }
    if (!events_.contains(name) && host_.ListenEvent(name) != KEEL_RESULT_OK) {
        script.error = "native event registration failed"; return false;
    }
    events_[name].insert(&script);
    script.events.emplace(name, callback);
    return true;
}

Cell Foundation::PlayerHandle(Script& script, const Player& player) {
    for (auto id : handles_.Owned(script.owner, PlayerType))
        if (std::get<Player>(handles_.Get(id, script.owner, PlayerType)).SameConnection(player)) return id;
    Limit(script);
    return handles_.Add(script.owner, PlayerType, player);
}

KeelResult Foundation::ResolvePlayer(Script& script, Cell handle, Player& player) {
    if (!handles_.Contains(handle, script.owner, PlayerType)) {
        script.error = "Player is no longer available."; return KEEL_RESULT_NOT_FOUND;
    }
    auto expected = std::get<Player>(handles_.Get(handle, script.owner, PlayerType));
    const auto result = host_.Lookup(expected.slot, player);
    if (result != KEEL_RESULT_OK) { script.error = "player lookup failed (KeelResult " + std::to_string(result) + ")"; return result; }
    if (!player.SameConnection(expected)) { script.error = "Player is no longer available."; return KEEL_RESULT_NOT_FOUND; }
    return KEEL_RESULT_OK;
}

bool Foundation::Allowed(Script& script, Cell player, const std::string& permission) {
    if (!ValidPermission(permission)) throw NativeError("invalid permission name");
    if (!player) return true;
    Player current;
    if (ResolvePlayer(script, player, current) != KEEL_RESULT_OK) return false;
    if (!permissions_.Allows(current, permission)) { script.error = "permission denied: " + permission; return false; }
    return true;
}

}
