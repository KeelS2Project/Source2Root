#include "hooks.h"
#include <source2root/extension.hpp>
#include <bit>
#include <limits>

namespace {
using namespace keels2::authoring;
using source2root::NativeCall;
namespace dh = source2root::dhooks;
class DHooks final : public source2root::Extension {
public:
    static constexpr PluginInfo Info{"Source2Root DHooks", "KeelS2 Project", "1.0.0", "Configured typed script detours"};
    static constexpr PluginRequirement Requirements[]{{"Source2Root", "1.0.0", DependencyRequirement::exact}};
    DHooks() : Extension("source2root.dhooks") { frames_.reserve(8); }
    void OnGameFrame(bool, bool, bool) override {
        try { if (service_) service_->Collect(); }
        catch (const std::exception& error) { LogError("Hook cleanup: {}",error.what()); }
    }
private:
    static constexpr unsigned TargetType = 1, HookType = 2, CallType = 3;
    struct Window { std::int32_t id; std::uint64_t owner; dh::Frame* frame; };
    std::shared_ptr<dh::Service> service_;
    std::vector<Window> frames_;
    std::int32_t next_frame_ = 1;
    template<typename T> const T& Require(const char* name, unsigned version) {
        const void* raw = nullptr;
        if (HostContext().QueryService(name,version,&raw) != KEEL_RESULT_OK || !raw) throw dh::Error("Required hook host service is unavailable.");
        const auto* api = static_cast<const T*>(raw);
        if (api->size != sizeof(T) || api->api_version != version) throw dh::Error("Incompatible hook host service.");
        return *api;
    }
    bool OnExtensionStart() override {
        const void* calls = nullptr;
        if (HostContext().QueryService(KEELCALL_SERVICE_NAME,KEELCALL_API_VERSION,&calls) != KEEL_RESULT_OK) calls = nullptr;
        service_ = std::make_shared<dh::Service>(HostContext().PluginHandle(),Require<KeelHookApi>(KEELHOOK_SERVICE_NAME,KEELHOOK_API_VERSION),
            Require<KeelNativeRuntimeApi>(KEELS2_NATIVE_RUNTIME_SERVICE_NAME,KEELS2_NATIVE_RUNTIME_API_VERSION),static_cast<const KeelCallApi*>(calls));
        return RegisterNative("DHook_Open",1,&DHooks::Open)
            && RegisterNative("DHook_CloseTarget",1,&DHooks::CloseTarget)
            && RegisterNative("DHook_Add",5,&DHooks::Add)
            && RegisterNative("DHook_Close",1,&DHooks::Close)
            && RegisterNative("DHook_Enable",2,&DHooks::Enable)
            && RegisterNative("DHook_IsActive",1,&DHooks::Active)
            && RegisterNative("DHook_ArgumentCount",1,&DHooks::Count)
            && RegisterNative("DHook_FrameFlags",1,&DHooks::Flags)
            && RegisterNative("DHook_ValueType",2,&DHooks::Type)
            && RegisterNative("DHook_GetInt",3,&DHooks::Integer)
            && RegisterNative("DHook_GetIntegerText",4,&DHooks::IntegerText)
            && RegisterNative("DHook_GetFloat",3,&DHooks::Number)
            && RegisterNative("DHook_GetFloatText",4,&DHooks::NumberText)
            && RegisterNative("DHook_IsNull",3,&DHooks::IsNull)
            && RegisterNative("DHook_SetInt",3,&DHooks::SetInteger)
            && RegisterNative("DHook_SetIntegerText",3,&DHooks::SetIntegerText)
            && RegisterNative("DHook_SetFloat",3,&DHooks::SetNumber)
            && RegisterNative("DHook_SetFloatText",3,&DHooks::SetNumberText)
            && RegisterNative("DHook_SetNull",2,&DHooks::SetNull)
            && RegisterNative("DHook_CopyValue",3,&DHooks::Copy)
            && RegisterNative("SDKCall_Prepare",1,&DHooks::PrepareCall)
            && RegisterNative("SDKCall_Close",1,&DHooks::CloseCall)
            && RegisterNative("SDKCall_Reset",1,&DHooks::ResetCall)
            && RegisterNative("SDKCall_Execute",2,&DHooks::ExecuteCall)
            && RegisterNative("SDKCall_ArgumentCount",1,&DHooks::CallCount)
            && RegisterNative("SDKCall_ValueType",2,&DHooks::CallTypeAt)
            && RegisterNative("SDKCall_GetInt",3,&DHooks::CallInteger)
            && RegisterNative("SDKCall_GetIntegerText",4,&DHooks::CallIntegerText)
            && RegisterNative("SDKCall_GetFloat",3,&DHooks::CallNumber)
            && RegisterNative("SDKCall_GetFloatText",4,&DHooks::CallNumberText)
            && RegisterNative("SDKCall_IsNull",3,&DHooks::CallIsNull)
            && RegisterNative("SDKCall_SetInt",3,&DHooks::CallSetInteger)
            && RegisterNative("SDKCall_SetIntegerText",3,&DHooks::CallSetIntegerText)
            && RegisterNative("SDKCall_SetFloat",3,&DHooks::CallSetNumber)
            && RegisterNative("SDKCall_SetFloatText",3,&DHooks::CallSetNumberText)
            && RegisterNative("SDKCall_SetNull",2,&DHooks::CallSetNull);
    }
    bool PrepareExtensionUnload() override {
        if (!service_) return true;
        service_->Collect();
        return frames_.empty() && service_->Empty();
    }
    template<typename Function> static std::int32_t Invoke(NativeCall& call, Function function) {
        try { return function(); } catch (const dh::Error& error) { return call.Fail(error.what()); }
    }
    dh::Frame& Frame(NativeCall& call) {
        const auto id = call.Int(1);
        for (const auto& value : frames_) if (value.id == id && value.owner == call.Owner()) return *value.frame;
        throw std::invalid_argument("Hook frame is stale, inactive or belongs to another script.");
    }
    static dh::Hook& Hook(NativeCall& call) { return call.Resource<dh::Hook>(call.Int(1),HookType); }
    std::int32_t Open(NativeCall& call) {
        return Invoke(call,[&] {
            const auto definition = dh::ReadDefinition(std::filesystem::path(call.ConfigPath())/"targets.json",call.String(1),call.ScriptId());
            return call.Own(TargetType,service_->Open(definition));
        });
    }
    std::int32_t CloseTarget(NativeCall& call) { call.Close(call.Int(1),TargetType); return 1; }
    std::int32_t Add(NativeCall& call) {
        auto& target = call.Resource<dh::Target>(call.Int(1),TargetType);
        const auto owner = call.Owner(); const auto phases = call.Int(2), priority = call.Int(4), data = call.Int(5);
        if (phases <= 0 || (phases & ~KH_PHASE_BOTH)) return call.Fail("Invalid hook phases.");
        const auto token = call.Callback(3);
        try {
            if (RetainCallback(token) != KEEL_RESULT_OK) throw dh::Error("Persistent callback service is unavailable.");
            auto hook = service_->Attach(target,static_cast<unsigned>(phases),priority,
                [this,owner,token,data](dh::Frame& frame) { return Dispatch(owner,token,data,frame); },
                [this,token] { CancelCallback(token); });
            return call.Own(HookType,std::move(hook));
        } catch (const dh::Error& error) { CancelCallback(token); return call.Fail(error.what()); }
        catch (...) { CancelCallback(token); throw; }
    }
    int Dispatch(std::uint64_t owner, SrCallback token, std::int32_t data, dh::Frame& frame) {
        if (frames_.size() == 8) return -1;
        if (next_frame_ == std::numeric_limits<std::int32_t>::max()) return -2;
        const auto id = next_frame_++;
        frames_.push_back({id,owner,&frame});
        struct Pop { std::vector<Window>& frames; ~Pop() { frames.pop_back(); } } pop{frames_};
        std::vector<SrCallbackArgument> arguments(3);
        for (auto& value : arguments) value = {sizeof(value),SR_CALLBACK_INT32,0,0,{}};
        arguments[0].value.integer = id; arguments[1].value.integer = static_cast<std::int32_t>(frame.Phase()); arguments[2].value.integer = data;
        std::int32_t action = 0;
        const auto result = InvokeCallback(token,arguments,action);
        if (result == KEEL_RESULT_BUSY) return -1;
        if (result != KEEL_RESULT_OK) return -2;
        if (action < 0 || action > KH_ACTION_SUPERSEDE || (frame.Phase() == KH_PHASE_POST && action == KH_ACTION_SUPERSEDE)) {
            LogError("Invalid script hook action; callback retired."); return -2;
        }
        return action;
    }
    std::int32_t Close(NativeCall& call) { Hook(call).Close(); call.Close(call.Int(1),HookType); return 1; }
    std::int32_t Enable(NativeCall& call) {
        return Invoke(call,[&] {
            const auto value = call.Int(2); if (value != 0 && value != 1) throw dh::Error("Hook enable flag must be boolean.");
            Hook(call).Enable(value != 0); return 1;
        });
    }
    std::int32_t Active(NativeCall& call) { return Hook(call).Active(); }
    std::int32_t Count(NativeCall& call) { return static_cast<std::int32_t>(Frame(call).Count()); }
    std::int32_t Flags(NativeCall& call) { return static_cast<std::int32_t>(Frame(call).Flags()); }
    std::int32_t Type(NativeCall& call) { return Invoke(call,[&] { return static_cast<std::int32_t>(Frame(call).Type(call.Int(2))); }); }
    std::int32_t Integer(NativeCall& call) {
        call.OutputCell(3,0); return Invoke(call,[&] { call.OutputCell(3,Frame(call).Integer(call.Int(2))); return 1; });
    }
    std::int32_t IntegerText(NativeCall& call) {
        call.Output(3,call.Int(4),""); return Invoke(call,[&] { call.Output(3,call.Int(4),Frame(call).IntegerText(call.Int(2))); return 1; });
    }
    std::int32_t Number(NativeCall& call) {
        call.OutputCell(3,0); return Invoke(call,[&] { call.OutputCell(3,std::bit_cast<std::int32_t>(Frame(call).Number(call.Int(2)))); return 1; });
    }
    std::int32_t NumberText(NativeCall& call) {
        call.Output(3,call.Int(4),""); return Invoke(call,[&] { call.Output(3,call.Int(4),Frame(call).NumberText(call.Int(2))); return 1; });
    }
    std::int32_t IsNull(NativeCall& call) {
        call.OutputCell(3,0); return Invoke(call,[&] { call.OutputCell(3,Frame(call).IsNull(call.Int(2))); return 1; });
    }
    std::int32_t SetInteger(NativeCall& call) { return Invoke(call,[&] { Frame(call).SetInteger(call.Int(2),call.Int(3)); return 1; }); }
    std::int32_t SetIntegerText(NativeCall& call) { return Invoke(call,[&] { Frame(call).SetIntegerText(call.Int(2),call.String(3)); return 1; }); }
    std::int32_t SetNumber(NativeCall& call) { return Invoke(call,[&] { Frame(call).SetNumber(call.Int(2),call.Float(3)); return 1; }); }
    std::int32_t SetNumberText(NativeCall& call) { return Invoke(call,[&] { Frame(call).SetNumberText(call.Int(2),call.String(3)); return 1; }); }
    std::int32_t SetNull(NativeCall& call) { return Invoke(call,[&] { Frame(call).SetNull(call.Int(2)); return 1; }); }
    std::int32_t Copy(NativeCall& call) { return Invoke(call,[&] { Frame(call).Copy(call.Int(2),call.Int(3)); return 1; }); }
    static dh::Call& Prepared(NativeCall& call) { return call.Resource<dh::Call>(call.Int(1),CallType); }
    std::int32_t PrepareCall(NativeCall& call) {
        return Invoke(call,[&] { return call.Own(CallType,service_->Prepare(call.Resource<dh::Target>(call.Int(1),TargetType))); });
    }
    std::int32_t CloseCall(NativeCall& call) { call.Close(call.Int(1),CallType); return 1; }
    std::int32_t ResetCall(NativeCall& call) { return Invoke(call,[&] { Prepared(call).Reset(); return 1; }); }
    std::int32_t ExecuteCall(NativeCall& call) { return Invoke(call,[&] { Prepared(call).Execute(call.Int(2)); return 1; }); }
    std::int32_t CallCount(NativeCall& call) { return static_cast<std::int32_t>(Prepared(call).Count()); }
    std::int32_t CallTypeAt(NativeCall& call) { return Invoke(call,[&] { return static_cast<std::int32_t>(Prepared(call).Type(call.Int(2))); }); }
    std::int32_t CallInteger(NativeCall& call) {
        call.OutputCell(3,0); return Invoke(call,[&] { call.OutputCell(3,Prepared(call).Read(call.Int(2)).Integer(call.Int(2))); return 1; });
    }
    std::int32_t CallIntegerText(NativeCall& call) {
        call.Output(3,call.Int(4),""); return Invoke(call,[&] { call.Output(3,call.Int(4),Prepared(call).Read(call.Int(2)).IntegerText(call.Int(2))); return 1; });
    }
    std::int32_t CallNumber(NativeCall& call) {
        call.OutputCell(3,0); return Invoke(call,[&] { call.OutputCell(3,std::bit_cast<std::int32_t>(Prepared(call).Read(call.Int(2)).Number(call.Int(2)))); return 1; });
    }
    std::int32_t CallNumberText(NativeCall& call) {
        call.Output(3,call.Int(4),""); return Invoke(call,[&] { call.Output(3,call.Int(4),Prepared(call).Read(call.Int(2)).NumberText(call.Int(2))); return 1; });
    }
    std::int32_t CallIsNull(NativeCall& call) {
        call.OutputCell(3,0); return Invoke(call,[&] { call.OutputCell(3,Prepared(call).Read(call.Int(2)).IsNull(call.Int(2))); return 1; });
    }
    std::int32_t CallSetInteger(NativeCall& call) { return Invoke(call,[&] { Prepared(call).SetInteger(call.Int(2),call.Int(3)); return 1; }); }
    std::int32_t CallSetIntegerText(NativeCall& call) { return Invoke(call,[&] { Prepared(call).SetIntegerText(call.Int(2),call.String(3)); return 1; }); }
    std::int32_t CallSetNumber(NativeCall& call) { return Invoke(call,[&] { Prepared(call).SetNumber(call.Int(2),call.Float(3)); return 1; }); }
    std::int32_t CallSetNumberText(NativeCall& call) { return Invoke(call,[&] { Prepared(call).SetNumberText(call.Int(2),call.String(3)); return 1; }); }
    std::int32_t CallSetNull(NativeCall& call) { return Invoke(call,[&] { Prepared(call).SetNull(call.Int(2)); return 1; }); }
};
}
KEELS2_PLUGIN(DHooks)
