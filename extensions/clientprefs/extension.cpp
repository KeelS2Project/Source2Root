#include "service.h"
#include "menu.h"
#include <source2root/extension.hpp>
#include <charconv>

namespace {
using namespace keels2::authoring;
using source2root::NativeCall;
namespace prefs = source2root::prefs;

class ClientPreferences final : public source2root::Extension {
public:
    static constexpr PluginInfo Info{
        "Source2Root Client Preferences", "KeelS2 Project", "1.0.0", "Persistent authenticated player settings"};

    static constexpr PluginRequirement Requirements[]{{"Source2Root", "1.0.0", DependencyRequirement::exact}};
    ClientPreferences() : Extension("source2root.clientprefs") {}

    void OnGameFrame(bool, bool, bool) override {
        if (!service_)
            return;

        try {
            Sync();
        } catch (const prefs::Error& error) {
            if (!sync_error_)
                LogError("Client preferences connection update failed: {}", error.what());

            sync_error_ = true;
            players_.clear();
            service_->Sync({});
        }

        service_->Pump();
        TickMenus();
        // A script callback can add another wait; do not iterate a live vector.
        const auto waiting = waiting_;

        for (const auto& weak : waiting)
            if (auto request = weak.lock(); request && !request->done)
                Complete(*request);

        std::erase_if(waiting_, [](const auto& weak) {
            const auto value = weak.lock();
            return !value || value->done;
        });
    }

private:
    static constexpr unsigned CookieType = 1, RequestType = 2, PrefabType = 3, MenuType = 4;
    using CookieRef = std::shared_ptr<prefs::Cookie>;

    enum class WaitFor { Catalog, Cookie, Cache, Save, IdentitySave };

    struct Request {
        ClientPreferences* extension = nullptr;
        SrCallback callback = 0;
        WaitFor kind = WaitFor::Catalog;
        CookieRef cookie;
        prefs::Identity player;
        std::uint64_t identity = 0;
        std::int32_t handle = 0, player_handle = 0, data = 0;
        bool done = false;
        ~Request() {
            if (extension && callback)
                extension->CancelCallback(callback);
        }
    };
    using RequestRef = std::shared_ptr<Request>;
    std::unique_ptr<prefs::Service> service_;
    std::vector<prefs::Identity> players_;
    std::vector<std::weak_ptr<Request>> waiting_;
    bool snapshot_failed_ = false;
    bool sync_error_ = false;

    struct MenuView {
        prefs::Identity player;
        std::uint64_t owner = 0;
        SrMenuSession session = 0;
        prefs::MenuPage page;
        std::weak_ptr<prefs::Prefab> choice;
        bool live = true, choosing = false;
        int pending = -1;
    };

    struct MenuLease {
        explicit MenuLease(std::shared_ptr<MenuView> value) : view(std::move(value)) {}

        std::shared_ptr<MenuView> view;
        ~MenuLease() {
            view->live = false;
        }
    };
    std::vector<std::weak_ptr<prefs::Prefab>> prefabs_;
    // Callback user_data stays alive here until the host confirms closure,
    // even if the script resource has already been destroyed.
    std::vector<std::shared_ptr<MenuView>> menus_;

