#include "catalog.h"
#include <source2root/extension.hpp>
#include <map>

namespace {
using namespace keels2::authoring;
using source2root::NativeCall;
namespace top = source2root::topmenus;

class TopMenus final : public source2root::Extension {
public:
    static constexpr PluginInfo Info{"Source2Root Top Menus", "KeelS2 Project", "1.0.0", "Shared categorized plugin menus"};
    static constexpr PluginRequirement Requirements[]{{"Source2Root", "1.0.0", DependencyRequirement::exact}};
    TopMenus() : Extension("source2root.topmenus") {}

    void OnGameFrame(bool, bool, bool) override {
        const auto snapshot = views_;

        for (const auto& view : snapshot) {
            try {
                Tick(*view);
            } catch (const std::exception& error) {
                LogError("Top menu: {}", error.what());
                view->live = false;
            }

            if (!view->live)
                CloseView(*view);
        }

        std::erase_if(views_, [](const auto& view) {
            return !view->live && !view->session;
        });
    }

private:
    static constexpr unsigned MenuType = 1, ObjectType = 2, ViewType = 3;
    using MenuRef = std::shared_ptr<top::Menu>;
    using ObjectRef = std::shared_ptr<top::Object>;

    struct Registration {
        ObjectRef object;
        ~Registration() {
            object->Close();
        }
    };

    struct View {
        KeelPlayerConnection player{};
        std::uint64_t owner = 0;
        MenuRef menu;
        SrMenuSession session = 0;
        top::Page page;
        std::uint32_t timeout = 20000;
        bool live = true;
        int pending = -1;
    };

    struct DisplayLease {
        std::shared_ptr<View> view;
        ~DisplayLease() {
            view->live = false;
        }
    };
    top::Catalog catalog_;
    std::map<const top::Object*, SrCallback> callbacks_;
    std::vector<std::shared_ptr<View>> views_;
    bool drawing_ = false;

    bool OnExtensionStart() override {
        return RegisterNative("TopMenu_Open", 2, &TopMenus::Open)
            && RegisterNative("TopMenu_Close", 1, &TopMenus::Close)
            && RegisterNative("TopMenu_SetTitle", 2, &TopMenus::SetTitle)
            && RegisterNative("TopMenu_AddCategory", 5, &TopMenus::AddCategory)
            && RegisterNative("TopMenu_AddItem", 8, &TopMenus::AddItem)
            && RegisterNative("TopMenu_Remove", 1, &TopMenus::Remove)
            && RegisterNative("TopMenu_SetLabel", 2, &TopMenus::SetLabel)
            && RegisterNative("TopMenu_SetOrder", 2, &TopMenus::SetOrder)
            && RegisterNative("TopMenu_Show", 3, &TopMenus::Show)
            && RegisterNative("TopMenu_Refresh", 1, &TopMenus::Refresh)
            && RegisterNative("TopMenu_CloseDisplay", 1, &TopMenus::CloseDisplay)
            && RegisterNative("TopMenu_IsOpen", 1, &TopMenus::IsOpen);
    }

    bool PrepareExtensionUnload() override {
        bool closed = true;

        for (const auto& view : views_) {
            view->live = false;

            if (!CloseView(*view))
                closed = false;
        }

        if (closed)
            views_.clear();

        return closed;
    }

    template<typename Function> static std::int32_t Invoke(NativeCall& call, Function function) {
        try {
            return function();
        } catch (const top::Error& error) {
            return call.Fail(error.what());
        }
    }

    static MenuRef Menu(NativeCall& call) {
        return call.Resource<MenuRef>(call.Int(1), MenuType);
    }

    static ObjectRef Object(NativeCall& call) {
        return call.Resource<Registration>(call.Int(1), ObjectType).object;
    }

    bool Allowed(std::uint64_t owner, const KeelPlayerConnection& player, const std::string& permission = {}) {
        KeelBool allowed = KEEL_FALSE;
        return ConsumerPermission(owner, player, permission.c_str(), allowed) == KEEL_RESULT_OK && allowed == KEEL_TRUE;
    }

    std::vector<ObjectRef> Active(const MenuRef& menu) {
        return menu->Active([&](auto owner) {
            return ConsumerStatus(owner) == KEEL_RESULT_OK;
        });
    }

    bool Current(const ObjectRef& object) {
        if (!object || !object->live || ConsumerStatus(object->owner) != KEEL_RESULT_OK)
            return false;

        const auto active = Active(object->menu);
        return std::find(active.begin(), active.end(), object) != active.end();
    }

