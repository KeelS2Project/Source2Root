#include "hooks.h"
#include <cstring>
#include <functional>
#include <map>
#include <thread>

using namespace source2root::dhooks;
namespace {
void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

template<class Function> void Reject(Function function, const char* message) {
    bool failed = false;

    try {
        function();
    } catch (const Error&) {
        failed = true;
    }

    Check(failed, message);
}

struct Host {
    static Host* active;
    std::thread::id thread = std::this_thread::get_id();
    std::map<KeelEntityHandle, KeelEntityInfo> leases;
    KeelEntityHandle next = 1;
    std::uint64_t epoch = 1, generation = 20;
    std::uint32_t controller = 0x1007, pawn = 0x1008;
    std::int32_t objects[2]{31,47};
    unsigned calls = 0, visits = 0, released = 0;
    bool connected = true, inconsistent = false, omit_callback = false;
    KeelResult call_status = KEEL_RESULT_OK;
    std::function<void()> during, during_find;
    Host() {
        active = this;
    }

    static KeelResult Thread(KeelPluginHandle owner) {
        return owner == 7 && active->thread == std::this_thread::get_id() ? KEEL_RESULT_OK : KEEL_RESULT_WRONG_THREAD;
    }

    static KeelResult Resolve(KeelPluginHandle,
                              const KeelHookTargetSpec* spec,
                              const KeelHookPrototype* prototype,
                              KeelHookTargetHandle* out) {
        Check(spec->flags == KH_TARGET_METHOD && prototype->argument_count == 4,"checked method prototype");
        *out = 9;
        return KEEL_RESULT_OK;
    }

