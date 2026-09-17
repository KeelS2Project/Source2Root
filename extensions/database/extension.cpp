#include "database.h"
#include "query.h"
#include "settings.h"
#if defined(SR_POSTGRESQL_DRIVER)
#include "driver.h"
#endif
#if defined(SR_MYSQL_DRIVER)
#include "mysql_driver.h"
#endif
#include <source2root/extension.hpp>
#include <source2root/work_queue.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace {
using namespace keels2::authoring;
using source2root::NativeCall;
using Database = source2root::sqlite::Database;
using Statement = source2root::sqlite::Statement;
using Connection = std::shared_ptr<Database>;

class DatabaseExtension final : public source2root::Extension {
public:
    static constexpr PluginInfo Info{"Source2Root Database", "KeelS2 Project", "1.0.0", "Database access for script plugins"};
    static constexpr PluginRequirement Requirements[]{{"Source2Root", "1.0.0", DependencyRequirement::exact}};
    DatabaseExtension() : Extension("source2root.database") {}
    void OnGameFrame(bool, bool, bool) override { if (queue_) queue_->Dispatch(); }
private:
    static constexpr std::uint32_t ConnectionType = 1, StatementType = 2, RequestType = 3, QueryType = 4;
    struct Result {
        source2root::sqlite::QueryResult query;
        std::string error;
        std::int32_t handle = 0;
        bool ready = false;
    };
    struct Request {
        DatabaseExtension* extension;
        SrCallback callback;
        std::shared_ptr<Result> result;
        std::unique_ptr<source2root::WorkQueue::Ticket> ticket;
        ~Request() {
            ticket.reset();
            extension->CancelCallback(callback);
        }
    };
    std::unique_ptr<source2root::WorkQueue> queue_;
    bool PrepareExtensionUnload() override {
        if (queue_) {
            queue_->Dispatch();
            if (queue_->Pending()) return false;
            queue_.reset();
        }
        return true;
    }
    bool OnExtensionStart() override {
        return RegisterNative("SQL_OpenSQLite", 1, &DatabaseExtension::Open)
            && RegisterNative("SQL_Close", 1, &DatabaseExtension::Close)
            && RegisterNative("SQL_Prepare", 2, &DatabaseExtension::Prepare)
            && RegisterNative("SQL_BindInt", 3, &DatabaseExtension::BindInt)
            && RegisterNative("SQL_BindFloat", 3, &DatabaseExtension::BindFloat)
            && RegisterNative("SQL_BindString", 3, &DatabaseExtension::BindString)
            && RegisterNative("SQL_BindNull", 2, &DatabaseExtension::BindNull)
            && RegisterNative("SQL_Step", 1, &DatabaseExtension::Step)
            && RegisterNative("SQL_Reset", 1, &DatabaseExtension::Reset)
            && RegisterNative("SQL_Finalize", 1, &DatabaseExtension::Finalize)
            && RegisterNative("SQL_ColumnCount", 1, &DatabaseExtension::ColumnCount)
            && RegisterNative("SQL_IsNull", 2, &DatabaseExtension::IsNull)
            && RegisterNative("SQL_GetInt", 3, &DatabaseExtension::GetInt)
            && RegisterNative("SQL_GetFloat", 3, &DatabaseExtension::GetFloat)
            && RegisterNative("SQL_GetString", 4, &DatabaseExtension::GetString)
            && RegisterNative("SQL_Begin", 1, &DatabaseExtension::Begin)
            && RegisterNative("SQL_Commit", 1, &DatabaseExtension::Commit)
            && RegisterNative("SQL_Rollback", 1, &DatabaseExtension::Rollback)
            && RegisterNative("SQL_AffectedRows", 1, &DatabaseExtension::AffectedRows)
            && RegisterNative("SQL_InsertId", 3, &DatabaseExtension::InsertId)
            && RegisterNative("SQL_QuerySQLiteAsync", 4, &DatabaseExtension::QueryAsync)
            && RegisterNative("SQL_CloseRequest", 1, &DatabaseExtension::CloseRequest)
            && RegisterNative("SQL_RequestReady", 1, &DatabaseExtension::RequestReady)
            && RegisterNative("SQL_ResultRows", 1, &DatabaseExtension::ResultRows)
            && RegisterNative("SQL_ResultColumns", 1, &DatabaseExtension::ResultColumns)
            && RegisterNative("SQL_ResultIsNull", 3, &DatabaseExtension::ResultIsNull)
            && RegisterNative("SQL_ResultInt", 4, &DatabaseExtension::ResultInt)
            && RegisterNative("SQL_ResultFloat", 4, &DatabaseExtension::ResultFloat)
            && RegisterNative("SQL_ResultString", 5, &DatabaseExtension::ResultString)
            && RegisterNative("SQL_ResultChanges", 1, &DatabaseExtension::ResultChanges)
            && RegisterNative("SQL_ResultInsertId", 3, &DatabaseExtension::ResultInsertId)
            && RegisterNative("SQL_CreateQuery", 1, &DatabaseExtension::CreateQuery)
            && RegisterNative("SQL_CloseQuery", 1, &DatabaseExtension::CloseQuery)
            && RegisterNative("SQL_QueryBindInt", 3, &DatabaseExtension::QueryBindInt)
            && RegisterNative("SQL_QueryBindFloat", 3, &DatabaseExtension::QueryBindFloat)
            && RegisterNative("SQL_QueryBindString", 3, &DatabaseExtension::QueryBindString)
            && RegisterNative("SQL_QueryBindNull", 2, &DatabaseExtension::QueryBindNull)
            && RegisterNative("SQL_ExecuteSQLiteAsync", 4, &DatabaseExtension::ExecuteSQLiteAsync)
            && RegisterNative("SQL_ExecuteAsync", 4, &DatabaseExtension::ExecuteAsync);
    }
    template <typename Function>
    static std::int32_t Sql(NativeCall& call, Function function, std::int32_t failure = 0) {
        try { return function(); }
        catch (const source2root::sqlite::Error& error) { return call.Fail(error.what(), failure); }
        catch (const std::filesystem::filesystem_error&) { return call.Fail("Database directory is unavailable.", failure); }
    }
    static Connection& Db(NativeCall& call) { return call.Resource<Connection>(call.Int(1), ConnectionType); }
    static Statement& Stmt(NativeCall& call) { return call.Resource<Statement>(call.Int(1), StatementType); }
    static std::filesystem::path Filename(NativeCall& call) {
        const auto name = call.String(1);
        if (name.empty() || name.size() > 64 || !std::all_of(name.begin(), name.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        })) throw source2root::sqlite::Error("Database name must use 1..64 letters, digits, underscores or hyphens.");
        const auto directory = std::filesystem::path(call.DataPath()) / "sqlite";
        std::filesystem::create_directories(directory);
        if (std::filesystem::is_symlink(directory)) throw source2root::sqlite::Error("Database directory must not be a symbolic link.");
        return directory / (name + ".sqlite");
    }
    std::int32_t Open(NativeCall& call) {
        return Sql(call, [&] {
            return call.Own(ConnectionType, std::make_unique<Connection>(std::make_shared<Database>(Filename(call))));
        });
    }
    std::int32_t QueryAsync(NativeCall& call) {
        return Sql(call, [&] {
            const auto filename = Filename(call);
            const auto sql = call.String(2);
            return Submit(call, [filename, sql](const auto& canceled) { return source2root::sqlite::Query(filename, sql, canceled); });
        });
    }
    std::int32_t Submit(NativeCall& call, std::function<source2root::db::QueryResult(const std::atomic_bool&)> work) {
        const auto data = call.Int(4);
        // Start workers only after this module has successfully loaded.
        if (!queue_) queue_ = std::make_unique<source2root::WorkQueue>(1, 32);
        auto result = std::make_shared<Result>();
        auto request = std::make_unique<Request>();
        request->extension = this;
        request->callback = call.Callback(3);
        request->result = result;
        const auto callback = request->callback;
        request->ticket = queue_->Submit([result, work = std::move(work)](const auto& canceled) {
            result->query = work(canceled);
        }, [this, result, callback, data](std::exception_ptr error) {
            if (!result->ready) {
                if (error) {
                    try { std::rethrow_exception(error); }
                    catch (const std::exception& failure) { result->error = std::string(failure.what()).substr(0, 4095); }
                    catch (...) { result->error = "Database worker failed."; }
                }
                result->ready = true;
            }
            return DeliverCallback(callback, {result->handle, data}, result->error.c_str()) != KEEL_RESULT_BUSY;
        });
        if (!request->ticket) return call.Fail("Database queue is full (32 requests).");
        result->handle = call.Own(RequestType, std::move(request));
        return result->handle;
    }
    static source2root::db::QueryInput& Input(NativeCall& call, unsigned index = 1) {
        return call.Resource<source2root::db::QueryInput>(call.Int(index), QueryType);
    }
    std::int32_t CreateQuery(NativeCall& call) {
        return Sql(call, [&] {
            auto input = std::make_unique<source2root::db::QueryInput>();
            input->sql = call.String(1);
            input->Validate();
            return call.Own(QueryType, std::move(input));
        });
    }
    std::int32_t CloseQuery(NativeCall& call) { call.Close(call.Int(1), QueryType); return 1; }
    std::int32_t QueryBindInt(NativeCall& call) { return Sql(call, [&] { Input(call).Bind(call.Int(2), call.Int(3)); return 1; }); }
    std::int32_t QueryBindFloat(NativeCall& call) { return Sql(call, [&] { Input(call).Bind(call.Int(2), static_cast<double>(call.Float(3))); return 1; }); }
    std::int32_t QueryBindString(NativeCall& call) { return Sql(call, [&] { Input(call).Bind(call.Int(2), call.String(3)); return 1; }); }
    std::int32_t QueryBindNull(NativeCall& call) { return Sql(call, [&] { Input(call).Bind(call.Int(2), {}); return 1; }); }
    std::int32_t ExecuteSQLiteAsync(NativeCall& call) {
        return Sql(call, [&] {
            const auto filename = Filename(call);
            const auto input = Input(call, 2);
            input.Validate();
            return Submit(call, [filename, input](const auto& canceled) { return source2root::sqlite::Query(filename, input, canceled); });
        });
    }
    std::int32_t ExecuteAsync(NativeCall& call) {
        return Sql(call, [&] {
            const auto config = std::filesystem::path(call.ConfigPath()) / "databases.json";
            const auto shared = std::filesystem::path(call.DataPath(true)) / "sqlite";
            const auto profile = call.String(1), plugin = call.ScriptId();
            const auto input = Input(call, 2);
            input.Validate();
            return Submit(call, [config, shared, profile, plugin, input](const auto& canceled) {
                const auto settings = source2root::db::ReadSettings(config, profile, plugin);
                if (settings.driver == "sqlite") {
                    std::filesystem::create_directories(shared);
                    if (std::filesystem::is_symlink(shared)) throw source2root::db::Error("Database directory must not be a symbolic link.");
                    return source2root::sqlite::Query(shared / (settings.database + ".sqlite"), input, canceled);
                }
                if (settings.driver == "postgresql") {
#if defined(SR_POSTGRESQL_DRIVER)
                    return source2root::postgresql::Query(settings, input, canceled);
#else
                    throw source2root::db::Error("PostgreSQL driver is not installed.");
#endif
                }
#if defined(SR_MYSQL_DRIVER)
                return source2root::mysql::Query(settings, input, canceled);
#else
                throw source2root::db::Error("MySQL/MariaDB driver is not installed.");
#endif
            });
        });
    }
    static Result& RequestResult(NativeCall& call) {
        return *call.Resource<Request>(call.Int(1), RequestType).result;
    }
    static source2root::sqlite::QueryResult& Completed(NativeCall& call) {
        auto& result = RequestResult(call);
        if (!result.ready) throw source2root::sqlite::Error("Database request is not complete.");
        if (!result.error.empty()) throw source2root::sqlite::Error(result.error);
        return result.query;
    }
    static const source2root::sqlite::QueryValue& Value(NativeCall& call) {
        auto& result = Completed(call);
        const auto row = call.Int(2), column = call.Int(3);
        if (row < 0 || static_cast<std::size_t>(row) >= result.rows.size() || column < 0 || column >= result.columns)
            throw source2root::sqlite::Error("Database result row or column is out of bounds.");
        return result.rows[row][column];
    }
    std::int32_t CloseRequest(NativeCall& call) { call.Close(call.Int(1), RequestType); return 1; }
    std::int32_t RequestReady(NativeCall& call) { return RequestResult(call).ready ? 1 : 0; }
    std::int32_t ResultRows(NativeCall& call) { return Sql(call, [&] { return static_cast<int>(Completed(call).rows.size()); }, -1); }
    std::int32_t ResultColumns(NativeCall& call) { return Sql(call, [&] { return Completed(call).columns; }, -1); }
    std::int32_t ResultIsNull(NativeCall& call) { return Sql(call, [&] { return Value(call).null ? 1 : 0; }, -1); }
    std::int32_t ResultInt(NativeCall& call) {
        call.OutputCell(4, 0);
        return Sql(call, [&] {
            const auto& value = Value(call);
            if (!value.integer) throw source2root::sqlite::Error("Value is null or exceeds SourcePawn cell range.");
            call.OutputCell(4, *value.integer); return 1;
        });
    }
    std::int32_t ResultFloat(NativeCall& call) {
        call.OutputCell(4, 0);
        return Sql(call, [&] {
            const auto& value = Value(call);
            if (!value.number) throw source2root::sqlite::Error("Value is null or exceeds finite SourcePawn float range.");
            call.OutputCell(4, std::bit_cast<std::int32_t>(*value.number)); return 1;
        });
    }
    std::int32_t ResultString(NativeCall& call) {
        call.Output(4, call.Int(5), "");
        return Sql(call, [&] { call.Output(4, call.Int(5), Value(call).text); return 1; });
    }
    std::int32_t ResultChanges(NativeCall& call) { return Sql(call, [&] { return Completed(call).changes; }, -1); }
    std::int32_t ResultInsertId(NativeCall& call) {
        call.Output(2, call.Int(3), "");
        return Sql(call, [&] { call.Output(2, call.Int(3), Completed(call).inserted); return 1; });
    }
    std::int32_t Close(NativeCall& call) { call.Close(call.Int(1), ConnectionType); return 1; }
    std::int32_t Prepare(NativeCall& call) {
        return Sql(call, [&] { return call.Own(StatementType, std::make_unique<Statement>(Db(call), call.String(2))); });
    }
    std::int32_t BindInt(NativeCall& call) { return Sql(call, [&] { Stmt(call).BindInt(call.Int(2), call.Int(3)); return 1; }); }
    std::int32_t BindFloat(NativeCall& call) { return Sql(call, [&] { Stmt(call).BindFloat(call.Int(2), call.Float(3)); return 1; }); }
    std::int32_t BindString(NativeCall& call) { return Sql(call, [&] { Stmt(call).BindString(call.Int(2), call.String(3)); return 1; }); }
    std::int32_t BindNull(NativeCall& call) { return Sql(call, [&] { Stmt(call).BindNull(call.Int(2)); return 1; }); }
    std::int32_t Step(NativeCall& call) { return Sql(call, [&] { return Stmt(call).Step() ? 1 : 0; }, -1); }
    std::int32_t Reset(NativeCall& call) { return Sql(call, [&] { Stmt(call).Reset(); return 1; }); }
    std::int32_t Finalize(NativeCall& call) { call.Close(call.Int(1), StatementType); return 1; }
    std::int32_t ColumnCount(NativeCall& call) { return Stmt(call).Columns(); }
    std::int32_t IsNull(NativeCall& call) { return Sql(call, [&] { return Stmt(call).IsNull(call.Int(2)) ? 1 : 0; }, -1); }
    std::int32_t GetInt(NativeCall& call) {
        call.OutputCell(3, 0);
        return Sql(call, [&] { call.OutputCell(3, Stmt(call).Int(call.Int(2))); return 1; });
    }
    std::int32_t GetFloat(NativeCall& call) {
        call.OutputCell(3, 0);
        return Sql(call, [&] {
            const auto value = Stmt(call).Float(call.Int(2));
            if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
                return call.Fail("Value exceeds finite SourcePawn float range.");
            call.OutputCell(3, std::bit_cast<std::int32_t>(static_cast<float>(value)));
            return 1;
        });
    }
    std::int32_t GetString(NativeCall& call) {
        call.Output(3, call.Int(4), "");
        return Sql(call, [&] { call.Output(3, call.Int(4), Stmt(call).String(call.Int(2))); return 1; });
    }
    std::int32_t Begin(NativeCall& call) { return Sql(call, [&] { Db(call)->Execute("BEGIN IMMEDIATE"); return 1; }); }
    std::int32_t Commit(NativeCall& call) { return Sql(call, [&] { Db(call)->Execute("COMMIT"); return 1; }); }
    std::int32_t Rollback(NativeCall& call) { return Sql(call, [&] { Db(call)->Execute("ROLLBACK"); return 1; }); }
    std::int32_t AffectedRows(NativeCall& call) { return Db(call)->Changes(); }
    std::int32_t InsertId(NativeCall& call) { call.Output(2, call.Int(3), std::to_string(Db(call)->InsertId())); return 1; }
};
}

KEELS2_PLUGIN(DatabaseExtension)
