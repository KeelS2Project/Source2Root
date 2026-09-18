#include "hooks.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <utility>
#include <variant>

namespace source2root::dhooks {
namespace {
using IntegerValue = std::variant<std::int64_t,std::uint64_t>;
IntegerValue ReadInteger(const KeelHookValue& value) {
    switch (value.type) {
        case KH_VALUE_BOOL: return std::uint64_t{value.scalar.boolean};
        case KH_VALUE_INT8: return std::int64_t{value.scalar.int8};
        case KH_VALUE_UINT8: return std::uint64_t{value.scalar.uint8};
        case KH_VALUE_INT16: return std::int64_t{value.scalar.int16};
        case KH_VALUE_UINT16: return std::uint64_t{value.scalar.uint16};
        case KH_VALUE_INT32: return std::int64_t{value.scalar.int32};
        case KH_VALUE_UINT32: return std::uint64_t{value.scalar.uint32};
        case KH_VALUE_INT64: return value.scalar.int64;
        case KH_VALUE_UINT64: return value.scalar.uint64;
        default: throw Error("Hook value is not an integer or boolean.");
    }
}
template<typename T> T Narrow(const IntegerValue& source) {
    return std::visit([](auto number) -> T {
        if (!std::in_range<T>(number)) throw Error("Integer is outside the hook value's range.");
        return static_cast<T>(number);
    },source);
}
void WriteInteger(KeelHookValue& value, const IntegerValue& source) {
    switch (value.type) {
        case KH_VALUE_BOOL: {
            const auto number = Narrow<std::uint32_t>(source);
            if (number > 1) throw Error("Boolean hook value requires zero or one.");
            value.scalar.boolean = number; break;
        }
        case KH_VALUE_INT8: value.scalar.int8 = Narrow<std::int8_t>(source); break;
        case KH_VALUE_UINT8: value.scalar.uint8 = Narrow<std::uint8_t>(source); break;
        case KH_VALUE_INT16: value.scalar.int16 = Narrow<std::int16_t>(source); break;
        case KH_VALUE_UINT16: value.scalar.uint16 = Narrow<std::uint16_t>(source); break;
        case KH_VALUE_INT32: value.scalar.int32 = Narrow<std::int32_t>(source); break;
        case KH_VALUE_UINT32: value.scalar.uint32 = Narrow<std::uint32_t>(source); break;
        case KH_VALUE_INT64: value.scalar.int64 = Narrow<std::int64_t>(source); break;
        case KH_VALUE_UINT64: value.scalar.uint64 = Narrow<std::uint64_t>(source); break;
        default: throw Error("Hook value is not an integer or boolean.");
    }
}
double ReadNumber(const KeelHookValue& value) {
    if (value.type == KH_VALUE_FLOAT32) return value.scalar.float32;
    if (value.type == KH_VALUE_FLOAT64) return value.scalar.float64;
    throw Error("Hook value is not a float.");
}
void WriteNumber(KeelHookValue& value, double number) {
    if (!std::isfinite(number)) throw Error("Hook float setter requires a finite value.");
    if (value.type == KH_VALUE_FLOAT64) value.scalar.float64 = number;
    else if (value.type == KH_VALUE_FLOAT32) {
        if (std::abs(number) > std::numeric_limits<float>::max()) throw Error("Hook float32 value is out of range.");
        value.scalar.float32 = static_cast<float>(number);
    } else throw Error("Hook value is not a float.");
}
}
Frame::Frame(const Definition& definition) : phase_(KH_PHASE_PRE), flags_(0), result_{} {
    Validate(definition);
    result_.type = definition.result;
    arguments_.resize(definition.arguments.size());
    for (unsigned i = 0; i < arguments_.size(); ++i) arguments_[i].type = definition.arguments[i];
}
Frame::Frame(KeelHookFrame& frame, const Definition& definition) : phase_(frame.phase), flags_(frame.flags), result_(frame.result) {
    if (frame.size != sizeof(frame) || (frame.phase != KH_PHASE_PRE && frame.phase != KH_PHASE_POST) ||
        frame.argument_count != definition.arguments.size() || (frame.argument_count && !frame.arguments) ||
        frame.result.type != definition.result || frame.result.reserved || (frame.flags & ~(KH_FRAME_ORIGINAL_CALLED | KH_FRAME_RECALLED)))
        throw Error("Invalid native hook frame.");
    if (frame.argument_count) arguments_.assign(frame.arguments,frame.arguments + frame.argument_count);
    for (unsigned i = 0; i < arguments_.size(); ++i)
        if (arguments_[i].type != definition.arguments[i] || arguments_[i].reserved)
            throw Error("Native hook argument type mismatch.");
}
const KeelHookValue& Frame::Value(unsigned slot) const {
    if (slot > arguments_.size()) throw Error("Hook value slot is out of range.");
    return slot ? arguments_[slot - 1] : result_;
}
KeelHookValue& Frame::Writable(unsigned slot) {
    Value(slot);
    if (slot && phase_ != KH_PHASE_PRE) throw Error("Post hooks cannot change arguments.");
    return slot ? arguments_[slot - 1] : result_;
}
unsigned Frame::Type(unsigned slot) const { return Value(slot).type; }
std::int32_t Frame::Integer(unsigned slot) const { return Narrow<std::int32_t>(ReadInteger(Value(slot))); }
std::string Frame::IntegerText(unsigned slot) const {
    return std::visit([](auto number) { return std::to_string(number); },ReadInteger(Value(slot)));
}
float Frame::Number(unsigned slot) const {
    const auto number = ReadNumber(Value(slot));
    if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max()) throw Error("Hook float is not representable as finite float32.");
    return static_cast<float>(number);
}
std::string Frame::NumberText(unsigned slot) const {
    std::array<char,64> buffer{};
    const auto [end,error] = std::to_chars(buffer.data(),buffer.data()+buffer.size(),ReadNumber(Value(slot)),std::chars_format::general,
        std::numeric_limits<double>::max_digits10);
    if (error != std::errc{}) throw Error("Cannot format hook float.");
    return {buffer.data(),end};
}
bool Frame::IsNull(unsigned slot) const {
    const auto& value = Value(slot);
    if (value.type != KH_VALUE_POINTER) throw Error("Hook value is not a pointer.");
    return value.scalar.pointer == nullptr;
}
void Frame::SetInteger(unsigned slot, std::int32_t value) { WriteInteger(Writable(slot),std::int64_t{value}); }
void Frame::SetIntegerText(unsigned slot, const std::string& text) {
    if (text.empty() || text.size() > 20) throw Error("Hook integer text is invalid.");
    const auto parse = [&]<typename T>() -> IntegerValue {
        T value{}; const auto [end,error] = std::from_chars(text.data(),text.data()+text.size(),value);
        if (error != std::errc{} || end != text.data()+text.size()) throw Error("Hook integer text is invalid or out of range.");
        return value;
    };
    const auto value = text.front() == '-' ? parse.template operator()<std::int64_t>() : parse.template operator()<std::uint64_t>();
    WriteInteger(Writable(slot),value);
}
void Frame::SetNumber(unsigned slot, float value) { WriteNumber(Writable(slot),value); }
void Frame::SetNumberText(unsigned slot, const std::string& text) {
    if (text.empty() || text.size() > 64) throw Error("Hook float text is invalid.");
    double value{}; const auto [end,error] = std::from_chars(text.data(),text.data()+text.size(),value,std::chars_format::general);
    if (error != std::errc{} || end != text.data()+text.size()) throw Error("Hook float text is invalid or out of range.");
    WriteNumber(Writable(slot),value);
}
void Frame::SetNull(unsigned slot) {
    auto& value = Writable(slot);
    if (value.type != KH_VALUE_POINTER) throw Error("Hook value is not a pointer.");
    value.scalar.pointer = nullptr;
}
void Frame::Copy(unsigned destination, unsigned source) {
    const auto copy = Value(source);
    auto& value = Writable(destination);
    if (value.type == KH_VALUE_VOID || value.type != copy.type) throw Error("Hook copy requires identical non-void types.");
    value = copy;
}
void Frame::Commit(KeelHookFrame& frame, unsigned action) const {
    if (phase_ == KH_PHASE_PRE) std::copy(arguments_.begin(),arguments_.end(),frame.arguments);
    if (action != KH_ACTION_CONTINUE) frame.result = result_;
}
}
