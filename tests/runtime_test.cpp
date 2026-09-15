#include "runtime.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <thread>

static void Require(bool value, const char* message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

int main(int argc, char** argv) {
    Require(argc == 4, "runtime_test library hello.smx faults.smx");
    std::vector<std::string> messages;
    sr::PawnRuntime runtime(argv[1], [&](const auto& text) {
        messages.push_back(text);
        std::cout << text << '\n';
    });
    auto hello = runtime.Load(argv[2]);
    SourcePawn::IPluginFunction* command = nullptr;
    SourcePawn::IPluginFunction* timer = nullptr;
    SourcePawn::IPluginFunction* event = nullptr;
    int replies = 0;
    runtime.Bind(*hello, "RegisterCommand", 3, [&](const sr::Arguments& args) {
        Require(args.String(1) == "sr_hello" && args.String(2) == "demo.hello", "real command registration");
        command = args.Callback(3);
        return 1;
    }, 4);
    runtime.Bind(*hello, "ListenEvent", 2, [&](const sr::Arguments& args) {
        Require(args.String(1) == "round_start", "real event registration");
        event = args.Callback(2);
        return 1;
    });
    runtime.Bind(*hello, "CreateTimer", 3, [&](const sr::Arguments& args) {
        Require(args.Int(1) == 250 && args.Int(3) == 0, "real timer registration");
        timer = args.Callback(2);
        return 1;
    }, 4);
    runtime.Bind(*hello, "LogMessage", 1, [&](const sr::Arguments& args) {
        messages.push_back(args.String(1));
        return 0;
    });
    runtime.Bind(*hello, "ReplyToCommand", 2, [&](const sr::Arguments& args) {
        ++replies;
        messages.push_back(args.String(2));
        return 1;
    });
    runtime.Bind(*hello, "SR_ExampleAdd", 2, [](const sr::Arguments& args) {
        return args.Int(1) + args.Int(2);
    });
    sr::Cell value = 0;
    Require(runtime.Invoke("hello", hello->GetFunctionByName("OnPluginStart"), {}, nullptr, value) && value == 1,
            "real initialization");
    Require(command && event, "registered callbacks");
    Require(runtime.Invoke("hello", command, {0}, "sample argument", value), "real command execution");
    Require(replies == 1 && timer, "command called natives and created timer");
    Require(runtime.Invoke("hello", timer, {0}, nullptr, value) && replies == 2, "real delayed callback");
    Require(runtime.Invoke("hello", event, {}, "round_start", value), "real event callback");
    hello.reset();
    auto faults = runtime.Load(argv[3]);
    Require(runtime.Invoke("faults", faults->GetFunctionByName("ReferenceValues"), {}, nullptr, value) && value == 42,
            "debug metadata and write-back for integer, Boolean and enum reference arguments");
    runtime.Bind(*faults, "TestArraySum", 2, [](const sr::Arguments& args) {
        const auto values = args.Array(1, args.Int(2));
        return std::accumulate(values.begin(), values.end(), sr::Cell{0});
    });
    Require(runtime.Invoke("faults", faults->GetFunctionByName("ArraySum"), {}, nullptr, value) && value == 24, "read valid VM array in test-only native");
    Require(!runtime.Invoke("faults", faults->GetFunctionByName("BadArray"), {}, nullptr, value), "reject bad array bounds");
    Require(!runtime.Invoke("faults", faults->GetFunctionByName("Divide"), {0}, nullptr, value), "contain arithmetic fault");
    runtime.Bind(*faults, "TestNativeDelay", 0, [](const sr::Arguments&) {
        // Model a durable native write: the upstream wall-clock watchdog
        // includes native time even though it cannot interrupt native code.
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        return 34;
    });
    Require(runtime.Invoke("faults", faults->GetFunctionByName("DelayedNative"), {}, nullptr, value) && value == 42,
            "bounded native latency leaves time to finish the script callback");
    const auto before = std::chrono::steady_clock::now();
    Require(!runtime.Invoke("faults", faults->GetFunctionByName("Runaway"), {}, nullptr, value), "interrupt interpreter loop");
    Require(std::chrono::steady_clock::now() - before < std::chrono::seconds(3), "watchdog deadline");
    Require(runtime.Idle(), "runtime is idle after faults");
    Require(runtime.Invoke("faults", faults->GetFunctionByName("ArraySum"), {}, nullptr, value) && value == 24,
            "runtime executes callbacks after watchdog recovery");
    bool stack = false;
    for (const auto& message : messages)
        stack = stack || message.find("runtime_fault.sp:") != std::string::npos;
    Require(stack, "file and line diagnostics");
    std::cout << "actual SourcePawn command, array/string bridge, callbacks, faults and x64 watchdog passed\n";
}
