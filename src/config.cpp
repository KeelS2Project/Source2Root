#include "config.h"

#include <charconv>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace sr {

std::string CoreSettings::Value(std::string_view name) const {
    if (name == "sr_show_activity")
        return std::to_string(activity);

    if (name == "sr_chat_public_trigger")
        return public_trigger;

    if (name == "sr_chat_silent_trigger")
        return silent_trigger;

    if (name == "sr_debug")
        return std::to_string(debug);

    throw std::runtime_error("unknown core setting: " + std::string(name));
}

void CoreSettings::SetField(std::string_view name, const std::string& value) {
    if (name == "sr_chat_public_trigger" || name == "sr_chat_silent_trigger") {
        if (value.size() > 8)
            throw std::runtime_error("chat triggers accept at most 8 characters");

        for (unsigned char c : value)
            if (c <= 32 || c >= 127 || c == '"' || c == ';' || c == '\\')
                throw std::runtime_error("chat triggers accept printable characters without spaces, quotes, semicolons or backslashes");

        (name == "sr_chat_public_trigger" ? public_trigger : silent_trigger) = value;
    } else if (name == "sr_show_activity" || name == "sr_debug") {
        int number = 0;
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
        const int maximum = name == "sr_show_activity" ? 15 : 1;

        if (error != std::errc{} || end != value.data() + value.size() || number < 0 || number > maximum)
            throw std::runtime_error(std::string(name) + " must be an integer from 0 to " + std::to_string(maximum));

        (name == "sr_show_activity" ? activity : debug) = number;
    } else
        throw std::runtime_error("unknown core setting: " + std::string(name));
}

void CoreSettings::Validate() const {
    if (!public_trigger.empty() && public_trigger == silent_trigger)
        throw std::runtime_error("public and silent chat triggers must differ");
}

void CoreSettings::Set(std::string_view name, const std::string& value) {
    auto candidate = *this;
    candidate.SetField(name, value);
    candidate.Validate();
    *this = std::move(candidate);
}

void CoreSettings::SetAll(const std::array<std::string, 4>& values) {
    auto candidate = *this;

    for (std::size_t i = 0; i < Names.size(); ++i)
        candidate.SetField(Names[i], values[i]);

    candidate.Validate();
    *this = std::move(candidate);
}

std::vector<std::string> CoreSettings::ParseLine(const std::string& line) {
    std::vector<std::string> words;
    std::size_t position = 0;

    while (position < line.size()) {
        while (position < line.size() && (line[position] == ' ' || line[position] == '\t' || line[position] == '\r'))
            ++position;

        if (position == line.size() || line.compare(position, 2, "//") == 0)
            break;

        const bool quoted = line[position] == '"';

        if (quoted)
            ++position;

        std::string word;
        bool closed = !quoted;

        while (position < line.size()) {
            const char c = line[position];

            if (quoted && c == '"') {
                ++position;
                closed = true;
                break;
            }

            if (!quoted && (c == ' ' || c == '\t' || c == '\r' || line.compare(position, 2, "//") == 0))
                break;

            if (c == ';' || static_cast<unsigned char>(c) < 32 || c == '"')
                throw std::runtime_error("invalid configuration syntax");

            word += c;
            ++position;
        }

        if (!closed)
            throw std::runtime_error("unterminated quoted value");

        if (quoted && position < line.size() && line[position] != ' ' && line[position] != '\t' &&
            line[position] != '\r' && line.compare(position, 2, "//") != 0)
            throw std::runtime_error("expected a space after quoted value");

        words.push_back(std::move(word));

        if (words.size() > 2)
            throw std::runtime_error("expected one setting and one value");
    }

    if (!words.empty() && words.size() != 2)
        throw std::runtime_error("expected one setting and one value");

    return words;
}

CoreSettings CoreSettings::Read(const std::filesystem::path& path, CoreSettings initial) {
    if (!std::filesystem::exists(path))
        return initial;

    const auto size = std::filesystem::file_size(path);

    if (size > 65536)
        throw std::runtime_error("core configuration exceeds 64 KiB");

    std::ifstream input(path, std::ios::binary);
    std::string source(static_cast<std::size_t>(size), '\0');

    if (!input.read(source.data(), static_cast<std::streamsize>(size)))
        throw std::runtime_error("cannot read complete core configuration");

    if (input.peek() != std::char_traits<char>::eof())
        throw std::runtime_error("core configuration changed while reading");

    std::istringstream lines(source);
    std::string line;
    unsigned number = 0;

    while (std::getline(lines, line)) {
        ++number;

        try {
            const auto words = ParseLine(line);

            if (!words.empty())
                initial.SetField(words[0], words[1]);
        } catch (const std::exception& error) {
            throw std::runtime_error("source2root.cfg line " + std::to_string(number) + ": " + error.what());
        }
    }

    initial.Validate();
    return initial;
}

}
