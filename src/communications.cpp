#include "foundation.h"

namespace sr {
bool Foundation::PreparePause() {
    Thread();
    if (!runtime_.Idle()) return Fail("Cannot pause Source2Root during a script callback.");
    bool success = true;
    for (auto& [id, slot] : scripts_)
        for (auto* script : {slot.current.get(), slot.replacement.get()})
            if (script && !ClearMenus(*script)) success = false;
    for (auto it = native_displays_.begin(); it != native_displays_.end();) {
        const auto slot = it->first; ++it;
        if (!CloseNativeDisplay(slot)) success = false;
    }
    if (!success) return false;
    if (!voice_.Suspend(voice_error_)) return Fail(voice_error_);
    CancelMap();
    return true;
}

void Foundation::NativeResumed() {
    Thread();
    ReconcilePlayers();
    if (!voice_.Resume(voice_error_)) Fail(voice_error_);
}

KeelResult Foundation::FilterVoice(int receiver, int sender, bool& listening) {
    Thread();
    return voice_.Filter(receiver, sender, listening);
}

bool Foundation::Gagged(const Player& player) const {
    for (const auto& [id, slot] : scripts_)
        for (const auto* script : {slot.current.get(), slot.replacement.get()}) if (script) {
            const auto found = script->gags.find(player.slot);
            if (found != script->gags.end() && found->second.SameConnection(player)) return true;
        }
    return false;
}

void Foundation::BindCommunications(Script& script) {
    auto bind = [&](const char* name, int count, auto callback) {
        runtime_.Bind(*script.vm, name, count, [&, callback](const Arguments& args) -> Cell { Thread(); return callback(args); });
    };
    const auto set = [&](const Arguments& args, bool voice) {
        if (script.state != PluginState::Running) { script.error = "Change communication restrictions from a running callback."; return false; }
        if (args.Int(2) != 0 && args.Int(2) != 1) { script.error = "Restriction state must be true or false."; return false; }
        Player player;
        if (ResolvePlayer(script, args.Int(1), player) != KEEL_RESULT_OK) return false;
        if (voice) return voice_.Set(script.owner, player, args.Int(2) != 0, script.error);
        if (args.Int(2)) script.gags[player.slot] = player;
        else script.gags.erase(player.slot);
        return true;
    };
    bind("SetPlayerMuted", 2, [set](const Arguments& args) { return set(args, true); });
    bind("SetPlayerGagged", 2, [set](const Arguments& args) { return set(args, false); });
    bind("IsPlayerMuted", 1, [&](const Arguments& args) {
        Player player;
        return ResolvePlayer(script, args.Int(1), player) == KEEL_RESULT_OK && voice_.Muted(player);
    });
    bind("IsPlayerGagged", 1, [&](const Arguments& args) {
        Player player;
        return ResolvePlayer(script, args.Int(1), player) == KEEL_RESULT_OK && Gagged(player);
    });
}
}
