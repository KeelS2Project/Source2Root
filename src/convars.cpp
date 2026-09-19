#include "foundation.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace sr {

bool Foundation::ConVarError(Script& script, const std::string& name, KeelResult result) {
    const auto detail = result == KEEL_RESULT_NOT_FOUND ? "not found" :
        result == KEEL_RESULT_ALREADY_EXISTS ? "name already registered" :
        result == KEEL_RESULT_INCOMPATIBLE ? "incompatible definition" :
        result == KEEL_RESULT_BUSY ? "still in use; retry cleanup" :
        result == KEEL_RESULT_NOT_READY ? "service not ready" :
        result == KEEL_RESULT_UNSUPPORTED ? "unsupported by the game host" : "engine operation failed";

    script.error = "ConVar " + name + ": " + detail;

    if (settings_.debug)
        host_.Log(script.manifest.id + ": " + script.error + " (KeelResult " + std::to_string(result) + ")");

    return false;
}

void Foundation::ValidateConVar(const ConVarDefinition& definition) {
    if (!definition.name.starts_with("sr_") || definition.name.size() < 4 || !ValidIdentifier(definition.name) ||
        definition.description.size() > 512)
        throw NativeError("invalid sr_ ConVar name or description");

    if (const auto* text = std::get_if<std::string>(&definition.initial)) {
        if (text->size() > 512)
            throw NativeError("ConVar strings accept at most 512 bytes");

        return;
    }

    if (definition.initial.index() != definition.minimum.index() ||
        definition.initial.index() != definition.maximum.index())
        throw NativeError("ConVar bounds must match its type");

    if (const auto* value = std::get_if<float>(&definition.initial)) {
        const auto minimum = std::get<float>(definition.minimum), maximum = std::get<float>(definition.maximum);

        if (!std::isfinite(*value) || !std::isfinite(minimum) || !std::isfinite(maximum) ||
            minimum > maximum || *value < minimum || *value > maximum)
            throw NativeError("ConVar default and bounds must form a finite, ordered range");
    } else {
        const auto integer = std::get<Cell>(definition.initial);
        const auto minimum = std::get<Cell>(definition.minimum), maximum = std::get<Cell>(definition.maximum);

        if (minimum > maximum || integer < minimum || integer > maximum)
            throw NativeError("ConVar default must be within its ordered bounds");
    }
}

Cell Foundation::CreateConVar(Script& script, ConVarDefinition definition) {
    Limit(script);
    ValidateConVar(definition);
    const auto name = definition.name;

    if (script.convars.contains(name)) {
        ConVarError(script, name, KEEL_RESULT_ALREADY_EXISTS);
        return 0;
    }

    auto found = convars_.find(name);

    if (found != convars_.end()) {
        for (const auto* owner : found->second.owners)
            if (owner->manifest.id != script.manifest.id) {
                ConVarError(script, name, KEEL_RESULT_ALREADY_EXISTS);
                return 0;
            }

        if (!(found->second.definition == definition)) {
            script.error = "ConVar " + name + ": definition changed; use a new name or restart the server";
            return 0;
        }
    }

    const auto handle = handles_.Add(script.owner, ConVarType, ScriptConVar{name});
    const bool create = found == convars_.end();

    try {
        if (create)
            found = convars_.emplace(name, Variable{std::move(definition), 0, {}}).first;

        found->second.owners.insert(&script);
        script.convars.emplace(name, handle);
    } catch (...) {
        if (found != convars_.end()) {
            found->second.owners.erase(&script);

            if (found->second.owners.empty())
                convars_.erase(found);
        }

        handles_.Remove(handle, script.owner, ConVarType);
        throw;
    }

    if (create) {
        const auto result = host_.CreateConVar(found->second.definition, found->second.native);

        if (result != KEEL_RESULT_OK) {
            script.convars.erase(name);
            handles_.Remove(handle, script.owner, ConVarType);
            convars_.erase(found);
            ConVarError(script, name, result);
            return 0;
        }
    }

    return handle;
}

bool Foundation::ReadConVar(Script& script, Cell handle, ConVarValue& value) {
    const auto& name = std::get<ScriptConVar>(handles_.Get(handle, script.owner, ConVarType)).name;
    const auto& variable = convars_.at(name);
    const auto result = host_.ReadConVar(variable.native, value);

    if (result != KEEL_RESULT_OK)
        return ConVarError(script, name, result);

    if (value.index() != variable.definition.initial.index() ||
        (std::holds_alternative<float>(value) && !std::isfinite(std::get<float>(value))))
        return ConVarError(script, name, KEEL_RESULT_INCOMPATIBLE);

    return true;
}

