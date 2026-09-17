#include "foundation.h"
#include "cs2_menu.h"
#include "build_info.h"

#include <keels2/authoring.hpp>
#include <keels2/services.hpp>
#include <keels2/source2.hpp>
#include <keels2/convar.h>
#include <eiface.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <optional>
#include <google/protobuf/stubs/common.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {
using namespace keels2::authoring;

struct ProtobufLifetime {
    ~ProtobufLifetime() { google::protobuf::ShutdownProtobufLibrary(); }
} protobuf_lifetime;

class Source2Root final : public Plugin, public sr::GameHost {
public:
    static constexpr PluginInfo Info{"Source2Root", "KeelS2Project", "1.0.0", "SourcePawn scripting and administration"};
    bool Load() override {
        try {
            if (runtime_.Connect(HostContext()) != KEEL_RESULT_OK || runtime_.CheckGameThread() != KEEL_RESULT_OK ||
                services_.Connect(HostContext()) != KEEL_RESULT_OK ||
                source2_.Connect(HostContext()) != KEEL_RESULT_OK) return false;
            const void* input_service = nullptr;
            if (HostContext().QueryService(KEELS2_PLAYER_INPUT_SERVICE_NAME, KEELS2_PLAYER_INPUT_API_VERSION,
                    &input_service) != KEEL_RESULT_OK) throw std::runtime_error("player input service is unavailable");
            player_input_ = static_cast<const KeelPlayerInputApi*>(input_service);
            if (!player_input_ || player_input_->size != sizeof(*player_input_) ||
                player_input_->api_version != KEELS2_PLAYER_INPUT_API_VERSION || !player_input_->read)
                throw std::runtime_error("player input service is incompatible");
            auto directory = ModuleDirectory();
            for (unsigned i = 0; i < 8 && !directory.empty(); ++i, directory = directory.parent_path()) {
                if (directory.filename() == "keels2" && directory.parent_path().filename() == "addons") {
                    root_ = directory.parent_path() / "source2root";
                    break;
                }
            }
            if (root_.empty()) throw std::runtime_error("module must be installed under addons/keels2");
            std::filesystem::create_directories(root_ / "logs");
            std::filesystem::create_directories(root_ / "plugins");
            std::filesystem::create_directories(root_ / "configs");
#if defined(_WIN32)
            const auto runtime = root_ / "bin/win64/libsourcepawn.dll";
#else
            const auto runtime = root_ / "bin/linuxsteamrt64/libsourcepawn.so";
#endif
            sr::CoreSettings settings;
            try { settings = sr::CoreSettings::Read(root_.parent_path().parent_path() / "cfg/source2root/source2root.cfg", settings); }
            catch (const std::exception& error) { Log("Could not read source2root.cfg: " + std::string(error.what()) + ". Using defaults."); }
            foundation_ = std::make_unique<sr::Foundation>(*this, runtime, root_, settings);
            scripts_started_ = false;
            CreateCoreSettings(settings);
            api_ = {sizeof(api_), SR_EXTENSION_API_VERSION, this, &RegisterNative, &UnregisterNative, &OpenMenu, &CloseMenu};
            if (services_.Publish(SR_EXTENSION_SERVICE, SR_EXTENSION_API_VERSION, &api_, publication_) != KEEL_RESULT_OK)
                throw std::runtime_error("could not publish extension API");
            native_api_ = {sizeof(native_api_), SR_NATIVE_API_VERSION, this, &RegisterContextNative, &UnregisterNative,
                &DeliverCallback, &CancelCallback, &PlayerSnapshot, &MenuStatus, &ConsumerStatus};
            if (services_.Publish(SR_NATIVE_SERVICE, SR_NATIVE_API_VERSION, &native_api_, native_publication_) != KEEL_RESULT_OK)
                throw std::runtime_error("could not publish native call API");
            if (!CreateCommand("sr", "Source2Root management", &Source2Root::Manage) ||
                !CreateCommand("sr_menu", "Menu input: up/down/select/back [session]", &Source2Root::MenuCommand,
                    FCVAR_CLIENT_CAN_EXECUTE | FCVAR_GAMEDLL))
                throw std::runtime_error(LastError());
            auto* cvars = GetCVarSystem<ICvar>();
            if (!cvars || !HookPre(cvars, &ICvar::DispatchConCommand, &Source2Root::ChatCommand))
                throw std::runtime_error("could not intercept client chat: " + std::string(LastError()));
            voice_engine_ = GetEngineInterface<IVEngineServer2>(INTERFACEVERSION_VENGINESERVER);
            if (voice_engine_ && !HookPre(voice_engine_, &IVEngineServer2::SetClientListening, &Source2Root::Listening, 1000))
                throw std::runtime_error(LastError());
            auto* messages = NetworkMessages();
            auto* events = GetEngineInterface<IGameEventSystem>(GAMEEVENTSYSTEM_INTERFACE_VERSION);
            if (messages && events) {
                menu_ = std::make_unique<sr::Cs2MenuBackend>(messages, events);
            } else Log("menu transport is unavailable; " + std::string(LastError()));
            return true;
        } catch (const std::exception& error) {
            LogError(error.what());
            if (foundation_) foundation_.reset();
            return false;
        }
    }
    bool PreparePause() override {
        return !foundation_ || foundation_->PreparePause();
    }
    bool PrepareUnload() override {
        if (runtime_.CheckGameThread() != KEEL_RESULT_OK || !foundation_) return !foundation_;
        if (!foundation_->Shutdown()) return false;
        if (native_publication_) {
            const auto result = services_.Withdraw(native_publication_);
            if (result != KEEL_RESULT_OK && result != KEEL_RESULT_NOT_FOUND) return false;
            native_publication_ = 0;
        }
        if (publication_) {
            auto result = services_.Withdraw(publication_);
            if (result != KEEL_RESULT_OK && result != KEEL_RESULT_NOT_FOUND) return false;
            publication_ = 0;
        }
        return true;
    }
    void Unload() override { foundation_.reset(); menu_.reset(); game_event_manager_ = nullptr; core_settings_.clear(); restart_ = 0; convars_ = nullptr; player_input_ = nullptr; }
    void OnAllPluginsLoaded() override {
        StartScripts();
    }
    void OnGameFrame(bool, bool, bool) override {
        StartScripts();
        if (foundation_) foundation_->Tick(sr::Foundation::Clock::now());
    }
    void OnPluginResumed(const PluginSnapshot& plugin) override {
        if (plugin.id != HostContext().PluginHandle() || !foundation_) return;
        foundation_->NativeResumed();
        std::array<std::string, 4> values;
        for (std::size_t i = 0; i < sr::CoreSettings::Names.size(); ++i) {
            const std::string name(sr::CoreSettings::Names[i]);
            sr::ConVarValue value;
            if (ReadConVar(core_settings_.at(name), value) != KEEL_RESULT_OK) {
                Log("Could not read " + name + " after resume. Keeping previous core settings.");
                return;
            }
            if (const auto* text = std::get_if<std::string>(&value)) values[i] = *text;
            else if (const auto* number = std::get_if<std::int32_t>(&value)) values[i] = std::to_string(*number);
            else { Log("Unexpected core ConVar type after resume."); return; }
        }
        if (foundation_->SetSettings(values)) return;
        Log("Could not apply core settings after resume. Restoring previous values.");
        restoring_settings_ = true;
        try {
            for (const auto name : sr::CoreSettings::Names) {
                sr::ConVarValue value;
                const auto& settings = foundation_->Settings();
                if (name == "sr_show_activity") value = static_cast<std::int32_t>(settings.activity);
                else if (name == "sr_debug") value = static_cast<std::int32_t>(settings.debug);
                else value = settings.Value(name);
                if (WriteConVar(core_settings_.at(std::string(name)), value) != KEEL_RESULT_OK)
                    Log("Could not restore core setting " + std::string(name) + ".");
            }
        } catch (...) { restoring_settings_ = false; throw; }
        restoring_settings_ = false;
    }
    void OnLevelShutdown() override {
        if (foundation_) foundation_->MapChanged();
    }
    Action ChatCommand(ConCommandRef, const CCommandContext& context, const CCommand& command) {
        const auto slot = context.GetPlayerSlot();
        if (!foundation_ || !slot.IsValid() || command.ArgC() < 2) return PLUGIN_CONTINUE;
        std::string name = command[0];
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
        if (name != "say" && name != "say_team") return PLUGIN_CONTINUE;
        const std::string text = command.ArgC() == 2 ? command[1] : command.ArgS();
        return foundation_->Dispatch(sr::Origin::PublicChat, slot.Get(), text)
            ? PLUGIN_SUPERSEDE : PLUGIN_CONTINUE;
    }
    KeelResult Lookup(int slot, sr::Player& result) override {
        PlayerInfo player;
        if (!GetPlayer(CPlayerSlot(slot), player)) return LastResult();
        result = {slot, player.connection, player.steam_id.ConvertToUint64(), player.authenticated,
                  player.bot || player.source_tv, player.name.String(), player.user_id, player.team, player.alive};
        return KEEL_RESULT_OK;
    }
    KeelResult NextPlayer(int after, sr::Player& result) override {
        PlayerInfo player;
        if (!GetNextPlayer(CPlayerSlot(after), player)) return LastResult();
        result = {player.slot.Get(), player.connection, player.steam_id.ConvertToUint64(), player.authenticated,
                  player.bot || player.source_tv, player.name.String(), player.user_id, player.team, player.alive};
        return KEEL_RESULT_OK;
    }
    KeelResult PlayerHealth(const sr::Player& expected, std::int32_t& health) override {
        PlayerInfo player;
        health = 0;
        if (!GetPlayer(PlayerConnection{CPlayerSlot(expected.slot), expected.connection}, player)) return LastResult();
        const void* value = nullptr;
        auto result = HostContext().QueryService(KEELS2_ENTITIES_SERVICE_NAME, KEELS2_ENTITIES_API_VERSION, &value);
        if (result != KEEL_RESULT_OK) return result;
        const auto* entities = static_cast<const KeelEntitiesApi*>(value);
        if (!entities || entities->size != sizeof(*entities) || entities->api_version != KEELS2_ENTITIES_API_VERSION ||
            !entities->find_by_source2_handle || !entities->read_field || !entities->release) return KEEL_RESULT_INCOMPATIBLE;
        result = HostContext().QueryService(KEELS2_SCHEMA_SERVICE_NAME, KEELS2_SCHEMA_API_VERSION, &value);
        if (result != KEEL_RESULT_OK) return result;
        const auto* schema = static_cast<const KeelSchemaApi*>(value);
        if (!schema || schema->size != sizeof(*schema) || schema->api_version != KEELS2_SCHEMA_API_VERSION ||
            !schema->resolve_field || !schema->release_field) return KEEL_RESULT_INCOMPATIBLE;
        const auto owner = HostContext().PluginHandle();
        KeelEntityHandle entity = 0;
        result = entities->find_by_source2_handle(owner, static_cast<uint32>(player.pawn.ToInt()), &entity);
        if (result != KEEL_RESULT_OK) return result;
        KeelSchemaFieldHandle field = 0;
        const KeelSchemaFieldSpec spec{sizeof(spec), KEELS2_SCHEMA_MODULE_SERVER, KEELS2_SCHEMA_INT32, 0,
                                      "CBaseEntity", "m_iHealth"};
        result = schema->resolve_field(owner, &spec, &field);
        if (result == KEEL_RESULT_OK) result = entities->read_field(owner, entity, field, &health, sizeof(health));
        if (field) {
            const auto cleanup = schema->release_field(owner, field);
            if (cleanup != KEEL_RESULT_OK && cleanup != KEEL_RESULT_NOT_FOUND) result = cleanup;
        }
        const auto cleanup = entities->release(owner, entity);
        if (cleanup != KEEL_RESULT_OK && cleanup != KEEL_RESULT_NOT_FOUND) result = cleanup;
        if (result != KEEL_RESULT_OK) health = 0;
        return result;
    }
    KeelResult Reply(const sr::Player* player, const std::string& text) override {
        if (!player) { Log(text); return KEEL_RESULT_OK; }
        PlayerInfo current;
        if (!GetPlayer(PlayerConnection{CPlayerSlot(player->slot), player->connection}, current)) return LastResult();
        return PrintToChat(current.slot, ("[S2R] " + text).c_str());
    }
    KeelResult GetListening(const sr::Player& receiver, const sr::Player& sender, bool& listening) override {
        listening = false;
        if (!voice_engine_) return KEEL_RESULT_UNSUPPORTED;
        PlayerInfo current;
        if (!GetPlayer(PlayerConnection{CPlayerSlot(receiver.slot), receiver.connection}, current) ||
            !GetPlayer(PlayerConnection{CPlayerSlot(sender.slot), sender.connection}, current)) return LastResult();
        listening = voice_engine_->GetClientListening(CPlayerSlot(receiver.slot), CPlayerSlot(sender.slot));
        return KEEL_RESULT_OK;
    }
    KeelResult SetListening(const sr::Player& receiver, const sr::Player& sender, bool listening) override {
        if (!voice_engine_) return KEEL_RESULT_UNSUPPORTED;
        PlayerInfo current;
        if (!GetPlayer(PlayerConnection{CPlayerSlot(receiver.slot), receiver.connection}, current) ||
            !GetPlayer(PlayerConnection{CPlayerSlot(sender.slot), sender.connection}, current)) return LastResult();
        return voice_engine_->SetClientListening(CPlayerSlot(receiver.slot), CPlayerSlot(sender.slot), listening)
            ? KEEL_RESULT_OK : KEEL_RESULT_ENGINE_FAILURE;
    }
    KeelResult MapInstalled(const std::string& name, bool& installed) override {
        installed = false;
        auto* engine = GetEngineInterface<IVEngineServer2>(INTERFACEVERSION_VENGINESERVER);
        if (!engine) return KEEL_RESULT_UNSUPPORTED;
        installed = engine->IsMapValid(name.c_str()) != 0;
        return KEEL_RESULT_OK;
    }
    KeelResult ChangeMap(const std::string& name) override {
        auto* engine = GetEngineInterface<IVEngineServer2>(INTERFACEVERSION_VENGINESERVER);
        if (!engine) return KEEL_RESULT_UNSUPPORTED;
        if (!engine->IsMapValid(name.c_str())) return KEEL_RESULT_NOT_FOUND;
        engine->ChangeLevel(name.c_str(), nullptr);
        return KEEL_RESULT_OK;
    }
    KeelResult RestartRound(int seconds) override {
        if (seconds < 1 || seconds > 60) return KEEL_RESULT_INVALID_ARGUMENT;
        if (!convars_ || !convars_->find) return KEEL_RESULT_UNSUPPORTED;
        if (!restart_) {
            const auto result = convars_->find(HostContext().PluginHandle(), "mp_restartgame", KEELS2_CONVAR_INT32, &restart_);
            if (result != KEEL_RESULT_OK) return result;
        }
        const auto value = NativeValue(static_cast<std::int32_t>(seconds));
        return convars_->queue_set(HostContext().PluginHandle(), restart_, KEELS2_CONVAR_GLOBAL_SLOT, &value);
    }
    KeelResult KickPlayer(const sr::Player& player, const std::string& reason) override {
        auto* engine = GetEngineInterface<IVEngineServer2>(INTERFACEVERSION_VENGINESERVER);
        if (!engine) return KEEL_RESULT_UNSUPPORTED;
        PlayerInfo current;
        if (!GetPlayer(PlayerConnection{CPlayerSlot(player.slot), player.connection}, current)) return LastResult();
        engine->DisconnectClient(current.slot, NETWORK_DISCONNECT_KICKED, reason.c_str());
        return KEEL_RESULT_OK;
    }
    KeelResult SlapPlayer(const sr::Player& player, int damage) override {
        if (damage < 0 || damage > 1000) return KEEL_RESULT_INVALID_ARGUMENT;
        Entity pawn;
        const auto result = PlayerPawn(player, pawn);
        if (result != KEEL_RESULT_OK) return result;
        const auto direction = ++impulse_sequence_;
        return pawn.ApplyImpulse(Vector(direction & 1 ? 200.0f : -200.0f,
            direction & 2 ? 200.0f : -200.0f, 300.0f), static_cast<float>(damage));
    }
    KeelResult SlayPlayer(const sr::Player& player) override {
        Entity pawn;
        const auto result = PlayerPawn(player, pawn);
        return result == KEEL_RESULT_OK ? pawn.Kill() : result;
    }
    void Log(const std::string& text) override {
        LogMessage(text.c_str());
        if (!root_.empty()) {
            std::ofstream log(root_ / "logs" / "source2root.log", std::ios::app);
            if (log) log << "[Source2Root] " << text << '\n';
        }
    }
    KeelResult RegisterCommand(const std::string& name) override {
        return CreateCommand(name.c_str(), "Source2Root script command", &Source2Root::ScriptCommand,
                             FCVAR_CLIENT_CAN_EXECUTE | FCVAR_GAMEDLL) ? KEEL_RESULT_OK : LastResult();
    }
    KeelResult RemoveCommand(const std::string& name) override {
        return Plugin::RemoveCommand(name.c_str()) ? KEEL_RESULT_OK : LastResult();
    }
    KeelResult CreateConVar(const sr::ConVarDefinition& definition, std::uint64_t& handle) override {
        handle = 0;
        if (!convars_) return KEEL_RESULT_NOT_READY;
        KeelConVarSpec spec{};
        spec.size = sizeof(spec);
        spec.name = definition.name.c_str();
        spec.description = definition.description.c_str();
        spec.flags = KEELS2_CVAR_FLAG_RELEASE;
        spec.default_value = NativeValue(definition.initial);
        spec.type = spec.default_value.type;
        if (spec.type != KEELS2_CONVAR_STRING) {
            spec.has_minimum = spec.has_maximum = KEEL_TRUE;
            spec.minimum_value = NativeValue(definition.minimum);
            spec.maximum_value = NativeValue(definition.maximum);
        }
        return convars_->create(HostContext().PluginHandle(), &spec, &handle);
    }
    KeelResult ReadConVar(std::uint64_t handle, sr::ConVarValue& value) override {
        if (!convars_) return KEEL_RESULT_NOT_READY;
        KeelConVarValue native{sizeof(native), 0, {}};
        const auto result = convars_->read(HostContext().PluginHandle(), handle, KEELS2_CONVAR_GLOBAL_SLOT, &native);
        if (result != KEEL_RESULT_OK) return result;
        if (native.size != sizeof(native)) return KEEL_RESULT_INCOMPATIBLE;
        switch (native.type) {
            case KEELS2_CONVAR_INT32: value = native.value.int32_value; break;
            case KEELS2_CONVAR_FLOAT32: value = native.value.float32_value; break;
            case KEELS2_CONVAR_STRING:
                if (!native.value.string_value) return KEEL_RESULT_INCOMPATIBLE;
                value = std::string(native.value.string_value); break;
            default: return KEEL_RESULT_INCOMPATIBLE;
        }
        return KEEL_RESULT_OK;
    }
    KeelResult WriteConVar(std::uint64_t handle, const sr::ConVarValue& value) override {
        if (!convars_) return KEEL_RESULT_NOT_READY;
        const auto native = NativeValue(value);
        return convars_->queue_set(HostContext().PluginHandle(), handle, KEELS2_CONVAR_GLOBAL_SLOT, &native);
    }
    KeelResult ReleaseConVar(std::uint64_t handle) override {
        return convars_ ? convars_->release(HostContext().PluginHandle(), handle) : KEEL_RESULT_NOT_READY;
    }
    KeelResult ListenEvent(const std::string& name) override {
        return ListenForGameEvent(name.c_str(), &Source2Root::GameEvent) ? KEEL_RESULT_OK : LastResult();
    }
    KeelResult RemoveEvent(const std::string& name) override {
        return StopListeningForGameEvent(name.c_str()) ? KEEL_RESULT_OK : KEEL_RESULT_BUSY;
    }
    KeelResult RenderMenu(const sr::Player& player, const std::string& html, int duration_ms) override {
        if (!menu_) return KEEL_RESULT_NOT_READY;
        if (!game_event_manager_) {
            keels2::source2::Interface manager;
            const auto query = source2_.Query(keels2::source2::Capability::game_event_manager, manager);
            if (query != KEEL_RESULT_OK) return query;
            game_event_manager_ = manager.Get<IGameEventManager2>();
        }
        auto result = menu_->Render(game_event_manager_, player.slot, html, duration_ms);
        if (result != KEEL_RESULT_OK) {
            if (last_menu_error_ != menu_->Error()) Log(menu_->Error());
            last_menu_error_ = menu_->Error();
        } else last_menu_error_.clear();
        return result;
    }
    KeelResult ReadPlayerInput(const sr::Player& player, KeelPlayerInput& input) override {
        input = {sizeof(input), 0, 0, 0};
        const KeelPlayerConnection connection{player.slot, 0, player.connection};
        const auto result = player_input_ ? player_input_->read(HostContext().PluginHandle(), &connection, &input) : KEEL_RESULT_NOT_READY;
        if (result == KEEL_RESULT_OK) input_errors_.erase(player.slot);
        else {
            const auto failure = std::pair{player.connection, result};
            const auto previous = input_errors_.find(player.slot);
            if (previous == input_errors_.end() || previous->second != failure) {
                input_errors_[player.slot] = failure;
                Log("menu input unavailable for slot " + std::to_string(player.slot) + " (KeelResult " + std::to_string(result) + ")");
            }
        }
        return result;
    }
    KeelResult AcquireProvider(const std::string& service, unsigned version) override {
        const void* value = nullptr;
        return HostContext().QueryService(service.c_str(), version, &value);
    }
    KeelResult ReleaseProvider(const std::string& service, unsigned version) override {
        return services_.Release(service.c_str(), version);
    }
private:
    void StartScripts() {
        if (!foundation_ || scripts_started_) return;
        scripts_started_ = true;
        foundation_->Discover();
        const auto scripts = foundation_->Status();
        const auto loaded = std::count_if(scripts.begin(), scripts.end(), [](const auto& script) {
            return script.state == sr::PluginState::Running;
        });
        const auto extensions = foundation_->ExtensionCount();
        Log("Version 1.0.0. Loaded " + std::to_string(loaded) + (loaded == 1 ? " plugin and " : " plugins and ") +
            std::to_string(extensions) + (extensions == 1 ? " extension." : " extensions."));
    }
    const KeelPlayerInputApi* player_input_ = nullptr;
    std::map<int, std::pair<std::uint64_t, KeelResult>> input_errors_;
    Action Listening(HookCall<bool>& call, CPlayerSlot receiver, CPlayerSlot sender, bool& listening) {
        if (!foundation_) return PLUGIN_CONTINUE;
        auto result = runtime_.CheckGameThread();
        if (result == KEEL_RESULT_OK) {
            try { result = foundation_->FilterVoice(receiver.Get(), sender.Get(), listening); }
            catch (...) { result = KEEL_RESULT_ENGINE_FAILURE; }
        }
        if (result == KEEL_RESULT_OK || result == KEEL_RESULT_NOT_FOUND) return PLUGIN_CONTINUE;
        call.SetResult(false);
        return PLUGIN_SUPERSEDE;
    }
    IVEngineServer2* voice_engine_ = nullptr;
    std::uint32_t impulse_sequence_ = 0;
    KeelResult PlayerPawn(const sr::Player& expected, Entity& pawn) {
        PlayerInfo current, checked;
        const PlayerConnection connection{CPlayerSlot(expected.slot), expected.connection};
        if (!GetPlayer(connection, current)) return LastResult();
        if (!current.alive) return KEEL_RESULT_NOT_FOUND;
        if (!FindEntity(current.pawn, pawn)) return LastResult();
        if (!GetPlayer(connection, checked)) return LastResult();
        if (!checked.alive || checked.pawn.ToInt() != current.pawn.ToInt()) return KEEL_RESULT_NOT_FOUND;
        return KEEL_RESULT_OK;
    }
    std::filesystem::path root_;
    std::unique_ptr<sr::Foundation> foundation_;
    bool scripts_started_ = false;
    std::unique_ptr<sr::Cs2MenuBackend> menu_;
    IGameEventManager2* game_event_manager_ = nullptr;
    keels2::services::Service services_;
    keels2::source2::Service source2_;
    keels2::source2::NativeRuntime runtime_;
    SrExtensionApi api_{};
    SrNativeApi native_api_{};
    KeelServiceHandle native_publication_ = 0;
    KeelServiceHandle publication_ = 0;
    std::string last_menu_error_;
    KeelConVarHandle restart_ = 0;
    const KeelConVarApi* convars_ = nullptr;
    std::map<std::string, KeelConVarHandle> core_settings_;
    bool restoring_settings_ = false;
    static KeelConVarValue NativeValue(const sr::ConVarValue& value) {
        KeelConVarValue result{sizeof(result), 0, {}};
        if (const auto* number = std::get_if<std::int32_t>(&value)) {
            result.type = KEELS2_CONVAR_INT32;
            result.value.int32_value = *number;
        } else if (const auto* number = std::get_if<float>(&value)) {
            result.type = KEELS2_CONVAR_FLOAT32;
            result.value.float32_value = *number;
        } else {
            result.type = KEELS2_CONVAR_STRING;
            result.value.string_value = std::get<std::string>(value).c_str();
        }
        return result;
    }
    void CreateCoreSettings(const sr::CoreSettings& settings) {
        core_settings_.clear();
        const sr::CoreSettings defaults;
        const void* value = nullptr;
        if (HostContext().QueryService(KEELS2_CONVAR_SERVICE_NAME, KEELS2_CONVAR_API_VERSION, &value) != KEEL_RESULT_OK)
            throw std::runtime_error("core ConVar service is unavailable");
        convars_ = static_cast<const KeelConVarApi*>(value);
        if (!convars_ || convars_->size != sizeof(*convars_) || convars_->api_version != KEELS2_CONVAR_API_VERSION ||
            !convars_->create || !convars_->queue_set || !convars_->read || !convars_->release)
            throw std::runtime_error("core ConVar service is incompatible");
        const char* descriptions[] = {"Activity flags: 1/2 for non-admins, 4/8 for admins; 0 disables announcements",
            "Visible chat command prefix; empty disables it", "Hidden chat command prefix; empty disables it",
            "Enable detailed Source2Root tracing"};
        for (std::size_t i = 0; i < sr::CoreSettings::Names.size(); ++i) {
            const std::string name(sr::CoreSettings::Names[i]);
            const bool numeric = name == "sr_show_activity" || name == "sr_debug";
            const auto text = defaults.Value(name);
            KeelConVarSpec spec{};
            spec.size = sizeof(spec);
            spec.type = numeric ? KEELS2_CONVAR_INT32 : KEELS2_CONVAR_STRING;
            spec.name = name.c_str();
            spec.description = descriptions[i];
            spec.flags = KEELS2_CVAR_FLAG_RELEASE;
            spec.default_value = {sizeof(KeelConVarValue), spec.type, {}};
            if (numeric) {
                spec.default_value.value.int32_value = name == "sr_show_activity" ? defaults.activity : defaults.debug;
                spec.has_minimum = spec.has_maximum = KEEL_TRUE;
                spec.minimum_value = spec.maximum_value = {sizeof(KeelConVarValue), KEELS2_CONVAR_INT32, {}};
                spec.maximum_value.value.int32_value = name == "sr_show_activity" ? 15 : 1;
            } else spec.default_value.value.string_value = text.c_str();
            spec.callback = &SettingChanged;
            spec.user_data = this;
            KeelConVarHandle handle = 0;
            if (convars_->create(HostContext().PluginHandle(), &spec, &handle) != KEEL_RESULT_OK)
                throw std::runtime_error("could not create core ConVar " + name);
            core_settings_.emplace(name, handle);
            auto initial = spec.default_value;
            const auto configured = settings.Value(name);
            if (numeric) initial.value.int32_value = name == "sr_show_activity" ? settings.activity : settings.debug;
            else initial.value.string_value = configured.c_str();
            if (convars_->queue_set(HostContext().PluginHandle(), handle, KEELS2_CONVAR_GLOBAL_SLOT, &initial) != KEEL_RESULT_OK)
                throw std::runtime_error("could not apply core setting " + name);
        }
    }
    static void SettingChanged(const KeelConVarChange* change, void* data) noexcept {
        auto& self = *static_cast<Source2Root*>(data);
        if (!self.foundation_ || !change || self.restoring_settings_) return;
        try {
            const auto text = change->new_value.type == KEELS2_CONVAR_STRING ? std::string(change->new_value.value.string_value) :
                std::to_string(change->new_value.value.int32_value);
            if (!self.foundation_->SetSetting(change->name, text) &&
                self.convars_->queue_set(self.HostContext().PluginHandle(), change->convar, change->slot, &change->old_value) != KEEL_RESULT_OK)
                self.Log("Could not restore rejected core setting " + std::string(change->name) + ".");
        } catch (...) { self.Log("Could not apply a core setting change."); }
    }
    void ConfigCommand(const CCommand& command) {
        if (command.ArgC() == 2) {
            Log("Source2Root Core Settings:\nUsage: sr config [setting [value]]");
            for (const auto name : sr::CoreSettings::Names)
                Log("  " + std::string(name) + " = \"" + foundation_->Settings().Value(name) + "\"");
            return;
        }
        if (command.ArgC() > 4) { Log("Usage: sr config [setting [value]]"); return; }
        const std::string name = command[2];
        const auto found = core_settings_.find(name);
        if (found == core_settings_.end()) { Log("Unknown core setting \"" + name + "\". Use sr config to list settings."); return; }
        if (command.ArgC() == 4) {
            try {
                auto candidate = foundation_->Settings();
                candidate.Set(name, command[3]);
                KeelConVarValue value{sizeof(value), KEELS2_CONVAR_STRING, {}};
                if (name == "sr_show_activity" || name == "sr_debug") {
                    value.type = KEELS2_CONVAR_INT32;
                    value.value.int32_value = name == "sr_show_activity" ? candidate.activity : candidate.debug;
                } else value.value.string_value = command[3];
                if (convars_->queue_set(HostContext().PluginHandle(), found->second, KEELS2_CONVAR_GLOBAL_SLOT, &value) != KEEL_RESULT_OK) {
                    Log("Could not set " + name + ".");
                    return;
                }
                if (foundation_->Settings().Value(name) != candidate.Value(name)) {
                    Log("Change queued for " + name + ".");
                    return;
                }
            } catch (const std::exception& error) { Log(error.what()); return; }
        }
        Log(name + " = \"" + foundation_->Settings().Value(name) + "\"");
    }
    static std::filesystem::path ModuleDirectory() {
        static int anchor;
#if defined(_WIN32)
        HMODULE module{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&anchor), &module)) throw std::runtime_error("cannot locate module");
        std::wstring path(32768, L'\0');
        auto count = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        if (!count || count == path.size()) throw std::runtime_error("cannot locate module path");
        path.resize(count);
        return std::filesystem::path(path).parent_path();
