#pragma once
#include "../database/settings.h"
#include <atomic>

namespace source2root::postgresql {
// SQL uses PostgreSQL $1..$64 placeholders. No transaction-control/COPY commands.
// One private connection and explicit transaction per query; commit follows
// successful result construction. Closing on failure abandons the transaction.
// Retained output <=256rows,32columns,256KiB,4095bytes/value. libpq may buffer a
// larger individual wire row before those application-level limits can be checked.
// INSERT ids must be requested with RETURNING; QueryResult.inserted remains"0".
void ValidateQuery(const db::QueryInput& input);
db::QueryResult Query(const db::Settings& settings, const db::QueryInput& input, const std::atomic_bool& canceled);
}
