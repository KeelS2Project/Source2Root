#pragma once

#include <bit>
#include <cstdint>
#include <string>
#include <variant>

namespace sr {

using ConVarValue = std::variant<std::int32_t, float, std::string>;

struct ConVarDefinition {
    std::string name, description;
    ConVarValue initial, minimum, maximum;
    bool operator==(const ConVarDefinition& other) const {
        return name == other.name && description == other.description && Same(initial, other.initial) &&
            Same(minimum, other.minimum) && Same(maximum, other.maximum);
    }
private:
    static bool Same(const ConVarValue& a, const ConVarValue& b) {
        if (a.index() != b.index()) return false;
        if (const auto* number = std::get_if<float>(&a))
            return std::bit_cast<std::uint32_t>(*number) == std::bit_cast<std::uint32_t>(std::get<float>(b));
        return a == b;
    }
};

}
