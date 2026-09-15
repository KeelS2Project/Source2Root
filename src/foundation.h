#pragma once

#include "bans.h"
#include "voice.h"

#include "handles.h"
#include "config.h"
#include "convars.h"
#include "identity.h"
#include "menu.h"
#include "runtime.h"
#include <source2root/extension.h>

#include <chrono>
#include <map>
#include <optional>
#include <set>
#include <thread>
#include <variant>

namespace sr {

class GameHost {
public:
    virtual ~GameHost() = default;
    virtual KeelResult Lookup(int slot, Player& player) = 0;
    virtual KeelResult NextPlayer(int, Player&) { return KEEL_RESULT_UNSUPPORTED; }
    virtual KeelResult Reply(const Player* player, const std::string& text) = 0;
    virtual void Log(const std::string& text) = 0;
    virtual KeelResult PlayerHealth(const Player&, std::int32_t&) { return KEEL_RESULT_UNSUPPORTED; }
    virtual KeelResult SlapPlayer(const Player&, int) { return KEEL_RESULT_UNSUPPORTED; }
    virtual KeelResult SlayPlayer(const Player&) { return KEEL_RESULT_UNSUPPORTED; }
    virtual KeelResult MapInstalled(const std::string&, bool&) { return KEEL_RESULT_UNSUPPORTED; }
    virtual KeelResult ChangeMap(const std::string&) { return KEEL_RESULT_UNSUPPORTED; }
    virtual KeelResult RestartRound(int) { return KEEL_RESULT_UNSUPPORTED; }
    virtual KeelResult KickPlayer(const Player&, const std::string&) { return KEEL_RESULT_UNSUPPORTED; }
    virtual KeelResult GetListening(const Player&, const Player&, bool&) { return KEEL_RESULT_UNSUPPORTED; }
    virtual KeelResult SetListening(const Player&, const Player&, bool) { return KEEL_RESULT_UNSUPPORTED; }
    virtual Timestamp UtcNow() const { return std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()); }
    virtual KeelResult RegisterCommand(const std::string& name) = 0;
    virtual KeelResult RemoveCommand(const std::string& name) = 0;
    virtual KeelResult ListenEvent(const std::string& name) = 0;
    virtual KeelResult RemoveEvent(const std::string& name) = 0;
    virtual KeelResult RenderMenu(const Player& player, const std::string& html, int duration_ms = 0) = 0;
    virtual KeelResult ReadPlayerInput(const Player&, KeelPlayerInput& input) {
        input = {sizeof(input), 0, 0, 0}; return KEEL_RESULT_UNSUPPORTED;
    }
    virtual KeelResult AcquireProvider(const std::string& service, unsigned version) = 0;
    virtual KeelResult ReleaseProvider(const std::string& service, unsigned version) = 0;
    virtual KeelResult CreateConVar(const ConVarDefinition&, std::uint64_t&) { return KEEL_RESULT_UNSUPPORTED; }
    virtual KeelResult ReadConVar(std::uint64_t, ConVarValue&) { return KEEL_RESULT_UNSUPPORTED; }
    virtual KeelResult WriteConVar(std::uint64_t, const ConVarValue&) { return KEEL_RESULT_UNSUPPORTED; }
    virtual KeelResult ReleaseConVar(std::uint64_t) { return KEEL_RESULT_UNSUPPORTED; }
};

enum class PluginState { Loading, Running, Retiring, Disabled, Failed, Paused };
enum class Origin { ServerConsole, ClientConsole, PublicChat, SilentChat };

struct Manifest {
    std::string id, name, author, version, entry, source;
    bool enabled = true;
    std::map<std::string, std::string> dependencies;
    static Manifest Read(const std::filesystem::path& path);
};

struct PluginStatus {
    std::string id;
    PluginState state;
    std::string error;
    std::size_t commands, events, handles;
    std::size_t convars;
};

