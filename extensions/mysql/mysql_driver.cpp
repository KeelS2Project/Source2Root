#include "mysql_driver.h"
#include <mysql.h>

#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>

namespace source2root::mysql {
namespace {
std::mutex runtime_mutex;
unsigned runtime_users = 0;
class Runtime {
public:
    Runtime() {
        std::lock_guard lock(runtime_mutex);
        if (!runtime_users && mysql_library_init(0, nullptr, nullptr)) throw db::Error("Database client initialization failed.");
        ++runtime_users;
    }
    ~Runtime() {
        std::lock_guard lock(runtime_mutex);
        if (!--runtime_users) mysql_library_end();
    }
};
using Connection = std::unique_ptr<MYSQL, decltype(&mysql_close)>;
using Statement = std::unique_ptr<MYSQL_STMT, decltype(&mysql_stmt_close)>;
void Option(MYSQL* connection, mysql_option option, const void* value) {
    if (mysql_optionsv(connection, option, value)) throw db::Error("Database connection option is unavailable.");
}
void StatementError(MYSQL_STMT* statement) {
    throw db::Error("Database statement failed (" + std::to_string(mysql_stmt_errno(statement)) + "): " +
        std::string(mysql_stmt_error(statement)).substr(0, 2048));
}
db::QueryValue Value(const char* buffer, unsigned long length, bool null) {
    db::QueryValue value;
    value.null = null;
    if (null) return value;
    if (length > 4095 || std::memchr(buffer, 0, length)) throw db::Error("Value cannot fit a SourcePawn string.");
    value.text.assign(buffer, length);
    const auto* begin = value.text.data();
    const auto* end = begin + value.text.size();
    std::int32_t integer;
    const auto parsed = std::from_chars(begin, end, integer);
    if (parsed.ec == std::errc{} && parsed.ptr == end) value.integer = integer;
    double number;
    const auto decimal = std::from_chars(begin, end, number);
    if (decimal.ec == std::errc{} && decimal.ptr == end && std::isfinite(number) && std::abs(number) <= std::numeric_limits<float>::max())
        value.number = static_cast<float>(number);
    return value;
}
}

db::QueryResult Query(const db::Settings& settings, const db::QueryInput& input, const std::atomic_bool& canceled) {
    input.Validate();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(settings.timeout * 3);
    const auto check = [&] {
        if (canceled.load(std::memory_order_relaxed)) throw db::Error("Database request canceled.");
        if (std::chrono::steady_clock::now() >= deadline) throw db::Error("Database request time limit exceeded.");
    };
    check();
    Runtime runtime;
    // Close the private connection before freeing its statement. Otherwise an
    // aborted streaming result may be drained by mysql_stmt_close indefinitely.
    Statement statement(nullptr, mysql_stmt_close);
    Connection connection(mysql_init(nullptr), mysql_close);
    if (!connection) throw db::Error("Database connection allocation failed.");
    unsigned disabled = 0;
    const std::size_t packet_limit = 1024 * 1024;
    my_bool verify = settings.tls ? 1 : 0, reconnect = 0;
    const auto protocol = settings.socket.empty() ? MYSQL_PROTOCOL_TCP : MYSQL_PROTOCOL_SOCKET;
    Option(connection.get(), MYSQL_OPT_PROTOCOL, &protocol);
    Option(connection.get(), MYSQL_OPT_CONNECT_TIMEOUT, &settings.timeout);
    Option(connection.get(), MYSQL_OPT_READ_TIMEOUT, &settings.timeout);
    Option(connection.get(), MYSQL_OPT_WRITE_TIMEOUT, &settings.timeout);
    Option(connection.get(), MYSQL_OPT_LOCAL_INFILE, &disabled);
    Option(connection.get(), MYSQL_OPT_MAX_ALLOWED_PACKET, &packet_limit);
    Option(connection.get(), MYSQL_OPT_RECONNECT, &reconnect);
    Option(connection.get(), MYSQL_SET_CHARSET_NAME, "utf8mb4");
    Option(connection.get(), MYSQL_OPT_SSL_ENFORCE, &verify);
    Option(connection.get(), MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &verify);
    Option(connection.get(), MARIADB_OPT_RESTRICTED_AUTH, "mysql_native_password,caching_sha2_password,sha256_password,client_ed25519");
    if (settings.tls) {
        Option(connection.get(), MARIADB_OPT_TLS_VERSION, "TLSv1.2,TLSv1.3");
        if (!settings.ca.empty()) Option(connection.get(), MYSQL_OPT_SSL_CA, settings.ca.c_str());
    }
    if (!mysql_real_connect(connection.get(), settings.host.c_str(), settings.user.c_str(), settings.password.c_str(),
        settings.database.c_str(), settings.port, settings.socket.empty() ? nullptr : settings.socket.c_str(), 0))
        throw db::Error("Database connection failed (" + std::to_string(mysql_errno(connection.get())) + ").");
    if (settings.tls && !mysql_get_ssl_cipher(connection.get())) throw db::Error("Database connection did not negotiate TLS.");
    check();
    if (mysql_autocommit(connection.get(), 0)) throw db::Error("Database transaction setup failed.");
    statement.reset(mysql_stmt_init(connection.get()));
    if (!statement) throw db::Error("Database statement allocation failed.");
    if (mysql_stmt_prepare(statement.get(), input.sql.c_str(), input.sql.size())) StatementError(statement.get());
    if (mysql_stmt_param_count(statement.get()) != input.parameters.size()) throw db::Error("Query parameter count does not match its bindings.");
    std::vector<MYSQL_BIND> bindings(input.parameters.size());
    std::vector<unsigned long> lengths(input.parameters.size());
    for (std::size_t i = 0; i < input.parameters.size(); ++i) {
        const auto& parameter = *input.parameters[i];
        auto& bind = bindings[i];
        if (const auto* value = std::get_if<std::int32_t>(&parameter)) {
            bind.buffer_type = MYSQL_TYPE_LONG; bind.buffer = const_cast<std::int32_t*>(value);
        } else if (const auto* value = std::get_if<double>(&parameter)) {
            bind.buffer_type = MYSQL_TYPE_DOUBLE; bind.buffer = const_cast<double*>(value);
        } else if (const auto* value = std::get_if<std::string>(&parameter)) {
            bind.buffer_type = MYSQL_TYPE_STRING; bind.buffer = const_cast<char*>(value->data());
            lengths[i] = value->size(); bind.length = &lengths[i]; bind.buffer_length = lengths[i];
        } else bind.buffer_type = MYSQL_TYPE_NULL;
    }
    if (!bindings.empty() && mysql_stmt_bind_param(statement.get(), bindings.data())) StatementError(statement.get());
    check();
    if (mysql_stmt_execute(statement.get())) StatementError(statement.get());
    check();
    db::QueryResult result;
    const auto columns = mysql_stmt_field_count(statement.get());
    if (columns > 32) throw db::Error("Database result exceeds 32 columns.");
    result.columns = static_cast<int>(columns);
    if (columns) {
        std::vector<MYSQL_BIND> outputs(columns);
        std::vector<std::array<char, 4096>> buffers(columns);
        std::vector<unsigned long> sizes(columns);
        std::vector<my_bool> nulls(columns), errors(columns);
        for (unsigned i = 0; i < columns; ++i) {
            auto& output = outputs[i];
            output.buffer_type = MYSQL_TYPE_STRING;
            output.buffer = buffers[i].data(); output.buffer_length = buffers[i].size();
            output.length = &sizes[i]; output.is_null = &nulls[i]; output.error = &errors[i];
        }
        if (mysql_stmt_bind_result(statement.get(), outputs.data())) StatementError(statement.get());
        std::size_t bytes = 0;
        for (;;) {
            const auto fetched = mysql_stmt_fetch(statement.get());
            check();
            if (fetched == MYSQL_NO_DATA) break;
            if (fetched == MYSQL_DATA_TRUNCATED) throw db::Error("Value cannot fit a SourcePawn string.");
            if (fetched) StatementError(statement.get());
            if (result.rows.size() >= 256) throw db::Error("Database result exceeds 256 rows.");
            std::vector<db::QueryValue> row;
            row.reserve(columns);
            for (unsigned i = 0; i < columns; ++i) {
                auto value = Value(buffers[i].data(), sizes[i], nulls[i]);
                bytes += value.text.size();
                if (bytes > 256 * 1024) throw db::Error("Database result exceeds 256 KiB.");
                row.push_back(std::move(value));
            }
            result.rows.push_back(std::move(row));
        }
    }
    const auto affected = mysql_stmt_affected_rows(statement.get());
    if (affected != static_cast<my_ulonglong>(-1)) {
        if (affected > static_cast<my_ulonglong>(std::numeric_limits<int>::max())) throw db::Error("Affected row count exceeds SourcePawn cell range.");
        result.changes = static_cast<int>(affected);
    }
    result.inserted = std::to_string(mysql_stmt_insert_id(statement.get()));
    if (mysql_stmt_next_result(statement.get()) != -1) throw db::Error("Multiple result sets are not supported.");
    check();
    if (mysql_commit(connection.get())) throw db::Error("Database commit failed; outcome may be unknown.");
    return result;
}

}
