#include "entities.h"
#include <source2root/extension.hpp>
#include <bit>

namespace {
using namespace keels2::authoring;
using source2root::NativeCall;
namespace sdk = source2root::sdktools;
class SDKTools final : public source2root::Extension {
public:
    static constexpr PluginInfo Info{"Source2Root SDKTools", "KeelS2 Project", "1.0.0", "Owned entities, schema access and game operations"};
    static constexpr PluginRequirement Requirements[]{{"Source2Root", "1.0.0", DependencyRequirement::exact}};
    SDKTools() : Extension("source2root.sdktools") {}
private:
    static constexpr unsigned EntityType = 1, FieldType = 2;
    std::shared_ptr<sdk::Service> service_;
    template <typename T> const T& Require(const char* name, unsigned version) {
        const void* raw = nullptr;
        if (HostContext().QueryService(name, version, &raw) != KEEL_RESULT_OK || !raw)
            throw sdk::Error(std::string("Required host service is unavailable: ") + name);
        // Validate the prefix before copying a complete table in Service.
        const auto* api = static_cast<const T*>(raw);
        if (api->size != sizeof(T) || api->api_version != version) throw sdk::Error("Incompatible host service table.");
        return *api;
    }
    const KeelEntityWritesApi* OptionalWrites() {
        const void* raw = nullptr;
        const auto result = HostContext().QueryService(KEELS2_ENTITY_WRITES_SERVICE_NAME, KEELS2_ENTITY_WRITES_API_VERSION, &raw);
        if (result == KEEL_RESULT_NOT_FOUND || result == KEEL_RESULT_UNSUPPORTED) return nullptr;
        if (result != KEEL_RESULT_OK || !raw) throw sdk::Error("Entity write service query failed.");
        const auto* api = static_cast<const KeelEntityWritesApi*>(raw);
        if (api->size != sizeof(*api) || api->api_version != KEELS2_ENTITY_WRITES_API_VERSION) throw sdk::Error("Incompatible entity write service table.");
        return api;
    }
    const KeelEntityToolsApi* OptionalTools() {
        const void* raw = nullptr;
        const auto result = HostContext().QueryService(KEELS2_ENTITY_TOOLS_SERVICE_NAME, KEELS2_ENTITY_TOOLS_API_VERSION, &raw);
        if (result == KEEL_RESULT_NOT_FOUND || result == KEEL_RESULT_UNSUPPORTED) return nullptr;
        if (result != KEEL_RESULT_OK || !raw) throw sdk::Error("Entity tools service query failed.");
        const auto* api = static_cast<const KeelEntityToolsApi*>(raw);
        if (api->size != sizeof(*api) || api->api_version != KEELS2_ENTITY_TOOLS_API_VERSION) throw sdk::Error("Incompatible entity tools service table.");
        return api;
    }
    const KeelEntityConstructionApi* OptionalConstruction() {
        const void* raw{};
        const auto result = HostContext().QueryService(KEELS2_ENTITY_CONSTRUCTION_SERVICE_NAME,1,&raw);
        if (result == KEEL_RESULT_NOT_FOUND || result == KEEL_RESULT_UNSUPPORTED) return nullptr;
        if (result != KEEL_RESULT_OK || !raw) throw sdk::Error("Entity construction service query failed.");
        const auto* api = static_cast<const KeelEntityConstructionApi*>(raw);
        if (api->size != sizeof(*api) || api->api_version != 1) throw sdk::Error("Incompatible entity construction service.");
        return api;
    }
    const KeelEntityInputApi* OptionalInput() {
        const void* raw{};
        const auto result = HostContext().QueryService(KEELS2_ENTITY_INPUT_SERVICE_NAME,1,&raw);
        if (result == KEEL_RESULT_NOT_FOUND || result == KEEL_RESULT_UNSUPPORTED) return nullptr;
        if (result != KEEL_RESULT_OK || !raw) throw sdk::Error("Entity input service query failed.");
        const auto* api = static_cast<const KeelEntityInputApi*>(raw);
        if (api->size != sizeof(*api) || api->api_version != 1) throw sdk::Error("Incompatible entity input service.");
        return api;
    }
    bool OnExtensionStart() override {
        service_ = std::make_shared<sdk::Service>(HostContext().PluginHandle(),
            Require<KeelEntitiesApi>(KEELS2_ENTITIES_SERVICE_NAME, KEELS2_ENTITIES_API_VERSION),
            Require<KeelSchemaApi>(KEELS2_SCHEMA_SERVICE_NAME, KEELS2_SCHEMA_API_VERSION),
            Require<KeelPlayersApi>(KEELS2_PLAYERS_SERVICE_NAME, KEELS2_PLAYERS_API_VERSION),
            Require<KeelNativeRuntimeApi>(KEELS2_NATIVE_RUNTIME_SERVICE_NAME, KEELS2_NATIVE_RUNTIME_API_VERSION), OptionalWrites(), OptionalTools(), OptionalConstruction(), OptionalInput());
        return RegisterNative("Entity_GetInputCapabilities", 2, &SDKTools::InputCapabilities)
            && RegisterNative("Entity_InputVoid", 7, &SDKTools::Input<KEELS2_INPUT_VOID>)
            && RegisterNative("Entity_InputString", 8, &SDKTools::Input<KEELS2_INPUT_STRING>)
            && RegisterNative("Entity_InputBool", 8, &SDKTools::Input<KEELS2_INPUT_BOOL>)
            && RegisterNative("Entity_InputInt", 8, &SDKTools::Input<KEELS2_INPUT_INT32>)
            && RegisterNative("Entity_InputFloat", 8, &SDKTools::Input<KEELS2_INPUT_FLOAT>)
            && RegisterNative("Entity_InputVector", 8, &SDKTools::Input<KEELS2_INPUT_VECTOR>)
            && RegisterNative("Entity_InputAngles", 8, &SDKTools::Input<KEELS2_INPUT_ANGLES>)
            && RegisterNative("Entity_InputColor", 8, &SDKTools::Input<KEELS2_INPUT_COLOR>)
            && RegisterNative("Entity_InputEntity", 8, &SDKTools::Input<KEELS2_INPUT_ENTITY>)
            && RegisterNative("Entity_ConstructionAvailable", 0, &SDKTools::ConstructionAvailable)
            && RegisterNative("Entity_Create", 1, &SDKTools::Create)
            && RegisterNative("Entity_IsPending", 1, &SDKTools::Pending)
            && RegisterNative("Entity_SetKeyString", 3, &SDKTools::SetKey<KEELS2_ENTITY_KEY_STRING>)
            && RegisterNative("Entity_SetKeyBool", 3, &SDKTools::SetKey<KEELS2_ENTITY_KEY_BOOL>)
            && RegisterNative("Entity_SetKeyInt", 3, &SDKTools::SetKey<KEELS2_ENTITY_KEY_INT32>)
            && RegisterNative("Entity_SetKeyFloat", 3, &SDKTools::SetKey<KEELS2_ENTITY_KEY_FLOAT>)
            && RegisterNative("Entity_SetKeyVector", 3, &SDKTools::SetKey<KEELS2_ENTITY_KEY_VECTOR>)
            && RegisterNative("Entity_SetKeyAngles", 3, &SDKTools::SetKey<KEELS2_ENTITY_KEY_ANGLES>)
            && RegisterNative("Entity_SetKeyColor", 3, &SDKTools::SetKey<KEELS2_ENTITY_KEY_COLOR>)
            && RegisterNative("Entity_DispatchSpawn", 2, &SDKTools::Spawn)
            && RegisterNative("Entity_Find", 1, &SDKTools::Find)
            && RegisterNative("Entity_FromHandle", 1, &SDKTools::FromHandle)
            && RegisterNative("Entity_FromPlayer", 2, &SDKTools::FromPlayer)
            && RegisterNative("Entity_Close", 1, &SDKTools::Close)
            && RegisterNative("Entity_IsValid", 1, &SDKTools::Valid)
            && RegisterNative("Entity_Index", 1, &SDKTools::Index)
            && RegisterNative("Entity_SourceHandle", 2, &SDKTools::SourceHandle)
            && RegisterNative("Entity_Same", 2, &SDKTools::Same)
            && RegisterNative("Schema_Find", 3, &SDKTools::Resolve)
            && RegisterNative("Schema_Close", 1, &SDKTools::CloseField)
            && RegisterNative("Schema_FieldType", 1, &SDKTools::Type)
            && RegisterNative("Schema_FieldSize", 1, &SDKTools::Size)
            && RegisterNative("Schema_FieldInfo", 7, &SDKTools::InfoText)
            && RegisterNative("Entity_ReadInt", 3, &SDKTools::Integer)
            && RegisterNative("Entity_ReadIntegerText", 4, &SDKTools::IntegerText)
            && RegisterNative("Entity_ReadFloat", 3, &SDKTools::Number)
            && RegisterNative("Entity_ReadVector", 3, &SDKTools::Vector)
            && RegisterNative("Entity_ReadSourceHandle", 3, &SDKTools::ReadHandle)
            && RegisterNative("Entity_ReadEntity", 2, &SDKTools::ReadEntity)
            && RegisterNative("Entity_GetWriteCapabilities", 1, &SDKTools::WriteCapabilities)
            && RegisterNative("Entity_WriteInt", 3, &SDKTools::SetInteger)
            && RegisterNative("Entity_WriteIntegerText", 3, &SDKTools::SetIntegerText)
            && RegisterNative("Entity_WriteFloat", 3, &SDKTools::SetNumber)
            && RegisterNative("Entity_WriteVector", 3, &SDKTools::SetVector)
            && RegisterNative("Entity_GetToolCapabilities", 1, &SDKTools::ToolCapabilities)
            && RegisterNative("Entity_Teleport", 5, &SDKTools::Teleport)
            && RegisterNative("Entity_SetModel", 2, &SDKTools::SetModel)
            && RegisterNative("Entity_Remove", 1, &SDKTools::Remove);
    }
    template <typename Function> static std::int32_t Invoke(NativeCall& call, Function function, int failure = 0) {
        try { return function(); } catch (const sdk::Error& error) { return call.Fail(error.what(), failure); }
    }
    static sdk::Entity& Entity(NativeCall& call, unsigned index = 1) { return call.Resource<sdk::Entity>(call.Int(index), EntityType); }
    static sdk::Field& Field(NativeCall& call, unsigned index = 2) { return call.Resource<sdk::Field>(call.Int(index), FieldType); }
    std::int32_t InputCapabilities(NativeCall& call) {
        call.OutputCell(1,0); call.OutputCell(2,0);
        return Invoke(call,[&] {
            const auto service = service_; const auto types = service->InputCapabilities();
            call.OutputCell(1,static_cast<std::int32_t>(types[0])); call.OutputCell(2,static_cast<std::int32_t>(types[1])); return 1;
        });
    }
    template<unsigned Type> std::int32_t Input(NativeCall& call) {
        call.OutputCell(3,0); bool invoked{};
        return Invoke(call,[&] {
            const auto name = call.String(2); std::string text;
            KeelEntityInputValue value{}; value.size = sizeof(value); value.type = Type;
            if constexpr (Type == KEELS2_INPUT_STRING) { text = call.String(4); value.string_value = text.c_str(); }
            else if constexpr (Type == KEELS2_INPUT_BOOL || Type == KEELS2_INPUT_INT32) value.int_value = call.Int(4);
            else if constexpr (Type == KEELS2_INPUT_FLOAT) value.float_value = call.Float(4);
            else if constexpr (Type == KEELS2_INPUT_COLOR) {
                const auto cells = call.Array(4,4);
                for (unsigned i = 0; i < 4; ++i) {
                    if (cells[i] < 0 || cells[i] > 255) throw sdk::Error("Input color components require 0..255.");
                    value.color_value[i] = static_cast<std::uint8_t>(cells[i]);
                }
            } else if constexpr (Type == KEELS2_INPUT_VECTOR || Type == KEELS2_INPUT_ANGLES) {
                const auto cells = call.Array(4,3);
                for (unsigned i = 0; i < 3; ++i) value.vector_value[i] = std::bit_cast<float>(cells[i]);
            }
            constexpr unsigned offset = Type == KEELS2_INPUT_VOID ? 0 : 1;
            const auto queue = call.Int(6+offset); const auto delay = call.Float(7+offset);
            if (queue != 0 && queue != 1) throw sdk::Error("Input queue selection must be false or true.");
            auto* activator = call.Int(4+offset) ? &Entity(call,4+offset) : nullptr;
            auto* caller = call.Int(5+offset) ? &Entity(call,5+offset) : nullptr;
            sdk::Entity* payload{};
            if constexpr (Type == KEELS2_INPUT_ENTITY) payload = &Entity(call,4);
            try { Entity(call).Input(name.c_str(),value,invoked,activator,caller,payload,queue != 0,delay); }
            catch (...) { call.OutputCell(3,invoked ? 1 : 0); throw; }
            call.OutputCell(3,invoked ? 1 : 0); return 1;
        });
    }
    std::int32_t ConstructionAvailable(NativeCall& call) {
        return Invoke(call,[&] { const auto service = service_; service->ConstructionReady(); return 1; });
    }
    std::int32_t Create(NativeCall& call) {
        return Invoke(call,[&] { const auto service = service_; return call.Own(EntityType,service->Create(call.String(1))); });
    }
    std::int32_t Pending(NativeCall& call) { return Invoke(call,[&] { return Entity(call).Pending() ? 1 : 0; }); }
    template<unsigned Type> std::int32_t SetKey(NativeCall& call) {
        return Invoke(call,[&] {
            const auto name = call.String(2); std::string text;
            KeelEntityKeyValue value{}; value.size = sizeof(value); value.type = Type; value.name = name.c_str();
            if constexpr (Type == KEELS2_ENTITY_KEY_STRING) { text = call.String(3); value.string_value = text.c_str(); }
            else if constexpr (Type == KEELS2_ENTITY_KEY_BOOL || Type == KEELS2_ENTITY_KEY_INT32) value.int_value = call.Int(3);
            else if constexpr (Type == KEELS2_ENTITY_KEY_FLOAT) value.float_value = call.Float(3);
            else if constexpr (Type == KEELS2_ENTITY_KEY_COLOR) {
                const auto cells = call.Array(3,4);
                for (unsigned i = 0; i < 4; ++i) {
                    if (cells[i] < 0 || cells[i] > 255) throw sdk::Error("Entity key color components require 0..255.");
                    value.color_value[i] = static_cast<std::uint8_t>(cells[i]);
                }
            } else {
                const auto cells = call.Array(3,3);
                for (unsigned i = 0; i < 3; ++i) value.vector_value[i] = std::bit_cast<float>(cells[i]);
            }
            Entity(call).SetKey(value); return 1;
        });
    }
    std::int32_t Spawn(NativeCall& call) {
        call.OutputCell(2,0); bool invoked{};
        return Invoke(call,[&] {
            try { Entity(call).Spawn(invoked); }
            catch (...) { call.OutputCell(2,invoked ? 1 : 0); throw; }
            call.OutputCell(2,invoked ? 1 : 0); return 1;
        });
    }
    std::int32_t Find(NativeCall& call) {
        return Invoke(call, [&] { return call.Own(EntityType, service_->Find(call.Int(1))); });
    }
    std::int32_t FromHandle(NativeCall& call) {
        return Invoke(call, [&] { return call.Own(EntityType, service_->FromSource(std::bit_cast<std::uint32_t>(call.Int(1)))); });
    }
    std::int32_t FromPlayer(NativeCall& call) {
        return Invoke(call, [&] {
            const auto pawn = call.Int(2);
            if (pawn != 0 && pawn != 1) throw sdk::Error("Pawn selection must be false or true.");
            SrPlayerIdentity player{};
            if (!call.Player(call.Int(1), player)) throw sdk::Error("Player connection is no longer available.");
            return call.Own(EntityType, service_->FromPlayer({player.slot, 0, player.connection}, pawn != 0));
        });
    }
    std::int32_t Close(NativeCall& call) {
        // Remove the script resource before its destructor enters cancellation;
        // reentrant callbacks must already see a closed script handle.
        return Invoke(call, [&] { call.Close(call.Int(1), EntityType); return 1; });
    }
    std::int32_t Valid(NativeCall& call) { return Entity(call).Valid() ? 1 : 0; }
    std::int32_t Index(NativeCall& call) { return Invoke(call, [&] { return Entity(call).Describe().index; }, -1); }
    std::int32_t SourceHandle(NativeCall& call) {
        call.OutputCell(2, -1);
        return Invoke(call, [&] { call.OutputCell(2, std::bit_cast<std::int32_t>(Entity(call).Describe().source2_handle)); return 1; });
    }
    std::int32_t Same(NativeCall& call) { return Invoke(call, [&] { return Entity(call).Same(Entity(call, 2)) ? 1 : 0; }); }
    std::int32_t Resolve(NativeCall& call) {
        return Invoke(call, [&] { return call.Own(FieldType, service_->Resolve(call.String(1), call.String(2), call.Int(3))); });
    }
    std::int32_t CloseField(NativeCall& call) {
        return Invoke(call, [&] { Field(call, 1).Close(); call.Close(call.Int(1), FieldType); return 1; });
    }
    std::int32_t Type(NativeCall& call) { return Field(call, 1).Type(); }
    std::int32_t Size(NativeCall& call) { return Field(call, 1).Size(); }
    std::int32_t InfoText(NativeCall& call) {
        call.Output(2, call.Int(3), ""); call.Output(4, call.Int(5), ""); call.Output(6, call.Int(7), "");
        return Invoke(call, [&] {
            auto& field = Field(call, 1);
            if (field.ClassName().size() >= static_cast<unsigned>(call.Int(3)) || field.Name().size() >= static_cast<unsigned>(call.Int(5)) ||
                field.Profile().size() >= static_cast<unsigned>(call.Int(7))) throw sdk::Error("Schema metadata output buffer is too small.");
            call.Output(2, call.Int(3), field.ClassName()); call.Output(4, call.Int(5), field.Name()); call.Output(6, call.Int(7), field.Profile());
            return 1;
        });
    }
    std::int32_t Integer(NativeCall& call) {
        call.OutputCell(3, 0);
        return Invoke(call, [&] { call.OutputCell(3, Entity(call).Integer(Field(call))); return 1; });
    }
    std::int32_t IntegerText(NativeCall& call) {
        call.Output(3, call.Int(4), "");
        return Invoke(call, [&] {
            const auto value = Entity(call).IntegerText(Field(call));
            if (value.size() >= static_cast<unsigned>(call.Int(4))) throw sdk::Error("Integer output buffer is too small.");
            call.Output(3, call.Int(4), value); return 1;
        });
    }
    std::int32_t Number(NativeCall& call) {
        call.OutputCell(3, 0);
        return Invoke(call, [&] { call.OutputCell(3, std::bit_cast<std::int32_t>(Entity(call).Number(Field(call)))); return 1; });
    }
    std::int32_t Vector(NativeCall& call) {
        call.OutputArray(3, 3, {0, 0, 0});
        return Invoke(call, [&] {
            const auto value = Entity(call).Vector(Field(call));
            call.OutputArray(3, 3, {std::bit_cast<std::int32_t>(value[0]), std::bit_cast<std::int32_t>(value[1]), std::bit_cast<std::int32_t>(value[2])});
            return 1;
        });
    }
    std::int32_t ReadHandle(NativeCall& call) {
        call.OutputCell(3, -1);
        return Invoke(call, [&] { call.OutputCell(3, std::bit_cast<std::int32_t>(Entity(call).SourceHandle(Field(call)))); return 1; });
    }
    std::int32_t WriteCapabilities(NativeCall& call) {
        call.OutputCell(1, 0);
        return Invoke(call, [&] { call.OutputCell(1, service_->WriteCapabilities()); return 1; });
    }
    std::int32_t ToolCapabilities(NativeCall& call) {
        call.OutputCell(1,0);
        return Invoke(call,[&] { const auto service = service_; call.OutputCell(1,service->ToolCapabilities()); return 1; });
    }
    std::int32_t Teleport(NativeCall& call) {
        return Invoke(call,[&] {
            const auto flags = static_cast<unsigned>(call.Int(5));
            if (!flags || (flags & ~7u)) throw sdk::Error("Teleport requires position, angles or velocity flags.");
            std::array<std::array<float,3>,3> vectors{};
            for (unsigned i = 0; i < 3; ++i) if (flags & (1u<<i)) {
                const auto cells = call.Array(i+2,3);
                for (unsigned j = 0; j < 3; ++j) vectors[i][j] = std::bit_cast<float>(cells[j]);
            }
            Entity(call).Teleport(flags,vectors[0],vectors[1],vectors[2]); return 1;
        });
    }
    std::int32_t SetModel(NativeCall& call) {
        return Invoke(call,[&] { const auto model = call.String(2); Entity(call).SetModel(model); return 1; });
    }
    std::int32_t Remove(NativeCall& call) { return Invoke(call,[&] { Entity(call).Remove(); return 1; }); }
    std::int32_t SetInteger(NativeCall& call) {
        return Invoke(call, [&] { Entity(call).SetInteger(Field(call), call.Int(3)); return 1; });
    }
    std::int32_t SetIntegerText(NativeCall& call) {
        return Invoke(call, [&] { Entity(call).SetIntegerText(Field(call), call.String(3)); return 1; });
    }
    std::int32_t SetNumber(NativeCall& call) {
        return Invoke(call, [&] { Entity(call).SetNumber(Field(call), call.Float(3)); return 1; });
    }
    std::int32_t SetVector(NativeCall& call) {
        return Invoke(call, [&] {
            const auto cells = call.Array(3, 3);
            Entity(call).SetVector(Field(call), {std::bit_cast<float>(cells[0]), std::bit_cast<float>(cells[1]), std::bit_cast<float>(cells[2])}); return 1;
        });
    }
    std::int32_t ReadEntity(NativeCall& call) {
        return Invoke(call, [&] {
            auto& entity = Entity(call);
            const auto handle = entity.SourceHandle(Field(call));
            if (handle == KEELS2_INVALID_SOURCE2_ENTITY_HANDLE) return 0;
            auto result = service_->FromSource(handle);
            entity.Describe();
            return call.Own(EntityType, std::move(result));
        });
    }
};
}
KEELS2_PLUGIN(SDKTools)
