#pragma once
#include <keels2/native_runtime.h>
#include <keels2/round_control.h>

namespace source2root::cstrike {
class Rounds final {
public:
    Rounds(KeelPluginHandle plugin, const KeelNativeRuntimeApi& runtime, const KeelRoundControlApi* api);
    Rounds(const Rounds&) = delete;
    Rounds& operator=(const Rounds&) = delete;
    unsigned Capabilities() const;
    void Terminate(float delay, int reason, int team);

private:
    void Thread() const;
    KeelPluginHandle plugin_;
    const KeelNativeRuntimeApi runtime_;
    const KeelRoundControlApi api_;
    unsigned active_ = 0;
};
}
