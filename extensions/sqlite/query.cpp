#include "query.h"

#include <cmath>
#include <limits>

namespace source2root::sqlite {

QueryResult Query(const std::filesystem::path& filename, const std::string& sql, const std::atomic_bool& canceled) {
    return Query(filename, db::QueryInput{sql, {}}, canceled);
}

QueryResult
Query(const std::filesystem::path& filename, const db::QueryInput& input, const std::atomic_bool& canceled) {
    input.Validate();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(25);
    const auto check = [&] {
        if (canceled.load(std::memory_order_relaxed))
            throw Error("Database request canceled.");

        if (std::chrono::steady_clock::now() >= deadline)
            throw Error("Database request time limit exceeded.");
    };
    check();
    auto database = std::make_shared<Database>(filename);
    database->Execute("BEGIN");
    database->AllowTransactions(false);
    Statement statement(database, input.sql);

    if (static_cast<std::size_t>(statement.Parameters()) != input.parameters.size())
        throw Error("Query parameter count does not match its bindings.");

    for (std::size_t i = 0; i < input.parameters.size(); ++i) {
        const auto& parameter = *input.parameters[i];
        const auto index = static_cast<int>(i + 1);

        if (const auto* value = std::get_if<std::int32_t>(&parameter))
            statement.BindInt(index, *value);
        else if (const auto* value = std::get_if<double>(&parameter))
            statement.BindFloat(index, *value);
        else if (const auto* value = std::get_if<std::string>(&parameter))
            statement.BindString(index, *value);
        else
            statement.BindNull(index);
    }

    QueryResult result;
    result.columns = statement.Columns();

    if (result.columns > 32)
        throw Error("Database result exceeds 32 columns.");

    std::size_t bytes = 0;
    check();

    while (statement.Step()) {
        check();

        if (result.rows.size() >= 256)
            throw Error("Database result exceeds 256 rows.");

        std::vector<QueryValue> row;
        row.reserve(result.columns);

        for (int column = 0; column < result.columns; ++column) {
            QueryValue value;
            value.null = statement.IsNull(column);

            if (!value.null) {
                value.text = statement.String(column);
                bytes += value.text.size();

                if (bytes > 256 * 1024)
                    throw Error("Database result exceeds 256 KiB.");

                try {
                    value.integer = statement.Int(column);
                } catch (const Error&) {
                }

                const auto number = statement.Float(column);

                if (std::isfinite(number) && std::abs(number) <= std::numeric_limits<float>::max())
                    value.number = static_cast<float>(number);
            }

            row.push_back(std::move(value));
        }

        result.rows.push_back(std::move(row));
    }

    check();
    result.changes = database->Changes();
    result.inserted = std::to_string(database->InsertId());
    database->AllowTransactions(true);
    database->Execute("COMMIT");
    return result;
}
}
