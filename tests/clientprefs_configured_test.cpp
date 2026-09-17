#include "service.h"
#if defined(SR_MYSQL_DRIVER)
#include "mysql_store.h"
#endif
#include <json.hpp>
#include <fstream>
#include <iostream>
#include <barrier>

using namespace source2root;
using namespace source2root::prefs;
using Json = nlohmann::json;
static void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template <typename Operation> static void Reject(Operation operation, const char* message) {
    bool failed = false;
    try { operation(); } catch (const Error&) { failed = true; }
    Check(failed, message);
}
template <typename Predicate> static void Until(Service& service, Predicate done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!done()) {
        service.Pump();
        Check(std::chrono::steady_clock::now() < deadline, "preferences completion deadline");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
static void Write(const std::filesystem::path& path, const Json& config) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path) << config.dump(2);
    std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
}
static Json Local(const std::string& name) {
    return {{"schema", 1}, {"connections", {{"clientprefs", {{"driver", "sqlite"}, {"database", name},
        {"allow_plugins", {"source2root.clientprefs"}}}}}}};
}
constexpr std::uint64_t First = 76561198000000001ull;
const Definition Music{"music", "Music preference", Access::Public}, Rank{"rank", "Server-managed rank", Access::Protected};

static void LocalChecks(const std::filesystem::path& root) {
    const auto path = root / "config/databases.json", data = root / "data";
    auto storage = ConfiguredStorage(data, path);
    storage()->Register(Music);
    Check(std::filesystem::is_regular_file(data / "clientprefs.sqlite"), "missing config uses scoped SQLite default");
    Write(path, Local("clientprefs"));
    Check(storage()->Catalog().size() == 1, "explicit equivalent default target works");
    Write(path, Local("different"));
    Reject([&] { storage(); }, "target cannot change under live service");
    Check(!std::filesystem::exists(data / "different.sqlite"), "target rejection performs no writes to new database");
    Write(path, Local("clientprefs"));
    Check(storage()->Catalog().size() == 1, "restoring target repairs factory");
    auto fresh = ConfiguredStorage(root / "fresh", path);
    auto invalid = Local("valid"); invalid["connections"]["clientprefs"]["allow_plugins"] = {"another_plugin"};
    Write(path, invalid);
    Reject([&] { fresh(); }, "service identity must be permitted");
    Write(path, Local("valid"));
    fresh()->Register(Music);
    std::filesystem::remove(path);
    Reject([&] { fresh(); }, "deleted configuration cannot switch configured SQLite to default");
    Check(!std::filesystem::exists(root / "fresh/clientprefs.sqlite"), "no fallback database created");
    std::filesystem::create_symlink(root / "missing", path);
    Reject([&] { ConfiguredStorage(root / "broken-link", path)(); }, "broken config link is an error, not implicit default");
    std::filesystem::remove(path);
}

