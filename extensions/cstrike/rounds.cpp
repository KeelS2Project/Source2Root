#include "rounds.h"
#include "players.h"
#include <keels2/detail/authoring_status.hpp>
#include <cmath>
#include <string>

namespace source2root::cstrike {
namespace {
void Check(KeelResult result, const char* operation) {
    if (result != KEEL_RESULT_OK)
        throw Error(std::string(operation) + ": " + keels2::detail::ResultDescription(result) + ".");
}
}

Rounds::Rounds(KeelPluginHandle plugin, const KeelNativeRuntimeApi& runtime, const KeelRoundControlApi* api)
    : plugin_(plugin), runtime_(runtime), api_(api ? *api : KeelRoundControlApi{}) {
    if (!plugin || runtime.size != sizeof(runtime) || runtime.api_version != KEELS2_NATIVE_RUNTIME_API_VERSION ||
        !runtime.check_game_thread)
        throw Error("Incompatible native runtime service.");

    if (api && (api->size != sizeof(*api) || api->api_version != KEELS2_ROUND_CONTROL_API_VERSION ||
                !api->capabilities || !api->terminate))
        throw Error("Incompatible round control service.");
}

void Rounds::Thread() const {
    Check(runtime_.check_game_thread(plugin_), "Counter-Strike round operation");
}

unsigned Rounds::Capabilities() const {
    Thread();

    if (!api_.capabilities)
        throw Error("Round control service is unavailable.");

    unsigned mask = 0;
    Check(api_.capabilities(plugin_,&mask),"Counter-Strike round capabilities");
    return mask & KEELS2_ROUND_CONTROL_TERMINATE;
}

void Rounds::Terminate(float delay, int reason, int team) {
    Thread();

    if (!std::isfinite(delay) || delay < 0 || delay > 3600)
        throw Error("Round delay must be finite and between 0 and 3600 seconds.");

    if (!(reason == 1 || (reason >= 4 && reason <= 14) || (reason >= 16 && reason <= 22)))
        throw Error("Unknown CS2 round-end reason.");

    if (team != 0 && team != 2 && team != 3)
        throw Error("Winning team must be automatic (0), T (2) or CT (3).");

    if (active_ >= 8)
        throw Error("Counter-Strike round recursion limit (8) reached.");

    struct Hold {
        unsigned& count;
        explicit Hold(unsigned& value) : count(value) {
            ++count;
        }

        ~Hold() {
            --count;
        }
    } hold(active_);

    if (!(Capabilities() & KEELS2_ROUND_CONTROL_TERMINATE))
        throw Error("Round termination is unavailable for this game build.");

    const KeelRoundTermination request{sizeof(request), static_cast<unsigned>(reason), delay, team, 0};
    Check(api_.terminate(plugin_,&request),"Counter-Strike round termination");
}
}
