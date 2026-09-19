#pragma once
#include "store.h"
#include "../mysql/mysql_driver.h"

namespace source2root::prefs {
class MysqlStore final : public Storage {
public:
    explicit MysqlStore(db::Settings settings) : settings_(std::move(settings)) {}

    std::vector<Definition> Catalog() override;
    Definition Register(const Definition& cookie) override;
    Values Load(std::uint64_t account) override;
    void Save(std::uint64_t account, const Values& values) override;

private:
    db::Settings settings_;
    const std::atomic_bool canceled_{false};
};
}