    bool OnExtensionStart() override {
        return RegisterNative("Prefs_RegisterCookie", 3, &ClientPreferences::Register)
            && RegisterNative("Prefs_FindCookie", 1, &ClientPreferences::Find)
            && RegisterNative("Prefs_CloseCookie", 1, &ClientPreferences::CloseCookie)
            && RegisterNative("Prefs_CookieState", 1, &ClientPreferences::CookieState)
            && RegisterNative("Prefs_CookieError", 3, &ClientPreferences::CookieError)
            && RegisterNative("Prefs_GetAccess", 1, &ClientPreferences::GetAccess)
            && RegisterNative("Prefs_IsReady", 0, &ClientPreferences::IsReady)
            && RegisterNative("Prefs_IsCached", 1, &ClientPreferences::IsCached)
            && RegisterNative("Prefs_IsSaved", 1, &ClientPreferences::IsSaved)
            && RegisterNative("Prefs_Get", 4, &ClientPreferences::Get)
            && RegisterNative("Prefs_Set", 3, &ClientPreferences::Set)
            && RegisterNative("Prefs_GetTime", 4, &ClientPreferences::GetTime)
            && RegisterNative("Prefs_PlayerError", 3, &ClientPreferences::PlayerError)
            && RegisterNative("Prefs_WhenReady", 2, &ClientPreferences::WhenReady)
            && RegisterNative("Prefs_WhenCookieReady", 3, &ClientPreferences::WhenCookieReady)
            && RegisterNative("Prefs_WhenCached", 3, &ClientPreferences::WhenCached)
            && RegisterNative("Prefs_WhenSaved", 3, &ClientPreferences::WhenSaved)
            && RegisterNative("Prefs_CloseRequest", 1, &ClientPreferences::CloseRequest)
            && RegisterNative("Prefs_Retry", 1, &ClientPreferences::Retry)
            && RegisterNative("Prefs_Refresh", 1, &ClientPreferences::Refresh)
            && RegisterNative("Prefs_SetIdentity", 3, &ClientPreferences::SetIdentity)
            && RegisterNative("Prefs_IsIdentitySaved", 1, &ClientPreferences::IsIdentitySaved)
            && RegisterNative("Prefs_IdentityError", 3, &ClientPreferences::IdentityError)
            && RegisterNative("Prefs_WhenIdentitySaved", 3, &ClientPreferences::WhenIdentitySaved)
            && RegisterNative("Prefs_UserCookieCount", 0, &ClientPreferences::UserCookieCount)
            && RegisterNative("Prefs_UserCookieName", 3, &ClientPreferences::UserCookieName)
            && RegisterNative("Prefs_UserSet", 3, &ClientPreferences::UserSet)
            && RegisterNative("Prefs_SetPrefabMenu", 3, &ClientPreferences::SetPrefab)
            && RegisterNative("Prefs_ClosePrefabMenu", 1, &ClientPreferences::ClosePrefab)
            && RegisterNative("Prefs_ShowMenu", 1, &ClientPreferences::ShowSettings)
            && RegisterNative("Prefs_CloseMenu", 1, &ClientPreferences::CloseSettings)
            && RegisterNative("Prefs_MenuOpen", 1, &ClientPreferences::MenuOpen);
    }

    bool PrepareExtensionUnload() override {
        bool closed = true;

        for (const auto& menu : menus_) {
            menu->live = false;

            if (!CloseView(*menu))
                closed = false;
        }

        if (!closed)
            return false;

        menus_.clear();
        // Keep successful registration/catalog state after an unload refusal:
        // the provider may still be leased by scripts that continue running.
        return !service_ || service_->CanStop();
    }

    template <typename Function> std::int32_t Invoke(NativeCall& call, Function function, std::int32_t failure = 0) {
        try {
            if (!service_) service_ = std::make_unique<prefs::Service>(prefs::ConfiguredStorage(
                call.DataPath(true), std::filesystem::path(call.ConfigPath()) / "databases.json"));

            Sync();
            return function();
        } catch (const prefs::Error& error) {
            return call.Fail(error.what(), failure);
        } catch (const std::filesystem::filesystem_error&) {
            return call.Fail("Client preferences data directory is unavailable.", failure);
        }
    }

    std::int32_t Refresh(NativeCall& call) {
        return Invoke(call, [&] {
            service_->Refresh(Player(call));
            return 1;
        });
    }

    static prefs::Identity Identity(const SrPlayerIdentity& player) {
        if (!player.authenticated || player.bot)
            throw prefs::Error("Client preferences require an authenticated human player.");

        prefs::ValidateAccount(player.steam_id);
        return {player.slot, player.connection, player.steam_id};
    }

