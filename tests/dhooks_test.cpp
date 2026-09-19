#include "hooks.h"
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <thread>

using namespace source2root::dhooks;
namespace {
void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

template<typename Function> void Reject(Function function, const char* message) {
    bool failed = false;

    try {
        function();
    } catch (const Error&) {
        failed = true;
    }

    Check(failed,message);
}

struct Host {
    static Host* active;
    std::thread::id thread = std::this_thread::get_id();
    std::map<KeelHookCallbackHandle,KeelHookCallbackSpec> callbacks;
    unsigned resolutions = 0, releases = 0, removals = 0;
    bool removal_failure = false, release_failure = false;
    std::map<std::string,KeelHookTargetHandle> targets;
    KeelHookTargetHandle next_target = 10;
    KeelHookCallbackHandle next = 1;
    Host() {
        active = this;
    }

    static KeelResult Thread(KeelPluginHandle owner) {
        return owner == 17 && std::this_thread::get_id() == active->thread ? KEEL_RESULT_OK : KEEL_RESULT_WRONG_THREAD;
    }

    static KeelResult Resolve(KeelPluginHandle owner,
                              const KeelHookTargetSpec* spec,
                              const KeelHookPrototype* prototype,
                              KeelHookTargetHandle* handle) {
        Check(owner == 17 && spec->size == sizeof(*spec) && prototype->size == sizeof(*prototype) &&
            spec->mechanism == KH_MECHANISM_DETOUR && prototype->calling_convention == KH_CALL_NATIVE &&
            prototype->fixed_argument_count == prototype->argument_count,"host resolver envelope");

        ++active->resolutions;
        const std::string symbol = spec->symbol ? spec->symbol : "";

        if (symbol == "Scalar")
            *handle = 9;
        else {
            const auto [at,inserted] = active->targets.emplace(symbol,active->next_target);

            if (inserted)
                ++active->next_target;

            *handle = at->second;
        }

        return KEEL_RESULT_OK;
    }

    static KeelResult Release(KeelPluginHandle owner, KeelHookTargetHandle handle) {
        Check(owner == 17 && handle >= 9,"release owner");

        if (active->release_failure || !active->callbacks.empty())
            return KEEL_RESULT_BUSY;

        ++active->releases;
        return KEEL_RESULT_OK;
    }

    static KeelResult Add(KeelPluginHandle owner,
                          KeelHookTargetHandle handle,
                          const KeelHookCallbackSpec* spec,
                          KeelHookCallbackHandle* id) {
        Check(owner == 17 && handle == 9 && spec->size == sizeof(*spec),"attach envelope");
        *id = active->next++;
        active->callbacks.emplace(*id, *spec);
        return KEEL_RESULT_OK;
    }

    static KeelResult Remove(KeelPluginHandle owner, KeelHookCallbackHandle id) {
        Check(owner == 17,"remove owner");

        if (active->removal_failure)
            return KEEL_RESULT_ENGINE_FAILURE;

        ++active->removals;
        return active->callbacks.erase(id) ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND;
    }

    static KeelResult Enable(KeelPluginHandle owner, KeelHookCallbackHandle id, KeelBool enabled) {
        Check(owner == 17 && active->callbacks.contains(id) && enabled <= 1, "enable envelope");
        return KEEL_RESULT_OK;
    }

    KeelHookApi hooks{
        sizeof(hooks), KEELHOOK_API_VERSION, Resolve, Release, Add, Remove, nullptr, nullptr, nullptr, Enable};

    KeelNativeRuntimeApi runtime{sizeof(runtime), KEELS2_NATIVE_RUNTIME_API_VERSION, Thread, nullptr, nullptr, nullptr};
    unsigned Fire(KeelHookFrame& frame, KeelHookCallbackHandle id = 1) {
        const auto callback = callbacks.at(id);
        return callback.callback(&frame, callback.user_data);
    }
};
Host* Host::active = nullptr;
Definition Example() {
    Definition result;
    result.source = KH_TARGET_SYMBOL;
    result.module = "fixture";
    result.symbol = "Scalar";
    result.result = KH_VALUE_INT32;
    result.arguments = {KH_VALUE_INT32,
                        KH_VALUE_FLOAT32,
                        KH_VALUE_UINT64,
                        KH_VALUE_FLOAT64,
                        KH_VALUE_POINTER,
                        KH_VALUE_POINTER,
                        KH_VALUE_INT8,
                        KH_VALUE_BOOL};

    return result;
}

struct Values {
    KeelHookValue values[8]{};
    KeelHookFrame frame{sizeof(frame), KH_PHASE_PRE, 9, 8, 0, values, {}};
    Values() {
        const auto types = Example().arguments;

        for (unsigned i = 0; i < 8; ++i)
            values[i].type = types[i];

        values[0].scalar.int32 = 7;
        values[1].scalar.float32 = 1.5f;
        values[2].scalar.uint64 = UINT64_MAX;
        values[3].scalar.float64 = 1.2345678901234567;
        values[4].scalar.pointer = this;
        values[5].scalar.pointer = nullptr;
        values[6].scalar.int8 = -8;
        values[7].scalar.boolean = KEEL_TRUE;
        frame.result.type = KH_VALUE_INT32;
        frame.result.scalar.int32 = 5;
    }