    int Dispatch(const ObjectRef& object, const KeelPlayerConnection& player, int action) {
        const auto found = callbacks_.find(object.get());

        if (found == callbacks_.end() || !Current(object))
            return 0;

        const auto callback = found->second;
        std::int32_t handle = 0;

        if (ConsumerPlayer(object->owner, player, handle) != KEEL_RESULT_OK)
            return 0;

        std::vector<SrCallbackArgument> arguments(4);

        for (auto& argument : arguments)
            argument = {sizeof(argument), SR_CALLBACK_INT32, 0, 0, {}};

        arguments[0].value.integer = object->handle;
        arguments[1].value.integer = handle;
        arguments[2].value.integer = action;
        arguments[3].value.integer = object->data;
        std::int32_t result = 0;
        const auto status = InvokeCallback(callback, arguments, result);

        if (status == KEEL_RESULT_BUSY)
            return 0;

        if (status != KEEL_RESULT_OK || (action == 0 && (result < 0 || result > 2))) {
            object->Close();
            return 0;
        }

        return result;
    }

    top::Access Access(const ObjectRef& object, const KeelPlayerConnection& player) {
        if (!Current(object) || !Allowed(object->owner, player, object->permission))
            return top::Access::Hidden;

        if (object->kind == top::Kind::Category)
            return top::Access::Enabled;

        const auto result = Dispatch(object, player, 0);

        if (!Current(object) || !Allowed(object->owner, player, object->permission))
            return top::Access::Hidden;

        return static_cast<top::Access>(result);
    }

    std::int32_t Open(NativeCall& call) {
        return Invoke(call, [&] {
            return call.Own(MenuType, std::make_unique<MenuRef>(catalog_.Open(call.String(1), call.String(2))));
        });
    }

    std::int32_t Close(NativeCall& call) {
        call.Close(call.Int(1), MenuType);
        return 1;
    }

    std::int32_t SetTitle(NativeCall& call) {
        return Invoke(call, [&] {
            Menu(call)->SetTitle(call.String(2));
            return 1;
        });
    }

    std::int32_t AddCategory(NativeCall& call) {
        return Invoke(call, [&] {
            auto object = Menu(call)->Add(top::Kind::Category, call.String(2), "", call.String(3), call.String(4),
                call.ScriptId(), call.Owner(), call.Int(5));

            object->handle = call.Own(ObjectType, std::make_unique<Registration>(object));
            return object->handle;
        });
    }

    std::int32_t AddItem(NativeCall& call) {
        return Invoke(call, [&] {
            auto object = Menu(call)->Add(top::Kind::Item, call.String(3), call.String(2), call.String(4), call.String(6),
                call.ScriptId(), call.Owner(), call.Int(7));

            const auto token = call.Callback(5);

            try {
                if (RetainCallback(token) != KEEL_RESULT_OK)
                    throw top::Error("Persistent top menu callbacks are unavailable.");

                object->data = call.Int(8);
                callbacks_.emplace(object.get(), token);
                object->release = [this, token, key = object.get()] {
                    callbacks_.erase(key);
                    CancelCallback(token);
                };
                object->handle = call.Own(ObjectType, std::make_unique<Registration>(object));
            } catch (...) {
                object->Close();
                callbacks_.erase(object.get());
                CancelCallback(token);
                throw;
            }

            return object->handle;
        });
    }

    std::int32_t Remove(NativeCall& call) {
        call.Close(call.Int(1), ObjectType);
        return 1;
    }

    std::int32_t SetLabel(NativeCall& call) {
        return Invoke(call, [&] {
            Object(call)->SetLabel(call.String(2));
            return 1;
        });
    }

    std::int32_t SetOrder(NativeCall& call) {
        return Invoke(call, [&] {
            Object(call)->SetOrder(call.Int(2));
            return 1;
        });
    }

    bool CloseView(View& view) {
        if (!view.session)
            return true;

        const auto status = CloseNativeMenu(view.session);

        if (status != KEEL_RESULT_OK && status != KEEL_RESULT_NOT_FOUND)
            return false;

        view.session = 0;
        return true;
    }

    static void Selected(void* raw, const KeelPlayerConnection* player, std::int32_t item) noexcept {
        auto* view = static_cast<View*>(raw);

        if (view && view->live && player && player->slot == view->player.slot && player->generation == view->player.generation)
            view->pending = item;
    }

    void Draw(View& view, const std::string& category, std::size_t page) {
        if (drawing_)
            throw top::Error("Top menu display cannot reenter an access callback.");

        struct Guard {
            bool& flag;
            ~Guard() {
                flag = false;
            }
        } guard{drawing_};
        drawing_ = true;
        const auto built = view.menu->Build(
            category,
            page,
            [&](auto owner) {
                return ConsumerStatus(owner) == KEEL_RESULT_OK;
            },
            [&](const auto& object) {
                return Access(object, view.player);
            });

        if (!view.live || !Allowed(view.owner, view.player))
            throw top::Error("Top menu display is no longer active.");

        if (!CloseView(view))
            throw top::Error("The previous top menu is still closing.");

        view.page = built;
        view.pending = -1;
        std::vector<SrMenuItem> items;

        for (const auto& row : view.page.rows)
            items.push_back({row.label.c_str(), row.enabled ? KEEL_TRUE : KEEL_FALSE});

        const SrMenuSpec spec{sizeof(spec), SR_EXTENSION_API_VERSION, view.page.title.c_str(), "", items.data(),
            static_cast<std::uint32_t>(items.size()), view.timeout, &Selected, &view, nullptr, 0};

        if (OpenNativeMenu(view.player, spec, view.session) != KEEL_RESULT_OK)
            throw top::Error("The top menu renderer is unavailable.");
    }

