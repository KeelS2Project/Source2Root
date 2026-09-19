#include "driver.h"
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#else
#include <poll.h>
#endif
#include <libpq-fe.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <string_view>

extern "C" void Source2RootPQReleaseTLSMethod(void);

namespace source2root::postgresql {
namespace {
// Extension unload first drains the owned worker queue and destroys every
// connection. This finalizer releases only our statically linked libpq state.
struct TLSLifetime {
    ~TLSLifetime() {
        Source2RootPQReleaseTLSMethod();
    }
} tls_lifetime;
using Clock = std::chrono::steady_clock;
using Connection = std::unique_ptr<PGconn, decltype(&PQfinish)>;
using Result = std::unique_ptr<PGresult, decltype(&PQclear)>;
bool Space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

bool Letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

std::string FirstKeyword(std::string_view sql) {
    std::size_t i = 0;

    for (;;) {
        while (i < sql.size() && Space(sql[i]))
            ++i;

        if (sql.substr(i, 2) == "--") {
            i += 2;

            while (i < sql.size() && sql[i] != '\n' && sql[i] != '\r')
                ++i;
        } else if (sql.substr(i, 2) == "/*") {
            i += 2;
            unsigned depth = 1;

            while (i < sql.size() && depth) {
                if (sql.substr(i, 2) == "/*") {
                    ++depth;
                    i += 2;
                } else if (sql.substr(i, 2) == "*/") {
                    --depth;
                    i += 2;
                } else
                    ++i;
            }

            if (depth)
                throw db::Error("Unterminated SQL comment.");
        } else
            break;
    }

    std::string keyword;

    while (i < sql.size() && Letter(sql[i])) {
        const char c = sql[i++];
        keyword.push_back(c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c);
    }

    if (keyword.empty())
        throw db::Error("PostgreSQL SQL must begin with a command keyword.");

    return keyword;
}

void Option(std::string& output, std::string_view name, std::string_view value) {
    output += name;
    output += "='";

    for (const char c : value) {
        if (c == '\\' || c == '\'')
            output += '\\';

        output += c;
    }

    output += "' ";
}

std::string ConnectionInfo(const db::Settings& settings) {
    // Empty entries in PQconnectStartParams are ignored. A quoted conninfo
    // string preserves explicit empty fields and blocks their PG* fallbacks.
    // PGSERVICE is special: even service='' attempts a service-file lookup.
    if (std::getenv("PGSERVICE"))
        throw db::Error("PGSERVICE must be unset for configured PostgreSQL connections.");

    std::string info;
    Option(info, "host", settings.socket.empty() ? settings.host : settings.socket);
    Option(info, "hostaddr", "");
    Option(info, "port", std::to_string(settings.port));
    Option(info, "dbname", settings.database);
    Option(info, "user", settings.user);
    Option(info, "password", settings.password);
#if defined(_WIN32)
    Option(info, "passfile", "NUL");
#else
    Option(info, "passfile", "/dev/null");
#endif
    Option(info, "client_encoding", "UTF8");
    Option(info, "application_name", "Source2Root");
    Option(info, "connect_timeout", std::to_string(settings.timeout));
    const auto milliseconds = std::to_string(settings.timeout * 1000);
    Option(info, "options", "-c statement_timeout=" + milliseconds + " -c lock_timeout=" + milliseconds);
    Option(info, "sslmode", settings.tls ? "verify-full" : "disable");
    Option(info, "sslrootcert", settings.tls ? (settings.ca.empty() ? "system" : settings.ca) : "");
    Option(info, "sslcertmode", "disable");

    for (const auto name : {"sslcert", "sslkey", "sslpassword", "sslcrl", "sslcrldir", "sslkeylogfile", "requirepeer"})
        Option(info, name, "");

    Option(info, "ssl_min_protocol_version", "TLSv1.2");
    Option(info, "ssl_max_protocol_version", "");
    Option(info, "sslnegotiation", "postgres");
    Option(info, "sslcompression", "0");
    Option(info, "sslsni", "1");
    Option(info, "gssencmode", "disable");
    Option(info, "gssdelegation", "0");
    Option(info, "channel_binding", "prefer");
    Option(info, "require_auth", "scram-sha-256,md5,password,none");
    Option(info, "target_session_attrs", "any");
    Option(info, "load_balance_hosts", "disable");
    Option(info, "min_protocol_version", "3.0");
    Option(info, "max_protocol_version", "latest");
    return info;
}

[[noreturn]] void StatementError(const PGresult* result) {
    // Server messages can echo SQL, parameters and credentials. Retain only
    // a validated SQLSTATE, never PQerrorMessage/PQresultErrorMessage.
    const char* state = PQresultErrorField(result, PG_DIAG_SQLSTATE);

    if (state && std::strlen(state) == 5 && std::all_of(state, state + 5, [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z'); }))
        throw db::Error("PostgreSQL statement failed (SQLSTATE " + std::string(state) + ").");

    throw db::Error("PostgreSQL statement failed.");
}

db::QueryValue Value(const PGresult* result, int column) {
    db::QueryValue value;
    value.null = PQgetisnull(result, 0, column) != 0;

    if (value.null)
        return value;

    const auto length = PQgetlength(result, 0, column);
    const auto* bytes = PQgetvalue(result, 0, column);

    if (length < 0 || length > 4095 || std::memchr(bytes, 0, length))
        throw db::Error("Value cannot fit a SourcePawn string.");

    value.text.assign(bytes, length);
    const auto* end = bytes + length;
    std::int32_t integer;
    const auto parsed = std::from_chars(bytes, end, integer);

    if (parsed.ec == std::errc{} && parsed.ptr == end)
        value.integer = integer;

    double number;
    const auto decimal = std::from_chars(bytes, end, number);

    if (decimal.ec == std::errc{} && decimal.ptr == end && std::isfinite(number) &&
        std::abs(number) <= std::numeric_limits<float>::max())
        value.number = static_cast<float>(number);
    // PostgreSQL boolean OID; preserve textual t/f as returned by libpq.
    if (PQftype(result, column) == 16 && (value.text == "t" || value.text == "f")) {
        value.integer = value.text == "t" ? 1 : 0;
        value.number = static_cast<float>(*value.integer);
    }

    return value;
}

class Session {
    const std::atomic_bool& canceled_;
    const Clock::time_point deadline_;
    Connection connection_{nullptr, PQfinish};
    void Check(Clock::time_point limit = Clock::time_point::max()) const {
        if (canceled_.load(std::memory_order_relaxed))
            throw db::Error("Database request canceled.");

        if (Clock::now() >= std::min(deadline_, limit))
            throw db::Error("Database request time limit exceeded.");
    }

    short Wait(short events, Clock::time_point limit = Clock::time_point::max()) {
        for (;;) {
            Check(limit);
            const auto socket = PQsocket(connection_.get());

            if (socket < 0)
                throw db::Error("PostgreSQL connection is unavailable.");
#if defined(_WIN32)
            WSAPOLLFD descriptor{static_cast<SOCKET>(socket), events, 0};
            const auto result = WSAPoll(&descriptor, 1, 50);

            if (result == SOCKET_ERROR && WSAGetLastError() == WSAEINTR)
                continue;
#else
            pollfd descriptor{socket, events, 0};
            const auto result = poll(&descriptor, 1, 50);

            if (result < 0 && errno == EINTR)
                continue;
#endif
            if (result < 0)
                throw db::Error("PostgreSQL socket polling failed.");

            if (result) {
                Check(limit);

                if (descriptor.revents & (POLLNVAL | POLLERR))
                    throw db::Error("PostgreSQL connection failed.");
                // HUP can accompany the final readable response. Let libpq
                // consume that response or report the disconnection.
                return descriptor.revents;
            }
        }
    }

    void Consume() {
        if (!PQconsumeInput(connection_.get()))
            throw db::Error("PostgreSQL connection read failed.");
    }

    db::QueryResult Execute(const db::QueryInput& input) {
        Check();
        std::vector<std::string> text(input.parameters.size());
        std::vector<const char*> values(input.parameters.size(), nullptr);

        for (std::size_t i = 0; i < input.parameters.size(); ++i) {
            const auto& parameter = *input.parameters[i];
            std::array<char, 128> buffer{};

            if (const auto* integer = std::get_if<std::int32_t>(&parameter)) {
                const auto converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), *integer);

                if (converted.ec != std::errc{})
                    throw db::Error("Parameter conversion failed.");

                text[i].assign(buffer.data(), converted.ptr);
            } else if (const auto* number = std::get_if<double>(&parameter)) {
                const auto converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), *number,
                    std::chars_format::general, std::numeric_limits<double>::max_digits10);

                if (converted.ec != std::errc{})
                    throw db::Error("Parameter conversion failed.");

                text[i].assign(buffer.data(), converted.ptr);
            } else if (const auto* string = std::get_if<std::string>(&parameter))
                text[i] = *string;
            else
                continue;

            values[i] = text[i].c_str();
        }

        // Extended protocol rejects multiple SQL commands even with no parameters.
        if (!PQsendQueryParams(connection_.get(), input.sql.c_str(), static_cast<int>(values.size()), nullptr,
                values.data(), nullptr, nullptr, 0) || !PQsetSingleRowMode(connection_.get()))
            throw db::Error("PostgreSQL query submission failed.");

        for (;;) {
            Check();
            const auto pending = PQflush(connection_.get());

            if (pending < 0)
                throw db::Error("PostgreSQL connection write failed.");

            if (!pending)
                break;

            if (Wait(POLLIN | POLLOUT) & (POLLIN | POLLHUP))
                Consume();
        }

        db::QueryResult output;
        std::size_t bytes = 0;
        bool complete = false;

        for (;;) {
            Check();

            while (PQisBusy(connection_.get())) {
                Wait(POLLIN);
                Consume();
                Check();
            }

            Result result(PQgetResult(connection_.get()), PQclear);

            if (!result)
                break;

            const auto status = PQresultStatus(result.get());

            if (status != PGRES_SINGLE_TUPLE && status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK)
                StatementError(result.get());

            if (complete)
                throw db::Error("Multiple result sets are not supported.");

            const auto columns = PQnfields(result.get());

            if (columns > 32)
                throw db::Error("Database result exceeds 32 columns.");

            output.columns = columns;

            if (status == PGRES_SINGLE_TUPLE) {
                if (PQntuples(result.get()) != 1)
                    throw db::Error("Invalid PostgreSQL single-row result.");

                if (output.rows.size() >= 256)
                    throw db::Error("Database result exceeds 256 rows.");

                std::vector<db::QueryValue> row;
                row.reserve(columns);

                for (int column = 0; column < columns; ++column) {
                    auto value = Value(result.get(), column);
                    bytes += value.text.size();

                    if (bytes > 256 * 1024)
                        throw db::Error("Database result exceeds 256 KiB.");

                    row.push_back(std::move(value));
                }

                output.rows.push_back(std::move(row));
            } else {
                if (PQntuples(result.get()))
                    throw db::Error("PostgreSQL result was not streamed.");

                complete = true;
                const auto* count = PQcmdTuples(result.get());

                if (*count) {
                    const auto* end = count + std::strlen(count);
                    const auto parsed = std::from_chars(count, end, output.changes);

                    if (parsed.ec != std::errc{} || parsed.ptr != end || output.changes < 0)
                        throw db::Error("Affected row count exceeds SourcePawn cell range.");
                }
            }
        }

        if (!complete || PQstatus(connection_.get()) != CONNECTION_OK)
            throw db::Error("PostgreSQL query did not complete.");

        return output;
    }

public:
    Session(const db::Settings& settings, const std::atomic_bool& canceled)
        : canceled_(canceled), deadline_(Clock::now() + std::chrono::seconds(settings.timeout * 3)) {
        db::ValidatePostgreSQLSettings(settings);
        Check();
        const auto connect_deadline = Clock::now() + std::chrono::seconds(settings.timeout);
        const auto info = ConnectionInfo(settings);
        connection_.reset(PQconnectStart(info.c_str()));

        if (!connection_)
            throw db::Error("PostgreSQL connection allocation failed.");

        PQsetNoticeProcessor(
            connection_.get(),
            [](void*, const char*) {
            },
            nullptr);

        auto connect = PGRES_POLLING_WRITING;

        while (connect != PGRES_POLLING_OK) {
            if (connect == PGRES_POLLING_FAILED)
                throw db::Error("PostgreSQL connection failed.");
            // libpq's asynchronous connection API ignores connect_timeout;
            // enforce it here as well as the total query deadline.
            if (connect != PGRES_POLLING_ACTIVE)
                Wait(connect == PGRES_POLLING_READING ? POLLIN : POLLOUT, connect_deadline);

            Check(connect_deadline);
            connect = PQconnectPoll(connection_.get());
        }

        Check(connect_deadline);

        if (PQsetnonblocking(connection_.get(), 1))
            throw db::Error("PostgreSQL nonblocking mode is unavailable.");

        if (settings.tls && !PQsslInUse(connection_.get()))
            throw db::Error("PostgreSQL connection did not negotiate TLS.");
    }

    db::QueryResult Query(const db::QueryInput& input) {
        Execute({"BEGIN", {}});
        auto result = Execute(input);

        if (PQtransactionStatus(connection_.get()) != PQTRANS_INTRANS)
            throw db::Error("PostgreSQL query changed its enclosing transaction.");

        Check();

        try {
            Execute({"COMMIT", {}});
        } catch (...) {
            throw db::Error("PostgreSQL commit failed; outcome may be unknown.");
        }

        return result;
    }
};
}

void ValidateQuery(const db::QueryInput& input) {
    input.Validate();
    const auto keyword = FirstKeyword(input.sql);

    for (const auto* prohibited : {"BEGIN", "START", "COMMIT", "END", "ROLLBACK", "ABORT", "SAVEPOINT", "RELEASE",
            "PREPARE", "DEALLOCATE", "SET", "RESET", "DISCARD", "COPY"})
        if (keyword == prohibited)
            throw db::Error("PostgreSQL transaction, session-control and COPY commands are unavailable.");
}

db::QueryResult Query(const db::Settings& settings, const db::QueryInput& input, const std::atomic_bool& canceled) {
    ValidateQuery(input);
    Session session(settings, canceled);
    return session.Query(input);
}
}
