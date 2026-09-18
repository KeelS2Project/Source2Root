#include "mysql_driver.h"
#include <chrono>
#include <iostream>
#include <thread>

using namespace source2root;
static void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template <typename Operation> static void Reject(Operation operation, const char* message) {
    bool failed = false;
    try { operation(); } catch (const db::Error&) { failed = true; }
    Check(failed, message);
}
int main(int argc, char** argv) {
    try {
        Check(argc == 3, "mysql_test private config untrusted_ca");
        const auto config = db::ReadSettings(argv[1], "acceptance", "driver_test");
        Check(config.tls, "fixture must use verified TLS");
        std::atomic_bool canceled{false};
        auto run = [&](const std::string& sql) { return mysql::Query(config, db::QueryInput{sql, {}}, canceled); };
        std::cout << "Server: " << run("SELECT VERSION()").rows[0][0].text << '\n';
        run("DROP TABLE IF EXISTS source2root_driver_checks");
        run("CREATE TABLE source2root_driver_checks(id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,n INT,value TEXT,real_value DOUBLE,empty_value TEXT) ENGINE=InnoDB");
        db::QueryInput insert{"INSERT INTO source2root_driver_checks(n,value,real_value,empty_value) VALUES(?,?,?,?)", {}};
        insert.Bind(1, std::int32_t{42}); insert.Bind(2, std::string("quote'; DROP TABLE source2root_driver_checks; --"));
        insert.Bind(3, 1.25); insert.Bind(4, std::monostate{});
        auto result = mysql::Query(config, insert, canceled);
        Check(result.changes == 1 && result.inserted == "1", "bound insert and generated identity");
        result = run("SELECT n,value,real_value,empty_value,18446744073709551615 FROM source2root_driver_checks");
        Check(result.rows.size() == 1 && result.columns == 5 && result.rows[0][0].integer == 42 &&
            result.rows[0][1].text == std::get<std::string>(*insert.parameters[1]) && result.rows[0][2].number == 1.25f &&
            result.rows[0][3].null && !result.rows[0][4].integer && result.rows[0][4].text == "18446744073709551615",
            "typed results, injection-safe parameters and unsigned 64-bit text");
        for (const auto* sql : {"SELECT ?", "SELECT 1; SELECT 2", "SELECT * FROM missing_table", "SELECT CONCAT('a',CHAR(0),'b')", "SELECT REPEAT('x',4096)"})
            Reject([&] { run(sql); }, "invalid query or oversized result rejected");
        Reject([&] { run("WITH RECURSIVE numbers(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM numbers WHERE x<257) SELECT x FROM numbers"); }, "row limit enforced");
        const auto streaming = std::chrono::steady_clock::now();
        Reject([&] { run("WITH RECURSIVE numbers(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM numbers WHERE x<1000) SELECT CASE WHEN x<=257 THEN x ELSE SLEEP(0.01) END, REPEAT('x',1024) FROM numbers"); },
            "bounded streaming result rejected");
        Check(std::chrono::steady_clock::now() - streaming < std::chrono::seconds(3), "aborted result closes the connection without draining slow remaining rows");
        auto invalid = config;
        invalid.ca = argv[2];
        Reject([&] { mysql::Query(invalid, {"SELECT 1", {}}, canceled); }, "untrusted TLS issuer rejected");
        invalid.ca.clear();
        Reject([&] { mysql::Query(invalid, {"SELECT 1", {}}, canceled); }, "private issuer requires its CA even on loopback");
        invalid = db::ReadSettings(argv[1], "hostname_mismatch", "driver_test");
        bool hostname_rejected = false;
        try { mysql::Query(invalid, {"SELECT 1", {}}, canceled); }
        catch (const db::Error& error) {
            hostname_rejected = std::string(error.what()).find("(2026)") != std::string::npos;
            if (!hostname_rejected) std::cerr << "Unexpected hostname rejection: " << error.what() << '\n';
        }
        Check(hostname_rejected, "trusted certificate with wrong server identity rejected by TLS");
        invalid = config; invalid.password += "wrong";
        bool hidden = false;
        try { mysql::Query(invalid, {"SELECT 1", {}}, canceled); }
        catch (const db::Error& error) { hidden = std::string(error.what()).find(config.password) == std::string::npos; }
        Check(hidden, "authentication failure cannot expose password");
        canceled = true;
        Reject([&] { mysql::Query(config, insert, canceled); }, "canceled query never executes");
        canceled = false;
        Check(run("SELECT count(*) FROM source2root_driver_checks").rows[0][0].integer == 1, "canceled write not committed");
        {
            mysql::Session session(config, canceled);
            session.Execute(insert);
            bool refused = false;
            std::thread foreign([&] { try { session.Execute({"SELECT 1", {}}); } catch (const db::Error&) { refused = true; } });
            foreign.join();
            Check(refused, "session refuses another thread without damaging owner transaction");
            session.Execute(insert);
            session.Commit();
        }
        Check(run("SELECT count(*) FROM source2root_driver_checks").rows[0][0].integer == 3, "multi-statement transaction commits atomically");
        {
            mysql::Session session(config, canceled);
            session.Execute(insert);
        }
        Check(run("SELECT count(*) FROM source2root_driver_checks").rows[0][0].integer == 3, "destroying uncommitted session rolls back");
        {
            mysql::Session session(config, canceled);
            session.Execute(insert);
            Reject([&] { session.Execute({"SELECT * FROM missing_table", {}}); }, "query failure poisons transaction");
            Reject([&] { session.Commit(); }, "failed session cannot commit earlier writes");
        }
        Check(run("SELECT count(*) FROM source2root_driver_checks").rows[0][0].integer == 3, "failed transaction rolls back earlier writes");
        {
            mysql::Session session(config, canceled);
            session.Execute(insert);
            canceled = true;
            Reject([&] { session.Commit(); }, "cancel before commit aborts session");
            canceled = false;
        }
        Check(run("SELECT count(*) FROM source2root_driver_checks").rows[0][0].integer == 3, "canceled transaction rolled back");
        const auto start = std::chrono::steady_clock::now();
        auto short_timeout = config;
        short_timeout.timeout = 2;
        Reject([&] { mysql::Query(short_timeout, {"SELECT SLEEP(4)", {}}, canceled); }, "finite network read timeout");
        Check(std::chrono::steady_clock::now() - start < std::chrono::seconds(6), "timeout bounds client wait");
        Check(run("SELECT 9").rows[0][0].integer == 9, "new connection succeeds after a timed-out query");
        run("DROP TABLE source2root_driver_checks");
        std::cout << "MySQL/MariaDB verified TLS, bound parameters, typed rows, limits, cancellation and timeout passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
