#include "hooks.h"
#include <source2root/extension.hpp>
#include <bit>
#include <limits>

namespace {
using namespace keels2::authoring;
using source2root::NativeCall;
namespace sdk = source2root::sdkhooks;
class SDKHooks final : public source2root::Extension {
public:
    static constexpr PluginInfo Info{"Source2Root SDKHooks", "KeelS2 Project", "1.0.0", "Typed per-entity damage, touch, spawn and weapon hooks"};
    static constexpr PluginRequirement Requirements[]{{"Source2Root", "1.0.0", DependencyRequirement::exact}};
    SDKHooks() : Extension("source2root.sdkhooks") { frames_.reserve(8); }
    void OnGameFrame(bool, bool, bool) override {
        try { if (service_) service_->Collect(true); }
        catch (const std::exception& error) { LogError("SDKHooks cleanup: {}",error.what()); }
    }
private:
    static constexpr unsigned HookType = 1;
    struct Window { std::int32_t id; std::uint64_t owner; sdk::Frame* frame; };
    std::vector<Window> frames_;
    std::int32_t next_frame_ = 1;
    std::shared_ptr<sdk::Service> service_;
    template<class T> const T& Require(const char* name, unsigned version) {
        const void* raw = nullptr;
        if (HostContext().QueryService(name,version,&raw) != KEEL_RESULT_OK || !raw) throw sdk::Error("Required SDKHooks host service is unavailable.");
        const auto* api = static_cast<const T*>(raw);
        if (api->size != sizeof(T) || api->api_version != version) throw sdk::Error("Incompatible SDKHooks service.");
        return *api;
    }
    const KeelEntityConstructionApi* OptionalConstruction() {
        const void* raw{};
        const auto result = HostContext().QueryService(KEELS2_ENTITY_CONSTRUCTION_SERVICE_NAME,1,&raw);
        if (result == KEEL_RESULT_NOT_FOUND || result == KEEL_RESULT_UNSUPPORTED) return nullptr;
        if (result != KEEL_RESULT_OK || !raw) throw sdk::Error("Pending entity service query failed.");
        const auto* api = static_cast<const KeelEntityConstructionApi*>(raw);
        if (api->size != sizeof(*api) || api->api_version != 1) throw sdk::Error("Incompatible pending entity service.");
        return api;
    }
    bool OnExtensionStart() override {
        service_ = std::make_shared<sdk::Service>(HostContext().PluginHandle(),
            Require<KeelHookApi>(KEELHOOK_SERVICE_NAME,KEELHOOK_API_VERSION),
            Require<KeelNativeRuntimeApi>(KEELS2_NATIVE_RUNTIME_SERVICE_NAME,KEELS2_NATIVE_RUNTIME_API_VERSION),
            Require<KeelEntitiesApi>(KEELS2_ENTITIES_SERVICE_NAME,KEELS2_ENTITIES_API_VERSION),
            Require<KeelEntityAccessApi>(KEELS2_ENTITY_ACCESS_SERVICE_NAME,KEELS2_ENTITY_ACCESS_API_VERSION),
            Require<KeelEntityCaptureApi>(KEELS2_ENTITY_CAPTURE_SERVICE_NAME,KEELS2_ENTITY_CAPTURE_API_VERSION),
            Require<KeelEntityHookDataApi>(KEELS2_ENTITY_HOOK_DATA_SERVICE_NAME,KEELS2_ENTITY_HOOK_DATA_API_VERSION), OptionalConstruction());
        return RegisterNative("SDKHook_Add",6,&SDKHooks::Add)
            && RegisterNative("SDKHook_Close",1,&SDKHooks::Close)
            && RegisterNative("SDKHook_Enable",2,&SDKHooks::Enable)
            && RegisterNative("SDKHook_IsActive",1,&SDKHooks::Active)
            && RegisterNative("SDKHook_Kind",1,&SDKHooks::Kind)
            && RegisterNative("SDKHook_Flags",1,&SDKHooks::Flags)
            && RegisterNative("SDKHook_Entity",1,&SDKHooks::Entity)
            && RegisterNative("SDKHook_Other",1,&SDKHooks::Other)
            && RegisterNative("SDKHook_GetDamage",9,&SDKHooks::Damage)
            && RegisterNative("SDKHook_SetDamage",5,&SDKHooks::SetDamage)
            && RegisterNative("SDKHook_GetResult",2,&SDKHooks::Result)
            && RegisterNative("SDKHook_SetResult",2,&SDKHooks::SetResult);
    }
    bool PrepareExtensionUnload() override {
        if (!service_) return true;
        service_->Collect(); return frames_.empty() && service_->Empty();
    }
    template<class F> static std::int32_t Invoke(NativeCall& call, F function) {
        try { return function(); } catch (const sdk::Error& error) { return call.Fail(error.what()); }
    }
    sdk::Frame& Frame(NativeCall& call) {
        const auto id = call.Int(1);
        for (const auto& window : frames_) if (window.id == id && window.owner == call.Owner()) return *window.frame;
        throw std::invalid_argument("SDKHooks frame is stale, inactive or belongs to another script.");
    }
    static sdk::Hook& Hook(NativeCall& call) { return call.Resource<sdk::Hook>(call.Int(1),HookType); }
    std::int32_t Add(NativeCall& call) {
        return Invoke(call,[&] {
            const auto definition = source2root::dhooks::ReadDefinition(std::filesystem::path(call.ConfigPath())/"targets.json",call.String(2),call.ScriptId());
            const auto owner = call.Owner(); const auto priority = call.Int(5), data = call.Int(6), phases = call.Int(3);
            if (phases < 1 || (phases & ~KH_PHASE_BOTH)) throw sdk::Error("Invalid SDKHooks phases.");
            const auto token = call.Callback(4);
            try {
                if (RetainCallback(token) != KEEL_RESULT_OK) throw sdk::Error("Persistent SDKHooks callbacks are unavailable.");
                auto hook = service_->Attach(definition,static_cast<std::uint32_t>(call.Int(1)),static_cast<unsigned>(phases),priority,
                    [this,owner,token,data](sdk::Frame& frame) { return Dispatch(owner,token,data,frame); },
                    [this,token] { CancelCallback(token); });
                return call.Own(HookType,std::move(hook));
            } catch (...) { CancelCallback(token); throw; }
        });
    }
    int Dispatch(std::uint64_t owner, SrCallback token, std::int32_t data, sdk::Frame& frame) {
        if (frames_.size() == 8) return -1;
        if (next_frame_ == std::numeric_limits<std::int32_t>::max()) return -2;
        const auto id = next_frame_++; frames_.push_back({id,owner,&frame});
        struct Pop { std::vector<Window>& values; ~Pop() { values.pop_back(); } } pop{frames_};
        std::vector<SrCallbackArgument> arguments(3);
        for (auto& value : arguments) value = {sizeof(value),SR_CALLBACK_INT32,0,0,{}};
        arguments[0].value.integer = id; arguments[1].value.integer = static_cast<std::int32_t>(frame.Phase()); arguments[2].value.integer = data;
        std::int32_t action = 0; const auto result = InvokeCallback(token,arguments,action);
        if (result == KEEL_RESULT_BUSY) return -1;
        if (result != KEEL_RESULT_OK) return -2;
        if (action < 0 || action > 2 || (frame.Phase() == KH_PHASE_POST && action == 2) ||
            (frame.Phase() == KH_PHASE_POST && action == 1 && frame.Type() != sdk::Kind::weapon_can_use)) {
            LogError("Invalid SDKHooks action; callback retired."); return -2;
        }
        return action;
    }
    std::int32_t Close(NativeCall& call) { Hook(call).Close(); call.Close(call.Int(1),HookType); return 1; }
    std::int32_t Enable(NativeCall& call) {
        return Invoke(call,[&] { const auto value = call.Int(2); if (value != 0 && value != 1) throw sdk::Error("Enable requires a boolean."); Hook(call).Enable(value != 0); return 1; });
    }
    std::int32_t Active(NativeCall& call) { return Hook(call).Active(); }
    std::int32_t Kind(NativeCall& call) { return static_cast<std::int32_t>(Frame(call).Type()); }
    std::int32_t Flags(NativeCall& call) { return static_cast<std::int32_t>(Frame(call).Flags()); }
    std::int32_t Entity(NativeCall& call) { return std::bit_cast<std::int32_t>(Frame(call).Entity()); }
    std::int32_t Other(NativeCall& call) { return std::bit_cast<std::int32_t>(Frame(call).Other()); }
    std::int32_t Damage(NativeCall& call) {
        for (unsigned i = 2; i <= 7; ++i) call.OutputCell(i,i >= 5 ? -1 : 0);
        call.OutputArray(8,3,{0,0,0}); call.OutputArray(9,3,{0,0,0});
        return Invoke(call,[&] {
            const auto& damage = Frame(call).Damage();
            call.OutputCell(2,std::bit_cast<std::int32_t>(damage.damage)); call.OutputCell(3,std::bit_cast<std::int32_t>(damage.damage_type));
            call.OutputCell(4,damage.damage_custom); call.OutputCell(5,std::bit_cast<std::int32_t>(damage.inflictor));
            call.OutputCell(6,std::bit_cast<std::int32_t>(damage.attacker)); call.OutputCell(7,std::bit_cast<std::int32_t>(damage.ability));
            std::vector<std::int32_t> force(3), position(3);
            for (unsigned i = 0; i < 3; ++i) { force[i] = std::bit_cast<std::int32_t>(damage.force[i]); position[i] = std::bit_cast<std::int32_t>(damage.position[i]); }
            call.OutputArray(8,3,force); call.OutputArray(9,3,position); return 1;
        });
    }
    std::int32_t SetDamage(NativeCall& call) {
        return Invoke(call,[&] {
            KeelDamageEdit edit{}; edit.size = sizeof(edit); edit.damage = call.Float(2); edit.damage_type = static_cast<std::uint32_t>(call.Int(3));
            const auto force = call.Array(4,3), position = call.Array(5,3);
            for (unsigned i = 0; i < 3; ++i) { edit.force[i] = std::bit_cast<float>(force[i]); edit.position[i] = std::bit_cast<float>(position[i]); }
            Frame(call).Damage(edit); return 1;
        });
    }
    std::int32_t Result(NativeCall& call) { call.OutputCell(2,0); return Invoke(call,[&] { call.OutputCell(2,Frame(call).Result()); return 1; }); }
    std::int32_t SetResult(NativeCall& call) {
        return Invoke(call,[&] { const auto value = call.Int(2); if (value != 0 && value != 1) throw sdk::Error("Hook result requires a boolean."); Frame(call).Result(value != 0); return 1; });
    }
};
}
KEELS2_PLUGIN(SDKHooks)
