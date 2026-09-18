#include "catalog.h"
#include <algorithm>
#include <iostream>
#include <set>

namespace top = source2root::topmenus;
static void Check(bool result, const char* message) { if (!result) throw std::runtime_error(message); }
template<typename Function> static void Reject(Function function, const char* message) {
    bool rejected = false;
    try { function(); } catch (const top::Error&) { rejected = true; }
    Check(rejected, message);
}

int main() {
    try {
        top::Catalog catalog;
        auto menu = catalog.Open("admin", "Administration");
        Check(catalog.Open("admin", "Ignored title") == menu && menu->title == "Administration", "named menus share one catalog");
        std::set<std::uint64_t> running{1, 2};
        auto live = [&](auto owner) { return running.contains(owner); };
        auto access = [](const auto& object) {
            if (object->permission == "deny") return top::Access::Hidden;
            return object->key == "disabled" ? top::Access::Disabled : top::Access::Enabled;
        };
        auto category = menu->Add(top::Kind::Category, "players", "", "Players", "", "first", 1, 0);
        Check(menu->Build("", 0, live, access).rows[0].label == "No available actions", "empty category hidden");
        auto item = menu->Add(top::Kind::Item, "test", "players", "Test", "", "second", 2, 0);
        auto hidden = menu->Add(top::Kind::Item, "secret", "players", "Secret", "deny", "first", 1, 0);
        auto disabled = menu->Add(top::Kind::Item, "disabled", "", "Disabled", "", "first", 1, 1);
        auto root = menu->Build("", 0, live, access);
        Check(root.rows.size() == 2 && root.rows[0].label == "Players" && !root.rows[1].enabled, "categories sorted with disabled root items");
        auto page = menu->Build("players", 0, live, access);
        Check(page.rows.size() == 2 && page.rows[0].object.lock() == item && page.rows[1].back, "shared contribution and explicit back");
        Reject([&] { menu->Add(top::Kind::Item, "test", "players", "Other", "", "first", 1, 0); }, "foreign duplicate rejected");
        Reject([&] { menu->Add(top::Kind::Item, "test", "players", "Duplicate", "", "second", 2, 0); }, "same generation duplicate rejected");
        auto replacement = menu->Add(top::Kind::Item, "test", "players", "Reloaded", "", "second", 3, -1);
        Check(menu->Build("players", 0, live, access).rows[0].object.lock() == item, "loading generation does not mask running item");
        running.insert(3);
        Check(menu->Build("players", 0, live, access).rows[0].object.lock() == replacement, "latest running generation wins");
        replacement->Close();
        Check(menu->Build("players", 0, live, access).rows[0].object.lock() == item, "aborted replacement restores existing registration");
        running.erase(2);
        Check(menu->Build("", 0, live, access).rows.size() == 1, "paused contributor hides empty category");
        running.insert(2);
        running.erase(1);
        Reject([&] { menu->Build("players", 0, live, access); }, "paused category closes its submenu");
        running.insert(1);
        const auto revision = menu->revision;
        menu->SetTitle("New title"); item->SetLabel("New label"); item->SetOrder(-9);
        Check(menu->revision == revision + 3 && menu->Build("", 0, live, access).title == "New title", "title label order invalidate cached display");
        Reject([&] { menu->Build("", 0, live, [&](const auto&) { item->SetLabel("Mutation"); return top::Access::Enabled; }); }, "access callback mutation discards stale page");
        std::vector<std::shared_ptr<top::Object>> many;
        for (int i = 0; i < 65; ++i) many.push_back(menu->Add(top::Kind::Item, "extra" + std::to_string(i), "players",
            "Item " + std::to_string(i), "", "first", 1, i));
        page = menu->Build("players", 0, live, access);
        Check(page.rows.size() == 30 && page.rows[28].page == 1 && page.rows[29].back, "first page has bounded content and navigation");
        page = menu->Build("players", 1, live, access);
        Check(page.rows.size() == 31 && page.rows[28].page == 0 && page.rows[29].page == 2 && page.rows[30].back, "middle page has both directions and back");
        page = menu->Build("players", 999, live, access);
        Check(page.page == 2 && page.rows.size() == 12, "oversized page request clamps to final page");
        unsigned releases = 0;
        item->release = [&] { ++releases; };
        item->Close(); item->Close(); item.reset();
        Check(releases == 1, "callback retirement happens once despite retained snapshots");
        category->Close();
        Reject([&] { menu->Build("players", 0, live, access); }, "closed category cannot dispatch child");
        Reject([&] { catalog.Open("../escape", "Invalid"); }, "invalid menu identifier rejected");
        Reject([&] { disabled->SetLabel("line\nbreak"); }, "control characters rejected");
        Reject([&] { menu->Add(top::Kind::Item, "invalid", "", "Invalid", "Admin..root", "first", 1, 0); },
            "permission grammar matches core validation");
        many.clear(); hidden.reset(); disabled.reset(); category.reset(); replacement.reset();
        for (int i = 0; i < 300; ++i) {
            auto temporary = menu->Add(top::Kind::Item, "temporary", "", "Temporary", "", "first", 1, 0);
            temporary->Close();
        }
        for (int i = 0; i < 256; ++i) many.push_back(menu->Add(top::Kind::Item, "bounded" + std::to_string(i), "", "Bounded", "", "first", 1, 0));
        Reject([&] { menu->Add(top::Kind::Item, "overflow", "", "Overflow", "", "first", 1, 0); }, "object catalog enforces aggregate limit");
        many.clear();
        std::vector<std::shared_ptr<top::Menu>> menus;
        for (int i = 0; i < 31; ++i) menus.push_back(catalog.Open("menu" + std::to_string(i), "Menu"));
        Reject([&] { catalog.Open("overflow", "Too many"); }, "catalog bounds retained menus");
        menus.clear();
        Check(bool(catalog.Open("reclaimed", "Reclaimed")), "expired menus release quota");
        std::cout << "Shared top menu ownership, access, reload, pagination and mutation passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
