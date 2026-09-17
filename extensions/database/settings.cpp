#include "settings.h"
#include <json.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <set>

namespace source2root::db {
namespace {
using Json = nlohmann::json;
bool Name(const std::string& value) {
    return !value.empty() && value.size() <= 64 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}
void Keys(const Json& value, const std::set<std::string>& allowed) {
    if (!value.is_object()) throw Error("Database configuration requires objects.");
    for (const auto& [key, entry] : value.items()) if (!allowed.contains(key)) throw Error("Unknown database configuration field.");
}
std::string Text(const Json& object, const char* key, const std::string& fallback = "", std::size_t limit = 1024) {
    if (!object.contains(key)) return fallback;
    if (!object.at(key).is_string()) throw Error("Database configuration field must be a string.");
    auto value = object.at(key).get<std::string>();
    if (value.size() > limit || std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        throw Error("Database configuration string is invalid.");
    return value;
}
unsigned Number(const Json& object, const char* key, unsigned fallback, unsigned maximum) {
    if (!object.contains(key)) return fallback;
    if (!object.at(key).is_number_unsigned()) throw Error("Database configuration field must be a positive integer.");
    const auto value = object.at(key).get<std::uint64_t>();
    if (!value || value > maximum) throw Error("Database configuration number is out of range.");
    return static_cast<unsigned>(value);
}
}

Settings ReadSettings(const std::filesystem::path& file, const std::string& profile, const std::string& plugin) {
    if (!Name(profile) || plugin.empty()) throw Error("Invalid database profile or plugin identity.");
    std::ifstream stream(file, std::ios::binary);
    if (!stream) throw Error("Database configuration is unavailable.");
    std::array<char, 65537> buffer{};
    stream.read(buffer.data(), buffer.size());
    const auto length = stream.gcount();
    if (length > 65536 || stream.bad()) throw Error("Database configuration is unreadable or exceeds 64 KiB.");
    try {
        std::vector<std::set<std::string>> keys;
        const auto json = Json::parse(buffer.data(), buffer.data() + length, [&](int, Json::parse_event_t event, Json& value) {
            if (event == Json::parse_event_t::object_start) keys.emplace_back();
            else if (event == Json::parse_event_t::object_end) keys.pop_back();
            else if (event == Json::parse_event_t::key && !keys.back().insert(value.get<std::string>()).second)
                throw Error("Duplicate database configuration field.");
            return true;
        });
        Keys(json, {"schema", "connections"});
        if (json.value("schema", 0) != 1 || !json.at("connections").is_object() || json.at("connections").size() > 64)
            throw Error("Unsupported database configuration schema.");
        if (!json.at("connections").contains(profile)) throw Error("Database profile was not found.");
        const auto& config = json.at("connections").at(profile);
        Keys(config, {"driver", "database", "allow_plugins", "host", "port", "user", "password", "socket", "tls", "ca", "timeout"});
        const auto& allowed = config.at("allow_plugins");
        if (!allowed.is_array() || allowed.empty() || allowed.size() > 128) throw Error("Database profile needs an explicit plugin allow list.");
        bool permitted = false;
        for (const auto& entry : allowed) {
            if (!entry.is_string()) throw Error("Invalid database plugin allow list.");
            if (entry == plugin || entry == "*") permitted = true;
        }
        if (!permitted) throw Error("This plugin is not allowed to use the database profile.");
        Settings result;
        result.driver = Text(config, "driver");
        result.database = Text(config, "database", "", 64);
        if (result.driver == "sqlite") {
            Keys(config, {"driver", "database", "allow_plugins"});
            if (!Name(result.database)) throw Error("Invalid configured SQLite database name.");
            return result;
        }
        if (result.driver != "mysql" && result.driver != "mariadb") throw Error("Unsupported database driver.");
        if (result.database.empty()) throw Error("Database name is required.");
        result.host = Text(config, "host", result.host, 255);
        result.user = Text(config, "user", "", 128);
        result.password = Text(config, "password");
        result.socket = Text(config, "socket");
        result.ca = Text(config, "ca");
        result.port = Number(config, "port", result.port, 65535);
        result.timeout = Number(config, "timeout", result.timeout, 30);
        result.tls = config.value("tls", true);
        if (result.host.empty() || result.user.empty()) throw Error("Database host and user are required.");
        if ((!result.socket.empty() && !std::filesystem::path(result.socket).is_absolute()) ||
            (!result.ca.empty() && !std::filesystem::path(result.ca).is_absolute()))
            throw Error("Database socket and CA paths must be absolute.");
        if (!result.tls && result.socket.empty() && result.host != "127.0.0.1" && result.host != "::1" && result.host != "localhost")
            throw Error("Remote database connections require verified TLS.");
        return result;
    } catch (const Error&) { throw; }
    catch (const std::exception&) { throw Error("Invalid database configuration."); }
}

}