    std::int32_t Show(NativeCall& call) {
        return Invoke(call, [&] {
            if (drawing_)
                throw top::Error("Top menu display cannot reenter an access callback.");

            auto menu = Menu(call);
            SrPlayerIdentity player{};
            const auto timeout = call.Int(3);

            if (!call.Player(call.Int(2), player))
                throw top::Error("Player connection is no longer available.");

            if (timeout < 250 || timeout > 120000)
                throw top::Error("Top menu timeout must be from 250 to 120000 milliseconds.");

            const KeelPlayerConnection connection{player.slot, 0, player.connection};

            if (!Allowed(call.Owner(), connection))
                throw top::Error("Show top menus from a running script callback.");

            for (const auto& view : views_)
                if (view->player.slot == player.slot) {
                    view->live = false;

                    if (!CloseView(*view))
                        throw top::Error("The previous top menu is still closing.");
                }

            std::erase_if(views_, [](const auto& view) {
                return !view->live && !view->session;
            });

            if (views_.size() >= 128)
                throw top::Error("Top menu display limit (128) reached.");

            auto view = std::make_shared<View>();
            view->menu = std::move(menu);
            view->owner = call.Owner();
            view->player = connection;
            view->timeout = static_cast<std::uint32_t>(timeout);
            views_.push_back(view);
            std::int32_t handle = 0;

            try {
                handle = call.Own(ViewType, std::make_unique<DisplayLease>(view));
                Draw(*view, "", 0);
            } catch (...) {
                view->live = false;

                if (handle)
                    call.Close(handle, ViewType);

                throw;
            }

            return handle;
        });
    }

    std::int32_t Refresh(NativeCall& call) {
        return Invoke(call, [&] {
            auto view = call.Resource<DisplayLease>(call.Int(1), ViewType).view;

            if (!view->live)
                throw top::Error("Top menu display is closed.");

            try {
                Draw(*view, view->page.category, view->page.page);
            } catch (...) {
                view->live = false;
                throw;
            }

            return 1;
        });
    }

    std::int32_t CloseDisplay(NativeCall& call) {
        call.Close(call.Int(1), ViewType);
        return 1;
    }

    std::int32_t IsOpen(NativeCall& call) {
        const auto view = call.Resource<DisplayLease>(call.Int(1), ViewType).view;
        return view->live && view->session && NativeMenuStatus(view->session) == KEEL_RESULT_OK ? 1 : 0;
    }

    bool Scope(const View& view) {
        if (view.page.category.empty())
            return true;

        const auto active = Active(view.menu);

        for (const auto& object : active) if (object->key == view.page.category && object->kind == top::Kind::Category)
            return Allowed(object->owner, view.player, object->permission);

        return false;
    }

    void Tick(View& view) {
        if (!view.live || !Allowed(view.owner, view.player) || !Scope(view)) {
            view.live = false;
            return;
        }

        bool changed = view.page.revision != view.menu->revision;

        for (const auto& row : view.page.rows) if (row.page < 0 && !row.back && !row.empty) {
                if (auto object = row.object.lock();
                    !object || !Current(object) || !Allowed(object->owner, view.player, object->permission)) {
                    changed = true;
                }
        }

        if (changed) {
            Draw(view, view.page.category, view.page.page);
            return;
        }

        if (view.pending < 0) {
            if (view.session && NativeMenuStatus(view.session) == KEEL_RESULT_NOT_FOUND)
                view.live = false;

            return;
        }

        if (!CloseView(view))
            return;

        const auto selected = std::exchange(view.pending, -1);

        if (static_cast<std::size_t>(selected) >= view.page.rows.size())
            throw top::Error("Invalid top menu selection.");

        const auto row = view.page.rows[selected];

        if (!row.enabled) {
            view.live = false;
            return;
        }

        if (row.back) {
            Draw(view, "", 0);
            return;
        }

        if (row.page >= 0) {
            Draw(view, view.page.category, static_cast<std::size_t>(row.page));
            return;
        }

        const auto object = row.object.lock();

        if (!object || Access(object, view.player) != top::Access::Enabled || !view.live || !Scope(view)) {
            view.live = false;
            return;
        }

        if (view.page.revision != view.menu->revision) {
            Draw(view, view.page.category, view.page.page);
            return;
        }

        if (object->kind == top::Kind::Category) {
            Draw(view, object->key, 0);
            return;
        }

        view.live = false;
        Dispatch(object, view.player, 1);
    }
};
}

KEELS2_PLUGIN(TopMenus)
