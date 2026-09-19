#include "hooks.h"
#include <iostream>
#include <functional>
#include <cstring>
#include <limits>
#include <thread>

using namespace source2root::dhooks;
void EntityCalls();
namespace {
void Check(bool value, const char* text) {
    if (!value)
        throw std::runtime_error(text);
}

template<class Function> void Reject(Function function, const char* text) {
    bool failed = false;

    try {
        function();
    } catch (const Error&) {
        failed = true;
    }

    Check(failed, text);
}

struct Host {
    static Host* active;
    std::thread::id thread = std::this_thread::get_id();
    unsigned calls = 0, releases = 0;
    KeelResult status = KEEL_RESULT_OK;
    bool malformed = false;
    bool buffers = false, unterminated = false;
    std::function<void()> during;
    Host() {
        active = this;
    }

    static KeelResult Thread(KeelPluginHandle owner) {
        return owner == 7 && active->thread == std::this_thread::get_id() ? KEEL_RESULT_OK : KEEL_RESULT_WRONG_THREAD;
    }

    static KeelResult Resolve(KeelPluginHandle,
                              const KeelHookTargetSpec*,
                              const KeelHookPrototype* prototype,
                              KeelHookTargetHandle* out) {
        Check(prototype->fixed_argument_count == prototype->argument_count,"fixed argument descriptor matches native ABI");
        *out = 9;
        return KEEL_RESULT_OK;
    }

    static KeelResult Release(KeelPluginHandle, KeelHookTargetHandle) {
        ++active->releases;
        return KEEL_RESULT_OK;
    }

    static KeelResult
    Add(KeelPluginHandle, KeelHookTargetHandle, const KeelHookCallbackSpec*, KeelHookCallbackHandle*) {
        return KEEL_RESULT_UNSUPPORTED;
    }

    static KeelResult Remove(KeelPluginHandle, KeelHookCallbackHandle) {
        return KEEL_RESULT_UNSUPPORTED;
    }

    static KeelResult Enable(KeelPluginHandle, KeelHookCallbackHandle, KeelBool) {
        return KEEL_RESULT_UNSUPPORTED;
    }

    static KeelResult Invoke(KeelPluginHandle owner, KeelHookTargetHandle target, unsigned flags,
        const KeelHookValue* values, unsigned count, KeelHookValue* result) {
        Check(owner == 7 && target == 9 && count == 5 && flags <= 1,"direct call envelope");

        if (active->buffers) {
            Check(values[0].type == KH_VALUE_POINTER && values[1].type == KH_VALUE_UINT32 &&
                values[2].type == KH_VALUE_POINTER && values[3].type == KH_VALUE_INT32 &&
                values[4].type == KH_VALUE_POINTER,"buffer pointer and count types");

            auto* text = static_cast<char*>(values[0].scalar.pointer);
            auto* array = static_cast<std::int32_t*>(values[2].scalar.pointer);
            auto* vector = static_cast<float*>(values[4].scalar.pointer);
            Check(text && values[1].scalar.uint32 == 8 && std::strcmp(text,"hello") == 0 &&
                array && values[3].scalar.int32 == 2 && array[0] == 7 && array[1] == 9 &&
                vector && vector[0] == 1 && vector[1] == 2 && vector[2] == 3,"private copied buffer contents");

            ++active->calls;

            if (active->during)
                active->during();

            if (active->unterminated)
                std::memset(text, 'x', 8);
            else
                std::memcpy(text, "changed", 8);

            array[0] = 14;
            array[1] = 18;
            vector[2] = 6;
            *result = {};
            result->type = KH_VALUE_INT32;
            result->scalar.int32 = 32;
            return active->status;
        }

        Check(values[0].type == KH_VALUE_INT32 && values[0].scalar.int32 == 3 &&
            values[1].type == KH_VALUE_FLOAT32 && values[1].scalar.float32 == 2.0f &&
            values[2].type == KH_VALUE_UINT64 && values[2].scalar.uint64 == UINT64_MAX &&
            values[3].type == KH_VALUE_FLOAT64 && values[3].scalar.float64 == 1.0000000000000002 &&
            values[4].type == KH_VALUE_POINTER && !values[4].scalar.pointer,"exact scalar arguments");

        ++active->calls;

        if (active->during)
            active->during();

        *result = {};
        result->type = active->malformed ? KH_VALUE_FLOAT32 : KH_VALUE_INT32;
        result->scalar.int32 = flags ? 90 : 8;
        return active->status;
    }

    KeelHookApi hooks{
        sizeof(hooks), KEELHOOK_API_VERSION, Resolve, Release, Add, Remove, nullptr, nullptr, nullptr, Enable};