    void Sync() {
        std::vector<SrPlayerIdentity> snapshot;
        std::vector<prefs::Identity> players;
        const auto result = PlayerSnapshot(snapshot);

        if (result == KEEL_RESULT_OK) for (const auto& player : snapshot) {
                try {
                    players.push_back(Identity(player));
                } catch (const prefs::Error&) { /* Bots/pending identities have no persistent cache. */
                }
        }

        if (result != KEEL_RESULT_OK && !snapshot_failed_)
            LogError("Player snapshot unavailable for client preferences ({}).", result);

        snapshot_failed_ = result != KEEL_RESULT_OK;
        // Failed/incomplete host snapshots invalidate sessions, never reuse old identities.
        service_->Sync(players);
        players_ = std::move(players);
        sync_error_ = false;
    }

    prefs::Identity Player(NativeCall& call) {
        SrPlayerIdentity player{};

        if (!call.Player(call.Int(1), player))
            throw prefs::Error("Player connection is no longer available.");

        return Identity(player);
    }

    static CookieRef& Cookie(NativeCall& call, unsigned argument = 1) {
        return call.Resource<CookieRef>(call.Int(argument), CookieType);
    }

    std::int32_t Register(NativeCall& call) {
        return Invoke(call, [&] {
            auto cookie = service_->Register({call.String(1), call.String(2), static_cast<prefs::Access>(call.Int(3))});
            return call.Own(CookieType, std::make_unique<CookieRef>(std::move(cookie)));
        });
    }

    std::int32_t Find(NativeCall& call) {
        return Invoke(call, [&] {
            const auto cookie = service_->Find(call.String(1));
            return cookie ? call.Own(CookieType, std::make_unique<CookieRef>(cookie)) : 0;
        });
    }

    std::int32_t CloseCookie(NativeCall& call) {
        call.Close(call.Int(1), CookieType);
        return 1;
    }

    std::int32_t CookieState(NativeCall& call) {
        return static_cast<int>(Cookie(call)->state);
    }

    std::int32_t CookieError(NativeCall& call) {
        call.Output(2, call.Int(3), Cookie(call)->error);
        return 1;
    }

    std::int32_t GetAccess(NativeCall& call) {
        return static_cast<int>(Cookie(call)->definition.access);
    }

    std::int32_t IsReady(NativeCall& call) {
        return Invoke(call, [&] {
            return service_->Ready() ? 1 : 0;
        });
    }

    std::int32_t IsCached(NativeCall& call) {
        return Invoke(call, [&] {
            return service_->Status(Player(call)) == prefs::State::Ready ? 1 : 0;
        });
    }

    std::int32_t IsSaved(NativeCall& call) {
        return Invoke(call, [&] {
            return service_->Persisted(Player(call)) ? 1 : 0;
        });
    }

    std::int32_t Get(NativeCall& call) {
        call.Output(3, call.Int(4), "");
        return Invoke(call, [&] {
            call.Output(3, call.Int(4), service_->Get(Player(call), Cookie(call, 2)).text);
            return 1;
        });
    }

    static std::int64_t Now() {
        return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

    std::int32_t Set(NativeCall& call) {
        return Invoke(call, [&] {
            service_->Set(Player(call), Cookie(call, 2), call.String(3), Now());
            return 1;
        });
    }

    static std::uint64_t Account(NativeCall& call) {
        const auto text = call.String(1);
        std::uint64_t id = 0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), id);

        if (text.empty() || text.front() == '0' || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
            throw prefs::Error("Client preferences identity must be a decimal Steam64 account.");

        prefs::ValidateAccount(id);
        return id;
    }

    std::int32_t SetIdentity(NativeCall& call) {
        return Invoke(call, [&] {
            service_->SetIdentity(Account(call), Cookie(call, 2), call.String(3), Now());
            return 1;
        });
    }

    std::int32_t IsIdentitySaved(NativeCall& call) {
        return Invoke(call, [&] {
            return service_->IdentityPersisted(Account(call)) ? 1 : 0;
        });
    }

    std::int32_t IdentityError(NativeCall& call) {
        call.Output(2, call.Int(3), "");
        return Invoke(call, [&] {
            call.Output(2, call.Int(3), service_->IdentityError(Account(call)));
            return 1;
        });
    }

