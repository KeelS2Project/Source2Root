#include "database.h"
#include <source2root/extension.hpp>

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
private:
    static constexpr std::uint32_t ConnectionType = 1, StatementType = 2;
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
            && RegisterNative("SQL_InsertId", 3, &DatabaseExtension::InsertId);
    }
    template <typename Function>
    static std::int32_t Sql(NativeCall& call, Function function, std::int32_t failure = 0) {
        try { return function(); }
        catch (const source2root::sqlite::Error& error) { return call.Fail(error.what(), failure); }
        catch (const std::filesystem::filesystem_error&) { return call.Fail("Database directory is unavailable.", failure); }
    }
    static Connection& Db(NativeCall& call) { return call.Resource<Connection>(call.Int(1), ConnectionType); }
    static Statement& Stmt(NativeCall& call) { return call.Resource<Statement>(call.Int(1), StatementType); }
    std::int32_t Open(NativeCall& call) {
        return Sql(call, [&] {
            const auto name = call.String(1);
            if (name.empty() || name.size() > 64 || !std::all_of(name.begin(), name.end(), [](unsigned char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
            })) return call.Fail("Database name must use 1..64 letters, digits, underscores or hyphens.");
            const auto directory = std::filesystem::path(call.DataPath()) / "sqlite";
            std::filesystem::create_directories(directory);
            if (std::filesystem::is_symlink(directory)) return call.Fail("Database directory must not be a symbolic link.");
            return call.Own(ConnectionType, std::make_unique<Connection>(std::make_shared<Database>(directory / (name + ".sqlite"))));
        });
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