    KeelNativeRuntimeApi runtime{sizeof(runtime), KEELS2_NATIVE_RUNTIME_API_VERSION, Thread, nullptr, nullptr, nullptr};
    KeelCallApi calls_api{sizeof(calls_api), KEELCALL_API_VERSION, Invoke};
};
Host* Host::active = nullptr;
Definition Example() {
    Definition result;
    result.source = KH_TARGET_SYMBOL;
    result.module = "fixture";
    result.symbol = "Scalar";
    result.result = KH_VALUE_INT32;
    result.allow_calls = true;
    result.arguments = {KH_VALUE_INT32, KH_VALUE_FLOAT32, KH_VALUE_UINT64, KH_VALUE_FLOAT64, KH_VALUE_POINTER};
    return result;
}

void Initialize(Call& call) {
    call.SetInteger(1, 3);
    call.SetNumber(2, 2);
    call.SetIntegerText(3, "18446744073709551615");
    call.SetNumberText(4, "1.0000000000000002");
    call.SetNull(5);
}

Definition BufferExample() {
    auto result = Example();
    result.symbol = "Buffers";
    result.arguments = {KH_VALUE_POINTER,KH_VALUE_UINT32,KH_VALUE_POINTER,KH_VALUE_INT32,KH_VALUE_POINTER};
    result.buffers = {{1,16,2,BufferKind::string},{3,4,4,BufferKind::int32},{5,3,0,BufferKind::vector3}};
    return result;
}

void InitializeBuffers(Call& call) {
    std::vector<std::int32_t> cells{7,9};
    call.SetString(1, "hello", 8);
    call.SetArray(3, cells);
    call.SetVector(5, {1, 2, 3});
    cells[0] = 100;
}

void Buffers(Host& host, const std::shared_ptr<Service>& service) {
    host.buffers = true;
    auto target = service->Open(BufferExample());
    auto limited = BufferExample();
    limited.buffers[0].capacity = 4;
    auto alias = service->Open(limited);
    auto restricted = service->Prepare(*alias);
    Reject(
        [&] {
            restricted->SetString(1, "hello", 8);
        },
        "alias keeps its smaller buffer policy");

    auto scalar_only = BufferExample();
    scalar_only.buffers.clear();
    auto opaque_target = service->Open(scalar_only);
    auto opaque = service->Prepare(*opaque_target);
    Reject(
        [&] {
            opaque->SetString(1, "a", 2);
        },
        "alias cannot inherit another alias buffer permission");

    auto call = service->Prepare(*target);
    Reject(
        [&] {
            call->String(1);
        },
        "unset string cannot be read");

    Reject(
        [&] {
            call->SetNull(1);
        },
        "buffer pointer cannot be replaced with null");

    Reject(
        [&] {
            call->SetInteger(2, 999);
        },
        "coupled count cannot be set manually");

    Reject(
        [&] {
            call->SetString(1, "hello", 5);
        },
        "string capacity includes terminator");

    Reject(
        [&] {
            call->SetString(1, std::string("a\0b", 3), 4);
        },
        "embedded string null rejected");

    Reject(
        [&] {
            call->SetArray(3, {});
        },
        "empty array rejected");

    Reject(
        [&] {
            call->SetArray(3, {1, 2, 3, 4, 5});
        },
        "array bound enforced");

    Reject(
        [&] {
            call->SetVector(5, {1, std::numeric_limits<float>::infinity(), 3});
        },
        "nonfinite vector rejected");

    Reject(
        [&] {
            call->SetArray(1, {1});
        },
        "buffer kind enforced");

    call->SetString(1,"");
    Check(call->String(1).empty() && call->Read(2).Integer(2)==1,"empty string owns a terminator and supplies its length");
    call->SetString(1,std::string(15,'a'));
    Check(call->String(1) == std::string(15, 'a') && call->Read(2).Integer(2) == 16,
          "implicit string capacity fits exact configured maximum");

    call->SetArray(3, {1, 2, 3, 4});
    Check(call->Array(3).size()==4 && call->Read(4).Integer(4)==4,"array configured maximum is usable");
    InitializeBuffers(*call);
    Check(call->Read(2).Integer(2)==8 && call->Read(4).Integer(4)==2,"allocated lengths supplied automatically");
    call->Execute(0);
    Check(call->String(1) == "changed" && call->Array(3) == std::vector<std::int32_t>({14, 18}) &&
              call->Vector(5) == std::array<float, 3>({1, 2, 6}) && call->Read(0).Integer(0) == 32,
          "native in/out buffers copied back");

    Reject(
        [&] {
            call->SetString(1, "oversized", 17);
        },
        "oversized setter refused");

    Check(call->Read(0).Integer(0)==32 && call->String(1)=="changed","failed setter preserves output and result");
    call->Reset();
    Reject(
        [&] {
            call->String(1);
        },
        "reset hides string storage");

    Reject(
        [&] {
            call->Array(3);
        },
        "reset hides array storage");

    Reject(
        [&] {
            call->Execute(0);
        },
        "reset clears buffers and coupled count initialization");

    InitializeBuffers(*call);
    host.unterminated = true;
    call->Execute(0);
    Reject(
        [&] {
            call->String(1);
        },
        "unterminated native output cannot read past allocation");

    host.unterminated = false;
    InitializeBuffers(*call);
    host.status = KEEL_RESULT_ENGINE_FAILURE;
    Reject(
        [&] {
            call->Execute(0);
        },
        "host error still reported");

    Check(call->String(1)=="changed","native side effects remain after failed call");
    host.status = KEEL_RESULT_OK;
    InitializeBuffers(*call);
    restricted.reset();
    opaque.reset();
    target.reset();
    alias.reset();
    opaque_target.reset();
    const auto before = host.releases;
    host.during = [&] {
        Reject(
            [&] {
                call->SetString(1, "x", 8);
            },
            "busy string setter rejected");

        Reject(
            [&] {
                call->SetArray(3, {1});
            },
            "busy array setter rejected");

        Reject(
            [&] {
                call->SetVector(5, {1, 2, 3});
            },
            "busy vector setter rejected");

        Reject(
            [&] {
                call->String(1);
            },
            "busy buffer read rejected");

        call.reset();
        service->Collect();
        Check(host.releases == before, "in-flight buffer call retains target");
    };
    call->Execute(0);
    host.during = {};
    service->Collect();
    Check(service->Empty() && host.releases==before+1,"closed buffer call releases target after native writes");
    host.buffers = false;
}
}

