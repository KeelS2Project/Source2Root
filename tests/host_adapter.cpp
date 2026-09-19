#include <keels2/game_adapter.hpp>
#include "host_fixture.h"
#include <array>
#include <atomic>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

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
    std::int32_t entity_objects[3]{31,47,63};
    struct Created {
        GameEntityIdentity identity; int object{91};
        bool pending{true}, owned{true}, busy{}, closed{};
        struct Key { KeelEntityKeyValue value; std::string text; };
        std::map<std::string,Key> keys;
        KeelEntityTeleport teleport{};
    };
    std::map<std::uint32_t,std::shared_ptr<Created>> created;
    std::uint32_t next_created{16};
    unsigned construction_mode{}, construction_creates{}, construction_cancels{}, construction_spawns{};
    bool construction_data_valid{};
    unsigned entity_input_mode{}, entity_input_calls{}, entity_input_direct{}, entity_input_queued{};
    bool entity_input_valid{true};
    std::vector<std::string> queued_input_names, queued_input_texts;
    std::shared_ptr<Created> Pending(std::uint64_t token) {
        if (token > UINT32_MAX) return {};
        const auto it = created.find(static_cast<std::uint32_t>(token));
        if (it == created.end() || it->second->identity.epoch != entity_epoch || !it->second->pending ||
            !it->second->owned || it->second->closed) return {};
        return it->second;
    }
    unsigned entity_call_count = 0;
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
    unsigned stat_read_caps = 15, stat_write_caps = 15, stat_reads = 0, stat_writes = 0, stat_mutation = 0;
    KeelResult stat_cap_status = KEEL_RESULT_OK, stat_read_status = KEEL_RESULT_OK, stat_write_status = KEEL_RESULT_OK;
    std::array<std::int32_t,4> statistics{800,7,3,2};
    KeelResult AccessStatistic(const GameEntityIdentity& controller, unsigned key, std::int32_t& value, bool write) {
        std::string error;
        const auto valid = ValidateEntity(controller,error);
        if (valid != KEEL_RESULT_OK) return valid;
        if (controller.source2_handle != 0x12003) return KEEL_RESULT_INCOMPATIBLE;
        std::size_t index;
        switch (key) {
            case KEELS2_PLAYER_STAT_MONEY: index = 0; break;
            case KEELS2_PLAYER_STAT_MATCH_KILLS: index = 1; break;
            case KEELS2_PLAYER_STAT_MATCH_DEATHS: index = 2; break;
            case KEELS2_PLAYER_STAT_MATCH_ASSISTS: index = 3; break;
            default: return KEEL_RESULT_INVALID_ARGUMENT;
        }
        if (!((write ? stat_write_caps : stat_read_caps) & key)) return KEEL_RESULT_UNSUPPORTED;
        if (write) {
            if (value < 0) return KEEL_RESULT_INVALID_ARGUMENT;
            ++stat_writes; statistics[index] = value;
            return stat_write_status;
        }
        ++stat_reads; value = statistics[index];
        if (stat_mutation == 1) ++entity_epoch;
        if (stat_mutation == 2) ++user_id;
        return stat_read_status;
    }
    unsigned entity_write_caps = 1, entity_write_count = 0;
    KeelResult entity_write_status = KEEL_RESULT_OK;
    bool entity_write_callback = false;
    unsigned tool_caps = 7, tool_calls = 0, tool_mode = 0;
    KeelResult tool_cap_status = KEEL_RESULT_OK, tool_status = KEEL_RESULT_OK;
    KeelEntityTeleport tool_teleport{};
    std::string tool_model;
    std::set<std::uint32_t> removed_entities;
    KeelResult ApplyTool(const GameEntityIdentity& entity, unsigned kind, const KeelEntityTeleport* request, const char* model) {
        std::string error;
        const auto valid = ValidateEntity(entity,error); if (valid != KEEL_RESULT_OK) return valid;
        if (!(tool_caps & kind)) return KEEL_RESULT_UNSUPPORTED;
        ++tool_calls;
        if (tool_mode == 1) {
            tool_mode = 0;
            if (!Dispatch("sr_sdk_tool_close",-1) || !Dispatch("keel plugins unload 2",-1)) return KEEL_RESULT_ENGINE_FAILURE;
        } else if (tool_mode == 2) {
            if (!Dispatch("sr_sdk_tool_reenter",-1)) return KEEL_RESULT_ENGINE_FAILURE;
        }
        // Read after callbacks to detect borrowed script buffers or freed owners.
        if (kind == KEELS2_ENTITY_TOOL_TELEPORT && request) tool_teleport = *request;
        else if (kind == KEELS2_ENTITY_TOOL_SET_MODEL && model) tool_model = model;
        else if (kind == KEELS2_ENTITY_TOOL_REMOVE) removed_entities.insert(entity.source2_handle);
        else return KEEL_RESULT_INVALID_ARGUMENT;
        return tool_status;
    }
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
    void Stop() noexcept override { thread = {}; commands.clear(); variables.clear(); created.clear(); }
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
        for (const auto& [source,record] : created) if (record->identity.index == index && !record->pending)
            return FindEntityBySource2Handle(source,entity,error);
        return FindEntityBySource2Handle(index == 3 ? 0x12003 : index == 4 ? 0x23004 : index == 5 ? 0x45005 : 0, entity, error);
    }
    KeelResult FindEntityBySource2Handle(uint32_t handle, GameEntityIdentity& entity, std::string&) override {
        if (const auto it = created.find(handle); it != created.end()) {
            if (it->second->pending || it->second->identity.epoch != entity_epoch || removed_entities.contains(handle)) return KEEL_RESULT_NOT_FOUND;
            entity = it->second->identity; return KEEL_RESULT_OK;
        }
        if (removed_entities.contains(handle) || (handle != 0x23004 && handle != 0x12003 && handle != 0x45005)) return KEEL_RESULT_NOT_FOUND;
        entity = {handle == 0x12003 ? 3 : handle == 0x23004 ? 4 : 5, handle, entity_epoch};
        if (mutate_pawn == 1) pawn_handle = 0x24004;
        if (mutate_pawn == 2) ++user_id;
        if (mutate_pawn == 3) player_alive = false;
        mutate_pawn = 0;
        return KEEL_RESULT_OK;
    }
    KeelResult ValidateEntity(const GameEntityIdentity& entity, std::string&) override {
        if (const auto it = created.find(entity.source2_handle); it != created.end())
            return !it->second->pending && it->second->identity.index == entity.index && entity.epoch == entity_epoch &&
                !removed_entities.contains(entity.source2_handle) ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND;
        if ((entity.source2_handle != 0x23004 && entity.source2_handle != 0x12003 && entity.source2_handle != 0x45005) ||
            entity.epoch != entity_epoch || removed_entities.contains(entity.source2_handle)) return KEEL_RESULT_NOT_FOUND;
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

extern "C" KEELS2_GAME_ADAPTER_EXPORT KeelResult KeelGameAdapter_QueryEntityAccess(
    unsigned version, GameAdapterEntityAccessApi* api) noexcept {
    if (!api || api->size != sizeof(*api)) return KEEL_RESULT_INVALID_ARGUMENT;
    *api = {};
    if (version != 1) return KEEL_RESULT_INCOMPATIBLE;
    *api = {sizeof(*api),1,
        [](GameAdapter* adapter, const GameEntityAccessRequest* requests, unsigned count,
            KeelEntityAccessCallback callback, void* data) noexcept -> KeelResult {
            if (!adapter || !requests || !count || count > 32 || !callback) return KEEL_RESULT_INVALID_ARGUMENT;
            try {
                auto& state = *static_cast<Adapter*>(adapter);
                void* pointers[32]{};
                for (unsigned i = 0; i < count; ++i) {
                    std::string error;
                    const auto status = state.ValidateEntity(requests[i].entity,error);
                    if (status != KEEL_RESULT_OK) return status;
                    if (const auto created = state.created.find(requests[i].entity.source2_handle); created != state.created.end()) {
                        if (!requests[i].class_name || std::strcmp(requests[i].class_name,"CBaseEntity")) return KEEL_RESULT_INCOMPATIBLE;
                        pointers[i] = &created->second->object; continue;
                    }
                    const auto index = requests[i].entity.source2_handle == 0x12003 ? 0 :
                        requests[i].entity.source2_handle == 0x23004 ? 1 : 2;
                    const char* names[]{"CCSPlayerController","CCSPlayerPawn","CTestEntity"};
                    if (!requests[i].class_name || std::strcmp(requests[i].class_name,names[index])) return KEEL_RESULT_INCOMPATIBLE;
                    pointers[i] = &state.entity_objects[index];
                }
                return callback(data,pointers,count);
            } catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
        }};
    return KEEL_RESULT_OK;
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT unsigned SrFixtureEntityCalls() { return active ? active->entity_call_count : 0; }

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

extern "C" KEELS2_GAME_ADAPTER_EXPORT KeelResult KeelGameAdapter_QueryPlayerStatistics(
    unsigned version, keels2::host::GameAdapterPlayerStatisticsApi* api) noexcept {
    if (!api || api->size != sizeof(*api)) return KEEL_RESULT_INVALID_ARGUMENT;
    *api = {};
    if (version != keels2::host::kGameAdapterPlayerStatisticsVersion) return KEEL_RESULT_INCOMPATIBLE;
    api->size = sizeof(*api); api->api_version = version;
    api->capabilities = [](keels2::host::GameAdapter* base, unsigned* readable, unsigned* writable) noexcept {
        if (!base || !readable || !writable) return KEEL_RESULT_INVALID_ARGUMENT;
        const auto& adapter = *static_cast<Adapter*>(base);
        *readable = adapter.stat_read_caps; *writable = adapter.stat_write_caps;
        return adapter.stat_cap_status;
    };
    api->read = [](keels2::host::GameAdapter* base, const GameEntityIdentity* controller, unsigned key, std::int32_t* value) noexcept {
        if (!base || !controller || !value) return KEEL_RESULT_INVALID_ARGUMENT;
        try { return static_cast<Adapter*>(base)->AccessStatistic(*controller,key,*value,false); }
        catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
    };
    api->write = [](keels2::host::GameAdapter* base, const GameEntityIdentity* controller, unsigned key, std::int32_t value) noexcept {
        if (!base || !controller) return KEEL_RESULT_INVALID_ARGUMENT;
        try { return static_cast<Adapter*>(base)->AccessStatistic(*controller,key,value,true); }
        catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
    };
    return KEEL_RESULT_OK;
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT unsigned SrFixtureStatisticsWrites() { return active ? active->stat_writes : 0; }
extern "C" KEELS2_GAME_ADAPTER_EXPORT void SrFixtureStatisticsState(
    unsigned readable, unsigned writable, KeelResult caps, KeelResult read, KeelResult write, unsigned mutation) {
    if (!active) return;
    active->stat_read_caps = readable; active->stat_write_caps = writable; active->stat_cap_status = caps;
    active->stat_read_status = read; active->stat_write_status = write; active->stat_mutation = mutation;
}

namespace { std::atomic<unsigned> hook_calls{0}; std::atomic<std::int32_t> hook_original{0}; }
extern "C" KEELS2_GAME_ADAPTER_EXPORT
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
std::int32_t SrFixtureHookScalar(std::int32_t value, float real) {
    ++hook_calls;
    const auto result = value * 2 + static_cast<std::int32_t>(real);
    hook_original = result;
    return result;
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT unsigned SrFixtureHookCalls() { return hook_calls; }
extern "C" KEELS2_GAME_ADAPTER_EXPORT std::int32_t SrFixtureHookOriginal() { return hook_original; }
extern "C" KEELS2_GAME_ADAPTER_EXPORT
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
std::int32_t SrFixtureHookBuffers(char* text, std::uint32_t capacity, std::int32_t* values, std::int32_t count, float* vector) {
    if (!text || capacity != 8 || std::strcmp(text,"hello") || !values || count != 3 || !vector ||
        static_cast<void*>(vector) == text || static_cast<void*>(vector) == values) return -1;
    std::memcpy(text,"changed",8);
    std::int32_t sum = 0;
    for (std::int32_t i = 0; i < count; ++i) { values[i] *= 2; sum += values[i]; }
    for (unsigned i = 0; i < 3; ++i) vector[i] *= 2;
    return sum;
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
std::int32_t SrFixtureEntityMethod(void* entity, std::int32_t value, char* text, std::uint32_t capacity) {
    if (!active || !entity || !text || capacity != 8 || std::strcmp(text,"hello") ||
        (entity != &active->entity_objects[0] && entity != &active->entity_objects[1])) return -1;
    ++active->entity_call_count;
    if (value == 99 && !active->Dispatch("sr_sdkcall_entity_close_active",-1)) return -2;
    std::memcpy(text,"changed",8);
    return *static_cast<std::int32_t*>(entity) + value;
}

extern "C" KEELS2_GAME_ADAPTER_EXPORT KeelResult KeelGameAdapter_QueryEntityCapture(
    unsigned version, GameAdapterEntityCaptureApi* api) noexcept {
    if (!api || api->size != sizeof(*api)) return KEEL_RESULT_INVALID_ARGUMENT;
    *api = {}; if (version != 1) return KEEL_RESULT_INCOMPATIBLE;
    *api = {sizeof(*api),1,[](GameAdapter* adapter,const void* pointer,GameEntityIdentity* out) noexcept -> KeelResult {
        if (!adapter || !out) return KEEL_RESULT_INVALID_ARGUMENT; *out = {};
        try { auto& state = *static_cast<Adapter*>(adapter); std::string error;
            const unsigned refs[]{0x12003,0x23004,0x45005};
            for (unsigned i = 0; i < 3; ++i) if (pointer == &state.entity_objects[i]) return state.FindEntityBySource2Handle(refs[i],*out,error);
            return KEEL_RESULT_NOT_FOUND;
        } catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
    }}; return KEEL_RESULT_OK;
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT KeelResult KeelGameAdapter_QueryEntityHookData(
    unsigned version, GameAdapterEntityHookDataApi* api) noexcept {
    if (!api || api->size != sizeof(*api)) return KEEL_RESULT_INVALID_ARGUMENT;
    *api = {}; if (version != 1) return KEEL_RESULT_INCOMPATIBLE;
    *api = {sizeof(*api),1,
        [](GameAdapter*,const void* record,KeelDamageInfo* out) noexcept -> KeelResult {
            if (!record || !out || out->size != sizeof(*out)) return KEEL_RESULT_INVALID_ARGUMENT;
            *out = *static_cast<const KeelDamageInfo*>(record); return KEEL_RESULT_OK;
        },
        [](GameAdapter*,void* record,const KeelDamageEdit* edit) noexcept -> KeelResult {
            if (!record || !edit || edit->size != sizeof(*edit)) return KEEL_RESULT_INVALID_ARGUMENT;
            auto& damage = *static_cast<KeelDamageInfo*>(record); damage.damage = edit->damage; damage.damage_type = edit->damage_type;
            std::copy_n(edit->force,3,damage.force); std::copy_n(edit->position,3,damage.position); return KEEL_RESULT_OK;
        },
        [](GameAdapter*,const GameEntityIdentity*,const void*,KeelBool* out) noexcept -> KeelResult {
            if (out) *out = KEEL_FALSE; return KEEL_RESULT_UNSUPPORTED;
        }}; return KEEL_RESULT_OK;
}
namespace { unsigned sdkhook_damage_calls = 0; float sdkhook_damage_value = 0; }
extern "C" KEELS2_GAME_ADAPTER_EXPORT
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
void SrFixtureDamageTarget(void* entity, KeelDamageInfo* damage, void* result) {
    if (!active || entity != &active->entity_objects[1] || !damage || !result) std::abort();
    ++sdkhook_damage_calls; sdkhook_damage_value = damage->damage;
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT int SrFixtureDamageInvoke() {
    if (!active) return -100;
    KeelDamageInfo damage{sizeof(damage),0,42,0x80000040,-7,0x45005,UINT32_MAX,UINT32_MAX,{1,2,3},{4,5,6}};
    const auto before = sdkhook_damage_calls; std::uint64_t result = 0x1122334455667788ull;
    SrFixtureDamageTarget(&active->entity_objects[1],&damage,&result);
    if (result != 0x1122334455667788ull || damage.damage_custom != -7 || damage.inflictor != 0x45005) return -101;
    return sdkhook_damage_calls == before ? -1 : static_cast<int>(sdkhook_damage_value);
}

extern "C" KEELS2_GAME_ADAPTER_EXPORT KeelResult KeelGameAdapter_QueryEntityTools(unsigned version, GameAdapterEntityToolsApi* api) noexcept {
    if (!api || api->size != sizeof(*api)) return KEEL_RESULT_INVALID_ARGUMENT;
    *api = {}; if (version != 1) return KEEL_RESULT_INCOMPATIBLE;
    *api = {sizeof(*api),1,
        [](GameAdapter* adapter,unsigned* flags) noexcept -> KeelResult {
            if (flags) *flags = 0;
            if (!adapter || !flags) return KEEL_RESULT_INVALID_ARGUMENT;
            try {
                auto& state = *static_cast<Adapter*>(adapter);
                if (state.tool_mode == 3) { state.tool_mode = 0; if (!state.Dispatch("sr_sdk_tool_close",-1)) return KEEL_RESULT_ENGINE_FAILURE; }
                *flags = state.tool_caps; return state.tool_cap_status;
            } catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
        },
        [](GameAdapter* adapter,const GameEntityIdentity* entity,unsigned kind,const KeelEntityTeleport* request,const char* model) noexcept -> KeelResult {
            if (!adapter || !entity) return KEEL_RESULT_INVALID_ARGUMENT;
            try { return static_cast<Adapter*>(adapter)->ApplyTool(*entity,kind,request,model); }
            catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
        }};
    return KEEL_RESULT_OK;
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT void SrFixtureToolState(unsigned caps,unsigned cap_status,unsigned status,unsigned mode) {
    if (active) { active->tool_caps = caps; active->tool_cap_status = cap_status; active->tool_status = status; active->tool_mode = mode; }
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT unsigned SrFixtureToolCount() { return active ? active->tool_calls : 0; }
extern "C" KEELS2_GAME_ADAPTER_EXPORT void SrFixtureRestoreEntities() { if (active) active->removed_entities.clear(); }
extern "C" KEELS2_GAME_ADAPTER_EXPORT bool SrFixtureToolData(KeelEntityTeleport* teleport,char* model,unsigned capacity) {
    if (!active || !teleport || !model || capacity <= active->tool_model.size()) return false;
    *teleport = active->tool_teleport; std::memcpy(model,active->tool_model.c_str(),active->tool_model.size()+1); return true;
}

// A deterministic engine boundary for the real host + independent SDKTools and
// SDKHooks plugins. Production native creation has separate registry/KV tests.
extern "C" KEELS2_GAME_ADAPTER_EXPORT
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
void SrFixtureConstructSpawn(void* instance, const void* values) {
    if (!active || !instance || !values) std::abort();
    for (const auto& [source,record] : active->created) {
        static_cast<void>(source);
        if (instance != &record->object) continue;
        if (values != &record->keys) std::abort();
        if (active->construction_mode != 2) record->pending = false;
        return;
    }
    std::abort();
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT KeelResult KeelGameAdapter_QueryEntityConstruction(
    unsigned version, GameAdapterEntityConstructionApi* api) noexcept {
    if (!api || api->size != sizeof(*api)) return KEEL_RESULT_INVALID_ARGUMENT;
    *api = {}; if (version != 1) return KEEL_RESULT_INCOMPATIBLE;
    *api = {sizeof(*api),1,
        [](GameAdapter* base) noexcept -> KeelResult { return base && base->IsGameThread() ? KEEL_RESULT_OK : KEEL_RESULT_WRONG_THREAD; },
        [](GameAdapter* base,const char* name,std::uint64_t* token,GameEntityIdentity* identity) noexcept -> KeelResult {
            if (token) *token = 0; if (identity) *identity = {};
            if (!base || !name || !token || !identity) return KEEL_RESULT_INVALID_ARGUMENT;
            try {
                auto& state = *static_cast<Adapter*>(base);
                if (!state.IsGameThread()) return KEEL_RESULT_WRONG_THREAD;
                if (std::strcmp(name,"prop_dynamic")) return KEEL_RESULT_UNSUPPORTED;
                auto record = std::make_shared<Adapter::Created>(); const auto index = state.next_created++;
                record->identity = {static_cast<int>(index),0x80000+index,state.entity_epoch};
                state.created.emplace(record->identity.source2_handle,record); ++state.construction_creates;
                *token = record->identity.source2_handle; *identity = record->identity; return KEEL_RESULT_OK;
            } catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
        },
        [](GameAdapter* base,std::uint64_t token,GameEntityIdentity* identity) noexcept -> KeelResult {
            if (!base || !identity) return KEEL_RESULT_INVALID_ARGUMENT; *identity = {};
            const auto record = static_cast<Adapter*>(base)->Pending(token);
            if (!record) return KEEL_RESULT_NOT_FOUND;
            *identity = record->identity; return KEEL_RESULT_OK;
        },
        [](GameAdapter* base,std::uint64_t token,const KeelEntityKeyValue* value) noexcept -> KeelResult {
            if (!base || !value) return KEEL_RESULT_INVALID_ARGUMENT;
            try {
                auto& state = *static_cast<Adapter*>(base); const auto record = state.Pending(token);
                if (!record) return KEEL_RESULT_NOT_FOUND;
                if (record->busy) return KEEL_RESULT_BUSY;
                std::string name = value->name;
                std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c) { return c >= 'A' && c <= 'Z' ? c+('a'-'A') : c; });
                if (name == "classname") return KEEL_RESULT_INVALID_ARGUMENT;
                auto key = Adapter::Created::Key{*value,value->type == KEELS2_ENTITY_KEY_STRING ? value->string_value : ""};
                key.value.name = key.value.string_value = nullptr;
                record->keys.insert_or_assign(name,std::move(key)); return KEEL_RESULT_OK;
            } catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
        },
        [](GameAdapter* base,std::uint64_t token,const KeelEntityTeleport* request) noexcept -> KeelResult {
            if (!base || !request) return KEEL_RESULT_INVALID_ARGUMENT;
            const auto record = static_cast<Adapter*>(base)->Pending(token);
            if (!record) return KEEL_RESULT_NOT_FOUND;
            if (record->busy) return KEEL_RESULT_BUSY;
            record->teleport = *request; return KEEL_RESULT_OK;
        },
        [](GameAdapter* base,std::uint64_t token,KeelBool* invoked) noexcept -> KeelResult {
            if (invoked) *invoked = KEEL_FALSE;
            if (!base || !invoked) return KEEL_RESULT_INVALID_ARGUMENT;
            try {
                auto& state = *static_cast<Adapter*>(base); const auto record = state.Pending(token);
                if (!record) return KEEL_RESULT_NOT_FOUND;
                if (record->busy) return KEEL_RESULT_BUSY;
                if (state.construction_mode == 1) return KEEL_RESULT_NOT_READY;
                record->busy = true; ++state.construction_spawns; *invoked = KEEL_TRUE;
                if (state.construction_mode == 3) state.Dispatch("keel plugins unload 2",-1);
                const auto& keys = record->keys;
                state.construction_data_valid = keys.size() == 7 && keys.contains("model") && keys.at("model").text == "models/test.vmdl" &&
                    keys.contains("solid") && keys.at("solid").value.type == KEELS2_ENTITY_KEY_BOOL && keys.at("solid").value.int_value == 1 &&
                    keys.contains("flags") && keys.at("flags").value.int_value == 7 && keys.contains("scale") && keys.at("scale").value.float_value == 1.5f &&
                    keys.contains("origin") && keys.at("origin").value.vector_value[2] == 3 && keys.contains("angles") && keys.at("angles").value.vector_value[0] == 4 &&
                    keys.contains("rendercolor") && keys.at("rendercolor").value.color_value[3] == 255 && record->teleport.flags == 1 && record->teleport.position[0] == 10;
                SrFixtureConstructSpawn(&record->object,&record->keys);
                record->busy = false; record->owned = false;
                if (record->pending) { state.created.erase(record->identity.source2_handle); return KEEL_RESULT_ENGINE_FAILURE; }
                return KEEL_RESULT_OK;
            } catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
        },
        [](GameAdapter* base,std::uint64_t token) noexcept -> KeelResult {
            if (!base || token > UINT32_MAX) return KEEL_RESULT_INVALID_ARGUMENT;
            try {
                auto& state = *static_cast<Adapter*>(base); const auto it = state.created.find(static_cast<std::uint32_t>(token));
                if (it == state.created.end()) return KEEL_RESULT_NOT_FOUND;
                const auto record = it->second; record->owned = false; record->closed = true;
                const bool current = record->identity.epoch == state.entity_epoch;
                if (record->pending) {
                    if (current) ++state.construction_cancels;
                    if (!record->busy) state.created.erase(it);
                }
                if (current && state.construction_mode == 6) {
                    state.construction_mode = 0;
                    if (!state.Dispatch("sr_construct_cancel_grow",-1)) return KEEL_RESULT_ENGINE_FAILURE;
                }
                return current ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND;
            } catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
        },
        [](GameAdapter* base,std::uint64_t token,const char* name,KeelEntityAccessCallback callback,void* data) noexcept -> KeelResult {
            if (!base || !name || !callback) return KEEL_RESULT_INVALID_ARGUMENT;
            try {
                const auto record = static_cast<Adapter*>(base)->Pending(token);
                if (!record) return KEEL_RESULT_NOT_FOUND;
                if (std::strcmp(name,"CBaseEntity")) return KEEL_RESULT_INCOMPATIBLE;
                void* pointers[]{&record->object}; return callback(data,pointers,1);
            } catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
        }};
    return KEEL_RESULT_OK;
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT void SrFixtureConstructionMode(unsigned mode) { if (active) active->construction_mode = mode; }
extern "C" KEELS2_GAME_ADAPTER_EXPORT unsigned SrFixtureConstructionCount(unsigned kind) {
    if (!active) return 0;
    if (kind == 0) return active->construction_creates;
    if (kind == 1) return active->construction_cancels;
    if (kind == 2) return active->construction_spawns;
    if (kind == 3) return active->construction_data_valid ? 1 : 0;
    return static_cast<unsigned>(std::count_if(active->created.begin(),active->created.end(),[](const auto& item) {
        return item.second->pending && item.second->identity.epoch == active->entity_epoch;
    }));
}

extern "C" KEELS2_GAME_ADAPTER_EXPORT KeelResult KeelGameAdapter_QueryEntityInput(unsigned version, GameAdapterEntityInputApi* api) noexcept {
    if (!api || api->size != sizeof(*api)) return KEEL_RESULT_INVALID_ARGUMENT;
    *api = {}; if (version != 1) return KEEL_RESULT_INCOMPATIBLE;
    *api = {sizeof(*api),1,
        [](GameAdapter*, unsigned* direct, unsigned* queued) noexcept -> KeelResult {
            *direct = 511; *queued = 383; return KEEL_RESULT_OK;
        },
        [](GameAdapter* base, const GameEntityInputRequest* request, KeelBool* invoked) noexcept -> KeelResult {
            *invoked = KEEL_FALSE;
            try {
                auto& state = *static_cast<Adapter*>(base); std::string error;
                for (const auto& entity : {request->target,request->activator,request->caller,request->value_entity})
                    if (entity.epoch && state.ValidateEntity(entity,error) != KEEL_RESULT_OK) return KEEL_RESULT_NOT_FOUND;
                if (request->queued && request->value.type == KEELS2_INPUT_COLOR) return KEEL_RESULT_UNSUPPORTED;
                if (state.entity_input_mode == 1) return KEEL_RESULT_NOT_READY;
                ++state.entity_input_calls; *invoked = KEEL_TRUE;
                if (state.entity_input_mode == 3) state.Dispatch("sr_input_callback",-1);
                if (state.entity_input_mode == 4) state.Dispatch("keel plugins unload 2",-1);
                if (state.entity_input_mode == 5) state.Dispatch("sr_input_recurse",-1);
                // Inspect after callbacks: input storage must remain readable.
                bool valid = std::string(request->input) == "Enable" && request->target.source2_handle == 0x23004;
                if (request->activator.epoch) valid &= request->activator.source2_handle == 0x12003;
                if (request->caller.epoch) valid &= request->caller.source2_handle == 0x23004;
                switch (request->value.type) {
                    case KEELS2_INPUT_VOID: break;
                    case KEELS2_INPUT_STRING: valid &= std::string(request->value.string_value) == "payload"; break;
                    case KEELS2_INPUT_BOOL: valid &= request->value.int_value == 1; break;
                    case KEELS2_INPUT_INT32: valid &= request->value.int_value == -17; break;
                    case KEELS2_INPUT_FLOAT: valid &= request->value.float_value == 1.25f; break;
                    case KEELS2_INPUT_VECTOR: valid &= request->value.vector_value[0] == 1 && request->value.vector_value[1] == 2 && request->value.vector_value[2] == 3; break;
                    case KEELS2_INPUT_ANGLES: valid &= request->value.vector_value[0] == 4 && request->value.vector_value[1] == 5 && request->value.vector_value[2] == 6; break;
                    case KEELS2_INPUT_COLOR: valid &= request->value.color_value[0] == 10 && request->value.color_value[1] == 20 && request->value.color_value[2] == 30 && request->value.color_value[3] == 255; break;
                    case KEELS2_INPUT_ENTITY: valid &= request->value_entity.source2_handle == 0x12003; break;
                    default: valid = false;
                }
                state.entity_input_valid &= valid;
                if (request->queued) {
                    state.entity_input_queued |= 1u << request->value.type;
                    state.queued_input_names.emplace_back(request->input);
                    state.queued_input_texts.emplace_back(request->value.type == KEELS2_INPUT_STRING ? request->value.string_value : "");
                } else state.entity_input_direct |= 1u << request->value.type;
                return state.entity_input_mode == 2 ? KEEL_RESULT_ENGINE_FAILURE : KEEL_RESULT_OK;
            } catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
        }};
    return KEEL_RESULT_OK;
}
extern "C" KEELS2_GAME_ADAPTER_EXPORT void SrFixtureEntityInputMode(unsigned mode) { if (active) active->entity_input_mode = mode; }
extern "C" KEELS2_GAME_ADAPTER_EXPORT unsigned SrFixtureEntityInputCount(unsigned kind) {
    if (!active) return 0;
    if (kind == 0) return active->entity_input_calls;
    if (kind == 1) return active->entity_input_direct;
    if (kind == 2) return active->entity_input_queued;
    if (kind == 3) return active->entity_input_valid ? 1 : 0;
    if (kind == 4) {
        for (const auto& name : active->queued_input_names) if (name != "Enable") return 0;
        for (const auto& text : active->queued_input_texts) if (!text.empty() && text != "payload") return 0;
        return static_cast<unsigned>(active->queued_input_names.size());
    }
    return 0;
}
