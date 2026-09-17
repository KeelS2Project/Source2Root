#pragma once

#include <source2root/native.h>
#include <array>
#include <bit>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace source2root {

class NativeCall {
public:
    explicit NativeCall(const SrNativeCall& call) : call_(call) {}
    std::uint64_t Owner() const { return call_.script_owner; }
    SrCallback Callback(unsigned index) const {
        SrCallback token = 0;
        Check(call_.capture_callback(call_.context, index, &token));
        return token;
    }
    std::int32_t Int(unsigned index) const {
        std::int32_t value = 0;
        Check(call_.read_cell(call_.context, index, &value));
        return value;
    }
    float Float(unsigned index) const { return std::bit_cast<float>(Int(index)); }
    std::string String(unsigned index) const {
        std::array<char, SR_NATIVE_BUFFER_LIMIT> value{};
        Check(call_.read_string(call_.context, index, value.data(), value.size()));
        return value.data();
    }
    std::vector<std::int32_t> Array(unsigned index, unsigned count) const {
        if (count > SR_NATIVE_BUFFER_LIMIT / sizeof(std::int32_t)) throw std::invalid_argument("Array is too large.");
        std::vector<std::int32_t> values(count);
        Check(call_.read_array(call_.context, index, values.data(), count));
        return values;
    }
    void Output(unsigned index, unsigned capacity, const std::string& value) const {
        Check(call_.write_string(call_.context, index, capacity, value.c_str()));
    }
    void OutputCell(unsigned index, std::int32_t value) const {
        Check(call_.write_cell(call_.context, index, value));
    }
    void OutputArray(unsigned index, unsigned capacity, const std::vector<std::int32_t>& values) const {
        Check(call_.write_array(call_.context, index, capacity, values.data(), values.size()));
    }
    std::string DataPath(bool shared = false) const {
        std::array<char, SR_NATIVE_BUFFER_LIMIT> value{};
        Check(call_.data_path(call_.context, shared ? KEEL_TRUE : KEEL_FALSE, value.data(), value.size()));
        return value.data();
    }
    std::string ConfigPath() const {
        std::array<char, SR_NATIVE_BUFFER_LIMIT> value{};
        Check(call_.config_path(call_.context, value.data(), value.size()));
        return value.data();
    }
    std::string ScriptId() const {
        std::array<char, SR_NATIVE_BUFFER_LIMIT> value{};
        Check(call_.script_id(call_.context, value.data(), value.size()));
        return value.data();
    }
    bool Player(std::int32_t handle, SrPlayerIdentity& player) const {
        player = {sizeof(player), -1, 0, 0, KEEL_FALSE, KEEL_FALSE};
        const auto result = call_.player_identity(call_.context, handle, &player);
        if (result == KEEL_RESULT_NOT_FOUND) return false;
        Check(result);
        return true;
    }
    template <typename T>
    std::int32_t Own(std::uint32_t type, std::unique_ptr<T> value) const {
        if (!value) throw std::invalid_argument("Resource is empty.");
        std::int32_t handle = 0;
        Check(call_.create_resource(call_.context, type, value.get(), [](void* raw) noexcept {
            delete static_cast<T*>(raw);
        }, &handle));
        value.release();
        return handle;
    }
    template <typename T>
    T& Resource(std::int32_t handle, std::uint32_t type) const {
        void* value = nullptr;
        Check(call_.get_resource(call_.context, handle, type, &value));
        return *static_cast<T*>(value);
    }
    void Close(std::int32_t handle, std::uint32_t type) const {
        Check(call_.close_resource(call_.context, handle, type));
    }
    std::int32_t Fail(const std::string& error, std::int32_t result = 0) const {
        Check(call_.set_error(call_.context, error.c_str()));
        return result;
    }
private:
    const SrNativeCall& call_;
    void Check(KeelResult result) const {
        if (result != KEEL_RESULT_OK) throw std::invalid_argument(call_.error && *call_.error ? call_.error : "Native call failed.");
    }
};

}
