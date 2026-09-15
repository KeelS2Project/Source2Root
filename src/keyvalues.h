#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace sr {

struct KeyValue {
    std::string name, value;
    std::vector<KeyValue> children;
    bool object = false;
};

std::vector<KeyValue> ReadKeyValues(const std::filesystem::path& file);

}
