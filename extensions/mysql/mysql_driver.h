#pragma once

#include "../database/settings.h"
#include <atomic>

namespace source2root::mysql {

db::QueryResult Query(const db::Settings& settings, const db::QueryInput& input, const std::atomic_bool& canceled);

}
