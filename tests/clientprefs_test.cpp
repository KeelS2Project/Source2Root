#include "service.h"
#include "menu.h"

#include <fstream>
#include <iostream>

using namespace source2root::prefs;
static void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template <typename Operation> static void Reject(Operation operation, const char* message) {
    bool failed = false;
    try { operation(); } catch (const Error&) { failed = true; }
    Check(failed, message);
}
template <typename Predicate> static void Until(Service& service, Predicate done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!done()) {
        service.Pump();
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("Timed out waiting for preferences jobs.");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
constexpr std::uint64_t First = 76561198000000001ull, Second = First + 1;
const Definition Public{"music", "Music preference", Access::Public};
const Definition Protected{"rank", "Server-managed rank", Access::Protected};
const Definition Private{"internal", "Internal value", Access::Private};

static void StorageChecks(const std::filesystem::path& root) {
    const auto path = root / "prefs.sqlite";
    {
        Store store(path);
        Check(store.Catalog().empty(), "new database catalog empty");
        Check(store.Register(Public) == Public && store.Register(Public) == Public, "registration idempotent");
        store.Register(Private); store.Register(Protected);
        Reject([&] { store.Register({Public.name, Public.description, Access::Private}); }, "access mode cannot silently change");
        Reject([&] { store.Register({"bad/name", "", Access::Public}); }, "path-like cookie names rejected");
        Reject([&] { store.Register({"bad", "", static_cast<Access>(99)}); }, "unknown access rejected");
        Reject([&] { store.Register({"bad", std::string(256, 'd'), Access::Public}); }, "description bound");
        Reject([&] { store.Load(0); }, "unauthenticated identity rejected");
        Reject([&] { store.Load(First | (2ull << 52)); }, "wrong account type rejected");
        const std::string text = "Unicode café 🎵 '; DROP TABLE sr_prefs_values; --";
        store.Save(First, {{Public.name, {text, 4000000000ll}}, {Private.name, {"", 9}}});
        const auto values = store.Load(First);
        Check(values.at(Public.name).text == text && values.at(Public.name).updated == 4000000000ll,
            "bound Unicode text and post-2038 timestamp preserved");
        Check(values.at(Private.name).text.empty() && store.Load(Second).empty(), "empty values and account isolation");
        Reject([&] { store.Save(First, {{Public.name, {"changed", 10}}, {"zz_missing", {"bad", 10}}}); }, "unknown cookie fails batch");
        Check(store.Load(First).at(Public.name).text == text, "failed batch rolls back earlier values");
        Reject([&] { store.Save(First, {{Public.name, {std::string(256, 'x'), 10}}}); }, "value bound");
        Reject([&] { store.Save(First, {{Public.name, {std::string("a\0b", 3), 10}}}); }, "embedded NUL rejected");
        for (unsigned i = 3; i < MaxCookies; ++i) store.Register({"limit" + std::to_string(i), "", Access::Public});
        Reject([&] { store.Register({"too_many", "", Access::Public}); }, "catalog bound persists");
    }
    Check(Store(path).Load(First).at(Public.name).updated == 4000000000ll && Store(path).Catalog().size() == MaxCookies,
        "catalog and values survive closing every connection");
    auto bad = std::make_shared<source2root::sqlite::Database>(root / "future.sqlite");
    bad->Execute("CREATE TABLE sr_prefs_schema(version INTEGER PRIMARY KEY)");
    bad->Execute("INSERT INTO sr_prefs_schema VALUES(2)");
    Reject([&] { Store future(root / "future.sqlite"); }, "unknown schema refused");
}

static void Cache(const std::filesystem::path& root) {
    const auto path = root / "prefs.sqlite";
    const Identity first{0, 1, First}, replacement{0, 2, Second}, rejoined{1, 3, First};
    {
        Service service(path);
        const auto music = service.Register(Public), rank = service.Register(Protected), hidden = service.Register(Private);
        service.Sync({first});
        Reject([&] { service.Get(first, music); }, "read before cache/registration completion refused");
        Until(service, [&] { return service.Pending() == 0; });
        Check(service.Ready() && music->state == State::Ready && service.Status(first) == State::Ready, "async catalog, registration and account load");
        Check(service.Register(Public) == music && service.Get(first, music).text.empty(), "shared cookie registration and unset value");
        const auto visible = service.UserCookies();
        Check(visible.size() == 2 && visible[0].name == "music" && visible[1].name == "rank", "private cookies hidden, protected visible");
        Reject([&] { service.UserSet(first, rank->definition.name, "1", 1); }, "player cannot write protected value");
        Reject([&] { service.UserSet(first, hidden->definition.name, "1", 1); }, "player cannot write private value");
        service.Set(first, hidden, "script-owned", 2);
        service.UserSet(first, Public.name, "first", 3);
        service.UserSet(first, Public.name, "latest", 4);
        Check(service.Get(first, music).text == "latest" && !service.Persisted(first), "cache acknowledgement is not durability acknowledgement");
        Until(service, [&] { return service.Persisted(first); });
        Check(Store(path).Load(First).at(Public.name).text == "latest", "newer edit survives an older in-flight save completion");
        Store(path).Save(First, {{Public.name, {"external", 8}}});
        Check(service.Get(first, music).text == "latest", "another writer does not mutate live caches");
        service.Refresh(first);
        Reject([&] { service.Set(first, music, "during-refresh", 8); }, "writes refused while refresh loads");
        Until(service, [&] { return service.Status(first) == State::Ready; });
        Check(service.Get(first, music).text == "external", "refresh reads another writer's durable value");
        service.Set(first, music, "latest", 9);
        Reject([&] { service.Refresh(first); }, "refresh cannot discard accepted pending writes");
        Until(service, [&] { return service.Persisted(first); });
        bool wrong_thread = false;
        std::thread worker([&] { try { service.Find("music"); } catch (const std::logic_error&) { wrong_thread = true; } });
        worker.join();
        Check(wrong_thread, "cache API refuses off-thread use");
        Reject([&] { service.Sync({first, first}); }, "duplicate slot snapshot rejected");
        Check(service.Get(first, music).text == "latest", "bad snapshot does not invalidate current sessions");
        service.Sync({replacement});
        Reject([&] { service.Get(first, music); }, "slot reuse invalidates previous connection");
        Until(service, [&] { return service.Status(replacement) == State::Ready; });
        Check(service.Get(replacement, music).text.empty(), "reused slot cannot see prior account values");
        service.Sync({replacement, rejoined});
        Until(service, [&] { return service.Status(rejoined) == State::Ready; });
        Check(service.Get(rejoined, music).text == "latest", "same authenticated account reconnects to its persisted values");
        // Hold a real SQLite write lock until the worker reports failure.
        auto lock = std::make_shared<source2root::sqlite::Database>(path);
        lock->Execute("BEGIN IMMEDIATE");
        service.Set(rejoined, music, "survives-disconnect", 5);
        Until(service, [&] { return service.Pending() == 0; });
        Check(!service.ErrorText(rejoined).empty() && !service.Persisted(rejoined), "failed database write stays dirty and reports its error");
        service.Sync({});
        Check(!service.CanStop(), "disconnected dirty account prevents silent unload/data loss");
        lock->Execute("ROLLBACK"); lock.reset();
        service.RetryWrites();
        Until(service, [&] { return service.CanStop(); });
        Check(Store(path).Load(First).at(Public.name).text == "survives-disconnect", "failed write retries after disconnect");
    }
    {
        Service service(path);
        // A disconnected load completion must never establish a new connection.
        service.Sync({first}); service.Sync({replacement});
        Until(service, [&] { return service.Pending() == 0; });
        Check(service.Ready() && service.Find(Private.name)->definition.access == Access::Private, "catalog restored on restart");
        Reject([&] { service.Status(first); }, "late load completion does not revive retired connection");
        const auto music = service.Find(Public.name);
        Check(service.Get(replacement, music).text.empty(), "late first-account load never leaks into reused slot");
        service.Sync({rejoined});
        Until(service, [&] { return service.Status(rejoined) == State::Ready; });
        Check(service.Get(rejoined, music).text == "survives-disconnect", "restart restores successful durable retry");
        auto foreign = std::make_shared<Cookie>(*music);
        Reject([&] { service.Get(rejoined, foreign); }, "foreign cookie object refused");
        Reject([&] { service.Register({Public.name, Public.description, Access::Private}); }, "persisted access metadata cannot be silently downgraded");
        Until(service, [&] { return service.CanStop(); });
    }
    {
        Service service(path);
        auto conflicting = service.Register({Public.name, Public.description, Access::Private});
        Until(service, [&] { return service.Pending() == 0; });
        Check(conflicting->state == State::Failed && service.Find(Public.name)->definition == Public &&
            service.Register(Public)->state == State::Ready, "persisted definition survives conflicting early registration and remains usable");
        Check(service.CanStop(), "failed registration does not retain work");
    }
    {
        std::ofstream(root / "not-a-directory") << "fixture";
        Service service(root / "not-a-directory/prefs.sqlite");
        const auto cookie = service.Register(Public);
        service.Sync({first});
        Until(service, [&] { return service.Pending() == 0; });
        Check(!service.Ready() && !service.ErrorText().empty() && cookie->state == State::Failed &&
            service.Status(first) == State::Failed, "storage initialization failure is visible at every readiness boundary");
        Reject([&] { service.Set(first, cookie, "not accepted", 6); }, "failed cache never acknowledges writes");
        Check(service.CanStop(), "no accepted writes remain after initial failure");
        std::filesystem::remove(root / "not-a-directory");
        service.RetryCatalog(); service.RetryLoad(first);
        auto retry = service.Register(Public);
        Until(service, [&] { return service.Pending() == 0; });
        Check(service.Ready() && retry->state == State::Ready && service.Status(first) == State::Ready,
            "operator can recover catalog, registration and cache after repairing storage");
        service.Set(first, retry, "recovered", 7);
        Until(service, [&] { return service.CanStop(); });
    }
}

static void Menus(const std::filesystem::path& root) {
    Service service(root / "prefs.sqlite");
    const Identity player{0, 1, First};
    auto music = service.Register(Public), rank = service.Register(Protected), hidden = service.Register(Private);
    service.Sync({player});
    Until(service, [&] { return service.Pending() == 0; });
    auto prefab = std::make_shared<Prefab>(Prefab{music, PrefabType::YesNo, "Music", 1});
    ValidatePrefab(*prefab);
    Reject([&] { ValidatePrefab({rank, PrefabType::YesNo, "Rank", 1}); }, "protected cookies have no editable prefab");
    Reject([&] { ValidatePrefab({hidden, PrefabType::YesNo, "Hidden", 1}); }, "private cookies have no menu prefab");
    Reject([&] { ValidatePrefab({music, static_cast<PrefabType>(99), "Bad", 1}); }, "unknown prefab rejected");
    const std::vector<std::weak_ptr<Prefab>> items{prefab};
    auto menu = BuildSettings(service, player, items, 0);
    Check(menu.rows.size() == 2 && menu.rows[0].enabled && !menu.rows[1].enabled &&
        menu.rows[1].text.find("[read only]") != std::string::npos, "public editable, protected visible, private omitted");
    for (int i = 0; i < 4; ++i) {
        const auto type = static_cast<PrefabType>(i);
        Check(ChoiceValue(type, true) == (i == 0 ? "yes" : i == 2 ? "on" : "1") &&
            ChoiceValue(type, false) == (i == 0 ? "no" : i == 2 ? "off" : "0"), "all four prefab encodings");
        Check(ChoiceLabel(type, true) == (i < 2 ? "Yes" : "On"), "prefab labels match encoded values");
    }
    auto replacement = std::make_shared<Prefab>(Prefab{music, PrefabType::OnOff, "Replacement", 2});
    std::vector<std::weak_ptr<Prefab>> staged{prefab, replacement};
    Check(BuildSettings(service, player, staged, 0).rows[0].text.starts_with("Replacement"), "latest live staged registration wins");
    replacement.reset();
    Check(BuildSettings(service, player, staged, 0).rows[0].text.starts_with("Music"), "failed staged replacement preserves prior registration");
    menu.rows.clear(); prefab.reset();
    Check(!BuildSettings(service, player, staged, 0).rows[0].enabled, "closed prefab leaves a public value read only");
    for (unsigned i = 0; i < 32; ++i) service.Register({"extra" + std::to_string(i), "", Access::Public});
    Until(service, [&] { return service.Pending() == 0; });
    const auto first = BuildSettings(service, player, {}, 0), last = BuildSettings(service, player, {}, 999);
    Check(first.page == 0 && first.rows.size() == SettingsPerPage + 1 && first.rows.back().page == 1 &&
        last.page == 1 && last.rows.back().page == 0 && first.rows.size() <= 32 && last.rows.size() <= 32,
        "catalog pagination fits native menu bounds and clamps obsolete page numbers");
    service.Set(player, music, std::string(255, 'x'), 1);
    menu = BuildSettings(service, player, {}, 1);
    for (const auto& row : menu.rows) Check(row.text.size() <= 96, "menu text respects renderer limit");
    Until(service, [&] { return service.CanStop(); });
    service.Sync({});
    Reject([&] { BuildSettings(service, player, {}, 0); }, "disconnected identity cannot create settings menu");
}

int main(int argc, char** argv) {
    try {
        Check(argc == 3, "clientprefs_test storage|cache private-fixture");
        const std::filesystem::path root(argv[2]);
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        if (std::string(argv[1]) == "storage") StorageChecks(root);
        else if (std::string(argv[1]) == "cache") Cache(root);
        else if (std::string(argv[1]) == "menus") Menus(root);
        else throw std::runtime_error("Unknown test mode.");
        std::cout << "Client preferences " << argv[1] << " checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
