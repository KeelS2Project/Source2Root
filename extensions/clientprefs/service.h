#pragma once

#include "store.h"
#include <source2root/work_queue.hpp>
#include <functional>

namespace source2root::prefs {

struct Identity {
    int slot = -1;
    std::uint64_t connection = 0, account = 0;
    bool operator==(const Identity&) const = default;
};
enum class State { Loading, Ready, Failed };
struct Cookie {
    Definition definition;
    State state = State::Loading;
    std::string error;
};

// Main-thread facade. The adapter supplies a complete, authenticated connection
// snapshot to Sync before dispatching completions or invoking player operations.
// Script unload cancels its callbacks, not accepted persistent writes. Call
// CanStop until true before unloading this service's code; failures remain dirty
// and require RetryWrites after the storage problem has been addressed.
class Service final {
public:
    explicit Service(std::filesystem::path filename);
    explicit Service(StorageFactory storage);
    ~Service();
    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;
    void Pump();
    void Sync(const std::vector<Identity>& players);
    bool Ready() const { Thread(); return catalog_state_ == State::Ready; }
    const std::string& ErrorText() const { Thread(); return catalog_error_; }
    std::shared_ptr<Cookie> Register(const Definition& cookie);
    std::shared_ptr<Cookie> Find(const std::string& name) const;
    std::vector<Definition> UserCookies() const;
    State Status(const Identity& player) const;
    std::string ErrorText(const Identity& player) const;
    Value Get(const Identity& player, const std::shared_ptr<Cookie>& cookie) const;
    void Set(const Identity& player, const std::shared_ptr<Cookie>& cookie, const std::string& text, std::int64_t now);
    void SetIdentity(std::uint64_t id, const std::shared_ptr<Cookie>& cookie, const std::string& text, std::int64_t now);
    bool IdentityPersisted(std::uint64_t id) const;
    std::string IdentityError(std::uint64_t id) const;
    void UserSet(const Identity& player, const std::string& name, const std::string& text, std::int64_t now);
    bool Persisted(const Identity& player) const;
    void RetryLoad(const Identity& player);
    void Refresh(const Identity& player);
    void RetryCatalog();
    void RetryWrites();
    bool CanStop();
    std::size_t Pending() const { Thread(); return queue_.Pending(); }
private:
    struct Account {
        State state = State::Loading;
        Values values, dirty;
        std::uint64_t revision = 0;
        bool loading = false, saving = false, write_failed = false;
        std::string error, write_error;
    };
    struct Task { std::unique_ptr<WorkQueue::Ticket> ticket; bool done = false; };
    const std::thread::id owner_ = std::this_thread::get_id();
    StorageFactory storage_;
    State catalog_state_ = State::Loading;
    std::string catalog_error_;
    std::map<std::string, std::shared_ptr<Cookie>> cookies_;
    std::map<int, Identity> players_;
    std::map<std::uint64_t, std::shared_ptr<Account>> accounts_;
    std::vector<std::shared_ptr<Task>> tasks_;
    WorkQueue queue_{1, 256}; // Declared last: joins workers before captured state is destroyed.
    bool Submit(std::function<void()> work, std::function<void(std::exception_ptr)> completion);
    std::shared_ptr<Account> Current(const Identity& player) const;
    void CheckCookie(const std::shared_ptr<Cookie>& cookie) const;
    void Load(std::uint64_t id, const std::shared_ptr<Account>& account);
    void Save(std::uint64_t id, const std::shared_ptr<Account>& account);
    void Write(std::uint64_t id, const std::shared_ptr<Account>& account, const std::shared_ptr<Cookie>& cookie,
        const std::string& text, std::int64_t now);
    void Evict();
    void Thread() const;
    void LoadCatalog();
};

}
