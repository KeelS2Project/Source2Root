#pragma once
#include "entities.h"

namespace source2root::cstrike {
enum class Statistic { score, mvps };
// CS2 controller fields, resolved by name and exact int32 type for every call.
// Uses the shared entity backend without requiring the SDKTools native module.
class Statistics final {
public:
    explicit Statistics(std::shared_ptr<sdktools::Service> service);
    std::int32_t Get(const KeelPlayerConnection& player, Statistic statistic) const;
    void Set(const KeelPlayerConnection& player, Statistic statistic, std::int32_t value) const;

private:
    std::shared_ptr<sdktools::Service> service_;
};
}