int main() {
    try {
        Host host;
        auto old = std::make_shared<Service>(7,host.hooks,host.runtime);
        auto old_target = old->Open(Example());
        Reject(
            [&] {
                old->Prepare(*old_target);
            },
            "optional service absent");

        old_target.reset();
        old->Collect();
        old.reset();
        host.releases = 0;
        auto service = std::make_shared<Service>(7,host.hooks,host.runtime,&host.calls_api);
        auto target = service->Open(Example());
        auto denied = Example();
        denied.allow_calls = false;
        auto alias = service->Open(denied);
        Reject(
            [&] {
                service->Prepare(*alias);
            },
            "denied alias cannot inherit direct-call permission");

        auto call = service->Prepare(*target);
        Check(call->Count() == 5 && call->Type(0) == KH_VALUE_INT32,"prepared shape");
        Reject(
            [&] {
                call->Read(0);
            },
            "result not yet available");

        Reject(
            [&] {
                call->Read(1);
            },
            "argument not initialized");

        Reject(
            [&] {
                call->Execute(0);
            },
            "missing arguments refused");

        Reject(
            [&] {
                call->SetInteger(0, 7);
            },
            "result slot not writable");

        Reject(
            [&] {
                call->SetInteger(6, 7);
            },
            "argument bounds enforced");

        Reject(
            [&] {
                call->SetIntegerText(3, "18446744073709551616");
            },
            "wide integer overflow");

        Reject(
            [&] {
                call->SetNumberText(4, "nan");
            },
            "nonfinite argument refused");

        Initialize(*call);
        Reject(
            [&] {
                call->Execute(2);
            },
            "flags validated");

        Check(host.calls == 0,"invalid attempts never dispatch");
        call->Execute(0);
        Check(call->Read(0).Integer(0) == 8, "original result");
        Reject(
            [&] {
                call->SetInteger(2, 1);
            },
            "wrong setter type");

        Check(call->Read(0).Integer(0) == 8,"failed setter preserves result");
        call->SetInteger(1, 3);
        Reject(
            [&] {
                call->Read(0);
            },
            "successful setter invalidates result");

        call->Execute(1);
        Check(call->Read(0).Integer(0) == 90, "hook-chain selection");
        host.status = KEEL_RESULT_ENGINE_FAILURE;
        Reject(
            [&] {
                call->Execute(0);
            },
            "host error surfaced");

        Reject(
            [&] {
                call->Read(0);
            },
            "failure hides stale result");

        host.status = KEEL_RESULT_OK;
        host.malformed = true;
        Reject(
            [&] {
                call->Execute(0);
            },
            "host result type validated");

        host.malformed = false;
        call->Reset();
        Reject(
            [&] {
                call->Execute(0);
            },
            "reset clears initialized arguments");

        Initialize(*call);
        bool rejected = false;
        std::thread worker([&] {
            try {
                call->Execute(0);
            } catch (const Error&) {
                rejected = true;
            }
        });
        worker.join();
        Check(rejected,"worker rejected before touching call state");
        target.reset();
        alias.reset();
        service->Collect();
        Check(!host.releases, "prepared call retains target");
        host.during = [&] {
            Reject(
                [&] {
                    call->Execute(0);
                },
                "same call recursion rejected");

            Reject(
                [&] {
                    call->SetInteger(1, 3);
                },
                "busy write rejected");

            Reject(
                [&] {
                    call->Reset();
                },
                "busy reset rejected");

            Reject(
                [&] {
                    call->Read(0);
                },
                "busy result rejected");

            call.reset();
            service->Collect();
            Check(!host.releases, "active state survives resource close");
        };
        call->Execute(1);
        host.during = {};
        service->Collect();
        Check(host.releases == 1 && service->Empty(), "closed active call releases target after return");
        auto method = Example();
        method.method = true;
        method.arguments[0] = KH_VALUE_POINTER;
        auto method_target = service->Open(method);
        Reject(
            [&] {
                service->Prepare(*method_target);
            },
            "script method adapter not yet available");

        method_target.reset();
        service->Collect();
        Buffers(host,service);
        EntityCalls();
        std::cout << "SDKCall ownership, scalar and buffer preparation passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
