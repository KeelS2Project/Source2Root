#include "rounds.h"
#include "players.h"
#include <functional>
#include <limits>
#include <thread>

namespace {
using source2root::cstrike::Rounds;
unsigned calls = 0, caps = 1;
KeelResult cap_status = KEEL_RESULT_OK, call_status = KEEL_RESULT_OK;
KeelRoundTermination last{};
std::function<void()> callback;
const std::thread::id thread = std::this_thread::get_id();
void Check(bool result, const char* message) {
    if (!result)
        throw std::runtime_error(message);
}

template <typename F> void Reject(F call) {
    bool failed = false;

    try {
        call();
    } catch (const source2root::cstrike::Error&) {
        failed = true;
    }

    Check(failed,"round call must return a domain error");
}

KeelResult Thread(KeelPluginHandle plugin) {
    if (plugin != 5)
        return KEEL_RESULT_NOT_READY;

    return std::this_thread::get_id() == thread ? KEEL_RESULT_OK : KEEL_RESULT_WRONG_THREAD;
}

KeelResult Caps(KeelPluginHandle, unsigned* out) {
    *out = caps;
    return cap_status;
}

KeelResult Terminate(KeelPluginHandle plugin, const KeelRoundTermination* request) {
    Check(plugin == 5, "round operation owner");
    ++calls;
    last = *request;

    if (callback)
        callback();

    return call_status;
}
}

void RunRoundChecks() {
    const KeelNativeRuntimeApi runtime{sizeof(runtime), 1, Thread, nullptr, nullptr, nullptr};
    const KeelRoundControlApi api{sizeof(api), 1, Caps, Terminate};
    Rounds rounds(5,runtime,&api), absent(5,runtime,nullptr);
    Reject([&] {
        absent.Capabilities();
    });
    Reject([&] {
        absent.Terminate(0, 10, 0);
    });
    Check(rounds.Capabilities() == 1,"round capabilities");
    caps = UINT32_MAX;
    Check(rounds.Capabilities() == 1, "unknown round capabilities hidden");
    caps = 1;

    for (int reason : {1,4,5,6,7,8,9,10,11,12,13,14,16,17,18,19,20,21,22}) {
        rounds.Terminate(2.5f,reason,3);
        Check(last.size == sizeof(last) && !last.reserved && last.reason == static_cast<unsigned>(reason) &&
                  last.delay == 2.5f && last.team == 3,
              "round request mapping");
    }

    rounds.Terminate(0, 10, 0);
    Check(last.delay == 0 && last.team == 0, "immediate draw with automatic team");
    rounds.Terminate(3600, 9, 2);
    Check(last.delay == 3600 && last.team == 2, "maximum delay and T team");
    const auto valid = calls;

    for (float delay :
         {-1.0f, 3601.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
        Reject([&] {
            rounds.Terminate(delay, 10, 0);
        });

    for (int reason : {-1, 0, 2, 3, 15, 23, std::numeric_limits<int>::max()})
        Reject([&] {
            rounds.Terminate(0, reason, 0);
        });

    for (int team : {-1, 1, 4, 999})
        Reject([&] {
            rounds.Terminate(0, 10, team);
        });

    caps = 0;
    Reject([&] {
        rounds.Terminate(0, 10, 0);
    });
    caps = 1;
    cap_status = KEEL_RESULT_UNSUPPORTED;
    Reject([&] {
        rounds.Capabilities();
    });
    Reject([&] {
        rounds.Terminate(0, 10, 0);
    });
    cap_status = KEEL_RESULT_OK;
    Check(calls == valid,"invalid or unsupported round operation does not dispatch");
    call_status = KEEL_RESULT_NOT_READY;
    Reject([&] {
        rounds.Terminate(0, 10, 0);
    });
    call_status = KEEL_RESULT_OK;
    Check(calls == valid+1,"engine readiness failure propagated");
    callback = [&] {
        rounds.Terminate(0, 10, 0);
    };
    const auto before = calls;
    Reject([&] {
        rounds.Terminate(0, 10, 0);
    });
    callback = {};
    Check(calls == before + 8, "round recursion bounded");
    rounds.Terminate(0, 10, 0);
    std::exception_ptr failure;
    std::thread worker([&] {
        try {
            Reject([&] {
                rounds.Capabilities();
            });
            Reject([&] {
                rounds.Terminate(0, 10, 0);
            });
        } catch (...) {
            failure = std::current_exception();
        }
    });
    worker.join();

    if (failure)
        std::rethrow_exception(failure);

    auto bad = api;
    bad.terminate = nullptr;
    Reject([&] {
        Rounds invalid(5, runtime, &bad);
    });
    Reject([&] {
        Rounds invalid(0, runtime, &api);
    });
}
