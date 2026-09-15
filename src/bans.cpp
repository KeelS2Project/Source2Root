#include "bans.h"
#include "identity.h"

#include <json.hpp>
#include <algorithm>
#include <charconv>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace sr {
namespace {
bool ParseSteamId(std::string_view text, std::uint64_t& result) {
    try { result = ParseSteamIdentity(std::string(text)); return true; }
    catch (const std::exception&) { result = 0; return false; }
}
bool SafeText(std::string_view text, std::size_t maximum) {
    return !text.empty() && text.size() <= maximum && std::none_of(text.begin(), text.end(),
        [](unsigned char c) { return c < 32 || c == 127; });
}
bool Unsigned(std::string_view text, std::uint64_t maximum, std::uint64_t& result) {
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    return !text.empty() && parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && result <= maximum;
}
bool ParseJson(std::string_view contents, nlohmann::json &document, std::string &error)
{
    error.clear();
    document = {};
    if (contents.size() > 1048576)
    {
        error = "JSON exceeds the file size limit";
        return false;
    }
    bool quoted = false, escaped = false;
    unsigned nesting = 0;
    for (const unsigned char c : contents) {
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') {
            if (++nesting > 16) { error = "JSON nesting exceeds 16 levels"; return false; }
        } else if ((c == '}' || c == ']') && nesting) --nesting;
    }
    std::vector<std::set<std::string>> keys;
    auto candidate = nlohmann::json::parse(
        contents,
        [&keys, &error](int depth, nlohmann::json::parse_event_t event, nlohmann::json &value) {
            if (depth > 16 && error.empty())
            {
                error = "JSON nesting exceeds 16 levels";
            }
            using Event = nlohmann::json::parse_event_t;
            if (event == Event::object_start)
            {
                keys.emplace_back();
            }
            else if (event == Event::object_end)
            {
                keys.pop_back();
            }
            else if (event == Event::key && !keys.back().insert(value.get<std::string>()).second &&
                     error.empty())
            {
                error = ("duplicate JSON key: " + value.get<std::string>()).c_str();
            }
            return true;
        },
        false);
    if (candidate.is_discarded() && error.empty())
    {
        error = "invalid JSON syntax";
    }
    if (!error.empty())
    {
        return false;
    }
    document = std::move(candidate);
    return true;
}

bool ParseBans(std::string_view contents, std::map<std::uint64_t, Ban> &output, std::string &error)
{
    nlohmann::json document;
    if (!ParseJson(contents, document, error))
    {
        return false;
    }
    try
    {
        if (!document.is_object() || document.at("schema") != 1 || !document.at("bans").is_array() ||
            document.at("bans").size() > 4096)
        {
            error = "invalid ban database schema or entry count";
            return false;
        }
        std::map<std::uint64_t, Ban> candidate;
        for (const auto &entry : document.at("bans"))
        {
            if (!entry.at("steamid").is_string())
            {
                error = "ban Steam IDs must be strings";
                return false;
            }
            std::uint64_t id{};
            if (!ParseSteamId(entry.at("steamid").get<std::string>(), id))
            {
                error = "invalid ban account";
                return false;
            }
            Ban ban;
            ban.account = id;
            if (!ParseUtc(entry.at("created_utc").get<std::string>(), ban.created, error))
            {
                return false;
            }
            ban.reason = entry.at("reason").get<std::string>();
            ban.actor = entry.at("actor").get<std::string>();
            if (!entry.at("expires_utc").is_null())
            {
                Timestamp expiry;
                if (!ParseUtc(entry.at("expires_utc").get<std::string>(), expiry, error))
                {
                    return false;
                }
                ban.expires = expiry;
            }
            std::uint64_t actor_account{};
            if ((ban.expires && *ban.expires <= ban.created) || !SafeText(ban.reason, 256) ||
                (ban.actor != "server" && !ParseSteamId(ban.actor, actor_account)))
            {
                error = "invalid ban expiry, reason or actor";
                return false;
            }
            if (!candidate.emplace(id, std::move(ban)).second)
            {
                error = "duplicate normalized ban account";
                return false;
            }
        }
        output = std::move(candidate);
        return true;
    }
    catch (const nlohmann::json::exception &exception)
    {
        error = ("invalid ban data: " + std::string(exception.what())).c_str();
        return false;
    }
}


}

bool Utc(Timestamp time, std::string &text, std::string &error)
{
    using namespace std::chrono;
    text.clear();
    error.clear();
    const auto day = floor<days>(time);
    if (day < sys_days{year{1970} / 1 / 1} || day > sys_days{year{9999} / 12 / 31})
    {
        error = "UTC timestamp is outside supported years 1970 through 9999";
        return false;
    }
    const year_month_day date{day};
    const hh_mm_ss clock{time - day};
    std::ostringstream formatted;
    formatted.imbue(std::locale::classic());
    formatted << std::setfill('0') << std::setw(4) << static_cast<int>(date.year()) << '-' << std::setw(2)
              << static_cast<unsigned>(date.month()) << '-' << std::setw(2)
              << static_cast<unsigned>(date.day()) << 'T' << std::setw(2) << clock.hours().count() << ':'
              << std::setw(2) << clock.minutes().count() << ':' << std::setw(2) << clock.seconds().count()
              << 'Z';
    text = formatted.str();
    return true;
}

