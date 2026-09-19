#include "foundation.h"

#include <algorithm>

namespace sr {

bool Foundation::ShowActivity(Script& script, Cell caller, const std::string& action, const std::string& confirmation) {
    if (script.state != PluginState::Running) {
        script.error = "Show activity from a running plugin callback.";
        return false;
    }

    if (action.empty() || action.front() == ' ' || std::any_of(action.begin(), action.end(), [](unsigned char c) {
            return c < 32 || c == 127;
        })) {
        script.error = "Activity requires a single line of text.";
        return false;
    }

    if (!confirmation.empty() &&
        (confirmation.front() == ' ' || std::any_of(confirmation.begin(), confirmation.end(), [](unsigned char c) {
             return c < 32 || c == 127;
         }))) {
        script.error = "Activity confirmation requires a single line of text.";
        return false;
    }

    Player actor;
    const auto kicked = script.self_kicks.find(caller);
    const bool self_kick = caller && caller == script.callback_player && kicked != script.self_kicks.end();

    if (self_kick)
        actor = kicked->second;
    else if (caller && ResolvePlayer(script, caller, actor) != KEEL_RESULT_OK)
        return false;

    auto capitalized = action;

    if (capitalized[0] >= 'a' && capitalized[0] <= 'z')
        capitalized[0] -= 'a' - 'A';

    bool delivered = true;
    auto failed = [&](const char* operation, KeelResult result) {
        if (delivered)
            script.error = std::string(operation) + " (KeelResult " + std::to_string(result) + ").";

        delivered = false;
    };

    if (!self_kick) {
        const auto reply = host_.Reply(caller ? &actor : nullptr, confirmation.empty() ? capitalized : confirmation);

        if (reply != KEEL_RESULT_OK)
            failed("Activity confirmation failed", reply);
    }

    const auto flags = settings_.activity;

    if (!(flags & 5))
        return delivered;

    auto name = caller ? actor.name.substr(0, 128) : "Server";

    for (char& c : name)
        if (static_cast<unsigned char>(c) < 32 || c == 127)
            c = ' ';

    if (name.empty())
        name = "ADMIN";

    const auto named = name + " " + action;
    const auto anonymous = "ADMIN: " + capitalized;
    int after = -1;

    for (;;) {
        Player candidate;
        const auto next = host_.NextPlayer(after, candidate);

        if (next == KEEL_RESULT_NOT_FOUND)
            break;

        if (next != KEEL_RESULT_OK) {
            failed("Activity recipient lookup failed", next);
            break;
        }

        if (candidate.slot <= after || !candidate.connection) {
            failed("Invalid activity recipient", KEEL_RESULT_INCOMPATIBLE);
            break;
        }

        after = candidate.slot;
        Player current;
        const auto lookup = host_.Lookup(candidate.slot, current);

        if (lookup == KEEL_RESULT_NOT_FOUND)
            continue;

        if (lookup != KEEL_RESULT_OK) {
            failed("Activity recipient lookup failed", lookup);
            continue;
        }

        if (!current.SameConnection(candidate) || current.bot || (caller && current.SameConnection(actor)))
            continue;

        const auto audience = permissions_.Find(current) ? 4 : 1;

        if (!(flags & audience))
            continue;

        const auto result = host_.Reply(&current, flags & (audience * 2) ? named : anonymous);

        if (result != KEEL_RESULT_OK)
            failed("Activity announcement failed", result);
    }

    return delivered;
}

}
