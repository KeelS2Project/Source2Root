#include "driver.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <thread>

using namespace source2root;
static void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template <typename Operation> static void Reject(Operation operation, const char* message) {
    bool rejected = false;
    try { operation(); } catch (const db::Error& error) {
        Check(std::string(error.what()).find("secret-sentinel") == std::string::npos, "diagnostics must not expose server values");
        rejected = true;
    }
    Check(rejected, message);
}
// Fixture process only: change environment before any concurrent query begins.
class Environment {
    std::string name_;
    std::optional<std::string> previous_;
    static void Set(const std::string& name, const char* value) {
#if defined(_WIN32)
        if (_putenv_s(name.c_str(), value ? value : "")) throw std::runtime_error("fixture environment update failed");
#else
        if (value ? setenv(name.c_str(), value, 1) : unsetenv(name.c_str())) throw std::runtime_error("fixture environment update failed");
#endif
    }
public:
    Environment(const char* name, const char* value) : name_(name) {
        if (const auto* old = std::getenv(name)) previous_ = old;
        Set(name_, value);
    }
    ~Environment() { try { Set(name_, previous_ ? previous_->c_str() : nullptr); } catch (...) {} }
};
static void Validation() {
    for (const char* sql : {"SELECT $1", "-- comment\r\n /* outer /*inner*/ */ SELECT 1", "WITH n AS (SELECT 1) SELECT * FROM n",
            "INSERT INTO table_name(value) VALUES($1) RETURNING id"})
        postgresql::ValidateQuery({sql, {}});
    for (const char* sql : {"COMMIT", " /*x*/ eNd", "--x\nROLLBACK", "/*nested /*x*/ */ START TRANSACTION", "BEGIN",
            "SAVEPOINT s", "RELEASE s", "PREPARE TRANSACTION 'x'", "COPY x TO STDOUT", "SET search_path=public", "RESET ALL",
            "DISCARD ALL", "; COMMIT", "/*unterminated", "--only comment"})
        Reject([&] { postgresql::ValidateQuery({sql, {}}); }, "unsafe or empty leading command refused");
    db::Settings settings; settings.driver = "postgresql"; settings.database = "fixture"; settings.user = "fixture"; settings.port = 5432;
    db::ValidatePostgreSQLSettings(settings);
    for (const auto* host : {"one,two", "/tmp", "host user=other", "postgresql://remote/database", "", "@abstract"}) {
        auto invalid = settings; invalid.host = host;
        Reject([&] { db::ValidatePostgreSQLSettings(invalid); }, "only one explicit TCP host accepted");
    }
    auto invalid = settings; invalid.tls = false; invalid.host = "remote.example";
    Reject([&] { db::ValidatePostgreSQLSettings(invalid); }, "remote TLS required for direct backend callers");
    invalid = settings; invalid.socket = (std::filesystem::temp_directory_path() / "postgresql").string();
    Reject([&] { db::ValidatePostgreSQLSettings(invalid); }, "Unix socket cannot silently downgrade TLS");
    invalid.tls = false; db::ValidatePostgreSQLSettings(invalid);
    invalid.socket = "/tmp/one,/tmp/two";
    Reject([&] { db::ValidatePostgreSQLSettings(invalid); }, "socket list refused");
    invalid = settings; invalid.timeout = 0;
    Reject([&] { db::ValidatePostgreSQLSettings(invalid); }, "zero timeout refused");
    std::atomic_bool canceled{true};
    Reject([&] { postgresql::Query(settings, {"SELECT 1", {}}, canceled); }, "canceled query refused before any connection");
    canceled = false;
    Environment service("PGSERVICE", "secret-sentinel");
    Reject([&] { postgresql::Query(settings, {"SELECT 1", {}}, canceled); }, "ambient service refused before any connection");
}
static void Integration(const char* config_path, const char* bad_ca) {
    Environment service("PGSERVICE", nullptr);
    Environment host("PGHOST", "unavailable.invalid"), address("PGHOSTADDR", "192.0.2.1"), password("PGPASSWORD", "secret-sentinel"),
        options("PGOPTIONS", "-c statement_timeout=1"), sslmode("PGSSLMODE", "disable"), certificate("PGSSLCERT", "unavailable.pem");
    const auto config = db::ReadSettings(config_path, "acceptance", "driver_test");
    Check(config.driver == "postgresql" && config.tls && config.timeout <= 3, "fixture needs PostgreSQL TLS and timeout <=3 seconds");
    std::atomic_bool canceled{false};
    auto run = [&](const std::string& sql) { return postgresql::Query(config, {sql, {}}, canceled); };
    std::cout << "Server: " << run("SELECT version()").rows.at(0).at(0).text << '\n';
    run("DROP TABLE IF EXISTS source2root_driver_checks");
    run("CREATE TABLE source2root_driver_checks(id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,n integer,value text,real_value double precision,empty_value text)");
    db::QueryInput insert{"INSERT INTO source2root_driver_checks(n,value,real_value,empty_value) VALUES($1,$2,$3,$4) RETURNING id", {}};
    insert.Bind(1, std::int32_t{42}); insert.Bind(2, std::string("quote'; DROP TABLE source2root_driver_checks; --\\end"));
    insert.Bind(3, 1.25); insert.Bind(4, std::monostate{});
    auto result = postgresql::Query(config, insert, canceled);
    Check(result.changes == 1 && result.inserted == "0" && result.rows.at(0).at(0).integer == 1, "bound insert with RETURNING identity");
    result = run("SELECT n,value,real_value,empty_value,9223372036854775807,true,false,'' FROM source2root_driver_checks");
    Check(result.rows.size() == 1 && result.columns == 8 && result.changes == 1 && result.rows[0][0].integer == 42 &&
        result.rows[0][1].text == std::get<std::string>(*insert.parameters[1]) && result.rows[0][2].number == 1.25f &&
        result.rows[0][3].null && !result.rows[0][4].integer && result.rows[0][4].text == "9223372036854775807" &&
        result.rows[0][5].integer == 1 && result.rows[0][6].integer == 0 && !result.rows[0][7].null && result.rows[0][7].text.empty(),
        "typed, boolean, null, empty and wide-integer result values");
    result = run("SELECT 1,2 WHERE false");
    Check(result.rows.empty() && result.columns == 2 && result.changes == 0, "empty rowset retains column count");
    for (const auto* sql : {"SELECT $1", "SELECT 1; SELECT 2", "SELECT * FROM missing_table", "SELECT repeat('x',4096)",
            "SELECT i FROM generate_series(1,257) i", "SELECT repeat('x',4095) FROM generate_series(1,65)",
            "SELECT 1/(i-200) FROM generate_series(1,256) i", "DO $$BEGIN RAISE EXCEPTION 'secret-sentinel'; END$$"})
        Reject([&] { run(sql); }, "invalid, late-error or oversized query fails without partial result");
    std::string columns = "SELECT 1";
    for (int i = 1; i < 33; ++i) columns += ",1";
    Reject([&] { run(columns); }, "column count bounded");
    db::QueryInput mismatch{"SELECT 1", {}}; mismatch.Bind(1, 2);
    Reject([&] { postgresql::Query(config, mismatch, canceled); }, "extra binding rejected by server");
    Reject([&] { run("INSERT INTO source2root_driver_checks(n) SELECT i FROM generate_series(1,257) i RETURNING id"); }, "RETURNING overflow aborts write");
    Check(run("SELECT count(*) FROM source2root_driver_checks").rows[0][0].integer == 1, "rejected result rolled back all rows");
    Reject([&] { run("WITH saved AS (INSERT INTO source2root_driver_checks(n) VALUES(9) RETURNING n) SELECT repeat('x',4096) FROM saved"); }, "oversized writable CTE result rejected");
    Check(run("SELECT count(*) FROM source2root_driver_checks").rows[0][0].integer == 1, "failed CTE result rolled back write");
    auto invalid = config; invalid.ca = bad_ca;
    Reject([&] { postgresql::Query(invalid, {"SELECT 1", {}}, canceled); }, "untrusted issuer rejected");
    invalid = db::ReadSettings(config_path, "hostname_mismatch", "driver_test");
    Reject([&] { postgresql::Query(invalid, {"SELECT 1", {}}, canceled); }, "trusted certificate with wrong hostname rejected");
    invalid = config; invalid.password = "secret-sentinel";
    Reject([&] { postgresql::Query(invalid, {"SELECT 1", {}}, canceled); }, "authentication failure hides credentials");
    canceled = true;
    Reject([&] { postgresql::Query(config, insert, canceled); }, "pre-canceled write never executes");
    canceled = false;
    auto start = std::chrono::steady_clock::now();
    {
        std::jthread canceler([&] { std::this_thread::sleep_for(std::chrono::milliseconds(150)); canceled = true; });
        Reject([&] { run("SELECT pg_sleep(10)"); }, "running query cooperatively canceled");
    }
    Check(std::chrono::steady_clock::now() - start < std::chrono::seconds(2), "canceled query closes without waiting for server sleep");
    canceled = false;
    start = std::chrono::steady_clock::now();
    Reject([&] { run("SELECT pg_sleep(10)"); }, "server statement timeout enforced");
    Check(std::chrono::steady_clock::now() - start < std::chrono::seconds(5), "timeout bounds a sleeping server statement");
    Check(run("SELECT count(*) FROM source2root_driver_checks").rows[0][0].integer == 1, "later connection works and canceled write absent");
    run("DROP TABLE source2root_driver_checks");
}
int main(int argc, char** argv) {
    try {
        if (argc == 1) Validation();
        else { Check(argc == 3, "postgresql_test [private_config untrusted_ca]"); Integration(argv[1], argv[2]); }
        std::cout << "PostgreSQL checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
