#include "foundation.h"

#include <iterator>

namespace sr {
void Foundation::BindBans(Script& script) {
    auto bind = [&](const char* name, int count, auto callback) {
        runtime_.Bind(*script.vm, name, count, [&, callback](const Arguments& args) -> Cell {
            Thread();
            return callback(args);
        });
    };
    auto account = [&](const std::string& text) -> std::uint64_t {
        try {
            return ParseSteamIdentity(text);
        } catch (const std::exception&) {
            script.error = "Invalid individual Steam account ID.";
            return 0;
        }
    };
    bind("NormalizeSteamID", 3, [&, account](const Arguments& args) {
        const auto id = account(args.String(1, 1024));
        args.Output(2, args.Int(3), "");

        if (!id)
            return 0;

        args.Output(2, args.Int(3), std::to_string(id));
        return 1;
    });
    bind("LoadBans", 0, [&](const Arguments&) {
        if (script.state != PluginState::Loading && script.state != PluginState::Running) {
            script.error = "Load bans during initialization or a running callback.";
            return false;
        }

        return bans_.Load(script.error);
    });
    bind("GetBan", 3, [&, account](const Arguments& args) {
        const auto id = account(args.String(1, 1024));
        args.Output(2, args.Int(3), "");

        if (!id || !bans_.Load(script.error))
            return 0;

        const auto* ban = bans_.ActiveBan(id, host_.UtcNow());

        if (!ban) {
            script.error = "No active ban for this account.";
            return 0;
        }

        args.Output(2, args.Int(3), ban->reason);
        return 1;
    });
    bind("GetBanCount", 0, [&](const Arguments&) -> Cell {
        return bans_.Load(script.error) ? static_cast<Cell>(bans_.Entries().size()) : -1;
    });
    bind("GetBanIdentity", 3, [&](const Arguments& args) {
        args.Output(2, args.Int(3), "");

        if (!bans_.Load(script.error))
            return 0;

        const auto index = args.Int(1);

        if (index < 0 || static_cast<std::size_t>(index) >= bans_.Entries().size()) {
            script.error = "Ban entry is no longer available.";
            return 0;
        }

        auto it = bans_.Entries().begin();
        std::advance(it, index);
        args.Output(2, args.Int(3), std::to_string(it->first));
        return 1;
    });
    bind("AddBan", 4, [&, account](const Arguments& args) {
        if (script.state != PluginState::Running) {
            script.error = "Change bans from a running plugin callback.";
            return false;
        }

        const auto id = account(args.String(2, 128));

        if (!id)
            return false;

        std::string actor = "server";

        if (args.Int(1)) {
            Player player;

            if (ResolvePlayer(script, args.Int(1), player) != KEEL_RESULT_OK)
                return false;

            if (!player.authenticated || player.bot || !ValidSteamIdentity(player.steam_id)) {
                script.error = "Actor identity is not ready.";
                return false;
            }

            actor = std::to_string(player.steam_id);
        }

        const auto minutes = args.Int(3);

        if (minutes < 0 || minutes > 5256000) {
            script.error = "Ban duration must be from 0 to 5256000 minutes.";
            return false;
        }

        return bans_.AddBan(id, minutes, args.String(4, 256), actor, host_.UtcNow(), script.error);
    });
    bind("RemoveBan", 2, [&, account](const Arguments& args) {
        args.OutputCell(2, 0);

        if (script.state != PluginState::Running) {
            script.error = "Change bans from a running plugin callback.";
            return 0;
        }

        const auto id = account(args.String(1, 1024));

        if (!id)
            return 0;

        bool removed = false;

        if (!bans_.RemoveBan(id, removed, script.error))
            return 0;

        args.OutputCell(2, removed ? 1 : 0);
        return 1;
    });
}
}
