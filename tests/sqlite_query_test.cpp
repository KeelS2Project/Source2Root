#include "query.h"

#include <iostream>

using namespace source2root::sqlite;
static void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

template <typename Operation>
static void Reject(Operation operation, const char* message) {
    bool failed = false;

    try {
        operation();
    } catch (const Error&) {
        failed = true;
    }

    Check(failed, message);
}

int main(int argc, char** argv) {
    try {
        Check(argc == 2, "sqlite_query private fixture directory");
        const std::filesystem::path root(argv[1]);
        std::filesystem::create_directories(root);
        const auto path = root / "query.sqlite";
        std::filesystem::remove(path);
        std::atomic_bool canceled{true};
        Reject(
            [&] {
                Query(path, "CREATE TABLE items(value)", canceled);
            },
            "canceled job rejected before opening database");

        Check(!std::filesystem::exists(path), "canceled job left no file");
        canceled = false;
        auto result = Query(path, "SELECT 42,1.25,'value',NULL,2147483648,1e308", canceled);
        Check(result.rows.size() == 1 && result.columns == 6 && result.rows[0][0].integer == 42 &&
            result.rows[0][1].number == 1.25f && result.rows[0][2].text == "value" && result.rows[0][3].null &&
            !result.rows[0][4].integer && result.rows[0][4].text == "2147483648" && !result.rows[0][5].number,
            "snapshot preserves typed values, nulls and numeric overflow");

        Query(path, "CREATE TABLE items(value)", canceled);
        source2root::db::QueryInput bound{"SELECT ?,?,?,?", {}};
        bound.Bind(1, std::int32_t{42});
        bound.Bind(2, 1.25);
        bound.Bind(3, std::string("quote'; DROP TABLE items; --"));
        bound.Bind(4, std::monostate{});
        result = Query(path, bound, canceled);
        Check(result.rows[0][0].integer == 42 && result.rows[0][1].number == 1.25f && result.rows[0][2].text ==
            "quote'; DROP TABLE items; --" && result.rows[0][3].null, "async bound parameters stay data");

        result = Query(path, "INSERT INTO items VALUES(7) RETURNING value", canceled);
        Check(result.changes == 1 && result.inserted == "1" && result.rows[0][0].integer == 7,
              "write commits with result metadata");

        for (const auto* sql : {"COMMIT", "BEGIN", "ROLLBACK", "SAVEPOINT escape", "SELECT ?", "SELECT 1; SELECT 2",
            "SELECT char(65,0,66)", "PRAGMA writable_schema=ON", "ATTACH 'outside.sqlite' AS other"})
            Reject(
                [&] {
                    Query(path, sql, canceled);
                },
                "unsupported or unsafe async statement rejected");

        Reject(
            [&] {
                Query(
                    path,
                    "INSERT INTO items WITH RECURSIVE numbers(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM numbers WHERE x<257) SELECT x FROM numbers RETURNING value",
                    canceled);
            },
            "row limit rejects write result");

        result = Query(path, "SELECT count(*) FROM items", canceled);
        Check(result.rows[0][0].integer == 1, "failed result rolls back the entire write");
        Reject(
            [&] {
                Query(
                    path,
                    "WITH RECURSIVE numbers(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM numbers WHERE x<256) SELECT hex(zeroblob(1000)) FROM numbers",
                    canceled);
            },
            "total result byte limit enforced");

        std::string columns = "SELECT 1";

        for (int i = 1; i < 33; ++i)
            columns += ",1";

        Reject(
            [&] {
                Query(path, columns, canceled);
            },
            "column limit enforced");

        const auto start = std::chrono::steady_clock::now();
        Reject(
            [&] {
                Query(
                    path,
                    "WITH RECURSIVE forever(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM forever) SELECT sum(x) FROM forever",
                    canceled);
            },
            "unbounded aggregate interrupted");

        Check(std::chrono::steady_clock::now() - start < std::chrono::seconds(2), "query execution deadline bounded");
        std::cout << "SQLite snapshots, write rollback, cancellation and result limits passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