#if defined(SR_MYSQL_DRIVER)
static void NetworkChecks(const std::filesystem::path& root, const std::filesystem::path& source) {
    Json config; std::ifstream(source) >> config;
    const auto path = root / "private/databases.json", data = root / "data";
    Write(path, config);
    const auto settings = db::ReadSettings(path, "clientprefs", "source2root.clientprefs");
    Check(settings.tls, "network fixture must use verified TLS");
    std::atomic_bool canceled{false};
    auto run = [&](const std::string& sql) { return mysql::Query(settings, {sql, {}}, canceled); };
    auto reset = [&] {
        run("DROP TABLE IF EXISTS sr_prefs_values"); run("DROP TABLE IF EXISTS sr_prefs_cookies"); run("DROP TABLE IF EXISTS sr_prefs_schema");
    };
    reset();
    auto storage = ConfiguredStorage(data, path);
    Check(storage()->Catalog().empty(), "empty shared database initialized");
    Check(storage()->Register(Music) == Music && storage()->Register(Music) == Music, "registration idempotent");
    storage()->Register(Rank);
    storage()->Register({"Music", "case-sensitive", Access::Public});
    Check(storage()->Catalog().size() == 3, "cookie names case-sensitive on both servers");
    Reject([&] { storage()->Register({Music.name, Music.description, Access::Private}); }, "metadata conflict refused");
    const std::string text = "café 🎵 '; DROP TABLE sr_prefs_values; --";
    storage()->Save(First, {{"music", {text, 4000000000ll}}, {"rank", {"", 8}}});
    const auto persisted = storage()->Load(First);
    Check(persisted.at("music").text == text && persisted.at("music").updated == 4000000000ll &&
        persisted.at("rank").text.empty() && storage()->Load(First + 1).empty(), "bound strings, empty values, wide timestamps and account isolation");
    Reject([&] { storage()->Save(First, {{"music", {"must-rollback", 9}}, {"zz_missing", {"invalid", 9}}}); }, "unknown cookie aborts batch after earlier write");
    Check(storage()->Load(First).at("music").text == text, "domain error rolls back entire write batch");
    run("ALTER TABLE sr_prefs_values ADD CONSTRAINT sr_prefs_reject CHECK (value <> 'refused')");
    Reject([&] { storage()->Save(First, {{"music", {"refused", 10}}}); }, "server rejection fails write");
    run("ALTER TABLE sr_prefs_values DROP CONSTRAINT sr_prefs_reject");
    Check(storage()->Load(First).at("music").text == text, "server error preserves prior durable value");

    const Identity player{0, 1, First};
    {
        Service a(storage), b(ConfiguredStorage(data, path));
        a.Sync({player}); b.Sync({player});
        Until(a, [&] { return a.Pending() == 0; }); Until(b, [&] { return b.Pending() == 0; });
        Check(a.Ready() && b.Ready(), "two independent server caches initialized");
        a.Set(player, a.Find("music"), "server-a", 11);
        b.Set(player, b.Find("rank"), "server-b", 12);
        Until(a, [&] { return a.Persisted(player); }); Until(b, [&] { return b.Persisted(player); });
        Check(storage()->Load(First).at("music").text == "server-a" && storage()->Load(First).at("rank").text == "server-b",
            "stale independent caches update only their changed cookies");
        Check(b.Get(player, b.Find("music")).text == text, "shared storage does not claim continuous cache replication");
        b.Refresh(player); Until(b, [&] { return b.Status(player) == State::Ready; });
        Check(b.Get(player, b.Find("music")).text == "server-a", "refresh observes another server's writes");
        b.Set(player, b.Find("music"), "last-commit", 1);
        Until(b, [&] { return b.Persisted(player); });
        Check(storage()->Load(First).at("music").text == "last-commit", "last commit wins even with older timestamp");
        auto broken = config; broken["connections"]["clientprefs"]["password"] = "intentionally-invalid-fixture-password";
        Write(path, broken);
        a.Set(player, a.Find("music"), "survives-offline-failure", 13);
        Until(a, [&] { return a.Pending() == 0; });
        Check(!a.ErrorText(player).empty() && !a.Persisted(player), "connection failure retains accepted dirty write");
        a.Sync({});
        Check(!a.CanStop(), "dirty disconnected account prevents unload");
        std::filesystem::remove(path);
        Reject([&] { storage(); }, "missing remote config cannot switch to SQLite");
        Check(!std::filesystem::exists(data / "clientprefs.sqlite"), "remote failure never creates a fallback database");
        Write(path, config);
        a.RetryWrites(); Until(a, [&] { return a.CanStop(); });
        Check(storage()->Load(First).at("music").text == "survives-offline-failure", "repaired credentials are reread and dirty offline writes commit");
        b.Sync({}); Until(b, [&] { return b.CanStop(); });
        a.SetIdentity(First + 1, a.Find("music"), "shared-offline", 14);
        a.SetIdentity(First + 1, a.Find("music"), "shared-offline-latest", 15);
        Until(a, [&] { return a.IdentityPersisted(First + 1); });
        Check(storage()->Load(First + 1).at("music").text == "shared-offline-latest", "offline account updates commit through shared SQL");
    }
    // Fill all but one catalog slot, then race two independent registrations.
    {
        mysql::Session session(settings, canceled);
        for (unsigned i = 3; i < MaxCookies - 1; ++i) {
            db::QueryInput query{"INSERT INTO sr_prefs_cookies(name,description,access) VALUES(?,'',0)", {}};
            query.Bind(1, "limit" + std::to_string(i)); session.Execute(query);
        }
        session.Commit();
    }
    std::barrier ready(3); std::atomic<unsigned> accepted{0}, rejected{0};
    auto register_last = [&](const char* name) {
        ready.arrive_and_wait();
        try { storage()->Register({name, "", Access::Public}); ++accepted; } catch (const Error&) { ++rejected; }
    };
    std::thread one(register_last, "last_one"), two(register_last, "last_two");
    ready.arrive_and_wait(); one.join(); two.join();
    Check(accepted == 1 && rejected == 1 && storage()->Catalog().size() == MaxCookies, "cross-server registration lock enforces global catalog limit");
    Values batch;
    for (const auto& cookie : storage()->Catalog()) batch.emplace(cookie.name, Value{std::string(255, 'x'), 4000000001ll});
    storage()->Save(First, batch);
    Check(storage()->Load(First).size() == MaxCookies, "maximum batch fits transaction command and byte limits");
    run("UPDATE sr_prefs_schema SET version=2 WHERE id=1");
    Reject([&] { storage()->Catalog(); }, "future schema refused");
    run("UPDATE sr_prefs_schema SET version=1 WHERE id=1");
    run("INSERT INTO sr_prefs_schema VALUES(2,1)");
    Reject([&] { storage()->Catalog(); }, "multiple schema rows refused");
    reset();
    run("CREATE TABLE sr_prefs_schema(id TINYINT PRIMARY KEY,version INT NOT NULL) ENGINE=MyISAM");
    Reject([&] { storage()->Catalog(); }, "nontransactional existing schema refused");
    reset();
}
#endif

int main(int argc, char** argv) {
    try {
        Check(argc == 2 || argc == 3, "clientprefs_configured private-fixture [private-network-config]");
        const std::filesystem::path root(argv[1]);
        std::filesystem::remove_all(root); std::filesystem::create_directories(root);
        std::filesystem::permissions(root, std::filesystem::perms::owner_all);
        if (argc == 2) LocalChecks(root);
#if defined(SR_MYSQL_DRIVER)
        else NetworkChecks(root, argv[2]);
#else
        else throw std::runtime_error("MySQL driver unavailable");
#endif
        std::cout << "Configured client preferences " << (argc == 2 ? "local" : "shared SQL") << " checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
