#include "service.h"

#include <limits>
#include <set>

namespace source2root::prefs {
namespace {
std::string Message(std::exception_ptr error) {
    try {
        if (error)
            std::rethrow_exception(error);
    } catch (const std::exception& failure) {
        return std::string(failure.what()).substr(0, 4095);
    } catch (...) {
        return "Client preferences worker failed.";
    }

    return {};
}
}

Service::Service(std::filesystem::path filename)
    : Service([filename = std::move(filename)] {
          return std::make_unique<Store>(filename);
      }) {
}

Service::Service(StorageFactory storage) : storage_(std::move(storage)) {
    if (!storage_)
        throw Error("Missing client preferences storage factory.");

    LoadCatalog();
}

void Service::LoadCatalog() {
    auto catalog = std::make_shared<std::vector<Definition>>();
    const auto storage = storage_;

    if (!Submit(
            [storage, catalog] {
                *catalog = storage()->Catalog();
            },
            [this, catalog](auto failure) {
                catalog_error_ = Message(failure);
                catalog_state_ = failure ? State::Failed : State::Ready;

                if (!failure)
                    for (const auto& definition : *catalog) {
                        if (const auto found = cookies_.find(definition.name); found != cookies_.end()) {
                            if (found->second->definition != definition) {
                                // Keep the conflicting script's handle failed while making
                                // the persisted definition available through Find/Register.
                                found->second->state = State::Failed;
                                found->second->error =
                                    "Cookie already exists with different description or access mode.";

                                found->second = std::make_shared<Cookie>(Cookie{definition, State::Ready, {}});
                            }

                            continue;
                        }

                        if (cookies_.size() == MaxCookies) {
                            catalog_state_ = State::Failed;
                            catalog_error_ = "Client preferences catalog limit (256) reached.";
                            break;
                        }

                        cookies_.emplace(definition.name,
                                         std::make_shared<Cookie>(Cookie{definition, State::Ready, {}}));
                    }
            })) {
        catalog_state_ = State::Failed;
        catalog_error_ = "Client preferences worker queue is full.";
    }
}

void Service::RetryCatalog() {
    Thread();

    if (catalog_state_ != State::Failed)
        return;

    catalog_state_ = State::Loading;
    catalog_error_.clear();
    LoadCatalog();
}

Service::~Service() = default;
void Service::Thread() const {
    if (std::this_thread::get_id() != owner_)
        throw std::logic_error("Client preferences require the owning thread.");
}

bool Service::Submit(std::function<void()> work, std::function<void(std::exception_ptr)> completion) {
    auto task = std::make_shared<Task>();
    // Reserve owner storage before publishing any work to the worker thread.
    tasks_.push_back(task);

    try {
        task->ticket = queue_.Submit(
            [work = std::move(work)](const auto&) {
                work();
            },
            [task, completion = std::move(completion)](auto error) {
                task->done = true;
                completion(error);
                return true;
            });
    } catch (...) {
        tasks_.pop_back();
        throw;
    }

    if (task->ticket)
        return true;

    tasks_.pop_back();
    return false;
}

std::shared_ptr<Cookie> Service::Register(const Definition& definition) {
    Thread();
    Validate(definition);

    if (const auto old = Find(definition.name)) {
        if (old->definition != definition)
            throw Error("Cookie already exists with different description or access mode.");

        if (old->state != State::Failed)
            return old;
    } else if (cookies_.size() == MaxCookies)
        throw Error("Client preferences catalog limit (256) reached.");

    auto cookie = Find(definition.name);

    if (!cookie)
        cookie = std::make_shared<Cookie>(Cookie{definition, State::Loading, {}});

    const auto storage = storage_;

    if (!Submit(
            [storage, definition] {
                storage()->Register(definition);
            },
            [this, cookie](auto error) {
                cookie->error = Message(error);
                cookie->state = error ? State::Failed : State::Ready;

                if (error && Find(cookie->definition.name) == cookie)
                    cookies_.erase(cookie->definition.name);
            }))
        throw Error("Client preferences worker queue is full.");

    cookie->state = State::Loading;
    cookie->error.clear();
    cookies_[definition.name] = cookie;
    return cookie;
}

std::shared_ptr<Cookie> Service::Find(const std::string& name) const {
    Thread();
    const auto found = cookies_.find(name);
    return found == cookies_.end() ? nullptr : found->second;
}

std::vector<Definition> Service::UserCookies() const {
    Thread();
    std::vector<Definition> result;

    for (const auto& [name, cookie] : cookies_)
        if (cookie->state == State::Ready && cookie->definition.access != Access::Private)
            result.push_back(cookie->definition);

    return result;
}

void Service::Sync(const std::vector<Identity>& players) {
    Thread();

    if (players.size() > MaxClients)
        throw Error("Client preferences connection limit (128) reached.");

    std::map<int, Identity> next;
    std::set<std::uint64_t> missing;

    for (const auto& player : players) {
        ValidateAccount(player.account);

        if (player.slot < 0 || player.slot >= static_cast<int>(MaxClients) || !player.connection ||
            !next.emplace(player.slot, player).second)
            throw Error("Invalid or duplicate client preferences connection.");

        if (!accounts_.contains(player.account))
            missing.insert(player.account);
    }

    // Do not discard existing sessions when an invalid snapshot is supplied.
    if (accounts_.size() + missing.size() > MaxAccounts)
        throw Error("Client preferences account cache limit (256) reached.");

    players_ = std::move(next);

    for (auto id : missing) {
        auto account = std::make_shared<Account>();
        accounts_.emplace(id, account);
        Load(id, account);
    }

    Evict();
}

std::shared_ptr<Service::Account> Service::Current(const Identity& player) const {
    Thread();
    const auto found = players_.find(player.slot);

    if (found == players_.end() || found->second != player)
        throw Error("Player connection is no longer available.");

    return accounts_.at(player.account);
}

void Service::CheckCookie(const std::shared_ptr<Cookie>& cookie) const {
    Thread();

    if (!cookie || Find(cookie->definition.name) != cookie)
        throw Error("Foreign or invalid cookie.");

    if (cookie->state != State::Ready)
        throw Error(cookie->error.empty() ? "Cookie registration is not complete." : cookie->error);
}

State Service::Status(const Identity& player) const {
    return Current(player)->state;
}

std::string Service::ErrorText(const Identity& player) const {
    return Current(player)->error;
}

Value Service::Get(const Identity& player, const std::shared_ptr<Cookie>& cookie) const {
    CheckCookie(cookie);
    const auto account = Current(player);

    if (account->state != State::Ready)
        throw Error("Client preferences are not cached.");

    const auto found = account->values.find(cookie->definition.name);
    return found == account->values.end() ? Value{} : found->second;
}

void Service::Set(const Identity& player,
                  const std::shared_ptr<Cookie>& cookie,
                  const std::string& text,
                  std::int64_t now) {
    CheckCookie(cookie);
    ValidateValue(text);
    const auto account = Current(player);

    if (account->state != State::Ready)
        throw Error("Client preferences are not cached.");

    Write(player.account, account, cookie, text, now);
}

void Service::SetIdentity(std::uint64_t id,
                          const std::shared_ptr<Cookie>& cookie,
                          const std::string& text,
                          std::int64_t now) {
    CheckCookie(cookie);
    ValidateAccount(id);
    ValidateValue(text);

    if (now < 0)
        throw Error("Invalid preference timestamp.");

    auto found = accounts_.find(id);

    if (found == accounts_.end()) {
        Evict();

        if (accounts_.size() == MaxAccounts)
            throw Error("Client preferences account cache limit (256) reached.");

        found = accounts_.emplace(id, std::make_shared<Account>()).first;
        // Queue the snapshot first. Its completion merges the accepted dirty
        // overlay, so a late load cannot erase an offline write or a newer edit.
        Load(id, found->second);
    }

    Write(id, found->second, cookie, text, now);
}

void Service::Write(std::uint64_t id, const std::shared_ptr<Account>& account, const std::shared_ptr<Cookie>& cookie,
    const std::string& text, std::int64_t now) {
    if (now < 0 || account->revision == std::numeric_limits<std::uint64_t>::max())
        throw Error("Invalid preference timestamp or write sequence exhausted.");

    const Value value{text, now, ++account->revision};
    account->values[cookie->definition.name] = value;
    account->dirty[cookie->definition.name] = value;
    account->write_failed = false;
    Save(id, account);
}

bool Service::IdentityPersisted(std::uint64_t id) const {
    Thread();
    ValidateAccount(id);
    const auto found = accounts_.find(id);
    // Clean offline accounts may already have been evicted. This is a local
    // durability status, not a database existence query or a remote cache read.
    return found == accounts_.end() || (!found->second->saving && found->second->dirty.empty());
}

std::string Service::IdentityError(std::uint64_t id) const {
    Thread();
    ValidateAccount(id);
    const auto found = accounts_.find(id);
    return found == accounts_.end() ? "" : found->second->write_error;
}

void Service::UserSet(const Identity& player, const std::string& name, const std::string& text, std::int64_t now) {
    const auto cookie = Find(name);
    CheckCookie(cookie);

    if (cookie->definition.access != Access::Public)
        throw Error("This setting cannot be changed by players.");

    Set(player, cookie, text, now);
}

bool Service::Persisted(const Identity& player) const {
    const auto account = Current(player);
    return account->state == State::Ready && !account->saving && account->dirty.empty();
}

void Service::Load(std::uint64_t id, const std::shared_ptr<Account>& account) {
    if (account->loading)
        return;

    const auto storage = storage_;
    auto values = std::make_shared<Values>();

    if (!Submit(
            [storage, id, values] {
                *values = storage()->Load(id);
            },
            [account, values](auto error) {
                account->loading = false;
                account->error = Message(error);
                account->state = error ? State::Failed : State::Ready;

                if (!error) {
                    account->values = std::move(*values);

                    for (const auto& [name, value] : account->dirty)
                        account->values[name] = value;
                }
            })) {
        account->state = State::Failed;
        account->error = "Client preferences worker queue is full.";
        return;
    }

    account->loading = true;
    account->state = State::Loading;
}

void Service::RetryLoad(const Identity& player) {
    auto account = Current(player);

    if (account->state == State::Failed)
        Load(player.account, account);
}

void Service::Refresh(const Identity& player) {
    const auto account = Current(player);

    if (account->loading || account->saving || !account->dirty.empty())
        throw Error("Client preferences refresh requires a clean, idle cache.");

    Load(player.account, account);
}

void Service::Save(std::uint64_t id, const std::shared_ptr<Account>& account) {
    if (account->saving || account->write_failed || account->dirty.empty())
        return;

    const auto storage = storage_;
    const auto batch = account->dirty;

    if (!Submit(
            [storage, id, batch] {
                storage()->Save(id, batch);
            },
            [account, batch](auto error) {
                account->saving = false;
                account->error = Message(error);
                account->write_failed = bool(error);
                account->write_error = account->error;

                if (!error) for (const auto& [name, value] : batch) {
                        const auto current = account->dirty.find(name);

                        if (current != account->dirty.end() && current->second.revision == value.revision)
                            account->dirty.erase(current);
                    }
            }))
        return; // Accepted cache writes remain dirty; Pump retries when capacity returns.
    account->saving = true;
    account->error.clear();
    account->write_error.clear();
}

void Service::RetryWrites() {
    Thread();

    for (const auto& [id, account] : accounts_) {
        account->write_failed = false;
        Save(id, account);
    }
}

void Service::Evict() {
    std::set<std::uint64_t> active;

    for (const auto& [slot, player] : players_)
        active.insert(player.account);

    std::erase_if(accounts_, [&](const auto& entry) {
        const auto& account = entry.second;
        return !active.contains(entry.first) && !account->loading && !account->saving && account->dirty.empty();
    });
}

void Service::Pump() {
    Thread();
    queue_.Dispatch();
    std::erase_if(tasks_, [](const auto& task) {
        return task->done;
    });

    for (const auto& [id, account] : accounts_)
        Save(id, account);

    Evict();
}

bool Service::CanStop() {
    Pump();

    if (Pending())
        return false;

    for (const auto& [id, account] : accounts_)
        if (!account->dirty.empty())
            return false;

    return true;
}

}