    bool Original() const {
        return values[0].scalar.int32 == 7 && values[1].scalar.float32 == 1.5f && frame.result.scalar.int32 == 5;
    }
};
void Definitions(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    const auto path = directory / "targets.json";
    const std::string valid =
        R"({"schema":1,"targets":{"scalar":{"allow_plugins":["first"],"source":"symbol","module":"fixture","symbol":"Scalar","return":"int32","arguments":["int32","float32"]}}})";

    auto save = [&](const std::string& text) {
        std::ofstream(path) << text;
    };
    save(valid);
    const auto value = ReadDefinition(path, "scalar", "first");
    Check(value.source == KH_TARGET_SYMBOL && value.arguments.size() == 2 && value.result == KH_VALUE_INT32,
          "typed config parses");

    Check(!value.allow_calls,"direct calls default to disabled");
    auto callable = valid;
    callable.insert(callable.find("\"allow_plugins\""), "\"allow_calls\":true,");
    save(callable);
    Check(ReadDefinition(path, "scalar", "first").allow_calls, "direct calls require explicit boolean permission");
    auto wrong_flag = valid;
    wrong_flag.insert(wrong_flag.find("\"allow_plugins\""), "\"allow_calls\":1,");
    save(wrong_flag);
    Reject(
        [&] {
            ReadDefinition(path, "scalar", "first");
        },
        "numeric direct-call flag refused");

    save(valid);
    Reject(
        [&] {
            ReadDefinition(path, "scalar", "second");
        },
        "plugin allowlist enforced");

    Reject(
        [&] {
            ReadDefinition(path, "missing", "first");
        },
        "missing target refused");

    for (const auto& text : {std::string(R"({"schema":1,"schema":1,"targets":{}})"),
         std::string(R"({"schema":1,"targets":{},"unknown":true})"),std::string(131073,' '),std::string("{}")}) {
        save(text);
        Reject(
            [&] {
                ReadDefinition(path, "scalar", "first");
            },
            "malformed catalog refused");
    }

    auto bad = Example();
    bad.arguments[0] = KH_VALUE_VOID;
    Reject(
        [&] {
            Validate(bad);
        },
        "void argument refused");

    bad = Example();
    bad.result = KH_VALUE_AGGREGATE;
    Reject(
        [&] {
            Validate(bad);
        },
        "aggregate requires future ABI bridge");

    bad = Example();
    bad.method = true;
    Reject(
        [&] {
            Validate(bad);
        },
        "method requires leading pointer");

    bad = Example();
    bad.module = std::string("bad\0module", 10);
    Reject(
        [&] {
            Validate(bad);
        },
        "embedded NUL refused");

    bad = Example();
    bad.source = KH_TARGET_PATTERN;
    bad.symbol.clear();
    bad.pattern = "AA BB";
    Reject(
        [&] {
            Validate(bad);
        },
        "pattern requires profile");

    bad.profile = "exact-profile";
    Validate(bad);
    const std::string buffers =
        R"({"schema":1,"targets":{"buffers":{"allow_calls":true,"allow_plugins":["first"],"source":"symbol","module":"fixture","symbol":"Buffers","return":"int32","arguments":["pointer","uint32","pointer","int32","pointer"],"buffers":[{"argument":1,"kind":"string","capacity":16,"length_argument":2},{"argument":3,"kind":"int32","capacity":4,"length_argument":4},{"argument":5,"kind":"vector3"}]}}})";

    save(buffers);
    auto configured = ReadDefinition(path, "buffers", "first");
    Check(configured.buffers.size()==3 && configured.buffers[0].capacity==16 &&
        configured.buffers[1].length_argument==4 && configured.buffers[2].kind==BufferKind::vector3 &&
        configured.buffers[2].capacity==3,"explicit buffer adapters parsed");

    auto invalid = configured;
    invalid.buffers.push_back(invalid.buffers[0]);
    Reject(
        [&] {
            Validate(invalid);
        },
        "duplicate pointer adapter rejected");

    invalid = configured;
    invalid.buffers[1].length_argument = 2;
    Reject(
        [&] {
            Validate(invalid);
        },
        "two buffers cannot share one length argument");

    invalid = configured;
    invalid.buffers[0].length_argument = 3;
    Reject(
        [&] {
            Validate(invalid);
        },
        "pointer slot cannot hold a length");

    invalid = configured;
    invalid.buffers[1].length_argument = 0;
    Reject(
        [&] {
            Validate(invalid);
        },
        "int array requires a checked length");

    invalid = configured;
    invalid.buffers[0].capacity = 4097;
    Reject(
        [&] {
            Validate(invalid);
        },
        "string allocation bound enforced");

    invalid = configured;
    invalid.buffers[1].capacity = 1025;
    Reject(
        [&] {
            Validate(invalid);
        },
        "array allocation bound enforced");

    invalid = configured;
    invalid.buffers[2].length_argument = 2;
    Reject(
        [&] {
            Validate(invalid);
        },
        "vector cannot set a count argument");

    invalid = configured;
    invalid.method = true;
    Reject(
        [&] {
            Validate(invalid);
        },
        "method object cannot be substituted by string memory");

    invalid = configured;
    invalid.arguments.assign(5, KH_VALUE_POINTER);
    invalid.buffers.clear();

    for (unsigned slot = 1; slot <= 5; ++slot)
        invalid.buffers.push_back({slot, 4096, 0, BufferKind::string});

    Reject(
        [&] {
            Validate(invalid);
        },
        "total buffer storage bound enforced");

    const std::string buffer_fields = "\"kind\":\"string\",\"capacity\":16";

    for (const auto& text : {std::string("\"kind\":\"float32\""),
                             std::string("\"kind\":1"),
                             std::string("\"kind\":\"string\",\"capacity\":0")}) {
        auto malformed = buffers;
        malformed.replace(malformed.find(buffer_fields),buffer_fields.size(),text);
        save(malformed);
        Reject(
            [&] {
                ReadDefinition(path, "buffers", "first");
            },
            "malformed buffer config rejected");
    }

    const std::string entity_config =
        R"({"schema":1,"targets":{"entity":{"allow_calls":true,"allow_plugins":["first"],"source":"symbol","module":"fixture","symbol":"Method","method":true,"return":"int32","arguments":["pointer","int32","pointer","uint32"],"entities":[{"argument":1,"class":"CTestEntity"}],"buffers":[{"argument":3,"kind":"string","capacity":16,"length_argument":4}]}}})";

    save(entity_config);
    const auto entity = ReadDefinition(path, "entity", "first");
    Check(entity.method && entity.entities.size()==1 && entity.entities[0].argument==1 &&
        entity.entities[0].class_name=="CTestEntity" && entity.buffers.size()==1,"checked method and buffer adapters parsed");

    invalid = entity;
    invalid.entities.push_back(invalid.entities[0]);
    Reject(
        [&] {
            Validate(invalid);
        },
        "duplicate entity argument rejected");

    invalid = entity;
    invalid.entities[0].argument = 2;
    Reject(
        [&] {
            Validate(invalid);
        },
        "entity needs pointer argument");

    invalid = entity;
    invalid.entities[0].argument = 3;
    Reject(
        [&] {
            Validate(invalid);
        },
        "entity and buffer may not share memory slot");

    for (const auto& name : {std::string(),std::string("bad class"),std::string(256,'x'),std::string("../CEntity")}) {
        invalid = entity;
        invalid.entities[0].class_name = name;
        Reject(
            [&] {
                Validate(invalid);
            },
            "invalid entity schema class rejected");
    }

    const std::string descriptor = R"({"argument":1,"class":"CTestEntity"})";

    for (const auto& replacement : {std::string(R"({"argument":0,"class":"CTestEntity"})"),
        std::string(R"({"argument":33,"class":"CTestEntity"})"),std::string(R"({"argument":1,"class":true})"),
        std::string(R"({"argument":1,"class":"CTestEntity","offset":4})")}) {
        auto bad_entity = entity_config;
        bad_entity.replace(bad_entity.find(descriptor),descriptor.size(),replacement);
        save(bad_entity);
        Reject(
            [&] {
                ReadDefinition(path, "entity", "first");
            },
            "malformed entity adapter rejected");
    }

    save(valid);
}
}

