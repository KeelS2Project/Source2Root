#include "keyvalues.h"

#include <fstream>
#include <stdexcept>
#include <string_view>

namespace sr {
namespace {

class Parser {
public:
    explicit Parser(std::string_view source) : source_(source) {
        if (source_.starts_with("\xef\xbb\xbf")) position_ = 3;
    }
    std::vector<KeyValue> Parse(unsigned depth = 0) {
        if (depth > 16) Error("objects are nested too deeply");
        std::vector<KeyValue> entries;
        while (Skip()) {
            if (source_[position_] == '}') {
                if (!depth) Error("unexpected closing brace");
                ++position_;
                return entries;
            }
            KeyValue entry;
            entry.name = Token();
            if (entry.name.empty()) Error("empty key");
            if (++entries_ > 65536) Error("too many entries");
            if (!Skip()) Error("key has no value");
            if (source_[position_] == '{') {
                ++position_;
                entry.object = true;
                entry.children = Parse(depth + 1);
            } else entry.value = Token();
            entries.push_back(std::move(entry));
        }
        if (depth) Error("missing closing brace");
        return entries;
    }
private:
    std::string_view source_;
    std::size_t position_ = 0, entries_ = 0;
    [[noreturn]] void Error(const char* message) const {
        unsigned line = 1;
        for (std::size_t i = 0; i < position_; ++i) if (source_[i] == '\n') ++line;
        throw std::runtime_error("line " + std::to_string(line) + ": " + message);
    }
    bool Skip() {
        while (position_ < source_.size()) {
            const char c = source_[position_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { ++position_; continue; }
            if (source_.substr(position_, 2) == "//") {
                while (position_ < source_.size() && source_[position_] != '\n') ++position_;
            } else if (source_.substr(position_, 2) == "/*") {
                const auto end = source_.find("*/", position_ + 2);
                if (end == std::string_view::npos) Error("unterminated comment");
                position_ = end + 2;
            } else return true;
        }
        return false;
    }
    std::string Token() {
        if (position_ == source_.size() || source_[position_] == '{' || source_[position_] == '}')
            Error("expected a key or string value");
        const bool quoted = source_[position_] == '"';
        if (quoted) ++position_;
        std::string text;
        while (position_ < source_.size()) {
            char c = source_[position_];
            if (quoted && c == '"') { ++position_; return text; }
            if (!quoted && (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '{' || c == '}' ||
                source_.substr(position_, 2) == "//" || source_.substr(position_, 2) == "/*")) break;
            if (static_cast<unsigned char>(c) < 32 || c == 127 || (!quoted && c == '"')) Error("invalid character in token");
            ++position_;
            if (quoted && c == '\\') {
                if (position_ == source_.size()) Error("unterminated escape");
                c = source_[position_++];
                if (c == 'n') c = '\n';
                else if (c == 'r') c = '\r';
                else if (c == 't') c = '\t';
                else if (c != '"' && c != '\\') Error("unsupported escape");
            }
            text += c;
            if (text.size() > 4096) Error("token exceeds 4096 bytes");
        }
        if (quoted) Error("unterminated quoted string");
        if (text.empty()) Error("expected a key or string value");
        return text;
    }
};

}

std::vector<KeyValue> ReadKeyValues(const std::filesystem::path& file) {
    try {
        const auto size = std::filesystem::file_size(file);
        if (size > 1024 * 1024) throw std::runtime_error("configuration exceeds 1 MiB");
        std::ifstream input(file, std::ios::binary);
        std::string source(static_cast<std::size_t>(size), '\0');
        if (!input.read(source.data(), static_cast<std::streamsize>(size))) throw std::runtime_error("cannot read complete configuration");
        if (input.peek() != std::char_traits<char>::eof()) throw std::runtime_error("configuration changed while reading");
        return Parser(source).Parse();
    } catch (const std::exception& error) {
        throw std::runtime_error(file.filename().string() + ": " + error.what());
    }
}

}
