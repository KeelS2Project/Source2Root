#include "bans.h"
#include "identity.h"
#include <json.hpp>
#include <cstdlib>
#include <fstream>
#include <iostream>

static void Require(bool value, const char* message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

static std::string Read(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}

static void Write(const std::filesystem::path& file, const std::string& text) {
    std::ofstream(file, std::ios::binary) << text;
}

int main(int argc, char** argv) {
    Require(argc == 2, "ban_store_test fixture root");
    const std::filesystem::path root(argv[1]), file = root / "data/bans.json";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(file.parent_path());
    std::string error, text;
    sr::Timestamp now;
    Require(sr::ParseUtc("2026-09-15T12:00:00Z", now, error), "parse fixed UTC clock");

    for (const auto* date :
         {"1970-01-01T00:00:00Z", "2038-01-19T03:14:08Z", "6053-01-01T00:00:00Z", "9999-12-31T23:59:59Z"}) {
        sr::Timestamp parsed;
        Require(sr::ParseUtc(date, parsed, error) && sr::Utc(parsed, text, error) && text == date,
                "UTC round trips across narrow duration and Unix time boundaries");
    }

    sr::Timestamp boundary;
    Require(sr::ParseUtc("9999-12-31T23:59:59Z", boundary, error) && boundary.time_since_epoch().count() == 253402300799LL,
            "UTC upper limit is represented in seconds without intermediate overflow");

    const auto id = sr::ParseSteamIdentity("[U:1:123]"), second = sr::ParseSteamIdentity("[U:1:124]");
    sr::BanStore store(file);
    Require(store.Load(error) && std::filesystem::exists(file), "initialize empty persistent database");
    Require(store.AddBan(id, 1, "Testing \"quotes\" and \\ slashes", "server", now, error), "save bounded timed ban");
    Require(store.ActiveBan(id, now + std::chrono::seconds(59)) && !store.ActiveBan(id, now + std::chrono::seconds(60)),
            "expiry is exclusive at exact second");

    Require(store.AddBan(second, 0, "Permanent", std::to_string(id), now, error), "save permanent ban and actor identity");
    auto saved = Read(file);
    sr::BanStore restarted(file);
    Require(restarted.Load(error) && restarted.Entries().size() == 2 && Read(file) == saved,
            "restart loads without rewriting data");

    Require(restarted.ActiveBan(id, now)->reason == "Testing \"quotes\" and \\ slashes" &&
                restarted.ActiveBan(second, now + std::chrono::hours(24 * 365))->actor == std::to_string(id),
            "reasons, actor and permanence survive restart");

    bool removed = true;
    Require(restarted.RemoveBan(sr::ParseSteamIdentity("[U:1:125]"), removed, error) && !removed && Read(file) == saved,
            "absent unban is a no-op");

    Write(file, saved + " ");
    Require(!restarted.AddBan(id, 0, "Changed", "server", now, error) &&
                error.find("changed externally") != std::string::npos && Read(file) == saved + " " &&
                restarted.ActiveBan(id, now)->reason != "Changed",
            "external edit cannot be overwritten or published into memory");

    Write(file, saved);
    std::filesystem::rename(file, root / "backup.json");
    Require(!restarted.RemoveBan(second, removed, error) && !std::filesystem::exists(file) &&
                restarted.ActiveBan(second, now),
            "missing previously loaded database is not recreated by mutation");

    std::filesystem::rename(root / "backup.json", file);
    Require(restarted.RemoveBan(id, removed, error) && removed && !restarted.ActiveBan(id, now),
            "successful unban updates disk and memory");

    sr::BanStore after_remove(file);
    Require(after_remove.Load(error) && !after_remove.ActiveBan(id, now) && after_remove.Entries().size() == 1,
            "unban survives restart");

    saved = Read(file);

    for (const auto& reason :
         {std::string(), std::string(257, 'x'), std::string("bad\nreason"), std::string("bad\0reason", 10)})
        Require(!after_remove.AddBan(id, 0, reason, "server", now, error) && Read(file) == saved,
                "invalid reasons cannot modify disk");

    Require(!after_remove.AddBan(id, 5256001, "reason", "server", now, error), "duration upper bound");
    Require(!after_remove.AddBan(id, 1, "reason", "display name", now, error),
            "actor must be a stable Steam identity or server");

    Require(!after_remove.AddBan(0, 1, "reason", "server", now, error), "invalid target cannot be persisted");
    sr::Timestamp limit;
    Require(sr::ParseUtc("9999-12-31T23:59:59Z", limit, error) &&
                !after_remove.AddBan(id, 1, "reason", "server", limit, error),
            "expiry overflow rejected before adding duration");

    Require(after_remove.AddBan(id, 0, "reason", "server", limit, error),
            "permanent ban may use final representable timestamp");

    Require(after_remove.AddBan(id, 1, "last minute", "server", limit - std::chrono::seconds(60), error) &&
            after_remove.Entries().at(id).expires == limit, "timed ban may expire at the final supported second");

    saved = Read(file);
    Require(!after_remove.AddBan(id, 1, "beyond range", "server", limit - std::chrono::seconds(59), error) &&
            Read(file) == saved && after_remove.Entries().at(id).expires == limit,
            "expiry one second beyond the UTC limit preserves disk and memory");

    for (const auto& date : {"1969-12-31T23:59:59Z",
                             "2025-02-29T00:00:00Z",
                             "2026-13-01T00:00:00Z",
                             "2026-01-01T24:00:00Z",
                             "2026-01-01T00:00:60Z",
                             "2026-01-01 00:00:00Z",
                             "2026-01-01T00:00:00+00:00"})
        Require(!sr::ParseUtc(date, limit, error), "invalid or out-of-range UTC rejected");

    Require(sr::ParseUtc("2024-02-29T23:59:59Z", limit, error) && sr::Utc(limit, text, error) &&
                text == "2024-02-29T23:59:59Z",
            "valid leap date round trips");

    const nlohmann::json entry{{"steamid", "STEAM_0:1:61"},
                               {"created_utc", "2026-09-15T12:00:00Z"},
                               {"expires_utc", nullptr},
                               {"reason", "legacy ban"},
                               {"actor", "[U:1:124]"}};

    const nlohmann::json valid{{"schema", 1}, {"bans", nlohmann::json::array({entry})}};
    Write(file, valid.dump());
    sr::BanStore legacy(file);
    Require(legacy.Load(error) && legacy.ActiveBan(id, now) && legacy.ActiveBan(id, now)->actor == "[U:1:124]",
            "legacy schema and identity formats remain readable without data loss");

    auto extended = valid;
    extended["migration_note"] = "preserve top-level metadata";
    extended["bans"][0]["imported"] = {{"label", "preserve record metadata"}};
    Write(file, extended.dump());
    sr::BanStore metadata(file);
    Require(metadata.Load(error) && metadata.AddBan(id, 0, "updated", "server", now, error),
            "update legacy record with additional metadata");

    const auto retained = nlohmann::json::parse(Read(file));
    Require(retained.at("migration_note") == extended.at("migration_note") &&
                retained.at("bans")[0].at("imported") == extended.at("bans")[0].at("imported"),
            "saving known ban fields preserves unknown metadata");

    auto invalid = [&](const std::string& contents) {
        Write(file, contents);
        sr::BanStore broken(file);
        Require(!broken.Load(error) && broken.Entries().empty() && Read(file) == contents,
                "invalid input remains unchanged and publishes no partial state");
    };
    invalid("{\"schema\":1,\"schema\":1,\"bans\":[]}");
    invalid(std::string(100, '[') + "0" + std::string(100, ']'));
    invalid(std::string(1048577, ' '));
    invalid("{");
    auto document = valid;
    document["bans"].push_back(entry);
    document["bans"][1]["steamid"] = "[U:1:123]";
    invalid(document.dump());

    for (const auto& field : {"reason", "actor", "created_utc", "expires_utc", "steamid"}) {
        document = valid;
        document["bans"][0][field] = 42;
        invalid(document.dump());
    }

    document = valid;
    document["bans"][0]["expires_utc"] = "2026-09-15T12:00:00Z";
    invalid(document.dump());
    document = valid;
    document["bans"][0]["reason"] = "bad\nreason";
    invalid(document.dump());
    document = {{"schema", 1}, {"bans", nlohmann::json::array()}};

    for (unsigned i = 1; i <= 4096; ++i) {
        auto row = entry;
        row["steamid"] = "[U:1:" + std::to_string(i) + "]";
        document["bans"].push_back(row);
    }

    Write(file, document.dump());
    sr::BanStore full(file);
    Require(full.Load(error) && full.Entries().size() == 4096, "supported entry limit loads");
    saved = Read(file);
    Require(!full.AddBan(sr::ParseSteamIdentity("[U:1:4097]"), 0, "new", "server", now, error) && Read(file) == saved,
            "full database refuses a new identity");

    Require(full.AddBan(id, 0, "replace existing", "server", now, error) && full.Entries().size() == 4096,
            "existing identity can be updated at entry limit");

    document["bans"].push_back(entry);
    invalid(document.dump());
    std::cout << "Ban persistence, migration, expiry, limits and recovery tests passed.\n";
}
