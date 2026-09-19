#pragma once

#include "../database/settings.h"
#include <atomic>
#include <memory>

#if defined(_WIN32)
#if defined(SR_MYSQL_BUILD)
#define SR_MYSQL_API __declspec(dllexport)
#else
#define SR_MYSQL_API __declspec(dllimport)
#endif
#else
#define SR_MYSQL_API __attribute__((visibility("default")))
#endif

namespace source2root::mysql {

// Worker-owned private connection. Commands share a transaction until Commit;
// destruction rolls back pending transactional writes. SQL/server implicit
// commits still apply. Any execution/commit failure closes and poisons it.
// Finite I/O timeouts, 512 commands and aggregate input/result bounds apply.
class SR_MYSQL_API Session final {
public:
    Session(const db::Settings& settings, const std::atomic_bool& canceled);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    db::QueryResult Execute(const db::QueryInput& input);
    void Commit();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

SR_MYSQL_API db::QueryResult
Query(const db::Settings& settings, const db::QueryInput& input, const std::atomic_bool& canceled);
}
