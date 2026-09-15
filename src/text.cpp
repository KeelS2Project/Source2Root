#include "foundation.h"
#include "text.h"

#include <algorithm>
#include <charconv>

namespace sr {

std::vector<std::string> ParseArguments(const std::string& text) {
    if (text.size() > 1024 || std::any_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127 || c == ';'; }))
        throw std::runtime_error("Invalid command syntax.");
    std::vector<std::string> result;
    std::size_t position = 0;
    while (position < text.size()) {
        if (text[position] == ' ') { ++position; continue; }
        if (result.size() == 32) throw std::runtime_error("Too many arguments (maximum 32).");
        const bool quoted = text[position] == '"';
        if (quoted) ++position;
        bool closed = !quoted;
        std::string token;
        while (position < text.size()) {
            char c = text[position++];
            if (c == '"') {
                if (!quoted || (position < text.size() && text[position] != ' '))
                    throw std::runtime_error("Quotes must surround a complete argument.");
                closed = true; break;
            }
            if (!quoted && c == ' ') break;
            if (c == '\\' && position < text.size() && (text[position] == '\\' || text[position] == '"')) c = text[position++];
            token += c;
            if (token.size() > 256) throw std::runtime_error("An argument exceeds 256 bytes.");
        }
        if (!closed) throw std::runtime_error("Unterminated quoted argument.");
        result.push_back(std::move(token));
    }
    return result;
}

void Foundation::BindText(Script& script) {
    runtime_.Bind(*script.vm, "Format", 3, [&](const Arguments& args) -> Cell {
        Thread();
        const auto capacity = args.Int(2);
        if (capacity < 1 || capacity > 4096) throw NativeError("output capacity out of bounds");
        const auto text = args.Format(3).substr(0, capacity - 1);
        args.Output(1, capacity, text);
        return static_cast<Cell>(text.size());
    }, 19);
    auto parse = [&](const Arguments& args, std::vector<std::string>& result) {
        try { result = ParseArguments(args.String(1, 1024)); return true; }
        catch (const NativeError&) { throw; }
        catch (const std::exception& error) { script.error = error.what(); return false; }
    };
    runtime_.Bind(*script.vm, "GetArgumentCount", 1, [&, parse](const Arguments& args) -> Cell {
        Thread(); std::vector<std::string> values;
        return parse(args, values) ? static_cast<Cell>(values.size()) : Cell{-1};
    });
    auto bind_argument = [&](const char* name, bool remaining) {
        runtime_.Bind(*script.vm, name, 4, [&, remaining](const Arguments& args) -> Cell {
            Thread();
            const auto text = args.String(1, 1024);
            args.Output(3, args.Int(4), "");
            std::vector<std::string> values;
            try { values = ParseArguments(text); }
            catch (const std::exception& error) { script.error = error.what(); return 0; }
            const auto index = args.Int(2);
            if (index < 0 || static_cast<std::size_t>(index) > values.size() || (!remaining && static_cast<std::size_t>(index) == values.size())) {
                script.error = "Argument index is out of range."; return 0;
            }
            std::string output;
            for (std::size_t i = index; i < values.size(); ++i) {
                if (i != static_cast<std::size_t>(index)) output += ' ';
                output += values[i];
                if (!remaining) break;
            }
            if (output.size() >= static_cast<std::size_t>(args.Int(4))) { script.error = "Argument output buffer is too small."; return 0; }
            args.Output(3, args.Int(4), output); return 1;
        });
    };
    bind_argument("GetArgument", false);
    bind_argument("GetRemainingArguments", true);
    runtime_.Bind(*script.vm, "ParseInt", 4, [&](const Arguments& args) -> Cell {
        Thread();
        const auto text = args.String(1, 1024);
        args.OutputCell(2, 0);
        if (args.Int(3) > args.Int(4)) throw NativeError("invalid integer bounds");
        Cell value = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{} || end != text.data() + text.size() || value < args.Int(3) || value > args.Int(4)) {
            script.error = "Expected an integer from " + std::to_string(args.Int(3)) + " to " + std::to_string(args.Int(4)) + "."; return 0;
        }
        args.OutputCell(2, value); return 1;
    });
}

}
