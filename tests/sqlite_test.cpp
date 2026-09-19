#include "database.h"

#include <chrono>
#include <filesystem>
#include <iostream>

using namespace source2root::sqlite;
static void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

template <typename Function>
static void Reject(Function operation, const char* message) {
    bool rejected = false;

    try {
        operation();
    } catch (const Error&) {
        rejected = true;
    }

    Check(rejected, message);
}

static void Run(const std::shared_ptr<Database>& database, const std::string& sql) {
    Statement statement(database, sql);
    Check(!statement.Step(), "statement must finish");
}

int main(int argc, char** argv) {
    try {
        Check(argc == 2, "sqlite_test private fixture directory");
        const std::filesystem::path root(argv[1]);
        std::filesystem::create_directories(root);
        const auto path = root / "database.sqlite";
        std::filesystem::remove(path);
        auto database = std::make_shared<Database>(path);

        Run(database, "CREATE TABLE values_test(id INTEGER PRIMARY KEY, n INTEGER, text TEXT, real REAL, empty TEXT)");
        {
            Statement insert(database, "INSERT INTO values_test(n,text,real,empty) VALUES(?,?,?,?)");
            insert.BindInt(1, -42);
            insert.BindString(2, "quote'; DROP TABLE values_test; --");
            insert.BindFloat(3, 1.25);
            insert.BindNull(4);
            Check(!insert.Step() && database->Changes() == 1 && database->InsertId() == 1, "bound insert");
            Reject(
                [&] {
                    insert.Step();
                },
                "step after done must not repeat writes");

            Reject(
                [&] {
                    insert.BindInt(1, 9);
                },
                "bind requires reset");

            insert.Reset();
            insert.BindInt(1, 9);
            Check(!insert.Step(), "reset for repeated use");
        }

        {
            Statement query(database, "SELECT n,text,real,empty FROM values_test WHERE id=1; -- trailing comment");
            Check(query.Columns() == 4, "column count");
            Reject(
                [&] {
                    query.Int(0);
                },
                "get requires row");

            Check(query.Step() && query.Int(0) == -42 && query.String(1) == "quote'; DROP TABLE values_test; --" &&
                query.Float(2) == 1.25 && query.IsNull(3), "typed fields and injection-safe binding");

            Reject(
                [&] {
                    query.String(4);
                },
                "column bounds");

            Check(!query.Step(), "query complete");
        }

        Reject(
            [&] {
                Statement invalid(database, "SELECT 1; SELECT 2");
            },
            "reject multiple statements");

        Reject(
            [&] {
                Statement invalid(database, "ATTACH DATABASE 'outside.sqlite' AS other");
            },
            "reject file escape via attach");

        Reject(
            [&] {
                Statement invalid(database, "PRAGMA writable_schema=ON");
            },
            "reject dangerous pragma");

        Reject(
            [&] {
                Statement invalid(database, "SELECT load_extension('anything')");
            },
            "extension loading absent");

        Reject(
            [&] {
                Statement invalid(database, "SELECT * FROM missing_table");
            },
            "SQL errors reported");

        {
            Statement big(database, "SELECT 2147483648, char(65,0,66)");
            Check(big.Step() && big.String(0) == "2147483648", "64-bit integers available losslessly as strings");
            Reject(
                [&] {
                    big.Int(0);
                },
                "cell overflow rejected");

            Reject(
                [&] {
                    big.String(1);
                },
                "embedded NUL not silently truncated");
        }

        database->Execute("BEGIN IMMEDIATE");
        Run(database, "INSERT INTO values_test(n) VALUES(10)");
        database->Execute("ROLLBACK");

        Check(!database->InTransaction(), "rollback completes");
        {
            Statement query(database, "SELECT count(*) FROM values_test");
            Check(query.Step() && query.Int(0) == 2, "rollback preserves rows");
        }

        {
            auto other = std::make_shared<Database>(path);
            database->Execute("BEGIN IMMEDIATE");
            const auto start = std::chrono::steady_clock::now();
            Reject(
                [&] {
                    other->Execute("BEGIN IMMEDIATE");
                },
                "busy connection reports failure");

            Check(std::chrono::steady_clock::now() - start < std::chrono::seconds(2), "busy wait bounded");
            database->Execute("COMMIT");
        }

        {
            Statement runaway(
                database,
                "WITH RECURSIVE loop(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM loop) SELECT sum(x) FROM loop");

            const auto start = std::chrono::steady_clock::now();
            Reject(
                [&] {
                    runaway.Step();
                },
                "runaway query interrupted");

            Check(std::chrono::steady_clock::now() - start < std::chrono::seconds(2), "execution time bounded");
        }

        {
            Statement retained(database, "SELECT count(*) FROM values_test");
            database.reset();
            Check(retained.Step() && retained.Int(0) == 2, "statement retains its connection");
        }

        database = std::make_shared<Database>(path);
        database->Execute("BEGIN IMMEDIATE");
        Run(database, "INSERT INTO values_test(n) VALUES(123)");
        database.reset();
        database = std::make_shared<Database>(path);
        Statement restored(database, "SELECT count(*) FROM values_test");
        Check(restored.Step() && restored.Int(0) == 2,
              "connection destruction rolls back transaction and retains committed data");
#if !defined(_WIN32)
        const auto link = root / "link.sqlite";
        std::filesystem::remove(link);
        std::filesystem::create_symlink(path, link);
        Reject(
            [&] {
                Database linked(link);
            },
            "do not follow database symlinks");
#endif
        std::cout << "SQLite binding, transactions, persistence, cleanup and query limits passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
