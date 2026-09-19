#include "identity.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>

static void Require(bool value, const char* message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "--validate") {
        sr::Permissions access;
        access.Load(argv[2]);
        const sr::Player player{1, 1, sr::ParseSteamIdentity(argv[3]), true, false, "Migration fixture"};
        const auto* admin = access.Find(player);
        Require(admin != nullptr, "migrated identity is present in native permission reader");
        std::cout << admin->group << '\n' << admin->immunity << '\n'
                  << access.Allows(player, "admin.kick") << '\n' << access.Allows(player, "custom.plugin.permission") << '\n';

        return 0;
    }

    Require(argc == 2, "permissions_test fixture-directory");
    const std::filesystem::path root(argv[1]);
    std::filesystem::create_directories(root);
    auto write = [&](const char* name, const std::string& text) {
        std::ofstream(root / name, std::ios::binary) << text;
    };
    const std::string groups = R"("Groups" {
        "Root" { "immunity" "100" }
        "moderator" { "immunity" "10" "permissions" { "admin.kick" "1" } }
        "zero" { "permissions" { "admin.kick" "1" } }
    })";
    const std::string admins = R"("Admins" {
        "Root account" { "identity" "STEAM_0:1:61" "group" "root" }
        "Moderator" { "identity" "[U:1:124]" "group" "MODERATOR" }
        "Equal root" { "identity" "76561197960265853" "group" "Root" }
        "Higher root" { "identity" "[U:1:126]" "group" "Root" "immunity" "200" }
        "Zero" { "identity" "[U:1:127]" "group" "zero" }
        "Maximum" { "identity" "[U:1:128]" "group" "Root" "immunity" "2147483647" }
    })";
    write("admin_groups.cfg", "\xef\xbb\xbf// groups\r\n/* comment */\r\n" + groups);
    write("admins.cfg", admins);
    sr::Permissions access;
    access.Load(root);
    sr::Player actor{0, 1, 76561197960265851ULL, true, false, "Changed display name"};
    sr::Player moderator{1, 2, 76561197960265852ULL, true, false, "Moderator"};
    sr::Player equal{2, 3, 76561197960265853ULL, true, false, "Equal"};
    sr::Player higher{3, 4, 76561197960265854ULL, true, false, "Higher"};
    sr::Player zero{4, 5, 76561197960265855ULL, true, false, "Zero"};
    sr::Player maximum{5, 6, 76561197960265856ULL, true, false, "Maximum"};
    sr::Player ordinary{6, 7, 76561197960265857ULL, true, false, "Root account"};
    Require(access.Allows(actor, "any.plugin.permission") && !access.Allows(actor, "invalid.*"),
            "root grants every valid named permission");

    Require(access.Find(actor)->group == "Root" && access.Immunity(actor) == 100 && access.Immunity(moderator) == 10,
        "case-insensitive group references retain display names and inherited immunity");

    Require(access.Immunity(higher) == 200 && access.Immunity(maximum) == std::numeric_limits<int>::max(),
            "administrator immunity overrides group default");

    Require(access.Allows(moderator, "admin.kick") && !access.Allows(moderator, "admin.ban"),
            "named permissions are independent");

    Require(!access.Find(ordinary) && !access.Allows(ordinary, "admin.kick") && access.Allows(ordinary, ""),
            "display labels cannot authorize users; public permission stays public");

    Require(access.CanTarget(&actor, moderator, "admin.ban") && !access.CanTarget(&moderator, actor, "admin.kick"),
            "strict higher immunity permits targeting downward only");

    Require(!access.CanTarget(&actor, equal, "admin.kick") && !access.CanTarget(&actor, higher, "admin.kick"),
            "root cannot bypass equal or higher immunity");

    Require(access.CanTarget(&moderator, moderator, "admin.kick") &&
                !access.CanTarget(&moderator, moderator, "admin.ban"),
            "self-targeting still requires command permission");

    Require(!access.CanTarget(&zero, ordinary, "admin.kick"), "equal zero immunity also denies targeting");
    Require(access.CanTarget(nullptr, maximum, "admin.kick"), "server console can target maximum immunity");
    Require(!access.CanTargetIdentity(&actor, equal.steam_id, "admin.ban") &&
                access.CanTargetIdentity(&actor, actor.steam_id, "admin.ban") &&
                access.CanTargetIdentity(nullptr, maximum.steam_id, "admin.ban"),
            "offline identity targeting uses the same immunity and self/server rules");

    auto pending = actor;
    pending.authenticated = false;
    Require(!access.Find(pending) && !access.CanTarget(&pending, moderator, "admin.kick") &&
                !access.CanTarget(&actor, pending, "admin.kick"),
            "unverified identities neither authorize nor bypass target immunity");

    pending.bot = true;
    pending.slot = 12;
    Require(access.CanTarget(&actor, pending, "admin.kick") && !access.Find(pending),
            "connected bots have zero immunity and no administrator identity");

    pending.connection = 0;
    Require(!access.CanTarget(nullptr, pending, "admin.kick"), "disconnected targets are invalid even for console");
    auto rejected = [&](const char* file, const std::string& contents) {
        write(file, contents);
        bool failed = false;

        try {
            access.Load(root);
        } catch (const std::exception&) {
            failed = true;
        }

        Require(failed && access.Allows(actor, "new.permission") && access.Immunity(higher) == 200,
                "invalid reload retains the complete previous configuration");

        write("admin_groups.cfg", groups);
        write("admins.cfg", admins);
    };
    rejected("admins.cfg", R"("Admins" { "Unknown" { "identity" "[U:1:123]" "group" "missing" } })");
    rejected(
        "admins.cfg",
        R"("Admins" { "A" { "identity" "[U:1:123]" "group" "root" } "B" { "identity" "STEAM_1:1:61" "group" "root" } })");

    rejected("admins.cfg", R"("Admins" { "A" { "identity" "[U:1:123]" "IDENTITY" "[U:1:124]" "group" "root" } })");
    rejected("admins.cfg", R"("Admins" { "A" { "identity" "[U:1:123]" "group" "root" "immunity" "2147483648" } })");
    rejected("admins.cfg", R"("Admins" { "A" { "identity" "[U:1:123]" "group" "root" "immunity" "-1" } })");
    rejected("admin_groups.cfg", R"("Groups" { "root" {} "ROOT" {} })");
    rejected("admin_groups.cfg", R"("Groups" { "root" { "permissions" { "admin.kick" "0" } } })");
    rejected("admin_groups.cfg", R"("Groups" { "root" { "permissions" { "admin.*" "1" } } })");
    rejected("admin_groups.cfg", R"("Groups" { "root" { "inherits" "all" } })");
    rejected("admin_groups.cfg", R"(#base "another-file.cfg" "Groups" {})");
    rejected("admins.cfg", "\"Admins\" { \"unterminated");
    rejected("admins.cfg", "\"Admins\" {} }");
    rejected("admins.cfg", "/* unterminated");
    rejected("admins.cfg", std::string(1024 * 1024 + 1, ' '));
    std::filesystem::remove(root / "admin_groups.cfg");
    bool failed = false;

    try {
        access.Load(root);
    } catch (...) {
        failed = true;
    }

    Require(failed && access.Allows(actor, "admin.kick"),
            "missing half of configuration pair retains privileges on explicit reload");

    write("admin_groups.cfg", groups);
    write("admins.cfg", "Admins {}");
    access.Load(root);
    Require(!access.Find(actor) && !access.CanTarget(&actor, moderator, "admin.kick"),
            "valid empty administrator file revokes permissions");

    std::filesystem::remove(root / "admin_groups.cfg");
    std::filesystem::remove(root / "admins.cfg");
    write("permissions.json", "{\"schema\":1,\"admins\":[]}");
    failed = false;

    try {
        access.Load(root);
    } catch (...) {
        failed = true;
    }

    Require(failed, "legacy file requires explicit migration instead of silently dropping its users");
    std::filesystem::remove(root / "permissions.json");
    access.Load(root);
    Require(!access.Find(actor), "absent configuration starts without administrators");
    std::cout
        << "KeyValues parsing, identities, groups, root permissions, numeric immunity, self/console exceptions and atomic reload passed.\n";
}
