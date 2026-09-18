#include "hooks.h"
#include <bitset>

namespace source2root::dhooks {
struct Call::State {
    std::shared_ptr<Service> service;
    std::shared_ptr<TargetData> target;
    Frame values;
    std::bitset<KEELHOOK_MAX_ARGUMENTS> initialized;
    bool busy = false, result = false;
    State(std::shared_ptr<Service> service_value, std::shared_ptr<TargetData> target_value, Frame frame)
        : service(std::move(service_value)), target(std::move(target_value)), values(std::move(frame)) {}
};
Call::Call(std::shared_ptr<Service> service, std::shared_ptr<TargetData> target, const Definition& definition)
    : state_(std::make_shared<State>(std::move(service),std::move(target),Frame(definition))) {}
Call::~Call() = default;
unsigned Call::Count() const { state_->service->Thread(); return state_->values.Count(); }
unsigned Call::Type(unsigned slot) const { state_->service->Thread(); return state_->values.Type(slot); }
const Frame& Call::Read(unsigned slot) const {
    state_->service->Thread();
    state_->values.Type(slot);
    if (state_->busy || (!slot ? !state_->result : !state_->initialized.test(slot - 1)))
        throw Error("SDKCall value is not available.");
    return state_->values;
}
template<class Function> void Call::Edit(unsigned slot, Function function) {
    state_->service->Thread();
    if (state_->busy || !slot || slot > state_->values.Count()) throw Error("SDKCall argument is busy or out of range.");
    function(state_->values);
    state_->initialized.set(slot - 1);
    state_->result = false;
}
void Call::SetInteger(unsigned slot, std::int32_t value) { Edit(slot,[&](auto& frame) { frame.SetInteger(slot,value); }); }
void Call::SetIntegerText(unsigned slot, const std::string& value) { Edit(slot,[&](auto& frame) { frame.SetIntegerText(slot,value); }); }
void Call::SetNumber(unsigned slot, float value) { Edit(slot,[&](auto& frame) { frame.SetNumber(slot,value); }); }
void Call::SetNumberText(unsigned slot, const std::string& value) { Edit(slot,[&](auto& frame) { frame.SetNumberText(slot,value); }); }
void Call::SetNull(unsigned slot) { Edit(slot,[&](auto& frame) { frame.SetNull(slot); }); }
void Call::Reset() {
    state_->service->Thread();
    if (state_->busy) throw Error("SDKCall is busy.");
    state_->initialized.reset(); state_->result = false;
}
void Call::Execute(unsigned flags) {
    // A hook can close this resource or unload its script while the host call
    // is active. Keep only independently owned state across that boundary.
    auto state = state_;
    state->service->Thread();
    if (state->busy) throw Error("SDKCall is already executing.");
    state->result = false;
    if (flags & ~KEELCALL_INVOKE_HOOKS) throw Error("Invalid SDKCall flags.");
    if (state->initialized.count() != state->values.Count()) throw Error("Every SDKCall argument must be initialized.");
    struct Guard { bool& busy; ~Guard() { busy = false; } } guard{state->busy};
    state->busy = true;
    KeelHookValue result{};
    state->service->Invoke(*state->target,flags,state->values.arguments_,result);
    state->values.result_ = result; state->result = true;
}
}
