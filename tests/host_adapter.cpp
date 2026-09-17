#include <keels2/game_adapter.hpp>
#include "host_fixture.h"
#include <array>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <map>
#include <sstream>
#include <thread>

namespace {
using namespace keels2::host;

class Adapter final : public GameAdapter {
public:
    explicit Adapter(const GameAdapterHostApi& host) : host(host) {}
    GameAdapterHostApi host;
    std::thread::id thread;
    std::map<GameCommandHandle, GameCommandSpec> commands;
    GameCommandHandle next = 1;
    struct Value {
        KeelConVarValue scalar{};
        std::string text;
        explicit Value(const KeelConVarValue& value) : scalar(value) {
            if (value.type == KEELS2_CONVAR_STRING) text = value.value.string_value;
        }
        KeelConVarValue Get() const {
            auto result = scalar;
            if (result.type == KEELS2_CONVAR_STRING) result.value.string_value = text.c_str();
            return result;
        }
    };
    struct Variable {
        std::string name, description;
        KeelConVarSpec spec;
        Value initial, current;
        GameConVarCallback callback;
        void* data;
        unsigned references = 1;
    };
    std::map<GameConVarHandle, Variable> variables;
    GameConVarHandle next_variable = 1;
    std::map<unsigned, std::pair<GameLifecycleCallback, void*>> lifecycle;
    GameSource2Callback source = nullptr;
    void* source_data = nullptr;
    KeelResult lookup = KEEL_RESULT_OK, entity_status = KEEL_RESULT_OK;
    unsigned entity_reads = 0, publications = 0;
    std::string chat;
    std::map<int, std::string> player_chat;
    bool activity_audience = false;
    int user_id = 70;
    unsigned action_count = 0, mutate_pawn = 0;
    bool player_alive = true, connected = true, immediate_disconnect = false, engine_available = true;
    bool authenticated = true, bot = false;
    bool reconnect_on_engine_query = false;
    unsigned kicks = 0;
    bool fail_restart = false;
    std::string kick_reason;
    uint32_t pawn_handle = 0x23004;
    std::uint64_t entity_epoch = 1;
    KeelResult action_status = KEEL_RESULT_OK;
    std::uint64_t input_buttons = 0, input_context = 1;
    KeelResult input_status = KEEL_RESULT_OK;
    KeelPlayerAction last_action{};
    unsigned management_caps = 7, management_count = 0;
    KeelResult management_cap_status = KEEL_RESULT_OK, management_status = KEEL_RESULT_OK;
    KeelPlayerManagementAction last_management{};
    unsigned round_caps = 1, round_count = 0;
    KeelResult round_cap_status = KEEL_RESULT_OK, round_status = KEEL_RESULT_OK;
    KeelRoundTermination last_round{};
    unsigned entity_write_caps = 1, entity_write_count = 0;
    KeelResult entity_write_status = KEEL_RESULT_OK;
    bool entity_write_callback = false;
    std::map<std::string,std::vector<std::byte>> written_fields;
    KeelResult ReadPlayer(int slot, KeelPlayerInfo& player) {
        if (lookup != KEEL_RESULT_OK) return lookup;
        if (!connected && slot == 3) return KEEL_RESULT_NOT_FOUND;
        if (slot != 3 && !(activity_audience && slot == 4)) return KEEL_RESULT_NOT_FOUND;
        player = {};
        player.size = sizeof(player); player.slot = slot; player.user_id = user_id + slot - 3;
        const bool verified = authenticated && !bot;
        player.flags = KEELS2_PLAYER_CONNECTED | (verified ? KEELS2_PLAYER_AUTHENTICATED : 0) |
            (bot ? KEELS2_PLAYER_BOT : 0) | (player_alive ? KEELS2_PLAYER_ALIVE : 0);
        player.steam_id = verified ? 76561197960265851ULL + slot - 3 : 0;
        player.controller_handle = 0x12003; player.pawn_handle = pawn_handle;
        std::strcpy(player.name, "Module fixture player");
        return KEEL_RESULT_OK;
    }
    const char* Name() const override { return "cs2"; }
    bool Start(KeelCreateInterfaceFn, KeelCreateInterfaceFn, const KeelHostCompatibilityInfo&, std::string&) override {
        thread = std::this_thread::get_id(); return true;
    }
    bool CompleteStartup(std::string&) override { return true; }
    void Stop() noexcept override { thread = {}; commands.clear(); variables.clear(); }
    bool IsGameThread() const noexcept override { return thread == std::this_thread::get_id(); }
    KeelResult QueryInterface(KeelSource2Capability capability, KeelSource2InterfaceInfo& info) const noexcept override {
        if (capability == KEELS2_SOURCE2_CAPABILITY_CVAR) {
            info = {sizeof(info), capability, KEELS2_SOURCE2_FACTORY_ENGINE, KEELS2_SOURCE2_OWNERSHIP_BORROWED,
                KEELS2_SOURCE2_LIFETIME_HOST, 0, SrFixtureCvar(), "VEngineCvar007",
                "sr_host_adapter", "headless", "source2root-headless-fixture"};
            return KEEL_RESULT_OK;
        }
        if (capability == KEELS2_SOURCE2_CAPABILITY_GAME_EVENT_MANAGER) {
            auto* manager = SrNetworkGameEventManager();
            if (!manager) return KEEL_RESULT_NOT_READY;
            info = {sizeof(info), capability, KEELS2_SOURCE2_FACTORY_NONE, KEELS2_SOURCE2_OWNERSHIP_BORROWED,
                KEELS2_SOURCE2_LIFETIME_HOST, 0, manager, "IGameEventManager2",
                "sr_host_adapter", "headless", "source2root-headless-fixture"};
            return KEEL_RESULT_OK;
        }
        if (capability != KEELS2_SOURCE2_CAPABILITY_SERVER) return KEEL_RESULT_NOT_FOUND;
        info = {sizeof(info), capability, KEELS2_SOURCE2_FACTORY_SERVER, KEELS2_SOURCE2_OWNERSHIP_BORROWED,
            KEELS2_SOURCE2_LIFETIME_HOST, 0, const_cast<Adapter*>(this), "HeadlessServerMetadata001",
            "sr_host_adapter", "headless", "source2root-headless-fixture"};
        return KEEL_RESULT_OK;
    }
    KeelResult QueryNamedInterface(KeelSource2Factory, const char*, KeelSource2InterfaceInfo&) override { return KEEL_RESULT_NOT_FOUND; }
    std::vector<GameInterfaceSnapshot> InterfaceSnapshots() const override { return {}; }
    KeelResult EnableLifecycleEvent(KeelLifecycleEventType event, const KeelHookApi&, KeelPluginHandle,
        GameLifecycleCallback callback, void* data, std::string&) override {
        lifecycle[event] = {callback, data}; return KEEL_RESULT_OK;
    }
    KeelResult InitializeSource2Callbacks(const KeelHookApi&, KeelPluginHandle, GameSource2Callback callback,
        void* data, std::string&) override { source = callback; source_data = data; return KEEL_RESULT_OK; }
    void ShutdownSource2Callbacks() noexcept override { source = nullptr; }
    KeelResult ListenForGameEvent(const char*, std::string&) override { return KEEL_RESULT_OK; }
    bool RegisterCommand(const GameCommandSpec& spec, GameCommandHandle& result, std::string&) override {
        result = next++; commands[result] = spec; return true;
    }
    void UnregisterCommand(GameCommandHandle handle) noexcept override { commands.erase(handle); }
    KeelResult CreateConVar(const KeelConVarSpec& spec, GameConVarCallback callback, GameNativeConVarCallback native, void* data,
        GameConVarHandle& handle, void** native_value, std::string&) override {
        if (native || (spec.type != KEELS2_CONVAR_INT32 && spec.type != KEELS2_CONVAR_FLOAT32 &&
            spec.type != KEELS2_CONVAR_STRING)) return KEEL_RESULT_UNSUPPORTED;
        for (const auto& [id, variable] : variables) if (variable.name == spec.name) return KEEL_RESULT_ALREADY_EXISTS;
        handle = next_variable++;
        variables.emplace(handle, Variable{spec.name, spec.description, spec, Value(spec.default_value),
            Value(spec.default_value), callback, data});
        if (native_value) *native_value = nullptr;
        return KEEL_RESULT_OK;
    }
    KeelResult FindConVar(const char* name, KeelConVarType type, GameConVarHandle& handle, void** native, std::string&) override {
        for (auto& [id, variable] : variables) if (variable.name == name && variable.spec.type == type) {
            ++variable.references;
            handle = id;
            if (native) *native = nullptr;
            return KEEL_RESULT_OK;
        }
        return KEEL_RESULT_NOT_FOUND;
    }
    void ReleaseConVar(GameConVarHandle handle) noexcept override {
        const auto found = variables.find(handle);
        if (found != variables.end() && !--found->second.references) variables.erase(found);
    }
    KeelResult ReadConVar(GameConVarHandle handle, int32_t slot, KeelConVarValue& value) const noexcept override {
        const auto found = variables.find(handle);
        if (found == variables.end()) return KEEL_RESULT_NOT_FOUND;
        if (slot != KEELS2_CONVAR_GLOBAL_SLOT) return KEEL_RESULT_INVALID_ARGUMENT;
        value = found->second.current.Get();
        return KEEL_RESULT_OK;
    }
    KeelResult QueueConVarSet(GameConVarHandle handle, int32_t slot, const KeelConVarValue& value) noexcept override {
        try {
            const auto found = variables.find(handle);
            if (found == variables.end()) return KEEL_RESULT_NOT_FOUND;
            auto& variable = found->second;
            if (variable.name == "mp_restartgame" && fail_restart) return KEEL_RESULT_ENGINE_FAILURE;
            if (slot != KEELS2_CONVAR_GLOBAL_SLOT || value.type != variable.spec.type) return KEEL_RESULT_INVALID_ARGUMENT;
            auto adjusted = value;
            if (value.type == KEELS2_CONVAR_INT32) {
                if (variable.spec.has_minimum) adjusted.value.int32_value = std::max(adjusted.value.int32_value, variable.spec.minimum_value.value.int32_value);
                if (variable.spec.has_maximum) adjusted.value.int32_value = std::min(adjusted.value.int32_value, variable.spec.maximum_value.value.int32_value);
            } else if (value.type == KEELS2_CONVAR_FLOAT32) {
                if (!std::isfinite(value.value.float32_value)) return KEEL_RESULT_INVALID_ARGUMENT;
                if (variable.spec.has_minimum) adjusted.value.float32_value = std::max(adjusted.value.float32_value, variable.spec.minimum_value.value.float32_value);
                if (variable.spec.has_maximum) adjusted.value.float32_value = std::min(adjusted.value.float32_value, variable.spec.maximum_value.value.float32_value);
            }
            const auto previous = variable.current;
            variable.current = Value(adjusted);
            const auto current = variable.current;
            if (variable.callback) variable.callback(slot, current.Get(), previous.Get(), variable.data);
            return KEEL_RESULT_OK;
        } catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
    }
    KeelResult DescribeConVar(GameConVarHandle handle, KeelConVarInfo& info) const noexcept override {
        const auto found = variables.find(handle);
        if (found == variables.end()) return KEEL_RESULT_NOT_FOUND;
        const auto& variable = found->second;
        info = {sizeof(info), variable.spec.type, variable.name.c_str(), variable.description.c_str(), variable.spec.flags,
            variable.initial.Get(), variable.spec.has_minimum, 0, variable.spec.minimum_value,
            variable.spec.has_maximum, 0, variable.spec.maximum_value};
        return KEEL_RESULT_OK;
    }
    bool SetVariable(const char* name, const char* text) {
        for (const auto& [id, variable] : variables) if (variable.name == name) {
            KeelConVarValue value{sizeof(value), variable.spec.type, {}};
            if (value.type == KEELS2_CONVAR_STRING) value.value.string_value = text;
            else if (value.type == KEELS2_CONVAR_FLOAT32) {
                const auto end = text + std::strlen(text);
                const auto parsed = std::from_chars(text, end, value.value.float32_value);
                if (parsed.ec != std::errc{} || parsed.ptr != end) return false;
            }
            else {
                const auto end = text + std::strlen(text);
                const auto parsed = std::from_chars(text, end, value.value.int32_value);
                if (parsed.ec != std::errc{} || parsed.ptr != end) return false;
            }
            return QueueConVarSet(id, KEELS2_CONVAR_GLOBAL_SLOT, value) == KEEL_RESULT_OK;
        }
        return false;
    }
    KeelResult ResolveSchemaField(const KeelSchemaFieldSpec& spec, GameSchemaField& field, std::string&) override {
        unsigned type = 0, size = 0;
        const std::string classname = spec.class_name, name = spec.field_name;
        if (classname == "CBaseEntity" && name == "m_iHealth") { type = KEELS2_SCHEMA_INT32; size = 4; }
        if (classname == "CCSPlayerController" && (name == "m_iScore" || name == "m_iMVPs")) {
            type = KEELS2_SCHEMA_INT32; size = 4;
        }
        if (classname == "CTestEntity") {
            if (name == "m_uWide") { type = KEELS2_SCHEMA_UINT64; size = 8; }
            if (name == "m_vecOrigin") { type = KEELS2_SCHEMA_VECTOR3; size = 12; }
            if (name == "m_hOther") { type = KEELS2_SCHEMA_ENTITY_HANDLE; size = 4; }
            if (name == "m_fValue") { type = KEELS2_SCHEMA_FLOAT32; size = 4; }
            if (name == "m_bFlag") { type = KEELS2_SCHEMA_BOOL; size = 1; }
        }
        if (!type) return KEEL_RESULT_NOT_FOUND;
        if (spec.value_type != type) return KEEL_RESULT_INCOMPATIBLE;
        field.declaring_class = this; field.offset = name == "m_iHealth" ? 12 : 16;
        field.value_size = size; field.value_alignment = size == 12 ? 4 : size;
        field.module = KEELS2_SCHEMA_MODULE_SERVER; field.value_type = type;
        field.class_name = spec.class_name; field.field_name = spec.field_name;
        field.module_name = "server"; field.compatibility_profile = "source2root-headless-fixture";
        return KEEL_RESULT_OK;
    }
    KeelResult FindEntityByIndex(int32_t index, GameEntityIdentity& entity, std::string& error) override {
        return FindEntityBySource2Handle(index == 3 ? 0x12003 : index == 4 ? 0x23004 : index == 5 ? 0x45005 : 0, entity, error);
    }
    KeelResult FindEntityBySource2Handle(uint32_t handle, GameEntityIdentity& entity, std::string&) override {
        if (handle != 0x23004 && handle != 0x12003 && handle != 0x45005) return KEEL_RESULT_NOT_FOUND;
        entity = {handle == 0x12003 ? 3 : handle == 0x23004 ? 4 : 5, handle, entity_epoch};
        if (mutate_pawn == 1) pawn_handle = 0x24004;
        if (mutate_pawn == 2) ++user_id;
        if (mutate_pawn == 3) player_alive = false;
        mutate_pawn = 0;
        return KEEL_RESULT_OK;
    }
    KeelResult ValidateEntity(const GameEntityIdentity& entity, std::string&) override {
        if ((entity.source2_handle != 0x23004 && entity.source2_handle != 0x12003 && entity.source2_handle != 0x45005) ||
            entity.epoch != entity_epoch) return KEEL_RESULT_NOT_FOUND;
        return entity_status;
    }
    KeelResult ReadEntityField(const GameEntityIdentity& entity, const GameSchemaField& field, void* value, uint32_t size, std::string& error) override {
        const auto status = ValidateEntity(entity, error);
        if (status != KEEL_RESULT_OK) return status;
        if (size != field.value_size) return KEEL_RESULT_INVALID_ARGUMENT;
        if (field.class_name == "CCSPlayerController" && entity.source2_handle != 0x12003) return KEEL_RESULT_INCOMPATIBLE;
        const auto saved = written_fields.find(WriteKey(entity,field));
        if (saved != written_fields.end()) {
            if (saved->second.size() != size) return KEEL_RESULT_INCOMPATIBLE;
            std::memcpy(value,saved->second.data(),size); ++entity_reads; return KEEL_RESULT_OK;
        }
        if (field.field_name == "m_iHealth") { const int32_t data = 73; std::memcpy(value, &data, sizeof(data)); }
        else if (field.field_name == "m_iScore") { const int32_t data = 12; std::memcpy(value, &data, sizeof(data)); }
        else if (field.field_name == "m_iMVPs") { const int32_t data = 2; std::memcpy(value, &data, sizeof(data)); }
        else if (field.field_name == "m_uWide") { const std::uint64_t data = UINT64_MAX; std::memcpy(value, &data, sizeof(data)); }
        else if (field.field_name == "m_vecOrigin") { const float data[]{1, 2, 3}; std::memcpy(value, data, sizeof(data)); }
        else if (field.field_name == "m_hOther") { const std::uint32_t data = 0x45005; std::memcpy(value, &data, sizeof(data)); }
        else if (field.field_name == "m_fValue") { const float data = 1.25f; std::memcpy(value, &data, sizeof(data)); }
        else if (field.field_name == "m_bFlag") { const std::uint8_t data = 1; std::memcpy(value, &data, sizeof(data)); }
        else return KEEL_RESULT_INVALID_ARGUMENT;
        ++entity_reads;
        return KEEL_RESULT_OK;
    }
    static std::string WriteKey(const GameEntityIdentity& entity, const GameSchemaField& field) {
        return std::to_string(entity.epoch) + ":" + std::to_string(entity.source2_handle) + ":" + field.class_name + "." + field.field_name;
    }
    KeelResult WriteEntityField(const GameEntityIdentity& entity, const GameSchemaField& field, const void* value, unsigned size) {
        std::string error;
        const auto valid = ValidateEntity(entity,error);
        if (valid != KEEL_RESULT_OK) return valid;
        if (!value || !size || size > 12 || size != field.value_size) return KEEL_RESULT_INVALID_ARGUMENT;
        if (field.class_name == "CCSPlayerController" && entity.source2_handle != 0x12003) return KEEL_RESULT_INCOMPATIBLE;
        if (field.value_type == KEELS2_SCHEMA_ENTITY_HANDLE || !(entity_write_caps & 1)) return KEEL_RESULT_UNSUPPORTED;
        if (entity_write_status != KEEL_RESULT_OK) return entity_write_status;
        const auto* bytes = static_cast<const std::byte*>(value);
        written_fields[WriteKey(entity,field)] = {bytes,bytes + size}; ++entity_write_count;
        if (entity_write_callback) {
            entity_write_callback = false;
            if (!Dispatch("sr_sdk_close_active",-1)) return KEEL_RESULT_ENGINE_FAILURE;
        }
        return KEEL_RESULT_OK;
    }
    bool Dispatch(const char* text, int slot) {
        std::istringstream parser(text);
        std::string name;
        parser >> name;
        for (const auto& [handle, entry] : commands) if (entry.name == name) {
            struct DispatchState { Adapter& adapter; GameCommandSpec spec; bool invoked = false; } state{*this, entry};
            SrFixtureWithCommand(text, slot, [](void* data, const void* context, const void* command,
                                               uint32_t count, const char* const* arguments) {
                auto& state = *static_cast<DispatchState*>(data);
                if (!state.adapter.host.begin_command_dispatch()) return;
                const GameCommandInvocation invocation{count, arguments, context, command};
                state.spec.callback(invocation, state.spec.user_data);
                state.adapter.host.end_command_dispatch();
                state.invoked = true;
            }, &state);
            return state.invoked;
        }
        return false;
    }
    bool Chat(const char* text, int slot, const char* verb = "say") {
        const char* arguments[] = {verb, text};
        const bool allowed = SrFixtureDispatchConCommand(2, arguments, slot);
        if (allowed) ++publications;
        return allowed;
    }
    void Frame() {
        auto found = lifecycle.find(KEELS2_LIFECYCLE_GAME_FRAME);
        if (found == lifecycle.end()) return;
        const KeelLifecycleGameFrame frame{sizeof(frame), KEEL_TRUE, KEEL_TRUE, KEEL_TRUE};
        const KeelLifecycleEvent event{sizeof(event), KEELS2_LIFECYCLE_GAME_FRAME, sizeof(frame), 0, &frame};
        found->second.first(event, found->second.second);
    }
};
Adapter* active = nullptr;
GameAdapter* Create(const GameAdapterHostApi* host) { active = new Adapter(*host); return active; }
void Destroy(GameAdapter* adapter) { delete adapter; active = nullptr; }
}