    std::int32_t GetTime(NativeCall& call) {
        call.Output(3, call.Int(4), "");
        return Invoke(call, [&] {
            call.Output(3, call.Int(4), std::to_string(service_->Get(Player(call), Cookie(call, 2)).updated));
            return 1;
        });
    }

    std::int32_t PlayerError(NativeCall& call) {
        call.Output(2, call.Int(3), "");
        return Invoke(call, [&] {
            call.Output(2, call.Int(3), service_->ErrorText(Player(call)));
            return 1;
        });
    }

    std::int32_t Wait(NativeCall& call, WaitFor kind) {
        return Invoke(call, [&] {
            std::erase_if(waiting_, [](const auto& weak) {
                const auto value = weak.lock();
                return !value || value->done;
            });

            if (waiting_.size() >= 256)
                throw prefs::Error("Client preferences callback limit (256) reached.");

            auto request = std::make_shared<Request>();
            request->extension = this;
            request->kind = kind;

            if (kind == WaitFor::Cookie)
                request->cookie = Cookie(call);

            if (kind == WaitFor::IdentitySave)
                request->identity = Account(call);

            if (kind == WaitFor::Cache || kind == WaitFor::Save) {
                request->player = Player(call);
                request->player_handle = call.Int(1);
            }

            const auto callback_index = kind == WaitFor::Catalog ? 1u : 2u;
            request->data = call.Int(callback_index + 1);
            request->callback = call.Callback(callback_index);
            waiting_.push_back(request);
            request->handle = call.Own(RequestType, std::make_unique<RequestRef>(request));
            return request->handle;
        });
    }

    std::int32_t WhenReady(NativeCall& call) {
        return Wait(call, WaitFor::Catalog);
    }

    std::int32_t WhenCookieReady(NativeCall& call) {
        return Wait(call, WaitFor::Cookie);
    }

    std::int32_t WhenCached(NativeCall& call) {
        return Wait(call, WaitFor::Cache);
    }

    std::int32_t WhenSaved(NativeCall& call) {
        return Wait(call, WaitFor::Save);
    }

    std::int32_t WhenIdentitySaved(NativeCall& call) {
        return Wait(call, WaitFor::IdentitySave);
    }

    std::int32_t CloseRequest(NativeCall& call) {
        call.Close(call.Int(1), RequestType);
        return 1;
    }

    void Complete(Request& request) {
        bool ready = false;
        std::string error;

        if (request.kind == WaitFor::Catalog) {
            ready = service_->Ready();
            error = service_->ErrorText();
        } else if (request.kind == WaitFor::Cookie) {
            ready = request.cookie->state == prefs::State::Ready;
            error = request.cookie->error;
        } else if (request.kind == WaitFor::IdentitySave) {
            ready = service_->IdentityPersisted(request.identity);
            error = service_->IdentityError(request.identity);
        } else {
            if (std::find(players_.begin(), players_.end(), request.player) == players_.end()) {
                CancelCallback(request.callback);
                request.done = true;
                return;
            }

            ready = request.kind == WaitFor::Cache ? service_->Status(request.player) == prefs::State::Ready
                                                   : service_->Persisted(request.player);

            error = service_->ErrorText(request.player);
        }

        if (!ready && error.empty())
            return;

        if (DeliverCallback(request.callback, {request.handle, request.player_handle, request.data}, error.c_str()) !=
            KEEL_RESULT_BUSY)
            request.done = true;
    }

    std::int32_t Retry(NativeCall& call) {
        return Invoke(call, [&] {
            if (call.Int(1))
                service_->RetryLoad(Player(call));

            service_->RetryCatalog();
            service_->RetryWrites();
            return 1;
        });
    }

    std::int32_t UserCookieCount(NativeCall& call) {
        return Invoke(call, [&] {
            return static_cast<int>(service_->UserCookies().size());
        });
    }

