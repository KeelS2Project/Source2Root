#include "foundation.h"

namespace sr {

Foundation::Script* Foundation::CommandOwner(const std::string& name) {
    const auto found = commands_.find(name);

    if (found != commands_.end())
        for (auto* owner : found->second)
            if (owner->state == PluginState::Running)
                return owner;

    return nullptr;
}

void Foundation::BindCommandMenus(Script& script) {
    runtime_.Bind(*script.vm, "GetCommandInfo", 5, [&](const Arguments& args) -> Cell {
        Thread();
        const auto name = args.String(2, 1024);
        args.Output(3, args.Int(4), "");
        args.OutputCell(5, 0);
        auto* owner = CommandOwner(name);

        if (!owner) {
            script.error = "Command is unavailable.";
            return 0;
        }

        const auto& command = owner->commands.at(name);

        if (!Allowed(script, args.Int(1), command.permission)) {
            script.error = "You do not have access to this command.";
            return 0;
        }

        if (command.usage.size() >= static_cast<std::size_t>(args.Int(4))) {
            script.error = "Command usage buffer is too small.";
            return 0;
        }

        args.Output(3, args.Int(4), command.usage);
        args.OutputCell(5, command.menu ? 1 : 0);
        return 1;
    });
    runtime_.Bind(*script.vm, "SetCommandMenu", 2, [&](const Arguments& args) -> Cell {
        Thread();

        if (script.state != PluginState::Loading && script.state != PluginState::Running) {
            script.error = "Cannot change command menus while retiring.";
            return 0;
        }

        const auto found = script.commands.find(args.String(1, 64));

        if (found == script.commands.end()) {
            script.error = "Register the command before adding its menu.";
            return 0;
        }

        found->second.menu = args.Callback(2);
        return 1;
    });
    runtime_.Bind(*script.vm, "OpenCommandMenu", 2, [&](const Arguments& args) -> Cell {
        Thread();

        if (script.state != PluginState::Running) {
            script.error = "Open command menus from a running callback.";
            return 0;
        }

        if (!args.Int(1)) {
            script.error = "Use this command in the game.";
            return 0;
        }

        Player caller;

        if (ResolvePlayer(script, args.Int(1), caller) != KEEL_RESULT_OK)
            return 0;

        const auto name = args.String(2, 1024);
        auto* owner = CommandOwner(name);

        if (!owner || !owner->commands.at(name).menu) {
            script.error = "Command menu is unavailable.";
            return 0;
        }

        const auto command = owner->commands.at(name);

        if (!permissions_.Allows(caller, command.permission)) {
            script.error = "You do not have access to this command.";
            return 0;
        }

        if (menu_call_depth_ >= 8) {
            script.error = "Command menu recursion limit reached.";
            return 0;
        }

        const auto player = PlayerHandle(*owner, caller);

        struct Depth {
            unsigned& value;
            explicit Depth(unsigned& depth) : value(depth) {
                ++value;
            }

            ~Depth() {
                --value;
            }
        } depth(menu_call_depth_);

        if (!Invoke(*owner, command.menu, {player}, "")) {
            script.error = "Command menu callback failed.";
            return 0;
        }

        return 1;
    });
}

}