bool ParseUtc(std::string_view text, Timestamp &time, std::string &error)
{
    using namespace std::chrono;
    time = {};
    error.clear();
    if (text.size() != 20 || text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' ||
        text[16] != ':' || text[19] != 'Z')
    {
        error = "expected a UTC timestamp YYYY-MM-DDTHH:MM:SSZ";
        return false;
    }
    std::uint64_t year_value{}, month_value{}, day_value{}, hour_value{}, minute_value{}, second_value{};
    if (!Unsigned(text.substr(0, 4), 9999, year_value) || year_value < 1970 ||
        !Unsigned(text.substr(5, 2), 12, month_value) || !Unsigned(text.substr(8, 2), 31, day_value) ||
        !Unsigned(text.substr(11, 2), 23, hour_value) || !Unsigned(text.substr(14, 2), 59, minute_value) ||
        !Unsigned(text.substr(17, 2), 59, second_value))
    {
        error = "invalid UTC timestamp";
        return false;
    }
    const year_month_day date{year{static_cast<int>(year_value)}, month{static_cast<unsigned>(month_value)},
                              day{static_cast<unsigned>(day_value)}};
    if (!date.ok())
    {
        error = "invalid calendar date";
        return false;
    }
    time = Timestamp{sys_days{date}} + hours{hour_value} + minutes{minute_value} + seconds{second_value};
    return true;
}

bool Ban::Active(Timestamp now) const
{
    return !expires || now < *expires;
}

bool BanStore::Read(const std::filesystem::path &path, std::string &contents, std::string &error)
{
    contents.clear();
    error.clear();
    std::error_code status;
    if (!std::filesystem::is_regular_file(path, status) || status)
    {
        error = ("missing or invalid file: " + path.string()).c_str();
        return false;
    }
    const auto size = std::filesystem::file_size(path, status);
    if (status || size > 1048576)
    {
        error = ("invalid or oversized file: " + path.string()).c_str();
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        error = ("cannot read " + path.string()).c_str();
        return false;
    }
    std::string candidate;
    candidate.resize(1048577);
    file.read(candidate.data(), static_cast<std::streamsize>(candidate.size()));
    const auto count = file.gcount();
    if (file.bad() || count > 1048576)
    {
        error = ("read failed or file grew beyond the size limit: " + path.string()).c_str();
        return false;
    }
    candidate.resize(static_cast<std::size_t>(count));
    contents = std::move(candidate);
    return true;
}


bool BanStore::Load(std::string& error) {
    error.clear();
    if (ban_contents_) return true;
    std::error_code status;
    const bool exists = std::filesystem::exists(ban_file_, status);
    if (status) { error = "Cannot inspect ban database: " + status.message(); return false; }
    std::string contents;
    std::map<std::uint64_t, Ban> candidate;
    if (exists) {
        if (!Read(ban_file_, contents, error) || !ParseBans(contents, candidate, error)) return false;
    } else {
        contents = "{\n  \"bans\": [],\n  \"schema\": 1\n}\n";
        if (!AtomicWrite(ban_file_, contents, error)) return false;
    }
    bans_ = std::move(candidate);
    ban_contents_ = std::move(contents);
    return true;
}

const Ban *BanStore::ActiveBan(std::uint64_t account, Timestamp now) const
{
    const auto found = bans_.find(account);
    if (found == bans_.end() || !found->second.Active(now))
    {
        return nullptr;
    }
    return &found->second;
}

bool BanStore::AddBan(std::uint64_t account, std::uint64_t minutes, std::string reason, std::string actor, Timestamp now,
                     std::string &error)
{
    if (!Load(error)) return false;
    error.clear();
    std::uint64_t actor_account{};
    if (!ValidSteamIdentity(account) || minutes > 5256000 || !SafeText(reason, 256) ||
        (actor != "server" && !ParseSteamId(actor, actor_account)))
    {
        error = "invalid ban account, duration, reason or actor";
        return false;
    }
    Ban ban;
    ban.account = account;
    ban.created = now;
    ban.reason = std::move(reason);
    ban.actor = std::move(actor);
    std::string timestamp;
    if (!Utc(now, timestamp, error))
    {
        return false;
    }
    if (minutes)
    {
        const Timestamp last = Timestamp{std::chrono::sys_days{std::chrono::year{9999} / 12 / 31}} +
                               std::chrono::hours{23} + std::chrono::minutes{59} + std::chrono::seconds{59};
        const auto duration = std::chrono::minutes{minutes};
        if (last - now < duration)
        {
            error = "ban expiry is outside supported UTC years";
            return false;
        }
        ban.expires = now + duration;
    }
    auto candidate = bans_;
    candidate[account] = std::move(ban);
    if (candidate.size() > 4096)
    {
        error = "ban database is full";
        return false;
    }
    return SaveBans(std::move(candidate), error);
}