    std::int32_t UserCookieName(NativeCall& call) {
        call.Output(2, call.Int(3), "");
        return Invoke(call, [&] {
            const auto visible = service_->UserCookies();
            const auto index = call.Int(1);

            if (index < 0 || static_cast<std::size_t>(index) >= visible.size())
                throw prefs::Error("Cookie index is out of range.");

            call.Output(2, call.Int(3), visible[index].name);
            return 1;
        });
    }

    std::int32_t UserSet(NativeCall& call) {
        return Invoke(call, [&] {
            service_->UserSet(Player(call), call.String(2), call.String(3), Now());
            return 1;
        });
    }

    std::int32_t SetPrefab(NativeCall& call) {
        return Invoke(call, [&] {
            auto prefab = std::make_shared<prefs::Prefab>(prefs::Prefab{Cookie(call),
                static_cast<prefs::PrefabType>(call.Int(2)), call.String(3), call.Owner()});

            prefs::ValidatePrefab(*prefab);
            std::erase_if(prefabs_, [](const auto& weak) {
                return weak.expired();
            });

            if (prefabs_.size() >= 256)
                throw prefs::Error("Preferences prefab limit (256) reached.");

            prefabs_.push_back(prefab);
            return call.Own(PrefabType, std::make_unique<std::shared_ptr<prefs::Prefab>>(prefab));
        });
    }

    std::int32_t ClosePrefab(NativeCall& call) {
        call.Close(call.Int(1), PrefabType);
        return 1;
    }

    bool CloseView(MenuView& view) {
        if (!view.session)
            return true;

        const auto status = CloseNativeMenu(view.session);

        if (status != KEEL_RESULT_OK && status != KEEL_RESULT_NOT_FOUND)
            return false;

        view.session = 0;
        return true;
    }

    static void Selected(void* raw, const KeelPlayerConnection* player, std::int32_t item) noexcept {
        auto* view = static_cast<MenuView*>(raw);

        if (view && player && view->live && player->slot == view->player.slot && player->generation == view->player.connection)
            view->pending = item; // Open submenus only after the host finishes closing this session.
    }

    void Display(MenuView& view, const std::string& title, const std::vector<SrMenuItem>& items) {
        if (!CloseView(view))
            throw prefs::Error("The previous settings menu is still closing.");

        const KeelPlayerConnection connection{view.player.slot, 0, view.player.connection};
        const SrMenuSpec spec{sizeof(spec), SR_EXTENSION_API_VERSION, title.c_str(), "", items.data(),
            static_cast<std::uint32_t>(items.size()), 20000, &Selected, &view, nullptr, 0};

        if (OpenNativeMenu(connection, spec, view.session) != KEEL_RESULT_OK)
            throw prefs::Error("The settings menu renderer is unavailable.");
    }

    std::vector<std::weak_ptr<prefs::Prefab>> ActivePrefabs() {
        std::vector<std::weak_ptr<prefs::Prefab>> result;
        std::erase_if(prefabs_, [](const auto& weak) {
            return weak.expired();
        });

        for (const auto& weak : prefabs_)
            if (auto prefab = weak.lock(); prefab && ConsumerStatus(prefab->owner) == KEEL_RESULT_OK)
                result.push_back(prefab);

        return result;
    }

    void RootMenu(MenuView& view, std::size_t page) {
        view.page = prefs::BuildSettings(*service_, view.player, ActivePrefabs(), page);
        std::vector<SrMenuItem> items;

        for (const auto& row : view.page.rows)
            items.push_back({row.text.c_str(), row.enabled ? KEEL_TRUE : KEEL_FALSE});

        view.choosing = false;
        view.choice.reset();
        Display(view, view.page.title, items);
    }

    void ChoiceMenu(MenuView& view, const std::shared_ptr<prefs::Prefab>& prefab) {
        prefs::ValidatePrefab(*prefab);

        if (ConsumerStatus(prefab->owner) != KEEL_RESULT_OK)
            throw prefs::Error("The setting's plugin is not running.");

        const auto yes = prefs::ChoiceLabel(prefab->type, true), no = prefs::ChoiceLabel(prefab->type, false);
        view.choosing = true;
        view.choice = prefab;
        Display(
            view, prefab->label, {{yes.c_str(), KEEL_TRUE}, {no.c_str(), KEEL_TRUE}, {"Back to settings", KEEL_TRUE}});
    }