#else
        Dl_info info{};
        if (!dladdr(&anchor, &info) || !info.dli_fname) throw std::runtime_error("cannot locate module path");
        return std::filesystem::absolute(info.dli_fname).parent_path();
#endif
    }
    INetworkMessages* NetworkMessages() {
        const void* value = nullptr;
        if (HostContext().QueryService(KEELS2_SOURCE2_SERVICE_NAME, KEELS2_SOURCE2_API_VERSION, &value) != KEEL_RESULT_OK)
            return nullptr;
        const auto* api = static_cast<const KeelSource2Api*>(value);
        if (!api || api->size != sizeof(*api) || api->api_version != KEELS2_SOURCE2_API_VERSION || !api->query_named_interface)
            return nullptr;
        KeelSource2InterfaceInfo info{};
        info.size = sizeof(info);
        return api->query_named_interface(HostContext().PluginHandle(), KEELS2_SOURCE2_FACTORY_NETWORK,
            NETWORKMESSAGES_INTERFACE_VERSION, &info) == KEEL_RESULT_OK ? static_cast<INetworkMessages*>(info.instance) : nullptr;
    }
    void ScriptCommand(const CCommandContext& context, const CCommand& command) {
        if (!foundation_) return;
        const int slot = context.GetPlayerSlot().Get();
        foundation_->Dispatch(slot == -1 ? sr::Origin::ServerConsole : sr::Origin::ClientConsole,
                              slot, command.GetCommandString());
    }
    void GameEvent(IGameEvent* event) { if (foundation_ && event) foundation_->Event(event->GetName()); }
    void ExtensionCommand(const std::vector<std::string>& arguments) {
        if (arguments.empty() || arguments[0] == "help") {
            Log("Source2Root Extension Commands:\nUsage: sr extensions <command> [arguments]\n\n"
                "  list              - List extensions with registered Source2Root natives\n"
                "  info <extension>  - Show extension information\n\n"
                "Use keel plugins to load, pause, reload or unload native extensions.");
            return;
        }
        if ((arguments[0] != "list" || arguments.size() != 1) && (arguments[0] != "info" || arguments.size() != 2)) {
            Log("Usage: sr extensions list | info <extension>");
            return;
        }
        std::vector<keels2::PluginDetails> extensions;
        for (auto owner : foundation_->Extensions()) {
            keels2::PluginDetails details{};
            if (GetPlugin(owner, details)) extensions.push_back(details);
        }
        std::sort(extensions.begin(), extensions.end(), [](const auto& a, const auto& b) {
            return std::string(a.file) < b.file;
        });
        if (arguments[0] == "list") {
            Log("Source2Root Native Extensions:");
            for (const auto& extension : extensions)
                Log("  " + std::to_string(extension.handle) + " | " + extension.file + " | " + extension.name + " | " + extension.version);
            if (extensions.empty()) Log("  No registered native extensions.");
            return;
        }
        const keels2::PluginDetails* selected = nullptr;
        for (const auto& extension : extensions) {
            if (arguments[1] != extension.file && arguments[1] != extension.name && arguments[1] != std::to_string(extension.handle)) continue;
            if (selected) { Log("More than one extension matches. Use its listed ID."); return; }
            selected = &extension;
        }
        if (!selected) { Log("Unknown Source2Root extension. Use sr extensions list."); return; }
        const auto state = selected->state == KEELS2_PLUGIN_STATE_RUNNING ? "running" :
            selected->state == KEELS2_PLUGIN_STATE_PAUSED ? "paused" :
            selected->state == KEELS2_PLUGIN_STATE_LOADING ? "loading" : "unavailable";
        Log(std::string(selected->name) + " | " + state + "\nFile: " + selected->file + "\nAuthor: " + selected->author +
            "\nVersion: " + selected->version + "\n" + selected->description);
    }
    void Manage(const CCommandContext& context, const CCommand& command) {
        if (context.GetPlayerSlot().Get() != -1 || !foundation_) return;
        const auto help = [&] {
            Log("Source2Root Menu:\nUsage: sr <command> [arguments]\n\n"
                "  plugins     - Manage script plugins\n"
                "  extensions  - Manage native extensions\n"
                "  config      - Show or change core settings\n"
                "  credits     - About Source2Root\n"
                "  version     - Show version and build information\n"
                "  help        - Show command help");
        };
        if (command.ArgC() == 1) { help(); return; }
        std::string name = command[1];
        bool requested_help = name == "help";
        if (requested_help) {
            if (command.ArgC() != 3) { help(); return; }
            name = command[2];
        }
        std::vector<std::string> arguments;
        if (!requested_help) for (int i = 2; i < command.ArgC(); ++i) arguments.emplace_back(command[i]);
        if (name == "plugins") foundation_->ManagePlugins(arguments);
        else if (name == "extensions") ExtensionCommand(arguments);
        else if (name == "config") {
            if (requested_help) Log("Usage: sr config [setting [value]]\nSet startup values in csgo/cfg/source2root/source2root.cfg.");
            else ConfigCommand(command);
        } else if (name == "credits" && arguments.empty()) {
            Log("Source2Root is a scripting and administration platform powered by KeelS2.\n"
                "Created by Peter Brev. SourcePawn is developed by AlliedModders.\nhttps://keels2.com");
        } else if (name == "version" && arguments.empty()) {
            Log("Source2Root Version 1.0.0\nBuild: " __DATE__ " " __TIME__ "\nRevision: " SOURCE2ROOT_BUILD_REVISION
                "\nScript API: 2\nNative extension API: 1\nPlatform: "
#if defined(_WIN32)
                "Windows x64"
#else
                "Linux x64"
#endif
            );
        } else { Log("Unknown command or invalid arguments. Use sr help."); help(); }
    }
    void MenuCommand(const CCommandContext& context, const CCommand& command) {
        if (!foundation_ || command.ArgC() < 2 || command.ArgC() > 3) return;
        sr::Player player;
        if (Lookup(context.GetPlayerSlot().Get(), player) != KEEL_RESULT_OK) return;
        std::uint64_t session = foundation_->CurrentMenu(player);
        if (command.ArgC() == 3) {
            std::string argument = command[2];
            auto [end, error] = std::from_chars(argument.data(), argument.data() + argument.size(), session);
            if (error != std::errc{} || end != argument.data() + argument.size()) return;
        }
        const std::string value = command[1];
        std::optional<sr::MenuInput> input;
        if (value == "up") input = sr::MenuInput::Up;
        else if (value == "down") input = sr::MenuInput::Down;
        else if (value == "select") input = sr::MenuInput::Select;
        else if (value == "back") input = sr::MenuInput::Back;
        if (input) foundation_->MenuInput(player, session, *input);
    }
    template <typename Operation>
    static KeelResult Call(void* context, Operation operation, bool cleanup = false) noexcept {
        if (!context) return KEEL_RESULT_INVALID_ARGUMENT;
        auto& self = *static_cast<Source2Root*>(context);
        if (!cleanup) {
            auto thread = self.runtime_.CheckGameThread();
            if (thread != KEEL_RESULT_OK) return thread;
        }
        if (!self.foundation_) return KEEL_RESULT_NOT_READY;
        try { return operation(*self.foundation_); }
        catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
    }
    static KeelResult RegisterNative(void* context, KeelPluginHandle owner, const SrNativeSpec* spec, SrRegistration* result) {
        if (!spec || !result) return KEEL_RESULT_INVALID_ARGUMENT;
        return Call(context, [&](auto& core) { return core.RegisterNative(owner, *spec, *result); });
    }
    static KeelResult RegisterContextNative(void* context, KeelPluginHandle owner, const SrContextNativeSpec* spec, SrRegistration* result) {
        if (!spec || !result) return KEEL_RESULT_INVALID_ARGUMENT;
        return Call(context, [&](auto& core) { return core.RegisterContextNative(owner, *spec, *result); });
    }
    static KeelResult UnregisterNative(void* context, KeelPluginHandle owner, SrRegistration registration) {
        return Call(context, [&](auto& core) { return core.UnregisterNative(owner, registration); }, true);
    }
    static KeelResult DeliverCallback(void* context, KeelPluginHandle owner, SrCallback callback,
        const std::int32_t* cells, std::uint32_t count, const char* text) {
        return Call(context, [&](auto& core) { return core.DeliverCallback(owner, callback, cells, count, text); });
    }
    static KeelResult CancelCallback(void* context, KeelPluginHandle owner, SrCallback callback) {
        return Call(context, [&](auto& core) { return core.CancelCallback(owner, callback); }, true);
    }
    static KeelResult PlayerSnapshot(void* context, SrPlayerIdentity* players, std::uint32_t capacity, std::uint32_t* count) {
        return Call(context, [&](auto& core) { return core.NativePlayerSnapshot(players, capacity, count); });
    }
    static KeelResult OpenMenu(void* context, KeelPluginHandle owner, const KeelPlayerConnection* player,
        const SrMenuSpec* spec, SrMenuSession* session) {
        if (!player || !spec || !session) return KEEL_RESULT_INVALID_ARGUMENT;
        return Call(context, [&](auto& core) { return core.OpenNativeMenu(owner, *player, *spec, *session); });
    }
    static KeelResult CloseMenu(void* context, KeelPluginHandle owner, SrMenuSession session) {
        return Call(context, [&](auto& core) { return core.CloseNativeMenu(owner, session); }, true);
    }
    static KeelResult MenuStatus(void* context, KeelPluginHandle owner, SrMenuSession session) {
        return Call(context, [&](auto& core) { return core.NativeMenuStatus(owner, session); }, true);
    }
    static KeelResult ConsumerStatus(void* context, KeelPluginHandle provider, std::uint64_t owner) {
        return Call(context, [&](auto& core) { return core.NativeConsumerStatus(provider, owner); }, true);
    }
};
}

KEELS2_PLUGIN(Source2Root)