    static KeelResult Release(KeelPluginHandle, KeelHookTargetHandle) {
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

    static KeelResult Find(KeelPluginHandle owner, std::uint32_t source, KeelEntityHandle* output) {
        Check(owner == 7, "entity provider owner");
        *output = 0;

        if (source != 0x1007 && source != 0x1008)
            return KEEL_RESULT_NOT_FOUND;

        *output = active->next++;
        active->leases[*output] = {
            sizeof(KeelEntityInfo), static_cast<std::int32_t>(source & 0xfff), source, 0, active->epoch};

        if (active->during_find)
            active->during_find();

        return KEEL_RESULT_OK;
    }

    static KeelResult Close(KeelPluginHandle owner, KeelEntityHandle handle) {
        Check(owner == 7 && active->leases.erase(handle) == 1,"exactly one entity release");
        ++active->released;
        return KEEL_RESULT_OK;
    }

    static KeelResult Describe(KeelPluginHandle, KeelEntityHandle handle, KeelEntityInfo* output) {
        const auto found = active->leases.find(handle);

        if (found == active->leases.end() || found->second.epoch != active->epoch)
            return KEEL_RESULT_NOT_FOUND;

        *output = found->second;

        if (active->inconsistent)
            ++output->source2_handle;

        return KEEL_RESULT_OK;
    }

    static KeelResult Player(KeelPluginHandle owner, const KeelPlayerConnection* connection, KeelPlayerInfo* output) {
        Check(owner == 7,"player provider owner");

        if (!active->connected || connection->slot != 3 || connection->generation != active->generation)
            return KEEL_RESULT_NOT_FOUND;

        *output = {};
        output->size = sizeof(*output);
        output->slot = 3;
        output->connection = active->generation;
        output->flags = KEELS2_PLAYER_CONNECTED;
        output->controller_handle = active->controller;
        output->pawn_handle = active->pawn;
        return KEEL_RESULT_OK;
    }

    static KeelResult Visit(KeelPluginHandle owner, const KeelEntityAccessSpec* requests, unsigned count,
        KeelEntityAccessCallback callback, void* data) {
        Check(owner == 7 && count == 1 && requests[0].size == sizeof(KeelEntityAccessSpec) && !requests[0].reserved,
              "entity visit shape");

        KeelEntityInfo info{};
        auto status = Describe(owner,requests[0].entity,&info);

        if (status != KEEL_RESULT_OK)
            return status;

        if (std::strcmp(requests[0].class_name, "CTestEntity"))
            return KEEL_RESULT_INCOMPATIBLE;

        ++active->visits;

        if (active->omit_callback)
            return KEEL_RESULT_OK;

        void* pointers[]{&active->objects[info.source2_handle == 0x1007 ? 0 : 1]};
        return callback(data,pointers,1);
    }

    static KeelResult Invoke(KeelPluginHandle owner, KeelHookTargetHandle target, unsigned flags,
        const KeelHookValue* values, unsigned count, KeelHookValue* output) {
        Check(owner == 7 && target == 9 && flags == 0 && count == 4,"entity calls only invoke original");
        Check(values[0].type == KH_VALUE_POINTER && values[1].type == KH_VALUE_INT32 &&
            values[2].type == KH_VALUE_POINTER && values[3].type == KH_VALUE_UINT32,"mixed method and buffer types");

        auto* object = static_cast<std::int32_t*>(values[0].scalar.pointer);
        auto* text = static_cast<char*>(values[2].scalar.pointer);
        Check((object == &active->objects[0] || object == &active->objects[1]) && text &&
            values[3].scalar.uint32 == 8 && std::strcmp(text,"hello") == 0,"entity and owned buffer pointers arrive privately");

        const auto retained = active->leases.size();
        ++active->calls;

        if (active->during)
            active->during();

        Check(active->leases.size() == retained,"native entity lease survives callback-triggered close");
        std::memcpy(text,"changed",8);
        *output = {};
        output->type = KH_VALUE_INT32;
        output->scalar.int32 = *object + values[1].scalar.int32;
        return active->call_status;
    }

    KeelHookApi hooks{
        sizeof(hooks), KEELHOOK_API_VERSION, Resolve, Release, Add, Remove, nullptr, nullptr, nullptr, Enable};

    KeelNativeRuntimeApi runtime{sizeof(runtime), KEELS2_NATIVE_RUNTIME_API_VERSION, Thread, nullptr, nullptr, nullptr};
    KeelCallApi calls_api{sizeof(calls_api), KEELCALL_API_VERSION, Invoke};
    KeelEntitiesApi entities{
        sizeof(entities), KEELS2_ENTITIES_API_VERSION, nullptr, Find, Close, Describe, nullptr, nullptr};

    KeelPlayersApi players{sizeof(players), KEELS2_PLAYERS_API_VERSION, nullptr, nullptr, Player};
    KeelEntityAccessApi access{sizeof(access), KEELS2_ENTITY_ACCESS_API_VERSION, Visit};
};
Host* Host::active = nullptr;
Definition Example() {
    Definition value;
    value.source = KH_TARGET_SYMBOL;
    value.module = "fixture";
    value.symbol = "Method";
    value.result = KH_VALUE_INT32;
    value.method = value.allow_calls = true;
    value.arguments = {KH_VALUE_POINTER,KH_VALUE_INT32,KH_VALUE_POINTER,KH_VALUE_UINT32};
    value.entities = {{1, "CTestEntity"}};
    value.buffers = {{3, 16, 4, BufferKind::string}};
    return value;
}

void Initialize(Call& call) {
    call.SetInteger(2, 11);
    call.SetString(3, "hello", 8);
}
}

void EntityCalls() {
    Host host;
    auto service = std::make_shared<Service>(7,host.hooks,host.runtime,&host.calls_api);
    auto target = service->Open(Example());
    Reject(
        [&] {
            service->Prepare(*target);
        },
        "older host refuses entity adapter without affecting scalar APIs");

    auto invalid_access = host.access;
    invalid_access.visit = nullptr;
    Reject(
        [&] {
            service->EntityServices(invalid_access, host.entities, host.players);
        },
        "bad entity service refused");

    service->EntityServices(host.access,host.entities,host.players);
    auto call = service->Prepare(*target);
    Initialize(*call);
    Reject(
        [&] {
            call->SetNull(1);
        },
        "configured entity cannot be nulled by scalar setter");

    Reject(
        [&] {
            call->SetEntityReference(2, 0x1007);
        },
        "scalar argument cannot accept entity");

    Reject(
        [&] {
            call->SetEntityReference(1, UINT32_MAX);
        },
        "invalid source reference refused");

    Reject(
        [&] {
            call->Execute(0);
        },
        "missing entity refused");

    call->SetEntityReference(1,0x1007);
    Check(!call->IsNull(1) && host.leases.size() == 1,"entity owns its native identity without script pointer exposure");
    Reject(
        [&] {
            call->Execute(1);
        },
        "entity pre-hook path refused before native dispatch");

    Check(!host.calls && !host.visits,"invalid hook flag has no engine side effect");
    call->Execute(0);
    Check(call->Read(0).Integer(0) == 42 && call->String(3) == "changed", "checked method and buffer copyback");
    Reject(
        [&] {
            call->SetEntityReference(1, 123);
        },
        "unknown source reference rejected");

    Check(call->Read(0).Integer(0) == 42 && host.leases.size() == 1,"failed entity setter preserves value and result");
    host.inconsistent = true;
    Reject(
        [&] {
            call->SetEntityReference(1, 0x1008);
        },
        "inconsistent acquired identity refused");

    Check(host.leases.size() == 1,"failed metadata lookup releases temporary lease");
    Reject(
        [&] {
            call->Execute(0);
        },
        "identity metadata drift refused");

    host.inconsistent = false;
    ++host.epoch;
    Reject(
        [&] {
            call->Execute(0);
        },
        "expired map epoch refused");

    call->SetEntityReference(1, 0x1008);
    Initialize(*call);
    call->Execute(0);
    Check(call->Read(0).Integer(0) == 58 && host.leases.size() == 1,"replacement releases old epoch lease");
    auto wrong = Example();
    wrong.entities[0].class_name = "OtherClass";
    auto alias = service->Open(wrong);
    auto wrong_call = service->Prepare(*alias);
    Initialize(*wrong_call);
    wrong_call->SetEntityReference(1, 0x1007);
    const auto before = host.calls;
    Reject(
        [&] {
            wrong_call->Execute(0);
        },
        "alias keeps its own exact class requirement");

    Check(host.calls == before, "wrong class never reaches original");
    wrong_call.reset();
    alias.reset();
    KeelPlayerConnection connection{3,0,20};
    call->SetPlayer(1, connection, false);
    Initialize(*call);
    call->Execute(0);
    Check(call->Read(0).Integer(0) == 42,"player controller method");
    ++host.generation;
    Reject(
        [&] {
            call->Execute(0);
        },
        "reconnect invalidates prepared player");

    connection.generation = host.generation;
    call->SetPlayer(1, connection, true);
    host.pawn = 0x1007;
    Reject(
        [&] {
            call->Execute(0);
        },
        "pawn changed within connection");

    host.pawn = 0x1008;
    call->SetPlayer(1, connection, true);
    host.controller = 0x1008;
    Reject(
        [&] {
            call->Execute(0);
        },
        "controller changed while pawn stayed the same");

    host.controller = 0x1007;
    host.during_find = [&] {
        host.pawn = 0x1007;
    };
    Reject(
        [&] {
            call->SetPlayer(1, connection, true);
        },
        "pawn changed during acquisition");

    host.during_find = {};
    host.pawn = 0x1008;
    Check(host.leases.size() == 1,"failed player assignment releases temporary identity");
    call->SetPlayer(1, connection, true);
    host.connected = false;
    Reject(
        [&] {
            call->Execute(0);
        },
        "disconnected player refused");

    host.connected = true;
    call->Reset();
    Check(host.leases.empty(), "reset releases retained entity");
    Reject(
        [&] {
            call->IsNull(1);
        },
        "reset invalidates entity getter");

    call->SetEntityReference(1, 0x1007);
    Initialize(*call);
    host.omit_callback = true;
    Reject(
        [&] {
            call->Execute(0);
        },
        "host cannot report success without invoking entity callback");

    host.omit_callback = false;
    host.call_status = KEEL_RESULT_ENGINE_FAILURE;
    Reject(
        [&] {
            call->Execute(0);
        },
        "native failure propagates through C callback");

    host.call_status = KEEL_RESULT_OK;
    Check(call->String(3) == "changed","failed native call may still change buffer");
    Initialize(*call);
    bool wrong_thread = false;
    std::thread worker([&] {
        try {
            call->SetEntityReference(1, 0x1008);
        } catch (const Error&) {
            wrong_thread = true;
        }
    });
    worker.join();
    Check(wrong_thread,"entity setter rejects worker thread");
    std::vector<std::unique_ptr<Call>> many;

    for (unsigned i = 0; i < 255; ++i) {
        auto item = service->Prepare(*target);
        item->SetEntityReference(1, 0x1007);
        many.push_back(std::move(item));
    }

    Reject(
        [&] {
            call->SetEntityReference(1, 0x1008);
        },
        "entity quota bounds replacement temporary lease");

    many.clear();
    Check(host.leases.size() == 1, "quota resources release");
    host.during = [&] {
        Reject(
            [&] {
                call->SetEntityReference(1, 0x1008);
            },
            "busy entity setter refused");

        Reject(
            [&] {
                call->SetPlayer(1, connection, false);
            },
            "busy player setter refused");

        Reject(
            [&] {
                call->Reset();
            },
            "busy reset refused");

        call.reset();
        target.reset();
        service->Collect();
        Check(!service->Empty(), "active entity call retains service and target");
    };
    call->Execute(0);
    host.during = {};
    service->Collect();
    Check(host.leases.empty() && service->Empty(), "callback-close releases all entity leases after native return");
}