int main(int argc, char** argv) {
    try {
        Check(argc == 2, "dhooks_test fixture-directory");
        Definitions(argv[1]);
        Host host;
        auto service = std::make_shared<Service>(17,host.hooks,host.runtime);
        auto target = service->Open(Example());
        auto alias = service->Open(Example());
        Check(service->TargetCount() == 1 && host.resolutions == 2,"aliases share one provider target lease");
        target.reset();
        service->Collect();
        Check(!host.releases, "live alias keeps target");
        unsigned calls = 0, retired = 0, mode = 0;
        std::unique_ptr<Hook> hook;
        hook = service->Attach(
            *alias,
            KH_PHASE_BOTH,
            20,
            [&](Frame& frame) {
                ++calls;
                Check(frame.Count() == 8 && frame.Type(1) == KH_VALUE_INT32 && frame.Integer(1) == 7 &&
                          frame.Number(2) == 1.5f,
                      "scalar frame mapping");

                Check(frame.IntegerText(3) == "18446744073709551615" && frame.NumberText(4) == "1.2345678901234567",
                      "wide values retained exactly");

                Reject(
                    [&] {
                        frame.Integer(3);
                    },
                    "wide integer cannot truncate");

                Reject(
                    [&] {
                        frame.SetInteger(7, 128);
                    },
                    "int8 overflow refused");

                Reject(
                    [&] {
                        frame.SetInteger(8, 2);
                    },
                    "boolean overflow refused");

                Reject(
                    [&] {
                        frame.SetIntegerText(3, "18446744073709551616");
                    },
                    "uint64 overflow refused");

                Reject(
                    [&] {
                        frame.SetNumberText(4, "nan");
                    },
                    "nonfinite setter refused");

                Reject(
                    [&] {
                        frame.Type(9);
                    },
                    "slot bounds enforced");

                if (frame.Phase() == KH_PHASE_POST) {
                    Reject(
                        [&] {
                            frame.SetInteger(1, 8);
                        },
                        "post cannot change arguments");

                    frame.SetInteger(0, 90);
                    return mode == 4 ? 2 : 1;
                }

                Check(!frame.IsNull(5) && frame.IsNull(6), "pointer null access without exposing addresses");
                frame.Copy(6, 5);
                Check(!frame.IsNull(6), "same-frame pointer copy");
                frame.SetNull(6);
                Reject(
                    [&] {
                        frame.Copy(1, 5);
                    },
                    "different-type copy refused");

                frame.SetInteger(1, 30);
                frame.SetNumber(2, 2.5f);
                frame.SetInteger(0, 80);
                frame.SetIntegerText(3, "9223372036854775808");
                frame.SetNumberText(4, "2.2250738585072014e-308");

                if (mode == 1)
                    return -1;

                if (mode == 2)
                    throw std::runtime_error("callback failed");

                if (mode == 3) {
                    hook.reset();
                    alias.reset();
                    service->Collect();
                    return 2;
                }

                if (mode == 5)
                    return -2;

                return 1;
            },
            [&] {
                ++retired;
            });

        Values changed;
        Check(host.Fire(changed.frame) == KH_ACTION_OVERRIDE && changed.values[0].scalar.int32 == 30 &&
                  changed.values[1].scalar.float32 == 2.5f && changed.frame.result.scalar.int32 == 80 &&
                  changed.values[2].scalar.uint64 == 9223372036854775808ULL,
              "successful callback commits typed values");

        for (mode = 1; mode <= 2; ++mode) {
            Values value;
            Check(host.Fire(value.frame) == KH_ACTION_CONTINUE && value.Original(), "failed callback discards edits");
        }

        mode = 0;
        Values post;
        post.frame.phase = KH_PHASE_POST;
        Check(host.Fire(post.frame) == KH_ACTION_OVERRIDE && post.values[0].scalar.int32 == 7 &&
                  post.frame.result.scalar.int32 == 90,
              "post overrides return only");

        mode = 4;
        Values invalid_post;
        invalid_post.frame.phase = KH_PHASE_POST;
        Check(host.Fire(invalid_post.frame) == KH_ACTION_CONTINUE && invalid_post.Original(),
              "post supercede rejected atomically");

        for (unsigned fault = 0; fault < 5; ++fault) {
            Values invalid;
            const auto before_invalid = calls;

            if (fault == 0)
                invalid.frame.size = 0;

            if (fault == 1)
                invalid.frame.target = 77;

            if (fault == 2)
                invalid.frame.argument_count = 9;

            if (fault == 3)
                invalid.values[0].type = KH_VALUE_POINTER;

            if (fault == 4)
                invalid.frame.flags = 128;

            Check(host.Fire(invalid.frame) == KH_ACTION_CONTINUE && invalid.Original() && calls == before_invalid,
                  "malformed native frame bypasses script");
        }

        const auto before = calls;
        hook->Enable(false);
        Values disabled;
        Check(host.Fire(disabled.frame) == KH_ACTION_CONTINUE && calls == before && disabled.Original(),
              "disabled callback bypasses VM");

        hook->Enable(true);
        mode = 0;
        Values worker_values;
        bool wrong_thread = false;
        std::thread worker([&] {
            if (host.Fire(worker_values.frame) != KH_ACTION_CONTINUE)
                return;

            try {
                service->Open(Example());
            } catch (const Error&) {
                wrong_thread = true;
            }
        });
        worker.join();
        Check(wrong_thread && calls == before && worker_values.Original(),"worker dispatch has no script side effects");
        mode = 3;
        Values self_closed;
        Check(host.Fire(self_closed.frame) == KH_ACTION_SUPERSEDE && retired == 1 && !hook && !alias &&
                  !host.callbacks.empty(),
              "self-close retains active native storage");

        host.removal_failure = true;
        service->Collect();
        Check(!service->Empty() && !host.callbacks.empty(), "failed removal retains callback storage");
        Values canceled;
        Check(host.Fire(canceled.frame) == KH_ACTION_CONTINUE && canceled.Original(),
              "retiring callback inert while removal retries");

        host.removal_failure = false;
        host.release_failure = true;
        service->Collect();
        Check(host.callbacks.empty() && !service->Empty(),"target release failure retains provider");
        host.release_failure = false;
        service->Collect();
        Check(service->Empty() && host.releases == 1, "one release for alias leases after hook cleanup");
        target = service->Open(Example());
        auto retiring = service->Attach(
            *target,
            KH_PHASE_PRE,
            0,
            [](Frame& frame) {
                frame.SetInteger(1, 99);
                return -2;
            },
            [&] {
                ++retired;
            });

        Values expired;
        Check(host.Fire(expired.frame, 2) == KH_ACTION_CONTINUE && expired.Original() && !retiring->Active() &&
                  retired == 2,
              "expired script token retires hook without edits");

        retiring.reset();
        target.reset();
        service->Collect();
        Check(service->Empty(), "expired hook fully released");
        target = service->Open(Example());
        unsigned recursive = 0;
        auto nested = service->Attach(
            *target,
            KH_PHASE_PRE,
            0,
            [&](Frame&) {
                ++recursive;
                Values next;
                host.Fire(next.frame, 3);
                return 0;
            },
            [] {
            });

        Values recurse;
        host.Fire(recurse.frame, 3);
        Check(recursive == 8, "backend recursion bounded8");
        nested.reset();
        target.reset();
        service->Collect();
        Check(service->Empty(), "nested callback cleanup");
        target = service->Open(Example());
        std::vector<std::unique_ptr<Hook>> many;

        for (unsigned i = 0; i < 256; ++i)
            many.push_back(service->Attach(
                *target,
                KH_PHASE_PRE,
                0,
                [](Frame&) {
                    return 0;
                },
                [] {
                }));

        Reject(
            [&] {
                service->Attach(
                    *target,
                    KH_PHASE_PRE,
                    0,
                    [](Frame&) {
                        return 0;
                    },
                    [] {
                    });
            },
            "provider hook quota enforced");

        many.clear();
        target.reset();
        service->Collect();
        Check(service->Empty(), "quota cleanup releases all hooks");
        std::vector<std::unique_ptr<Target>> targets;

        for (unsigned i = 0; i < 64; ++i) {
            auto definition = Example();
            definition.symbol = "Target" + std::to_string(i);
            targets.push_back(service->Open(definition));
        }

        Reject(
            [&] {
                service->Open(Example());
            },
            "provider target quota enforced");

        targets.clear();
        service->Collect();
        Check(service->Empty(), "target quota recovered");
        auto bad = host.hooks;
        bad.size = 0;
        Reject(
            [&] {
                std::make_shared<Service>(17, bad, host.runtime);
            },
            "invalid service rejected");

        target = service->Open(Example());
        auto pending = service->Attach(
            *target,
            KH_PHASE_PRE,
            0,
            [](Frame&) {
                return 0;
            },
            [] {
            });

        std::weak_ptr<Service> retained = service;
        pending.reset();
        target.reset();
        service.reset();

        Check(!retained.expired(),"native cleanup storage survives facade destruction");
        {
            auto cleanup = retained.lock();
            cleanup->Collect();
            Check(cleanup->Empty(), "retained cleanup completes");
        }

        Check(retained.expired(),"native cleanup releases self-retention");
        std::cout << "DHooks backend fixtures passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
