#pragma once

#include "database.h"
#include <atomic>
#include <optional>
#include <vector>

namespace source2root::sqlite {

struct QueryValue {
    bool null = false;
    std::string text;
    std::optional<std::int32_t> integer;
    std::optional<float> number;
};
struct QueryResult {
    int columns = 0, changes = 0;
    std::int64_t inserted = 0;
    std::vector<std::vector<QueryValue>> rows;
};

// A private connection per job: never races a script's synchronous transaction.
// One statement, no unbound parameters. Limits: 256 rows, 32 columns, 256 KiB
// text and 25 ms between SQLite operations (plus SQLite's own busy/progress
// limits). Canceled or failed operations roll back before the connection closes.
QueryResult Query(const std::filesystem::path& filename, const std::string& sql, const std::atomic_bool& canceled);

}