class Foundation {
public:
    using Clock = std::chrono::steady_clock;
    Foundation(GameHost& host, const std::filesystem::path& runtime_library,
               std::filesystem::path root, CoreSettings settings = {});
    ~Foundation();
    bool Load(const std::filesystem::path& manifest);
    bool Unload(const std::string& id);
    bool Reload(const std::string& id);
    bool Pause(const std::string& id);
    bool Resume(const std::string& id);
    bool Retry(const std::string& id);
    bool UnloadAll();
    bool Refresh();
    bool Shutdown();
    bool PreparePause();
    void NativeResumed();
    void Discover();
    void ReloadPermissions();
    bool SetSetting(const std::string& name, const std::string& value);
    bool SetSettings(const std::array<std::string, 4>& values);
    const CoreSettings& Settings() const { return settings_; }
    std::vector<PluginStatus> Status() const;
    std::size_t ExtensionCount() const;
    std::vector<KeelPluginHandle> Extensions() const;
    void ManagePlugins(const std::vector<std::string>& arguments);
    bool Dispatch(Origin origin, int slot, const std::string& text);
    void Event(const std::string& name);
    KeelResult FilterVoice(int receiver, int sender, bool& listening);
    void Tick(Clock::time_point now);
    void MapChanged();
    void Disconnected(int slot, std::uint64_t generation);
    bool MenuInput(const Player& player, std::uint64_t session, sr::MenuInput input);
    std::uint64_t CurrentMenu(const Player& player) const;
    KeelResult RegisterNative(KeelPluginHandle owner, const SrNativeSpec& spec, SrRegistration& registration);
    KeelResult UnregisterNative(KeelPluginHandle owner, SrRegistration registration);
    KeelResult OpenNativeMenu(KeelPluginHandle owner, const KeelPlayerConnection& player,
        const SrMenuSpec& spec, SrMenuSession& session);
    KeelResult CloseNativeMenu(KeelPluginHandle owner, SrMenuSession session);
    const std::string& Error() const { return error_; }
private:
    struct Command {
        std::string permission;
        SourcePawn::IPluginFunction* callback;
        std::string usage;
        SourcePawn::IPluginFunction* menu = nullptr;
    };
    struct Script {
        Manifest manifest;
        std::filesystem::path manifest_path;
        std::uint64_t owner = 0;
        std::uint64_t load_order = 0;
        PluginState state = PluginState::Loading;
        Clock::time_point suspended_at;
        unsigned active = 0, faults = 0;
        Cell callback_player = 0;
        std::map<Cell, Player> self_kicks;
        std::map<int, Player> gags;
        bool stop_notified = false;
        std::string error;
        std::vector<std::uint8_t> bytecode;
        std::map<std::string, Command> commands;
        std::map<std::string, Cell> convars;
        std::map<std::string, SourcePawn::IPluginFunction*> events;
        std::set<SrRegistration> providers;
        std::unique_ptr<SourcePawn::IPluginRuntime> vm;
    };
    struct Slot {
        std::unique_ptr<Script> current, replacement;
        PluginState replacement_state = PluginState::Running;
    };
    struct Timer { Clock::time_point due; SourcePawn::IPluginFunction* callback; Cell target; bool across_maps = false; };
    struct ScriptMenu { Menu menu; SourcePawn::IPluginFunction* callback; };
    struct ScriptConVar { std::string name; };
    struct ConfigFile { std::string text; std::size_t cursor = 0; };
    using Resource = std::variant<Player, Timer, ScriptMenu, ScriptConVar, ConfigFile>;
    static constexpr unsigned PlayerType = 1, TimerType = 2, MenuType = 3, ConVarType = 4, ConfigFileType = 5;
    struct Variable {
        ConVarDefinition definition;
        std::uint64_t native = 0;
        std::set<Script*> owners;
    };
    struct Display {
        std::uint64_t session;
        Script* script;
        Cell menu;
        Cell player_handle;
        Player player;
        Clock::time_point expires;
        MenuControls controls;
    };
    struct Provider {
        KeelPluginHandle owner;
        std::string name, service;
        unsigned version, argc, users = 0, active = 0;
        SrNativeFunction invoke;
        void* user_data;
    };
    struct NativeDisplay {
        std::uint64_t session;
        KeelPluginHandle owner;
        Player player;
        Menu menu;
        std::string service;
        unsigned version;
        SrMenuCallback callback;
        void* user_data;
        Clock::time_point expires;
        bool active = false, closing = false, cleared = false;
        MenuControls controls;
    };
    std::map<int, NativeDisplay> native_displays_;
    GameHost& host_;
    PawnRuntime runtime_;
    std::filesystem::path root_;
    BanStore bans_;
    Voice voice_;
    std::thread::id thread_;
    std::map<std::string, Slot> scripts_;
    std::map<std::string, std::set<Script*>> commands_, events_;
    std::map<std::string, Variable> convars_;
    std::map<SrRegistration, Provider> providers_;
    Handles<Resource> handles_;
    struct PendingMap { std::uint64_t owner; std::string plugin, name; };
    std::optional<PendingMap> pending_map_;
    std::map<int, Display> displays_;
    Permissions permissions_;
    CoreSettings settings_;
    std::string error_;
    std::uint64_t next_owner_ = 1, next_provider_ = 1, next_session_ = 1;
    std::uint64_t next_load_ = 1;
    bool managing_ = false;
    Clock::time_point now_ = Clock::now();
    Clock::time_point next_voice_;
    std::string voice_error_;
    void Thread() const;
    bool Fail(std::string message);
    bool CheckDependencies(const Manifest& manifest);
    std::string Dependent(const std::string& id, bool running_only = false) const;
    bool LoadScript(const std::filesystem::path& manifest);
    bool UnloadScript(const std::string& id);
    bool ReloadScript(const std::string& id);
    bool UnloadAllScripts();
    bool DiscoverScripts(bool refresh = false);
    void RecordFailure(const std::filesystem::path& path, const std::string& error);
    static bool SameBytecode(const std::filesystem::path& path, const std::vector<std::uint8_t>& expected);
    std::string SelectPlugin(const std::string& selector);
    std::filesystem::path SelectFile(const std::string& selector);
    static const char* StateName(PluginState state);
    bool ClearMenus(Script& script);
    void ResumeTimers(Script& script);
    std::unique_ptr<Script> Prepare(const std::filesystem::path& manifest, bool replacement);
    void Bind(Script& script);
    void BindCommandMenus(Script& script);
    void BindBans(Script& script);
    void BindCommunications(Script& script);
    void BindServer(Script& script);
    void CancelMap(std::uint64_t owner = 0);
    bool RunPendingMap();
    bool Gagged(const Player& player) const;
    Script* CommandOwner(const std::string& name);
    void BindPlayers(Script& script);
    void BindText(Script& script);
    bool ReadPlayers(Script& script, std::vector<Player>& players);
    void ReconcilePlayers();
    bool MakePlayerHandles(Script& script, const std::vector<Player>& players, int capacity, std::vector<Cell>& output);
    void BindConVars(Script& script);
    Cell CreateConVar(Script& script, ConVarDefinition definition);
    bool ReadConVar(Script& script, Cell handle, ConVarValue& value);
    bool WriteConVar(Script& script, Cell handle, const ConVarValue& value);
    bool CloseConVar(Script& script, const std::string& name);
    bool ConVarError(Script& script, const std::string& name, KeelResult result);
    static void ValidateConVar(const ConVarDefinition& definition);
    static std::string ConVarText(const ConVarValue& value);
    bool Cleanup(Script& script);
    bool Invoke(Script& script, SourcePawn::IPluginFunction* function,
                const std::vector<Cell>& cells = {}, const char* text = nullptr, Cell* result = nullptr);
    bool RegisterCommand(Script& script, const std::string& name, const std::string& permission,
                         SourcePawn::IPluginFunction* callback, const std::string& usage);
    bool ListenEvent(Script& script, const std::string& name, SourcePawn::IPluginFunction* callback);
    Cell PlayerHandle(Script& script, const Player& player);
    KeelResult ResolvePlayer(Script& script, Cell handle, Player& player);
    bool Allowed(Script& script, Cell player, const std::string& permission);
    bool ShowActivity(Script& script, Cell caller, const std::string& action, const std::string& confirmation);
    unsigned menu_call_depth_ = 0;
    void Limit(Script& script);
    bool CloseDisplay(int slot);
    void CloseOwnedMenus(Script& script);
    unsigned ProviderUsers(const Provider& provider) const;
    unsigned ServiceUsers(const std::string& service, unsigned version) const;
    bool CloseNativeDisplay(int slot, int selected = -1);
    bool NativeMenuInput(const Player& player, std::uint64_t session, sr::MenuInput input);
    MenuControls InitialMenuControls(const Player& player);
    void PollMenuInput();
    void TickNativeMenus();
};

}
