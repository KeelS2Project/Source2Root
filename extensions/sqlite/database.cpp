#include "database.h"
#include <sqlite3.h>

#include <cmath>
#include <cstring>
#include <limits>

namespace source2root::sqlite {

Database::Database(const std::filesystem::path& filename) {
    const auto utf8 = filename.u8string();
    const auto result = sqlite3_open_v2(reinterpret_cast<const char*>(utf8.c_str()), &database_,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX | SQLITE_OPEN_NOFOLLOW, nullptr);
    if (result != SQLITE_OK) {
        const std::string error = database_ ? sqlite3_errmsg(database_) : "Could not allocate SQLite connection.";
        if (database_) sqlite3_close(database_);
        database_ = nullptr;
        throw Error(error);
    }
    sqlite3_extended_result_codes(database_, 1);
    sqlite3_busy_timeout(database_, 20);
    sqlite3_limit(database_, SQLITE_LIMIT_LENGTH, 1024 * 1024);
    sqlite3_limit(database_, SQLITE_LIMIT_SQL_LENGTH, 4095);
    sqlite3_limit(database_, SQLITE_LIMIT_ATTACHED, 0);
    sqlite3_limit(database_, SQLITE_LIMIT_COLUMN, 128);
    sqlite3_db_config(database_, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr);
    sqlite3_db_config(database_, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, nullptr);
    sqlite3_set_authorizer(database_, [](void*, int action, const char* first, const char*, const char*, const char*) {
        if (action == SQLITE_ATTACH || action == SQLITE_DETACH) return SQLITE_DENY;
        if (action == SQLITE_PRAGMA && (!first || (std::strcmp(first, "table_info") && std::strcmp(first, "index_list") &&
            std::strcmp(first, "foreign_key_list")))) return SQLITE_DENY;
        return SQLITE_OK;
    }, nullptr);
    sqlite3_progress_handler(database_, 1000, [](void* raw) {
        return std::chrono::steady_clock::now() > static_cast<Database*>(raw)->deadline_ ? 1 : 0;
    }, this);
}

Database::~Database() { if (database_) sqlite3_close_v2(database_); }
void Database::Budget() { deadline_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(25); }
void Database::Check(int result) const { if (result != SQLITE_OK) throw Error(sqlite3_errmsg(database_)); }
bool Database::InTransaction() const { return sqlite3_get_autocommit(database_) == 0; }
std::int64_t Database::InsertId() const { return sqlite3_last_insert_rowid(database_); }
int Database::Changes() const { return sqlite3_changes(database_); }

void Database::Execute(const std::string& sql) {
    Budget();
    sqlite3_stmt* statement = nullptr;
    Check(sqlite3_prepare_v2(database_, sql.c_str(), -1, &statement, nullptr));
    if (!statement) throw Error("Empty SQL statement.");
    const auto result = sqlite3_step(statement);
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE) throw Error(sqlite3_errmsg(database_));
}

Statement::Statement(std::shared_ptr<Database> database, const std::string& sql) : database_(std::move(database)) {
    if (!database_ || sql.empty() || sql.size() > 4095) throw Error("SQL must contain one statement of at most 4095 bytes.");
    database_->Budget();
    const char* tail = nullptr;
    database_->Check(sqlite3_prepare_v3(database_->database_, sql.c_str(), static_cast<int>(sql.size() + 1),
        SQLITE_PREPARE_PERSISTENT, &statement_, &tail));
    if (!statement_) throw Error("Empty SQL statement.");
    try {
        while (tail && *tail) {
            sqlite3_stmt* extra = nullptr;
            const char* next = nullptr;
            const auto status = sqlite3_prepare_v3(database_->database_, tail, -1, 0, &extra, &next);
            const bool multiple = extra != nullptr;
            if (extra) sqlite3_finalize(extra);
            database_->Check(status);
            if (multiple) throw Error("Prepare accepts exactly one SQL statement.");
            if (!next || next == tail) break;
            tail = next;
        }
    } catch (...) { sqlite3_finalize(statement_); statement_ = nullptr; throw; }
}

Statement::~Statement() { if (statement_) sqlite3_finalize(statement_); }
void Statement::Parameter(int index) const {
    if (started_ || index < 1 || index > sqlite3_bind_parameter_count(statement_)) throw Error("Invalid parameter or statement needs resetting.");
}
void Statement::BindInt(int index, std::int32_t value) { Parameter(index); database_->Check(sqlite3_bind_int(statement_, index, value)); }
void Statement::BindFloat(int index, double value) {
    Parameter(index);
    if (!std::isfinite(value)) throw Error("Database values must be finite.");
    database_->Check(sqlite3_bind_double(statement_, index, value));
}
void Statement::BindString(int index, const std::string& value) {
    Parameter(index);
    database_->Check(sqlite3_bind_text(statement_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT));
}
void Statement::BindNull(int index) { Parameter(index); database_->Check(sqlite3_bind_null(statement_, index)); }
bool Statement::Step() {
    if (done_) throw Error("Statement is complete; reset it before reuse.");
    started_ = true;
    row_ = false;
    database_->Budget();
    const auto result = sqlite3_step(statement_);
    if (result == SQLITE_ROW) return row_ = true;
    done_ = true;
    if (result != SQLITE_DONE) throw Error(sqlite3_errmsg(database_->database_));
    return false;
}
void Statement::Reset() {
    sqlite3_reset(statement_);
    database_->Check(sqlite3_clear_bindings(statement_));
    row_ = done_ = started_ = false;
}
int Statement::Columns() const { return sqlite3_column_count(statement_); }
void Statement::Column(int index) const { if (!row_ || index < 0 || index >= Columns()) throw Error("No current row or invalid column."); }
bool Statement::IsNull(int column) const { Column(column); return sqlite3_column_type(statement_, column) == SQLITE_NULL; }
std::int32_t Statement::Int(int column) const {
    Column(column);
    const auto value = sqlite3_column_int64(statement_, column);
    if (value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max())
        throw Error("Integer exceeds SourcePawn cell range; read it as a string.");
    return static_cast<std::int32_t>(value);
}
double Statement::Float(int column) const { Column(column); return sqlite3_column_double(statement_, column); }
std::string Statement::String(int column) const {
    Column(column);
    const auto* text = sqlite3_column_text(statement_, column);
    if (!text) return {};
    const auto size = sqlite3_column_bytes(statement_, column);
    if (size > 4095 || std::memchr(text, 0, size)) throw Error("Value cannot fit a SourcePawn string.");
    return {reinterpret_cast<const char*>(text), static_cast<std::size_t>(size)};
}

}