extern "C" KEELS2_GAME_ADAPTER_EXPORT uint32_t KeelGameAdapter_Query(uint32_t version, keels2::host::GameAdapterProvider* provider) {
    if (version != 1 || !provider || provider->size != sizeof(*provider)) return 0;
#if defined(_WIN32)
    const char* platform = "win64";
#else
    const char* platform = "linuxsteamrt64";
#endif
    *provider = {sizeof(*provider), 1, "cs2", platform, &Create, &Destroy};
    return 1;
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT KeelResult KeelGameAdapter_CommandCaller(const void* context, int32_t* slot) noexcept {
    if (!context || !slot) return KEEL_RESULT_INVALID_ARGUMENT;
    *slot = SrFixtureCaller(context);
    return KEEL_RESULT_OK;
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT void* SrFixtureFactory(const char* name, int* status) {
    if (auto* network = SrNetworkInterface(name)) { if (status) *status = 0; return network; }
    if (active && active->engine_available && name && std::strcmp(name, "Source2EngineToServer001") == 0) {
        if (active->reconnect_on_engine_query) { ++active->user_id; active->reconnect_on_engine_query = false; }
        if (status) *status = 0;
        return SrFixtureEngine([](int slot, unsigned code, const char* reason, void* data) {
            auto& state = *static_cast<Adapter*>(data);
            if (slot != 3 || !reason) std::abort();
            ++state.kicks; state.kick_reason = reason;
            if (state.immediate_disconnect) state.connected = false;
        }, active);
    }
    volatile unsigned length = 0;
    if (name) for (auto p = name; *p; ++p) length = length + 1;
    if (status) *status = static_cast<int>(length + 1);
    return nullptr;
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT bool SrFixtureCommand(const char* text, int slot) { return active && active->Dispatch(text, slot); }
extern "C" KEELS2_GAME_ADAPTER_EXPORT void SrFixtureFrame() { if (active) active->Frame(); }
extern "C" KEELS2_GAME_ADAPTER_EXPORT bool SrFixtureNetworkInitialize(const char* path) { return SrNetworkInitialize(path); }
extern "C" KEELS2_GAME_ADAPTER_EXPORT const char* SrFixtureMenuText() { return SrNetworkMenuText(); }
extern "C" KEELS2_GAME_ADAPTER_EXPORT bool SrFixtureNetworkStop() { return SrNetworkStop(); }
extern "C" KEELS2_GAME_ADAPTER_EXPORT void SrFixtureInput(std::uint64_t buttons, std::uint64_t context, KeelResult result) {
    if (active) { active->input_buttons = buttons; active->input_context = context; active->input_status = result; }
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT unsigned SrFixtureCommands() { return active ? static_cast<unsigned>(active->commands.size()) : 0; }
extern "C" KEELS2_GAME_ADAPTER_EXPORT unsigned SrFixtureConVars() {
    unsigned count = 0;
    if (active) for (const auto& [id, variable] : active->variables) count += variable.name != "mp_restartgame";
    return count;
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT bool SrFixtureConVarEquals(const char* name, const char* expected) {
    if (!active) return false;
    for (const auto& [id, variable] : active->variables) if (variable.name == name) {
        const auto value = variable.current.Get();
        if (value.type == KEELS2_CONVAR_STRING) return std::string(value.value.string_value) == expected;
        if (value.type == KEELS2_CONVAR_INT32) return std::to_string(value.value.int32_value) == expected;
        return false;
    }
    return false;
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT bool SrFixtureSetConVar(const char* name, const char* value) {
    return active && name && value && active->SetVariable(name, value);
}

extern "C" KEELS2_GAME_ADAPTER_EXPORT KeelResult KeelGameAdapter_QueryEntityWrites(
    unsigned version, keels2::host::GameAdapterEntityWritesApi* api) noexcept {
    if (!api || api->size != sizeof(*api)) return KEEL_RESULT_INVALID_ARGUMENT;
    *api = {};
    if (version != 1) return KEEL_RESULT_INCOMPATIBLE;
    *api = {sizeof(*api),1,
        [](keels2::host::GameAdapter* adapter, unsigned* capabilities) noexcept -> KeelResult {
            if (capabilities) *capabilities = 0;
            if (!adapter || !capabilities) return KEEL_RESULT_INVALID_ARGUMENT;
            *capabilities = static_cast<Adapter*>(adapter)->entity_write_caps; return KEEL_RESULT_OK;
        },
        [](keels2::host::GameAdapter* adapter, const GameEntityIdentity* entity, const GameSchemaField* field,
            const void* value, unsigned size) noexcept -> KeelResult {
            if (!adapter || !entity || !field) return KEEL_RESULT_INVALID_ARGUMENT;
            try { return static_cast<Adapter*>(adapter)->WriteEntityField(*entity,*field,value,size); }
            catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
        }};
    return KEEL_RESULT_OK;
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT void SrFixtureWriteState(unsigned capabilities, unsigned status, bool callback) {
    if (active) { active->entity_write_caps = capabilities; active->entity_write_status = status; active->entity_write_callback = callback; }
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT unsigned SrFixtureWriteCount() { return active ? active->entity_write_count : 0; }
extern "C" KEELS2_GAME_ADAPTER_EXPORT KeelResult KeelGameAdapter_QueryPlayerManagement(
    uint32_t version, keels2::host::GameAdapterPlayerManagementApi* api) noexcept {
    if (!api || api->size != sizeof(*api)) return KEEL_RESULT_INVALID_ARGUMENT;
    *api = {};
    if (version != 1) return KEEL_RESULT_INCOMPATIBLE;
    *api = {sizeof(*api), 1,
        [](keels2::host::GameAdapter* adapter, unsigned* capabilities) noexcept -> KeelResult {
            if (capabilities) *capabilities = 0;
            if (!adapter || !capabilities) return KEEL_RESULT_INVALID_ARGUMENT;
            const auto* state = static_cast<Adapter*>(adapter);
            if (state->management_cap_status != KEEL_RESULT_OK) return state->management_cap_status;
            *capabilities = state->management_caps; return KEEL_RESULT_OK;
        },
        [](keels2::host::GameAdapter* adapter, const keels2::host::GameEntityIdentity* controller,
            const KeelPlayerManagementAction* action) noexcept -> KeelResult {
            if (!adapter || !controller || !action) return KEEL_RESULT_INVALID_ARGUMENT;
            auto* state = static_cast<Adapter*>(adapter);
            try {
                std::string error;
                const auto valid = state->ValidateEntity(*controller, error);
                if (valid != KEEL_RESULT_OK) return valid;
                if (controller->source2_handle != 0x12003) return KEEL_RESULT_INCOMPATIBLE;
                if (state->management_status != KEEL_RESULT_OK) return state->management_status;
                ++state->management_count; state->last_management = *action;
                return KEEL_RESULT_OK;
            } catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
        }};
    return KEEL_RESULT_OK;
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT unsigned SrFixtureManagementCount(KeelPlayerManagementAction* last) {
    if (!active) return 0;
    if (last) *last = active->last_management;
    return active->management_count;
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT void SrFixtureManagementState(unsigned caps, unsigned capability_result, unsigned action_result) {
    if (active) { active->management_caps = caps; active->management_cap_status = capability_result; active->management_status = action_result; }
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT KeelResult KeelGameAdapter_QueryPlayers(uint32_t version, keels2::host::GameAdapterPlayersApi* api) noexcept {
    if (version != 1 || !api || api->size != sizeof(*api)) return KEEL_RESULT_INCOMPATIBLE;
    *api = {sizeof(*api), 1, []() noexcept -> uint32_t { return 64; },
        [](keels2::host::GameAdapter* adapter, int32_t slot, KeelPlayerInfo* player) noexcept -> KeelResult {
            return player ? static_cast<Adapter*>(adapter)->ReadPlayer(slot, *player) : KEEL_RESULT_INVALID_ARGUMENT;
        }};
    return KEEL_RESULT_OK;
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT KeelResult KeelGameAdapter_QueryPlayerInput(uint32_t version, keels2::host::GameAdapterPlayerInputApi* api) noexcept {
    if (version != 1 || !api || api->size != sizeof(*api)) return KEEL_RESULT_INCOMPATIBLE;
    *api = {sizeof(*api), 1, [](keels2::host::GameAdapter* adapter, int32_t slot, uint32_t controller,
        std::uint64_t* buttons, std::uint64_t* context) noexcept -> KeelResult {
        auto* state = static_cast<Adapter*>(adapter);
        if (!state || !buttons || !context || slot != 3 || controller != 0x12003) return KEEL_RESULT_INVALID_ARGUMENT;
        *buttons = state->input_buttons; *context = state->input_context;
        return state->input_status;
    }};
    return KEEL_RESULT_OK;
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT KeelResult KeelGameAdapter_QueryMessaging(uint32_t version, keels2::host::GameAdapterMessagingApi* api) noexcept {
    if (version != 1 || !api || api->size != sizeof(*api)) return KEEL_RESULT_INCOMPATIBLE;
    *api = {sizeof(*api), 1, [](keels2::host::GameAdapter* adapter, int32_t slot, KeelBool broadcast, const char* text) noexcept -> KeelResult {
        auto* state = static_cast<Adapter*>(adapter);
        if (!text || (slot != 3 && !(state->activity_audience && slot == 4)) || broadcast) return KEEL_RESULT_INVALID_ARGUMENT;
        try {
            state->chat += std::string(text) + "\n";
            state->player_chat[slot] += std::string(text) + "\n";
            return KEEL_RESULT_OK;
        }
        catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
    }};
    return KEEL_RESULT_OK;
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT const char* SrFixtureChatOutput() { return active ? active->chat.c_str() : ""; }
extern "C" KEELS2_GAME_ADAPTER_EXPORT void SrFixtureActivityAudience(bool enabled) { if (active) active->activity_audience = enabled; }
extern "C" KEELS2_GAME_ADAPTER_EXPORT const char* SrFixturePlayerChat(int slot) {
    return active && active->player_chat.contains(slot) ? active->player_chat.at(slot).c_str() : "";
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT void SrFixtureEntityError(unsigned result) { if (active) active->entity_status = result; }
extern "C" KEELS2_GAME_ADAPTER_EXPORT unsigned SrFixtureEntityReads() { return active ? active->entity_reads : 0; }

extern "C" KEELS2_GAME_ADAPTER_EXPORT bool SrFixtureChat(const char* text, int slot) { return active && active->Chat(text, slot); }
extern "C" KEELS2_GAME_ADAPTER_EXPORT bool SrFixtureChatCommand(const char* verb, const char* text, int slot) {
    return active && active->Chat(text, slot, verb);
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT void SrFixtureReconnect() { if (active) ++active->user_id; }
extern "C" KEELS2_GAME_ADAPTER_EXPORT void SrFixtureAuthentication(bool authenticated, bool bot) {
    if (active) { active->authenticated = authenticated; active->bot = bot; }
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT void SrFixturePlayerLookup(KeelResult result) { if (active) active->lookup = result; }

extern "C" KEELS2_GAME_ADAPTER_EXPORT KeelResult KeelGameAdapter_PlayerAction(
    keels2::host::GameAdapter* adapter, const keels2::host::GameEntityIdentity* pawn,
    const KeelPlayerAction* action) noexcept {
    auto* state = static_cast<Adapter*>(adapter);
    if (!state || !pawn || !action || action->size != sizeof(*action)) return KEEL_RESULT_INVALID_ARGUMENT;
    std::string error;
    const auto valid = state->ValidateEntity(*pawn, error);
    if (valid != KEEL_RESULT_OK) return valid;
    if (state->action_status != KEEL_RESULT_OK) return state->action_status;
    ++state->action_count;
    state->last_action = *action;
    return KEEL_RESULT_OK;
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT unsigned SrFixturePlayerActions(KeelPlayerAction* last) {
    if (!active) return 0;
    if (last) *last = active->last_action;
    return active->action_count;
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT void SrFixtureActionState(unsigned status, unsigned mutate, bool alive) {
    if (!active) return;
    active->action_status = status;
    active->mutate_pawn = mutate;
    active->player_alive = alive;
    active->pawn_handle = 0x23004;
}

extern "C" KEELS2_GAME_ADAPTER_EXPORT void SrFixtureKickState(bool available, bool immediate, bool reconnect) {
    if (!active) return;
    active->engine_available = available;
    active->immediate_disconnect = immediate;
    active->reconnect_on_engine_query = reconnect;
    active->connected = true;
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT unsigned SrFixtureKicks() { return active ? active->kicks : 0; }
extern "C" KEELS2_GAME_ADAPTER_EXPORT void SrFixtureEntityEpoch() { if (active) ++active->entity_epoch; }
extern "C" KEELS2_GAME_ADAPTER_EXPORT const char* SrFixtureKickReason() { return active ? active->kick_reason.c_str() : ""; }

extern "C" KEELS2_GAME_ADAPTER_EXPORT bool SrFixtureReadListening(int receiver, int sender) { return SrEngineReadListening(receiver, sender); }
extern "C" KEELS2_GAME_ADAPTER_EXPORT bool SrFixtureWriteListening(int receiver, int sender, bool value) {
    int status = 0;
    auto* instance = SrFixtureFactory("Source2EngineToServer001", &status);
    return instance && status == 0 && SrEngineWriteListening(instance, receiver, sender, value);
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT void SrFixtureFailListening(bool fail) { SrEngineFailListening(fail); }
extern "C" KEELS2_GAME_ADAPTER_EXPORT unsigned SrFixtureListeningCalls() { return SrEngineListeningCalls(); }

extern "C" KEELS2_GAME_ADAPTER_EXPORT unsigned SrFixtureMapChanges() { return SrEngineMapChanges(); }
extern "C" KEELS2_GAME_ADAPTER_EXPORT const char* SrFixtureChangedMap() { return SrEngineChangedMap(); }
extern "C" KEELS2_GAME_ADAPTER_EXPORT void SrFixtureRestartVariable(unsigned type, bool fail) {
    if (!active) return;
    active->fail_restart = fail;
    for (auto it = active->variables.begin(); it != active->variables.end(); ++it) if (it->second.name == "mp_restartgame") {
        if (it->second.spec.type == type) return;
        active->variables.erase(it); break;
    }
    if (!type) return;
    KeelConVarSpec spec{};
    spec.size = sizeof(spec); spec.name = "mp_restartgame"; spec.description = "Engine round restart"; spec.type = type;
    spec.default_value = {sizeof(KeelConVarValue), type, {}};
    if (type == KEELS2_CONVAR_STRING) spec.default_value.value.string_value = "0";
    GameConVarHandle id = 0; std::string error;
    if (active->CreateConVar(spec, nullptr, nullptr, nullptr, id, nullptr, error) != KEEL_RESULT_OK) std::abort();
}

extern "C" KEELS2_GAME_ADAPTER_EXPORT KeelResult KeelGameAdapter_QueryRoundControl(
    unsigned version, keels2::host::GameAdapterRoundControlApi* api) noexcept {
    if (!api || api->size != sizeof(*api)) return KEEL_RESULT_INVALID_ARGUMENT;
    *api = {};
    if (version != keels2::host::kGameAdapterRoundControlVersion) return KEEL_RESULT_INCOMPATIBLE;
    api->size = sizeof(*api); api->api_version = version;
    api->capabilities = [](keels2::host::GameAdapter* base, unsigned* out) noexcept {
        if (!base || !out) return KEEL_RESULT_INVALID_ARGUMENT;
        auto& adapter = *static_cast<Adapter*>(base); *out = adapter.round_caps; return adapter.round_cap_status;
    };
    api->terminate = [](keels2::host::GameAdapter* base, const KeelRoundTermination* request) noexcept {
        if (!base || !request || request->size != sizeof(*request) || request->reserved) return KEEL_RESULT_INVALID_ARGUMENT;
        auto& adapter = *static_cast<Adapter*>(base);
        if (adapter.round_status != KEEL_RESULT_OK) return adapter.round_status;
        if (!(adapter.round_caps & 1)) return KEEL_RESULT_UNSUPPORTED;
        adapter.last_round = *request; ++adapter.round_count; return KEEL_RESULT_OK;
    };
    return KEEL_RESULT_OK;
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT unsigned SrFixtureRoundCount(KeelRoundTermination* last) {
    if (!active) return 0;
    if (last) *last = active->last_round;
    return active->round_count;
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT void SrFixtureRoundState(unsigned caps, unsigned capability_result, unsigned result) {
    if (!active) return;
    active->round_caps = caps; active->round_cap_status = static_cast<KeelResult>(capability_result);
    active->round_status = static_cast<KeelResult>(result);
}
