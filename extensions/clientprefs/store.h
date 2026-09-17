#pragma once

#include "../sqlite/database.h"
#include <map>
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

// Every Store belongs to one worker invocation. The service never shares a
// SQLite connection with the game thread or another job.
class Store final {
public:
    explicit Store(const std::filesystem::path& filename);
    std::vector<Definition> Catalog();
    Definition Register(const Definition& cookie);
    Values Load(std::uint64_t account);
    void Save(std::uint64_t account, const Values& values);
private:
    std::shared_ptr<sqlite::Database> database_;
};

}
