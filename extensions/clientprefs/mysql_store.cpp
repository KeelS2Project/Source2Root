#include "mysql_store.h"
#include <charconv>
#include <set>

namespace source2root::prefs {
namespace {
db::QueryResult
Run(mysql::Session& session, const std::string& sql, std::initializer_list<db::Parameter> parameters = {}) {
    db::QueryInput query{sql, {}};

    for (const auto& value : parameters)
        query.Bind(static_cast<int>(query.parameters.size() + 1), value);

    return session.Execute(query);
}

void Version(const db::QueryResult& result) {
    if (result.rows.size() != 1 || result.rows[0][0].integer != 1 || result.rows[0][1].integer != 1)
        throw Error("Unsupported client preferences schema.");
}

void Initialize(mysql::Session& session) {
    // DDL commits implicitly on both servers. Finish setup before any user
    // transaction and require transactional tables, including preexisting ones.
    Run(session, "CREATE TABLE IF NOT EXISTS sr_prefs_schema (id TINYINT PRIMARY KEY, version INT NOT NULL) ENGINE=InnoDB");
    Run(session,
        "CREATE TABLE IF NOT EXISTS sr_prefs_cookies (name VARBINARY(30) PRIMARY KEY, description VARBINARY(255) NOT NULL, access INT NOT NULL) ENGINE=InnoDB");

    Run(session,
        "CREATE TABLE IF NOT EXISTS sr_prefs_values (account VARBINARY(20) NOT NULL, cookie VARBINARY(30) NOT NULL, value VARBINARY(255) NOT NULL, updated BIGINT NOT NULL, PRIMARY KEY(account,cookie), FOREIGN KEY(cookie) REFERENCES sr_prefs_cookies(name)) ENGINE=InnoDB");

    const auto engines = Run(
        session,
        "SELECT TABLE_NAME,ENGINE FROM information_schema.TABLES WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME IN ('sr_prefs_schema','sr_prefs_cookies','sr_prefs_values')");

    if (engines.rows.size() != 3)
        throw Error("Client preferences tables are unavailable.");

    for (const auto& row : engines.rows)
        if (row[1].text != "InnoDB")
            throw Error("Client preferences tables require InnoDB.");

    Run(session, "INSERT INTO sr_prefs_schema(id,version) VALUES(1,1) ON DUPLICATE KEY UPDATE id=id");
    Version(Run(session, "SELECT id,version FROM sr_prefs_schema"));
    session.Commit();
}

Definition Row(const std::vector<db::QueryValue>& row) {
    if (row[0].null || row[1].null || !row[2].integer)
        throw Error("Invalid client preferences definition.");

    Definition result{row[0].text, row[1].text, static_cast<Access>(*row[2].integer)};
    Validate(result);
    return result;
}
}

std::vector<Definition> MysqlStore::Catalog() {
    mysql::Session session(settings_, canceled_);
    Initialize(session);
    std::vector<Definition> result;

    for (const auto& row : Run(session, "SELECT name,description,access FROM sr_prefs_cookies ORDER BY name").rows)
        result.push_back(Row(row));

    return result;
}

Definition MysqlStore::Register(const Definition& cookie) {
    Validate(cookie);
    mysql::Session session(settings_, canceled_);
    Initialize(session);
    // A global row lock serializes registration across independent servers,
    // including the catalog capacity check and conflicting definitions.
    Version(Run(session, "SELECT id,version FROM sr_prefs_schema WHERE id=1 FOR UPDATE"));
    const auto found = Run(session, "SELECT name,description,access FROM sr_prefs_cookies WHERE name=?", {cookie.name});

    if (!found.rows.empty()) {
        if (Row(found.rows[0]) != cookie)
            throw Error("Cookie already exists with different description or access mode.");
    } else {
        const auto count = Run(session, "SELECT count(*) FROM sr_prefs_cookies");

        if (count.rows.size() != 1 || !count.rows[0][0].integer || *count.rows[0][0].integer >= static_cast<int>(MaxCookies))
            throw Error("Client preferences catalog limit (256) reached.");

        Run(session,
            "INSERT INTO sr_prefs_cookies(name,description,access) VALUES(?,?,?)",
            {cookie.name, cookie.description, static_cast<std::int32_t>(cookie.access)});
    }

    session.Commit();
    return cookie;
}

Values MysqlStore::Load(std::uint64_t account) {
    ValidateAccount(account);
    mysql::Session session(settings_, canceled_);
    Initialize(session);
    Values values;
    const auto rows = Run(
        session,
        "SELECT v.cookie,v.value,v.updated FROM sr_prefs_values v JOIN sr_prefs_cookies c ON c.name=v.cookie WHERE account=? ORDER BY v.cookie",
        {std::to_string(account)});

    for (const auto& row : rows.rows) {
        Validate({row[0].text, "", Access::Public});
        ValidateValue(row[1].text);
        std::int64_t updated = 0;
        const auto& text = row[2].text;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), updated);

        if (row[0].null || row[1].null || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || updated < 0)
            throw Error("Invalid client preferences value or timestamp.");

        values.emplace(row[0].text, Value{row[1].text, updated});
    }

    return values;
}

void MysqlStore::Save(std::uint64_t account, const Values& values) {
    ValidateAccount(account);

    if (values.size() > MaxCookies)
        throw Error("Client preferences write exceeds 256 cookies.");

    for (const auto& [name, value] : values) {
        Validate({name, "", Access::Public});
        ValidateValue(value.text);

        if (value.updated < 0)
            throw Error("Invalid client preferences timestamp.");
    }

    mysql::Session session(settings_, canceled_);
    Initialize(session);
    std::set<std::string> registered;

    for (const auto& row : Run(session, "SELECT name FROM sr_prefs_cookies").rows)
        registered.insert(row[0].text);

    for (const auto& [name, value] : values) {
        if (!registered.contains(name))
            throw Error("Cannot save an unregistered cookie.");
        // Only changed cookies are written. A stale cache on another server
        // cannot replace this account's unrelated values. Last commit wins for
        // simultaneous writes to the same cookie, independent of timestamps.
        Run(session,
            "INSERT INTO sr_prefs_values(account,cookie,value,updated) VALUES(?,?,?,?) ON DUPLICATE KEY UPDATE value=?,updated=?",
            {std::to_string(account),
             name,
             value.text,
             std::to_string(value.updated),
             value.text,
             std::to_string(value.updated)});
    }

    session.Commit();
}
}
