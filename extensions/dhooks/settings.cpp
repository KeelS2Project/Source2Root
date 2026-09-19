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
    std::set<unsigned> buffer_arguments, length_arguments;
    unsigned buffer_bytes = 0;
    for (const auto& buffer : value.buffers) {
        if (!buffer.argument || buffer.argument > value.arguments.size() ||
            value.arguments[buffer.argument - 1] != KH_VALUE_POINTER ||
            !buffer_arguments.insert(buffer.argument).second || (value.method && buffer.argument == 1))
            throw Error("Buffer adapter requires one distinct pointer argument, excluding the method object.");
        if (buffer.length_argument) {
            if (buffer.length_argument > value.arguments.size() ||
                (value.arguments[buffer.length_argument - 1] != KH_VALUE_INT32 &&
                 value.arguments[buffer.length_argument - 1] != KH_VALUE_UINT32) ||
                !length_arguments.insert(buffer.length_argument).second)
                throw Error("Buffer length requires one distinct int32 or uint32 argument.");
        }
        switch (buffer.kind) {
            case BufferKind::string:
                if (!buffer.capacity || buffer.capacity > 4096) throw Error("String buffer capacity must be 1..4096 bytes.");
                buffer_bytes += buffer.capacity;
                break;
            case BufferKind::int32:
                if (!buffer.capacity || buffer.capacity > 1024 || !buffer.length_argument)
                    throw Error("Integer array needs a 1..1024 element limit and a length argument.");
                buffer_bytes += buffer.capacity * sizeof(std::int32_t);
                break;
            case BufferKind::vector3:
                if (buffer.capacity != 3 || buffer.length_argument) throw Error("Vector adapter requires exactly three floats and no length argument.");
                buffer_bytes += 3 * sizeof(float);
                break;
            default: throw Error("Unsupported SDKCall buffer adapter.");
        }
        if (buffer_bytes > 16384) throw Error("Configured SDKCall buffers exceed 16 KiB per call.");
    }
    std::set<unsigned> entity_arguments;
    for (const auto& entity : value.entities) {
        if (!entity.argument || entity.argument > value.arguments.size() || value.arguments[entity.argument - 1] != KH_VALUE_POINTER ||
            buffer_arguments.contains(entity.argument) || !entity_arguments.insert(entity.argument).second)
            throw Error("Entity adapter requires one distinct pointer argument without a buffer adapter.");
        if (entity.class_name.empty() || entity.class_name.size() > 255 ||
            std::any_of(entity.class_name.begin(), entity.class_name.end(), [](unsigned char c) {
                return !((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == ':');
            })) throw Error("Entity adapter requires an exact schema class name.");
    }
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
        Keys(target,{"allow_plugins","allow_calls","source","module","symbol","pattern","profile","offset","occurrence","method","return","arguments","buffers","entities","sdkhook"});
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
        if (target.contains("buffers")) {
            const auto& buffers = target.at("buffers");
            if (!buffers.is_array() || buffers.size() > KEELHOOK_MAX_ARGUMENTS) throw Error("SDKCall buffers require an array of at most 32 adapters.");
            for (const auto& entry : buffers) {
                Keys(entry,{"argument","kind","capacity","length_argument"});
                BufferSpec buffer;
                const auto kind = Text(entry,"kind");
                if (kind == "string") buffer.kind = BufferKind::string;
                else if (kind == "int32") buffer.kind = BufferKind::int32;
                else if (kind == "vector3") {
                    Keys(entry,{"argument","kind"}); buffer.kind = BufferKind::vector3;
                } else throw Error("Unknown SDKCall buffer kind.");
                const auto argument = Number(entry,"argument"), length_argument = Number(entry,"length_argument");
                const auto capacity = buffer.kind == BufferKind::vector3 ? 3 : Number(entry,"capacity");
                if (argument < 1 || argument > KEELHOOK_MAX_ARGUMENTS || length_argument < 0 ||
                    length_argument > KEELHOOK_MAX_ARGUMENTS || capacity < 1 || capacity > 4096)
                    throw Error("SDKCall buffer configuration is out of range.");
                buffer.argument = static_cast<unsigned>(argument);
                buffer.capacity = static_cast<unsigned>(capacity);
                buffer.length_argument = static_cast<unsigned>(length_argument);
                result.buffers.push_back(buffer);
            }
        }
        if (target.contains("entities")) {
            const auto& entities = target.at("entities");
            if (!entities.is_array() || entities.size() > KEELHOOK_MAX_ARGUMENTS) throw Error("SDKCall entities require at most 32 adapters.");
            for (const auto& entry : entities) {
                Keys(entry,{"argument","class"});
                const auto argument = Number(entry,"argument");
                if (argument < 1 || argument > KEELHOOK_MAX_ARGUMENTS) throw Error("Entity argument is out of range.");
                result.entities.push_back({static_cast<unsigned>(argument),Text(entry,"class")});
            }
        }
        if (target.contains("sdkhook")) {
            const auto& policy = target.at("sdkhook");
            Keys(policy,{"kind","class","block"});
            result.entity_hook = {Text(policy,"kind"),Text(policy,"class"),Text(policy,"block")};
            if (result.entity_hook.kind.empty() || result.entity_hook.kind.size() > 32 ||
                result.entity_hook.class_name.empty() || result.entity_hook.class_name.size() > 255 ||
                result.entity_hook.block.size() > 32) throw Error("Invalid SDKHooks target policy.");
        }
        Validate(result); return result;
    } catch (const Error&) { throw; }
    catch (const std::exception&) { throw Error("Invalid hook configuration."); }
}
}
