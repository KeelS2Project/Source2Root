#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace source2root::db {

class Error : public std::runtime_error { public: using std::runtime_error::runtime_error; };
using Parameter = std::variant<std::monostate, std::int32_t, double, std::string>;
struct QueryInput {
    std::string sql;
    std::vector<std::optional<Parameter>> parameters;
    void Bind(int index, Parameter value);
    void Validate() const;
};
struct QueryValue {
    bool null = false;
    std::string text;
    std::optional<std::int32_t> integer;
    std::optional<float> number;
};
struct QueryResult {
    int columns = 0, changes = 0;
    std::string inserted = "0";
    std::vector<std::vector<QueryValue>> rows;
};

}
