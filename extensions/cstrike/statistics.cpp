#include "statistics.h"
#include "players.h"

namespace source2root::cstrike {
namespace {
const char* Field(Statistic statistic) {
    switch (statistic) {
        case Statistic::score: return "m_iScore";
        case Statistic::mvps: return "m_iMVPs";
    }

    throw Error("Unknown Counter-Strike statistic.");
}
}

Statistics::Statistics(std::shared_ptr<sdktools::Service> service) : service_(std::move(service)) {
    if (!service_)
        throw Error("Counter-Strike statistics require the entity service.");
}

std::int32_t Statistics::Get(const KeelPlayerConnection& player, Statistic statistic) const {
    try {
        const auto service = service_;
        auto field = service->Resolve("CCSPlayerController", Field(statistic), KEELS2_SCHEMA_INT32);
        auto controller = service->FromPlayer(player, false);
        return controller->Integer(*field);
    } catch (const sdktools::Error& error) {
        throw Error(error.what());
    }
}

void Statistics::Set(const KeelPlayerConnection& player, Statistic statistic, std::int32_t value) const {
    const auto name = Field(statistic);

    if (statistic == Statistic::mvps && value < 0)
        throw Error("MVP count must be nonnegative.");

    try {
        const auto service = service_;
        auto field = service->Resolve("CCSPlayerController", name, KEELS2_SCHEMA_INT32);
        auto controller = service->FromPlayer(player, false);
        controller->SetInteger(*field, value);
        // Network notification may trigger callbacks. The temporary resources
        // retain their service through cleanup; do not re-read after mutation.
    } catch (const sdktools::Error& error) {
        throw Error(error.what());
    }
}
}
