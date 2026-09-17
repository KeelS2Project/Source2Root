#pragma once

#include "database.h"
#include <atomic>
#include <optional>
#include <vector>

namespace source2root::sqlite {

using QueryValue = db::QueryValue;
using QueryResult = db::QueryResult;

// A private connection per job: never races a script's synchronous transaction.
// One statement, all parameters bound. Limits: 256 rows, 32 columns, 256 KiB
// text and 25 ms between SQLite operations (plus SQLite's own busy/progress
// limits). Canceled or failed operations roll back before the connection closes.
QueryResult Query(const std::filesystem::path& filename, const std::string& sql, const std::atomic_bool& canceled);
QueryResult Query(const std::filesystem::path& filename, const db::QueryInput& input, const std::atomic_bool& canceled);

}