bool Foundation::WriteConVar(Script& script, Cell handle, const ConVarValue& value) {
    const auto& name = std::get<ScriptConVar>(handles_.Get(handle, script.owner, ConVarType)).name;
    const auto& variable = convars_.at(name);

    if (script.state != PluginState::Running) {
        script.error = "set ConVars after initialization, while the script is running";
        return false;
    }

    if (value.index() != variable.definition.initial.index())
        return ConVarError(script, name, KEEL_RESULT_INCOMPATIBLE);

    auto bounded = value;

    if (const auto* number = std::get_if<float>(&value)) {
        if (!std::isfinite(*number))
            throw NativeError("ConVar value must be finite");

        bounded = std::clamp(
            *number, std::get<float>(variable.definition.minimum), std::get<float>(variable.definition.maximum));
    } else if (const auto* number = std::get_if<Cell>(&value)) {
        bounded = std::clamp(*number, std::get<Cell>(variable.definition.minimum), std::get<Cell>(variable.definition.maximum));
    } else if (std::get<std::string>(value).size() > 512)
        throw NativeError("ConVar strings accept at most 512 bytes");

    const auto result = host_.WriteConVar(variable.native, bounded);
    return result == KEEL_RESULT_OK || ConVarError(script, name, result);
}

bool Foundation::CloseConVar(Script& script, const std::string& name) {
    const auto owned = script.convars.find(name);

    if (owned == script.convars.end())
        return ConVarError(script, name, KEEL_RESULT_NOT_FOUND);

    auto found = convars_.find(name);

    if (found == convars_.end())
        return ConVarError(script, name, KEEL_RESULT_NOT_FOUND);

    auto& variable = found->second;

    if (variable.owners.size() == 1 && variable.native) {
        const auto result = host_.ReleaseConVar(variable.native);

        if (result != KEEL_RESULT_OK && result != KEEL_RESULT_NOT_FOUND)
            return ConVarError(script, name, result);
    }

    variable.owners.erase(&script);

    if (variable.owners.empty())
        convars_.erase(found);

    if (handles_.Contains(owned->second, script.owner, ConVarType))
        handles_.Remove(owned->second, script.owner, ConVarType);

    script.convars.erase(owned);
    return true;
}

std::string Foundation::ConVarText(const ConVarValue& value) {
    if (const auto* number = std::get_if<Cell>(&value))
        return std::to_string(*number);

    std::ostringstream output;

    if (const auto* number = std::get_if<float>(&value)) {
        output << std::setprecision(std::numeric_limits<float>::max_digits10) << *number;
    } else {
        output << '"';

        for (unsigned char c : std::get<std::string>(value)) {
            if (c == '"' || c == '\\')
                output << '\\' << static_cast<char>(c);
            else if (c < 32 || c == 127)
                output << "\\x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(c);
            else
                output << static_cast<char>(c);
        }

        output << '"';
    }

    return output.str();
}

void Foundation::BindConVars(Script& script) {
    auto bind = [&](const char* name, int count, auto callback) {
        runtime_.Bind(*script.vm, name, count, [&, callback](const Arguments& args) -> Cell {
            Thread();
            return callback(args);
        });
    };
    bind("CreateIntConVar", 5, [&](const Arguments& args) {
        return CreateConVar(script, {args.String(1, 64), args.String(3), args.Int(2), args.Int(4), args.Int(5)});
    });
    bind("CreateFloatConVar", 5, [&](const Arguments& args) {
        return CreateConVar(script, {args.String(1, 64), args.String(3), std::bit_cast<float>(args.Int(2)),
            std::bit_cast<float>(args.Int(4)), std::bit_cast<float>(args.Int(5))});
    });
    bind("CreateStringConVar", 3, [&](const Arguments& args) {
        return CreateConVar(script, {args.String(1, 64), args.String(3), args.String(2), Cell{0}, Cell{0}});
    });
    bind("GetConVarInt", 2, [&](const Arguments& args) {
        args.OutputCell(2, 0);
        ConVarValue value;

        if (!ReadConVar(script, args.Int(1), value))
            return 0;

        const auto* number = std::get_if<Cell>(&value);

        if (!number) {
            script.error = "ConVar is not an integer";
            return 0;
        }

        args.OutputCell(2, *number);
        return 1;
    });
    bind("GetConVarFloat", 2, [&](const Arguments& args) {
        args.OutputCell(2, 0);
        ConVarValue value;

        if (!ReadConVar(script, args.Int(1), value))
            return 0;

        const auto* number = std::get_if<float>(&value);

        if (!number) {
            script.error = "ConVar is not a float";
            return 0;
        }

        args.OutputCell(2, std::bit_cast<Cell>(*number));
        return 1;
    });
    bind("GetConVarString", 3, [&](const Arguments& args) {
        args.Output(2, args.Int(3), "");
        ConVarValue value;

        if (!ReadConVar(script, args.Int(1), value))
            return 0;

        const auto* text = std::get_if<std::string>(&value);

        if (!text) {
            script.error = "ConVar is not a string";
            return 0;
        }

        args.Output(2, args.Int(3), *text);
        return 1;
    });
    bind("SetConVarInt", 2, [&](const Arguments& args) {
        return WriteConVar(script, args.Int(1), args.Int(2));
    });
    bind("SetConVarFloat", 2, [&](const Arguments& args) {
        return WriteConVar(script, args.Int(1), std::bit_cast<float>(args.Int(2)));
    });
    bind("SetConVarString", 2, [&](const Arguments& args) {
        return WriteConVar(script, args.Int(1), args.String(2));
    });
    bind("CloseConVar", 1, [&](const Arguments& args) {
        const auto name = std::get<ScriptConVar>(handles_.Get(args.Int(1), script.owner, ConVarType)).name;
        return CloseConVar(script, name);
    });
}

}