    std::int32_t ShowSettings(NativeCall& call) {
        return Invoke(call, [&] {
            if (ConsumerStatus(call.Owner()) != KEEL_RESULT_OK)
                throw prefs::Error("Show settings from a running script callback.");

            const auto player = Player(call);

            for (const auto& menu : menus_)
                if (menu->player.slot == player.slot) {
                    menu->live = false;

                    if (!CloseView(*menu))
                        throw prefs::Error("The previous settings menu is still closing.");
                }

            std::erase_if(menus_, [](const auto& menu) {
                return !menu->live && !menu->session;
            });

            if (menus_.size() >= 128)
                throw prefs::Error("Preferences menu limit (128) reached.");

            auto view = std::make_shared<MenuView>();
            view->player = player;
            view->owner = call.Owner();
            menus_.push_back(view);
            const auto handle = call.Own(MenuType, std::make_unique<MenuLease>(view));

            try {
                RootMenu(*view, 0);
            } catch (...) {
                call.Close(handle, MenuType);
                throw;
            }

            return handle;
        });
    }

    std::int32_t CloseSettings(NativeCall& call) {
        call.Close(call.Int(1), MenuType);
        return 1;
    }

    std::int32_t MenuOpen(NativeCall& call) {
        const auto& view = *call.Resource<MenuLease>(call.Int(1), MenuType).view;
        return view.live && view.session && NativeMenuStatus(view.session) == KEEL_RESULT_OK ? 1 : 0;
    }

    bool PrefabLive(const std::weak_ptr<prefs::Prefab>& weak) {
        const auto prefab = weak.lock();
        return prefab && prefab->cookie->state == prefs::State::Ready && ConsumerStatus(prefab->owner) == KEEL_RESULT_OK;
    }

    void TickMenus() {
        for (const auto& menu : menus_) {
            auto& view = *menu;

            if (std::find(players_.begin(), players_.end(), view.player) == players_.end() ||
                ConsumerStatus(view.owner) != KEEL_RESULT_OK)
                view.live = false;

            if (view.choosing && !PrefabLive(view.choice))
                view.live = false;

            if (!view.choosing) for (const auto& row : view.page.rows)
                    if (row.enabled && row.page < 0 && !PrefabLive(row.prefab))
                        view.live = false;

            if (view.live && view.pending >= 0) {
                if (!CloseView(view))
                    continue;

                const auto selected = std::exchange(view.pending, -1);

                try {
                    if (view.choosing) {
                        const auto prefab = view.choice.lock();

                        if (selected < 2 && prefab)
                            service_->UserSet(view.player, prefab->cookie->definition.name, prefs::ChoiceValue(prefab->type, selected == 0), Now());
                        else if (selected != 2)
                            throw prefs::Error("Invalid preference selection.");

                        RootMenu(view, view.page.page);
                    } else {
                        if (static_cast<std::size_t>(selected) >= view.page.rows.size())
                            throw prefs::Error("Invalid preference selection.");

                        const auto row = view.page.rows[selected];

                        if (row.page >= 0)
                            RootMenu(view, static_cast<std::size_t>(row.page));
                        else if (auto prefab = row.prefab.lock(); row.enabled && prefab)
                            ChoiceMenu(view, prefab);
                        else
                            throw prefs::Error("This preference is read only or no longer available.");
                    }
                } catch (const prefs::Error& error) {
                    LogError("{}", error.what());
                    view.live = false;
                }
            } else if (view.live && view.session && NativeMenuStatus(view.session) == KEEL_RESULT_NOT_FOUND)
                view.live = false;

            if (!view.live)
                CloseView(view);
        }

        std::erase_if(menus_, [](const auto& menu) {
            return !menu->live && !menu->session;
        });
    }
};
}

KEELS2_PLUGIN(ClientPreferences)
