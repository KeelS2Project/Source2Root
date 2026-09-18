#include "hooks.h"
#include <algorithm>
#include <bitset>
#include <cmath>

namespace source2root::dhooks {
struct Call::Buffer {
    BufferSpec spec;
    std::vector<char> text;
    std::vector<std::int32_t> cells;
    std::array<float, 3> vector{};
};
struct Call::State {
    std::shared_ptr<Service> service;
    std::shared_ptr<TargetData> target;
    Frame values;
    std::bitset<KEELHOOK_MAX_ARGUMENTS> initialized;
    std::vector<Buffer> buffers;
    bool busy = false, result = false;
    State(std::shared_ptr<Service> service_value, std::shared_ptr<TargetData> target_value, Frame frame)
        : service(std::move(service_value)), target(std::move(target_value)), values(std::move(frame)) {}
};
Call::Call(std::shared_ptr<Service> service, std::shared_ptr<TargetData> target, const Definition& definition)
    : state_(std::make_shared<State>(std::move(service),std::move(target),Frame(definition))) {
    state_->buffers.reserve(definition.buffers.size());
    for (const auto& spec : definition.buffers) {
        Buffer buffer; buffer.spec = spec; state_->buffers.push_back(std::move(buffer));
    }
}
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
    for (const auto& buffer : state_->buffers)
        if (buffer.spec.argument == slot || buffer.spec.length_argument == slot)
            throw Error("Configured buffer arguments and lengths require their buffer setter.");
    function(state_->values);
    state_->initialized.set(slot - 1);
    state_->result = false;
}
void Call::SetInteger(unsigned slot, std::int32_t value) { Edit(slot,[&](auto& frame) { frame.SetInteger(slot,value); }); }
void Call::SetIntegerText(unsigned slot, const std::string& value) { Edit(slot,[&](auto& frame) { frame.SetIntegerText(slot,value); }); }
void Call::SetNumber(unsigned slot, float value) { Edit(slot,[&](auto& frame) { frame.SetNumber(slot,value); }); }
void Call::SetNumberText(unsigned slot, const std::string& value) { Edit(slot,[&](auto& frame) { frame.SetNumberText(slot,value); }); }
void Call::SetNull(unsigned slot) { Edit(slot,[&](auto& frame) { frame.SetNull(slot); }); }
Call::Buffer& Call::WritableBuffer(unsigned slot, BufferKind kind) {
    state_->service->Thread();
    if (state_->busy) throw Error("SDKCall is busy.");
    for (auto& buffer : state_->buffers)
        if (buffer.spec.argument == slot && buffer.spec.kind == kind) return buffer;
    throw Error("SDKCall argument does not have this configured buffer adapter.");
}
const Call::Buffer& Call::ReadBuffer(unsigned slot, BufferKind kind) const {
    Read(slot);
    for (const auto& buffer : state_->buffers)
        if (buffer.spec.argument == slot && buffer.spec.kind == kind) return buffer;
    throw Error("SDKCall argument does not have this configured buffer adapter.");
}
void Call::CommitBuffer(Buffer& buffer, Buffer value) {
    buffer = std::move(value);
    auto& pointer = state_->values.arguments_[buffer.spec.argument - 1].scalar.pointer;
    unsigned size = 3;
    if (buffer.spec.kind == BufferKind::string) {
        pointer = buffer.text.data(); size = static_cast<unsigned>(buffer.text.size());
    } else if (buffer.spec.kind == BufferKind::int32) {
        pointer = buffer.cells.data(); size = static_cast<unsigned>(buffer.cells.size());
    } else pointer = buffer.vector.data();
    state_->initialized.set(buffer.spec.argument - 1);
    if (const auto slot = buffer.spec.length_argument) {
        auto& count = state_->values.arguments_[slot - 1];
        if (count.type == KH_VALUE_INT32) count.scalar.int32 = static_cast<std::int32_t>(size);
        else count.scalar.uint32 = size;
        state_->initialized.set(slot - 1);
    }
    state_->result = false;
}
void Call::SetString(unsigned slot, const std::string& value, unsigned capacity) {
    auto& buffer = WritableBuffer(slot, BufferKind::string);
    if (!capacity && value.size() < buffer.spec.capacity) capacity = static_cast<unsigned>(value.size() + 1);
    if (!capacity || capacity > buffer.spec.capacity || value.size() >= capacity || value.find('\0') != std::string::npos)
        throw Error("SDKCall string exceeds its configured buffer capacity.");
    Buffer next; next.spec = buffer.spec; next.text.resize(capacity);
    std::copy(value.begin(), value.end(), next.text.begin());
    CommitBuffer(buffer, std::move(next));
}
std::string Call::String(unsigned slot) const {
    const auto& buffer = ReadBuffer(slot, BufferKind::string);
    const auto end = std::find(buffer.text.begin(), buffer.text.end(), '\0');
    if (end == buffer.text.end()) throw Error("Native string buffer is not terminated within its capacity.");
    return {buffer.text.begin(), end};
}
void Call::SetArray(unsigned slot, const std::vector<std::int32_t>& value) {
    auto& buffer = WritableBuffer(slot, BufferKind::int32);
    if (value.empty() || value.size() > buffer.spec.capacity) throw Error("SDKCall array exceeds its configured element limit.");
    Buffer next; next.spec = buffer.spec; next.cells = value;
    CommitBuffer(buffer, std::move(next));
}
std::vector<std::int32_t> Call::Array(unsigned slot) const { return ReadBuffer(slot, BufferKind::int32).cells; }
void Call::SetVector(unsigned slot, const std::array<float, 3>& value) {
    auto& buffer = WritableBuffer(slot, BufferKind::vector3);
    if (std::any_of(value.begin(), value.end(), [](float number) { return !std::isfinite(number); }))
        throw Error("SDKCall vector components must be finite.");
    Buffer next; next.spec = buffer.spec; next.vector = value;
    CommitBuffer(buffer, std::move(next));
}
std::array<float, 3> Call::Vector(unsigned slot) const {
    const auto result = ReadBuffer(slot, BufferKind::vector3).vector;
    if (std::any_of(result.begin(), result.end(), [](float number) { return !std::isfinite(number); }))
        throw Error("Native vector buffer contains a nonfinite component.");
    return result;
}
void Call::Reset() {
    state_->service->Thread();
    if (state_->busy) throw Error("SDKCall is busy.");
    state_->initialized.reset(); state_->result = false;
    for (auto& buffer : state_->buffers) {
        buffer.text.clear(); buffer.cells.clear(); buffer.vector = {};
        state_->values.arguments_[buffer.spec.argument - 1].scalar.pointer = nullptr;
    }
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
    std::vector<BufferSpec> bounds;
    bounds.reserve(state->buffers.size());
    for (const auto& buffer : state->buffers) {
        auto bound = buffer.spec;
        if (bound.kind == BufferKind::string) bound.capacity = static_cast<unsigned>(buffer.text.size());
        else if (bound.kind == BufferKind::int32) bound.capacity = static_cast<unsigned>(buffer.cells.size());
        bounds.push_back(bound);
    }
    struct Guard { bool& busy; ~Guard() { busy = false; } } guard{state->busy};
    state->busy = true;
    KeelHookValue result{};
    state->service->Invoke(*state->target,flags,state->values.arguments_,bounds,result);
    state->values.result_ = result; state->result = true;
}
}
