#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace source2root::regex {
inline constexpr std::size_t TextLimit = 4095, CaptureLimit = 64, MatchLimit = 256;

enum Flag : std::uint32_t {
    Caseless = 1, Multiline = 2, Dotall = 4, Extended = 8, Anchored = 16,
    DollarEndOnly = 32, Ungreedy = 64, Utf = 128, Ucp = 256
};

class Error : public std::runtime_error {
public:
    explicit Error(const std::string& message, int offset = -1) : std::runtime_error(message), offset(offset) {}

    int offset;
};

struct Span {
    int start = -1, end = -1;
};

class Result {
public:
    std::size_t Count() const {
        return matches_.size();
    }

    unsigned Groups() const {
        return groups_;

    } // excludes group zero (whole match)
    Span Offsets(unsigned match, unsigned group) const;
    std::optional<std::string> Capture(unsigned match, unsigned group) const;
    unsigned Named(unsigned match, std::string_view name) const;

private:
    friend class Pattern;
    std::string subject_;
    unsigned groups_ = 0;
    std::vector<std::pair<std::string, unsigned>> names_;
    std::vector<std::vector<Span>> matches_;
};

struct Replacement {
    std::string text;
    unsigned count = 0;
};

class Pattern {
public:
    explicit Pattern(std::string_view pattern, std::uint32_t flags = 0);
    ~Pattern();
    Pattern(const Pattern&) = delete;
    Pattern& operator=(const Pattern&) = delete;
    Result Match(std::string_view subject, bool all = false, unsigned offset = 0) const;
    Replacement Replace(std::string_view subject, std::string_view replacement, bool all = false,
                        unsigned offset = 0, unsigned capacity = TextLimit + 1) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
