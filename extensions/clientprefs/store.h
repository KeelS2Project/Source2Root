#pragma once

#include "../sqlite/database.h"
#include <map>
#include <functional>
#include <vector>

namespace source2root::prefs {

using Error = db::Error;
enum class Access { Public, Protected, Private };
struct Definition {
    std::string name, description;
    Access access = Access::Public;
    bool operator==(const Definition&) const = default;
};
struct Value {
    std::string text;
    std::int64_t updated = 0;
    std::uint64_t revision = 0; // Memory-only write sequence, never a database key.
};
using Values = std::map<std::string, Value>;
constexpr std::size_t MaxCookies = 256, MaxClients = 128, MaxAccounts = 256;

void Validate(const Definition& cookie);
void ValidateAccount(std::uint64_t account);
void ValidateValue(const std::string& value);

// A backend belongs to one worker invocation, never the game thread.
class Storage {
public:
    virtual ~Storage() = default;
    virtual std::vector<Definition> Catalog() = 0;
    virtual Definition Register(const Definition& cookie) = 0;
    virtual Values Load(std::uint64_t account) = 0;
    virtual void Save(std::uint64_t account, const Values& values) = 0;
};
using StorageFactory = std::function<std::unique_ptr<Storage>()>;

class Store final : public Storage {
public:
    explicit Store(const std::filesystem::path& filename);
    std::vector<Definition> Catalog() override;
    Definition Register(const Definition& cookie) override;
    Values Load(std::uint64_t account) override;
    void Save(std::uint64_t account, const Values& values) override;
private:
    std::shared_ptr<sqlite::Database> database_;
};

// Reads the clientprefs profile on a worker. No file selects local SQLite.
// The target is fixed after the first valid configuration; credentials may be
// repaired for Retry, but a target change needs a drained extension reload.
StorageFactory ConfiguredStorage(std::filesystem::path data, std::filesystem::path config);

}