bool BanStore::RemoveBan(std::uint64_t account, bool &removed, std::string &error)
{
    removed = false;
    if (!Load(error)) return false;
    error.clear();
    if (!ValidSteamIdentity(account))
    {
        error = "invalid ban account";
        return false;
    }
    auto candidate = bans_;
    if (!candidate.erase(account))
    {
        return true;
    }
    if (!SaveBans(std::move(candidate), error))
    {
        return false;
    }
    removed = true;
    return true;
}

bool BanStore::SaveBans(std::map<std::uint64_t, Ban> candidate, std::string &error)
{
    std::string previous;
    if (!ban_contents_ || !Read(ban_file_, previous, error))
    {
        if (error.empty())
        {
            error = "ban database has not been loaded";
        }
        return false;
    }
    if (previous != *ban_contents_)
    {
        error = "ban file changed externally; correct it and reload Source2Root before modifying bans";
        return false;
    }
    auto document = nlohmann::json::parse(*ban_contents_);
    std::map<std::uint64_t, nlohmann::json> original;
    for (const auto& entry : document.at("bans"))
        original.emplace(ParseSteamIdentity(entry.at("steamid").get<std::string>()), entry);
    document["bans"] = nlohmann::json::array();
    for (const auto &[id, ban] : candidate)
    {
        std::string created;
        if (!Utc(ban.created, created, error))
        {
            return false;
        }
        nlohmann::json expiry = nullptr;
        if (ban.expires)
        {
            std::string expires;
            if (!Utc(*ban.expires, expires, error))
            {
                return false;
            }
            expiry = expires;
        }
        auto entry = original.contains(id) ? original.at(id) : nlohmann::json::object();
        entry["steamid"] = std::to_string(id);
        entry["created_utc"] = created;
        entry["expires_utc"] = expiry;
        entry["reason"] = ban.reason;
        entry["actor"] = ban.actor;
        document["bans"].push_back(std::move(entry));
    }
    std::string contents;
    try
    {
        contents = document.dump(2) + '\n';
    }
    catch (const nlohmann::json::exception &exception)
    {
        error = ("cannot encode ban database: " + std::string(exception.what())).c_str();
        return false;
    }
    if (contents.size() > 1048576)
    {
        error = "ban database exceeds the file size limit";
        return false;
    }
    if (!AtomicWrite(ban_file_, contents, error))
    {
        return false;
    }
    ban_contents_ = std::move(contents);
    bans_ = std::move(candidate);
    return true;
}

bool BanStore::AtomicWrite(const std::filesystem::path &path, const std::string &contents, std::string &error)
{
    std::error_code status;
    std::filesystem::create_directories(path.parent_path(), status);
    if (status)
    {
        error = ("cannot create ban directory: " + status.message()).c_str();
        return false;
    }
#ifdef _WIN32
    const auto temporary = path.wstring() + L".tmp." + std::to_wstring(GetCurrentProcessId());
    const auto handle =
        CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        error = "cannot create temporary ban file";
        return false;
    }
    DWORD written{};
    const bool success =
        WriteFile(handle, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr) &&
        written == contents.size() && FlushFileBuffers(handle);
    const bool closed = CloseHandle(handle) != 0;
    if (!success || !closed ||
        !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(temporary.c_str());
        error = "atomic ban save failed";
        return false;
    }
#else
    auto name = path.string() + ".tmp.XXXXXX";
    const int fd = mkstemp(name.data());
    if (fd < 0)
    {
        error = ("cannot create temporary ban file: " + std::string(std::strerror(errno))).c_str();
        return false;
    }
    std::size_t offset{};
    bool success = true;
    while (offset < contents.size())
    {
        const auto written = write(fd, contents.data() + offset, contents.size() - offset);
        if (written < 0 && errno == EINTR)
        {
            continue;
        }
        if (written <= 0)
        {
            success = false;
            break;
        }
        offset += static_cast<std::size_t>(written);
    }
    if (success)
    {
        success = fsync(fd) == 0;
    }
    const bool closed = close(fd) == 0;
    if (!success || !closed || rename(name.c_str(), path.c_str()) != 0)
    {
        unlink(name.c_str());
        error = "atomic ban save failed";
        return false;
    }
    const int directory = open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
    if (directory < 0)
    {
        error = "ban file replaced but directory durability could not be confirmed; reload before retrying";
        return false;
    }
    const bool synced = fsync(directory) == 0;
    const bool directory_closed = close(directory) == 0;
    if (!synced || !directory_closed)
    {
        error = "ban file replaced but directory sync failed; reload before retrying";
        return false;
    }
#endif
    return true;
}


}
