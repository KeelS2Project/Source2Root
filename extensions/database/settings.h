#pragma once

#include "types.h"
#include <filesystem>

namespace source2root::db {

struct Settings {
    std::string driver, database, host = "127.0.0.1", user, password, socket, ca;
    unsigned port = 3306, timeout = 3;
    bool tls = true;
};
Settings ReadSettings(const std::filesystem::path& file, const std::string& profile, const std::string& plugin);

}
