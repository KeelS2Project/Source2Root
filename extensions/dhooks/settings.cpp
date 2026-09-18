#include "hooks.h"
#include <json.hpp>
#include <algorithm>
#include <fstream>
#include <limits>
#include <map>
#include <set>

namespace source2root::dhooks {
namespace {
using Json = nlohmann::json;
bool TextValid(const std::string& value, std::size_t maximum) {
    return value.size() <= maximum && std::none_of(value.begin(),value.end(),[](unsigned char c) { return c < 32 || c == 127; });
}
void Keys(const Json& value, const std::set<std::string>& allowed) {
    if (!value.is_object()) throw Error("Hook configuration requires objects.");
    for (const auto& [key,entry] : value.items()) if (!allowed.contains(key)) throw Error("Unknown hook configuration field.");
}
std::string Text(const Json& value, const char* key) {
    if (!value.contains(key)) return {};
    if (!value.at(key).is_string()) throw Error("Hook configuration field requires text.");
    return value.at(key).get<std::string>();
}
unsigned Type(const Json& value) {
    static const std::map<std::string,unsigned> types{{"void",KH_VALUE_VOID},{"bool",KH_VALUE_BOOL},
        {"int8",KH_VALUE_INT8},{"uint8",KH_VALUE_UINT8},{"int16",KH_VALUE_INT16},{"uint16",KH_VALUE_UINT16},
        {"int32",KH_VALUE_INT32},{"uint32",KH_VALUE_UINT32},{"int64",KH_VALUE_INT64},{"uint64",KH_VALUE_UINT64},
        {"pointer",KH_VALUE_POINTER},{"float32",KH_VALUE_FLOAT32},{"float64",KH_VALUE_FLOAT64}};
    if (!value.is_string()) throw Error("Hook types require scalar type names.");
    const auto found = types.find(value.get<std::string>());
    if (found == types.end()) throw Error("Unknown hook scalar type.");
    return found->second;
}
std::int64_t Number(const Json& value, const char* key) {
    if (!value.contains(key)) return 0;
    const auto& number = value.at(key);
    if (!number.is_number_integer() || (number.is_number_unsigned() && number.get<std::uint64_t>() > INT64_MAX))
        throw Error("Hook configuration number requires a signed64 integer.");
    return number.get<std::int64_t>();
}
}
void Validate(const Definition& value) {
    if (value.result > KH_VALUE_FLOAT64 || value.arguments.size() > KEELHOOK_MAX_ARGUMENTS ||
        std::any_of(value.arguments.begin(),value.arguments.end(),[](unsigned type) { return !type || type > KH_VALUE_FLOAT64; }) ||
        (value.method && (value.arguments.empty() || value.arguments.front() != KH_VALUE_POINTER)))
        throw Error("Unsupported hook prototype; method targets require a leading pointer argument.");
    if (!TextValid(value.module,4095) || !TextValid(value.symbol,512) || !TextValid(value.pattern,16384) || !TextValid(value.profile,512))
        throw Error("Invalid hook resolver text.");
    if (value.source == KH_TARGET_PROFILE) {
        if (value.symbol.empty() || value.symbol.size() > 127 || !value.module.empty() || !value.pattern.empty() || !value.profile.empty() || value.offset || value.occurrence)
            throw Error("Profile hook target requires only its symbol name.");
    } else if (value.source == KH_TARGET_SYMBOL) {
        if (value.module.empty() || value.symbol.empty() || !value.pattern.empty() || !value.profile.empty() || value.occurrence)
            throw Error("Symbol hook target requires module and symbol.");
    } else if (value.source == KH_TARGET_PATTERN) {
        if (value.module.empty() || value.pattern.empty() || value.profile.empty() || !value.symbol.empty())
            throw Error("Pattern hook target requires module, pattern and exact compatibility profile.");
    } else throw Error("Unsupported hook target source.");
}
Definition ReadDefinition(const std::filesystem::path& file, const std::string& name, const std::string& script) {
    if (name.empty() || !TextValid(name,64) || script.empty() || !TextValid(script,64)) throw Error("Invalid hook target or script name.");
    std::ifstream stream(file,std::ios::binary);
    if (!stream) throw Error("Hook target configuration is unavailable.");
    std::vector<char> buffer(131073); stream.read(buffer.data(),buffer.size()); const auto length = stream.gcount();
    if (length > 131072 || stream.bad()) throw Error("Hook configuration is unreadable or exceeds128KiB.");
    try {
        std::vector<std::set<std::string>> keys;
        const auto json = Json::parse(buffer.data(),buffer.data()+length,[&](int depth, Json::parse_event_t event, Json& value) {
            if (depth > 16) throw Error("Hook configuration nesting exceeds limit.");
            if (event == Json::parse_event_t::object_start) keys.emplace_back();
            else if (event == Json::parse_event_t::object_end) keys.pop_back();
            else if (event == Json::parse_event_t::key && !keys.back().insert(value.get<std::string>()).second)
                throw Error("Duplicate hook configuration field.");
            return true;
        });
        Keys(json,{"schema","targets"});
        if (Number(json,"schema") != 1 || !json.at("targets").is_object() || json.at("targets").size() > 64)
            throw Error("Unsupported hook configuration schema.");
        if (!json.at("targets").contains(name)) throw Error("Hook target was not found.");
        const auto& target = json.at("targets").at(name);
        Keys(target,{"allow_plugins","allow_calls","source","module","symbol","pattern","profile","offset","occurrence","method","return","arguments"});
        const auto& allowed = target.at("allow_plugins");
        if (!allowed.is_array() || allowed.empty() || allowed.size() > 128) throw Error("Hook target requires a plugin allow list.");
        bool permitted = false;
        for (const auto& plugin : allowed) {
            if (!plugin.is_string() || plugin.get<std::string>().empty() || !TextValid(plugin.get<std::string>(),64))
                throw Error("Invalid hook plugin allow list.");
            if (plugin == script || plugin == "*") permitted = true;
        }
        if (!permitted) throw Error("This script is not allowed to use the hook target.");
        Definition result;
        const auto source = Text(target,"source");
        if (source == "profile") result.source = KH_TARGET_PROFILE;
        else if (source == "symbol") result.source = KH_TARGET_SYMBOL;
        else if (source == "pattern") result.source = KH_TARGET_PATTERN;
        else throw Error("Unknown hook target source.");
        result.module = Text(target,"module"); result.symbol = Text(target,"symbol");
        result.pattern = Text(target,"pattern"); result.profile = Text(target,"profile");
        result.offset = Number(target,"offset");
        const auto occurrence = Number(target,"occurrence");
        if (occurrence < 0 || occurrence > UINT32_MAX) throw Error("Hook pattern occurrence is out of range.");
        result.occurrence = static_cast<unsigned>(occurrence);
        if (target.contains("method") && !target.at("method").is_boolean()) throw Error("Hook method flag must be boolean.");
        if (target.contains("allow_calls") && !target.at("allow_calls").is_boolean()) throw Error("Hook direct-call flag must be boolean.");
        result.allow_calls = target.value("allow_calls",false);
        result.method = target.value("method",false); result.result = Type(target.at("return"));
        const auto& arguments = target.at("arguments");
        if (!arguments.is_array() || arguments.size() > KEELHOOK_MAX_ARGUMENTS) throw Error("Hook arguments require at most32 scalar types.");
        for (const auto& type : arguments) result.arguments.push_back(Type(type));
        Validate(result); return result;
    } catch (const Error&) { throw; }
    catch (const std::exception&) { throw Error("Invalid hook configuration."); }
}
}
