#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace sr {
using Timestamp = std::chrono::sys_seconds;
bool Utc(Timestamp time, std::string& text, std::string& error);
bool ParseUtc(std::string_view text, Timestamp& time, std::string& error);

struct Ban {
    std::uint64_t account{};
    Timestamp created;
    std::optional<Timestamp> expires;
    std::string reason, actor;
    bool Active(Timestamp now) const;
};

class BanStore {
public:
    explicit BanStore(std::filesystem::path path) : ban_file_(std::move(path)) {}

    bool Load(std::string& error);
    const Ban* ActiveBan(std::uint64_t account, Timestamp now) const;
    bool AddBan(std::uint64_t account, std::uint64_t minutes, std::string reason, std::string actor,
                Timestamp now, std::string& error);

    bool RemoveBan(std::uint64_t account, bool& removed, std::string& error);
    const std::map<std::uint64_t, Ban>& Entries() const {
        return bans_;
    }

private:
    static bool Read(const std::filesystem::path& path, std::string& contents, std::string& error);
    static bool AtomicWrite(const std::filesystem::path& path, const std::string& contents, std::string& error);
    bool SaveBans(std::map<std::uint64_t, Ban> candidate, std::string& error);
    std::filesystem::path ban_file_;
    std::map<std::uint64_t, Ban> bans_;
    std::optional<std::string> ban_contents_;
};
}
