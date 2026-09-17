#include "store.h"

#include <algorithm>
#include <charconv>

namespace source2root::prefs {
namespace {
void Name(const std::string& name) {
    if (name.empty() || name.size() > 30 || !std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '_' || c == '-' || c == '.';
    })) throw Error("Cookie names require 1..30 letters, digits, dots, underscores or hyphens.");
}
Definition Row(sqlite::Statement& statement) {
    Definition result{statement.String(0), statement.String(1), static_cast<Access>(statement.Int(2))};
    Validate(result);
    return result;
}
}

void Validate(const Definition& cookie) {
    Name(cookie.name);
    if (cookie.description.size() > 255 || cookie.description.find('\0') != std::string::npos)
        throw Error("Cookie descriptions must contain at most 255 bytes without NUL.");
    if (cookie.access < Access::Public || cookie.access > Access::Private) throw Error("Invalid cookie access mode.");
}
void ValidateAccount(std::uint64_t account) {
    // Individual public-universe Steam accounts, not bots, pending IDs or display names.
    if ((account >> 56) != 1 || ((account >> 52) & 15) != 1 ||
        ((account >> 32) & 0xfffff) != 1 || !(account & 0xffffffffu))
        throw Error("Client preferences require an authenticated individual Steam account.");
}
void ValidateValue(const std::string& value) {
    if (value.size() > 255 || value.find('\0') != std::string::npos)
        throw Error("Cookie values must contain at most 255 bytes without NUL.");
}

Store::Store(const std::filesystem::path& filename) {
    if (filename.empty() || filename.parent_path().empty()) throw Error("Missing client preferences data directory.");
    std::filesystem::create_directories(filename.parent_path());
    if (std::filesystem::is_symlink(filename.parent_path())) throw Error("Client preferences directory must not be a symbolic link.");
    database_ = std::make_shared<sqlite::Database>(filename);
    database_->Execute("BEGIN IMMEDIATE");
    try {
        database_->Execute("CREATE TABLE IF NOT EXISTS sr_prefs_schema (version INTEGER PRIMARY KEY)");
        sqlite::Statement schema(database_, "SELECT version FROM sr_prefs_schema");
        if (schema.Step()) {
            if (schema.Int(0) != 1 || schema.Step()) throw Error("Unsupported client preferences schema.");
        } else database_->Execute("INSERT INTO sr_prefs_schema VALUES (1)");
        database_->Execute("CREATE TABLE IF NOT EXISTS sr_prefs_cookies (name TEXT PRIMARY KEY, description TEXT NOT NULL, access INTEGER NOT NULL CHECK(access BETWEEN 0 AND 2))");
        database_->Execute("CREATE TABLE IF NOT EXISTS sr_prefs_values (account TEXT NOT NULL, cookie TEXT NOT NULL, value TEXT NOT NULL, updated INTEGER NOT NULL, PRIMARY KEY(account,cookie))");
        database_->Execute("COMMIT");
    } catch (...) { database_->Execute("ROLLBACK"); throw; }
}

std::vector<Definition> Store::Catalog() {
    std::vector<Definition> result;
    sqlite::Statement query(database_, "SELECT name,description,access FROM sr_prefs_cookies ORDER BY name");
    while (query.Step()) {
        if (result.size() == MaxCookies) throw Error("Client preferences catalog exceeds 256 cookies.");
        result.push_back(Row(query));
    }
    return result;
}

Definition Store::Register(const Definition& cookie) {
    Validate(cookie);
    database_->Execute("BEGIN IMMEDIATE");
    try {
        sqlite::Statement find(database_, "SELECT name,description,access FROM sr_prefs_cookies WHERE name=?");
        find.BindString(1, cookie.name);
        if (find.Step()) {
            if (Row(find) != cookie) throw Error("Cookie already exists with different description or access mode.");
        } else {
            sqlite::Statement count(database_, "SELECT count(*) FROM sr_prefs_cookies");
            if (!count.Step() || count.Int(0) >= static_cast<int>(MaxCookies)) throw Error("Client preferences catalog limit (256) reached.");
            sqlite::Statement insert(database_, "INSERT INTO sr_prefs_cookies(name,description,access) VALUES(?,?,?)");
            insert.BindString(1, cookie.name); insert.BindString(2, cookie.description); insert.BindInt(3, static_cast<int>(cookie.access));
            insert.Step();
        }
        database_->Execute("COMMIT");
        return cookie;
    } catch (...) { database_->Execute("ROLLBACK"); throw; }
}

Values Store::Load(std::uint64_t account) {
    ValidateAccount(account);
    Values values;
    sqlite::Statement query(database_, "SELECT v.cookie,v.value,v.updated FROM sr_prefs_values v JOIN sr_prefs_cookies c ON c.name=v.cookie WHERE account=? ORDER BY v.cookie");
    query.BindString(1, std::to_string(account));
    while (query.Step()) {
        if (values.size() == MaxCookies) throw Error("Client preferences account exceeds 256 cookies.");
        const auto name = query.String(0), text = query.String(1), timestamp = query.String(2);
        Name(name); ValidateValue(text);
        std::int64_t updated = 0;
        const auto converted = std::from_chars(timestamp.data(), timestamp.data() + timestamp.size(), updated);
        if (converted.ec != std::errc{} || converted.ptr != timestamp.data() + timestamp.size() || updated < 0)
            throw Error("Invalid client preferences timestamp.");
        values.emplace(name, Value{text, updated});
    }
    return values;
}

void Store::Save(std::uint64_t account, const Values& values) {
    ValidateAccount(account);
    if (values.size() > MaxCookies) throw Error("Client preferences write exceeds 256 cookies.");
    for (const auto& [name, value] : values) {
        Name(name); ValidateValue(value.text);
        if (value.updated < 0) throw Error("Invalid client preferences timestamp.");
    }
    database_->Execute("BEGIN IMMEDIATE");
    try {
        sqlite::Statement exists(database_, "SELECT name FROM sr_prefs_cookies WHERE name=?");
        sqlite::Statement save(database_, "INSERT INTO sr_prefs_values(account,cookie,value,updated) VALUES(?,?,?,?) ON CONFLICT(account,cookie) DO UPDATE SET value=excluded.value,updated=excluded.updated");
        for (const auto& [name, value] : values) {
            exists.BindString(1, name);
            if (!exists.Step()) throw Error("Cannot save an unregistered cookie.");
            exists.Reset();
            save.BindString(1, std::to_string(account)); save.BindString(2, name);
            save.BindString(3, value.text); save.BindString(4, std::to_string(value.updated));
            save.Step(); save.Reset();
        }
        database_->Execute("COMMIT");
    } catch (...) { database_->Execute("ROLLBACK"); throw; }
}

}
