#include "identity.h"

#include <charconv>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace sr {
namespace {

constexpr std::uint64_t IndividualBase = 76561197960265728ULL;

std::uint64_t Number(std::string_view value) {
    std::uint64_t result = 0;
    auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size())
        throw std::runtime_error("invalid Steam identity number");
    return result;
}

}

bool ValidSteamIdentity(std::uint64_t value) {
    return value > IndividualBase && value <= IndividualBase + std::numeric_limits<std::uint32_t>::max();
}

std::uint64_t ParseSteamIdentity(const std::string& text) {
    std::uint64_t result;
    if (text.starts_with("STEAM_")) {
        if (text.size() < 11 || (text[6] != '0' && text[6] != '1') || text[7] != ':' ||
            (text[8] != '0' && text[8] != '1') || text[9] != ':')
            throw std::runtime_error("expected STEAM_0:Y:Z or STEAM_1:Y:Z");
        const auto half = Number(std::string_view(text).substr(10));
        if (half > std::numeric_limits<std::uint32_t>::max() / 2)
            throw std::runtime_error("Steam account ID overflow");
        result = IndividualBase + half * 2 + static_cast<unsigned>(text[8] - '0');
    } else if (text.starts_with("[U:")) {
        if (!text.starts_with("[U:1:") || !text.ends_with(']'))
            throw std::runtime_error("expected [U:1:account]");
        const auto account = Number(std::string_view(text).substr(5, text.size() - 6));
        if (account > std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error("Steam account ID overflow");
        result = IndividualBase + account;
    } else {
        result = Number(text);
    }
    if (!ValidSteamIdentity(result))
        throw std::runtime_error("Steam identity must be an individual public account");
    return result;
}

bool ValidIdentifier(const std::string& text) {
    if (text.empty() || text.size() > 64) return false;
    for (unsigned char c : text)
        if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '_' && c != '-') return false;
    return true;
}

bool ValidPermission(const std::string& text) {
    if (text.size() > 96) return false;
    if (text.empty()) return true;
    if (text.front() == '.' || text.back() == '.') return false;
    bool dot = false;
    for (unsigned char c : text) {
        if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '_' && c != '.' && c != '-') return false;
        if (c == '.' && dot) return false;
        dot = c == '.';
    }
    return true;
}

}
