#include "entities.h"
#include <bit>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <thread>
#include <vector>

using namespace source2root::sdktools;
static void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

template <typename Function> static void Reject(Function function, const char* message) {
    bool failed = false;

    try {
        function();
    } catch (const Error&) {
        failed = true;
    }

    Check(failed, message);
}

namespace {
struct Fixture {
    struct FieldRecord {
        unsigned type;
        std::string classname, name;
    };
    static Fixture* active;
    std::thread::id thread = std::this_thread::get_id();
    std::uint64_t next = 1, epoch = 1, connection = 9;
    std::uint32_t pawn = 0x23004;
    std::map<KeelEntityHandle, KeelEntityInfo> entities;
    std::map<KeelSchemaFieldHandle, FieldRecord> fields;
    unsigned mutation = 0, metadata_fault = 0, reads = 0, writes = 0, write_caps = 1;
    KeelResult write_status = KEEL_RESULT_OK, caps_status = KEEL_RESULT_OK;
    std::vector<std::byte> last_write;
    std::function<void()> on_write, on_tool, on_tool_caps, on_describe, on_input, on_input_caps;
    unsigned input_calls{}, input_direct{511}, input_queued{383};
    KeelBool input_invoked{KEEL_TRUE};
    KeelResult input_result{KEEL_RESULT_OK};
    KeelEntityInputRequest last_input{};
    std::string input_name, input_text;
    unsigned tool_calls = 0, tool_kind = 0, tool_caps = 7;
    std::set<KeelEntityHandle> pending;
    unsigned creates = 0, cancels = 0, keys = 0, spawns = 0, pending_teleports = 0;
    KeelResult construction_status = KEEL_RESULT_OK, spawn_status = KEEL_RESULT_OK;
    bool invoke_spawn = true, bad_created_metadata = false;
    std::function<void()> on_create, on_key, on_spawn, on_cancel, on_pending_teleport;
    KeelEntityKeyValue last_key{};
    std::string key_name, key_text;
    KeelResult tool_status = KEEL_RESULT_OK, tool_caps_status = KEEL_RESULT_OK;
    KeelEntityTeleport last_teleport{};
    std::string last_model;
    bool wrong_identity = false, bad_bool = false, nonfinite = false;
    KeelResult available = KEEL_RESULT_OK;
    std::string profile = "fixture-v1";
    Fixture() {
        active = this;
    }

    ~Fixture() {
        active = nullptr;
    }

    static unsigned Size(unsigned type) {
        switch (type) {
            case 1: case 2: case 3: case 12: return 1;
            case 4: case 5: return 2;
            case 6: case 7: case 10: case 13: return 4;
            case 8: case 9: case 11: return 8;
            case 14: return 12;
        }

        return 0;
    }

    static KeelResult Thread(KeelPluginHandle) {
        return std::this_thread::get_id() == active->thread ? KEEL_RESULT_OK : KEEL_RESULT_WRONG_THREAD;
    }

    static KeelResult Find(KeelPluginHandle, std::uint32_t source, KeelEntityHandle* output) {
        auto& s = *active;
        *output = 0;

        if (s.available != KEEL_RESULT_OK)
            return s.available;

        if (source != 0x12003 && source != 0x23004 && source != 0x45005)
            return KEEL_RESULT_NOT_FOUND;

        const auto id = s.next++;
        s.entities[id] = {sizeof(KeelEntityInfo),
                          source == 0x12003   ? 3
                          : source == 0x23004 ? 4
                                              : 5,
                          source,
                          0,
                          s.epoch};

        *output = id;

        if (s.mutation == 1)
            s.pawn = 0x24004;

        if (s.mutation == 2)
            ++s.connection;

        s.mutation = 0;
        return KEEL_RESULT_OK;
    }

    static KeelResult ByIndex(KeelPluginHandle owner, std::int32_t index, KeelEntityHandle* output) {
        return Find(owner, index == 3 ? 0x12003 : index == 4 ? 0x23004 : index == 5 ? 0x45005 : 0, output);
    }

    static KeelResult Release(KeelPluginHandle, KeelEntityHandle handle) {
        auto& s = *active;
        const bool existed = s.entities.erase(handle) != 0;

        if (s.pending.erase(handle)) {
            ++s.cancels;
            const auto callback = s.on_cancel;

            if (callback)
                callback();
        }

        return existed ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND;
    }

    static KeelResult Describe(KeelPluginHandle, KeelEntityHandle handle, KeelEntityInfo* info) {
        auto& s = *active;
        const auto callback = s.on_describe;

        if (callback)
            callback();

        if (s.available != KEEL_RESULT_OK)
            return s.available;

        const auto found = s.entities.find(handle);

        if (found == s.entities.end() || found->second.epoch != s.epoch || s.pending.contains(handle))
            return KEEL_RESULT_NOT_FOUND;

        *info = found->second;

        if (s.wrong_identity)
            ++info->source2_handle;

        return KEEL_RESULT_OK;
    }

    static KeelResult Equal(KeelPluginHandle owner, KeelEntityHandle left, KeelEntityHandle right, KeelBool* equal) {
        KeelEntityInfo a{}, b{};

        if (Describe(owner, left, &a) || Describe(owner, right, &b))
            return KEEL_RESULT_NOT_FOUND;

        *equal = a.source2_handle == b.source2_handle && a.epoch == b.epoch;
        return KEEL_RESULT_OK;
    }

    static KeelResult Resolve(KeelPluginHandle, const KeelSchemaFieldSpec* spec, KeelSchemaFieldHandle* handle) {
        auto& s = *active;

        if (std::string(spec->class_name) != "CTestEntity" || std::string(spec->field_name) != "value")
            return KEEL_RESULT_NOT_FOUND;

        *handle = s.next++;
        s.fields[*handle] = {spec->value_type, spec->class_name, spec->field_name};
        return KEEL_RESULT_OK;
    }

    static KeelResult ReleaseField(KeelPluginHandle, KeelSchemaFieldHandle handle) {
        return active->fields.erase(handle) ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND;
    }

    static KeelResult DescribeField(KeelPluginHandle, KeelSchemaFieldHandle handle, KeelSchemaFieldInfo* info) {
        auto& s = *active;
        const auto found = s.fields.find(handle);

        if (found == s.fields.end())
            return KEEL_RESULT_NOT_FOUND;

        const auto& value = found->second;
        const auto size = Size(value.type);
        *info = {sizeof(*info), KEELS2_SCHEMA_MODULE_SERVER, value.type, size, size == 12 ? 4u : size,
            16, 0, value.classname.c_str(), value.name.c_str(), "server", s.profile.c_str()};

        if (s.metadata_fault == 1)
            info->value_size = 1024;

        if (s.metadata_fault == 2)
            info->value_type = 999;

        if (s.metadata_fault == 3)
            info->value_alignment = 0;

        if (s.metadata_fault == 4)
            info->offset = -1;

        if (s.metadata_fault == 5)
            info->class_name = "OtherClass";

        if (s.metadata_fault == 6)
            info->field_name = nullptr;

        if (s.metadata_fault == 7)
            info->module_name = nullptr;

        if (s.metadata_fault == 8)
            info->compatibility_profile = "";

        return KEEL_RESULT_OK;
    }

    template <typename T> static void Store(void* output, T value) {
        std::memcpy(output, &value, sizeof(value));
    }

    static KeelResult
    Read(KeelPluginHandle owner, KeelEntityHandle entity, KeelSchemaFieldHandle field, void* output, unsigned size) {
        auto& s = *active;
        KeelEntityInfo info{};

        if (Describe(owner, entity, &info))
            return KEEL_RESULT_NOT_FOUND;

        const auto found = s.fields.find(field);

        if (found == s.fields.end() || size != Size(found->second.type))
            return KEEL_RESULT_INCOMPATIBLE;

        ++s.reads;

        switch (found->second.type) {
        case 1:
        case 3:
            Store(output, std::uint8_t{255});
            break;

        case 2:
            Store(output, std::int8_t{-128});
            break;

        case 4:
            Store(output, std::int16_t{-32768});
            break;

        case 5:
            Store(output, std::uint16_t{65535});
            break;

        case 6:
            Store(output, std::int32_t{-2147483647 - 1});
            break;

        case 7:
            Store(output, std::numeric_limits<std::uint32_t>::max());
            break;

        case 8:
            Store(output, std::numeric_limits<std::int64_t>::min());
            break;

        case 9:
            Store(output, std::numeric_limits<std::uint64_t>::max());
            break;

        case 10:
            Store(output, s.nonfinite ? std::numeric_limits<float>::infinity() : 1.25f);
            break;

        case 11:
            Store(output, s.nonfinite ? std::numeric_limits<double>::max() : 1.25);
            break;

        case 12:
            Store(output, static_cast<std::uint8_t>(s.bad_bool ? 2 : 1));
            break;

        case 13:
            Store(output, std::uint32_t{0x45005});
            break;

        case 14: {
            const float vector[]{1, 2, s.nonfinite ? std::numeric_limits<float>::quiet_NaN() : 3};
            std::memcpy(output, vector, sizeof(vector));
            break;
        }
        }

        if (s.mutation == 3) {
            ++s.epoch;
            s.mutation = 0;
        }

        return KEEL_RESULT_OK;
    }

    static KeelResult Player(KeelPluginHandle, const KeelPlayerConnection* expected, KeelPlayerInfo* info) {
        auto& s = *active;

        if (expected->slot != 3 || expected->generation != s.connection)
            return KEEL_RESULT_NOT_FOUND;

        *info = {};
        info->size = sizeof(*info);
        info->slot = 3;
        info->connection = s.connection;
        info->flags = KEELS2_PLAYER_CONNECTED;
        info->controller_handle = 0x12003;
        info->pawn_handle = s.pawn;
        return KEEL_RESULT_OK;
    }

    static KeelResult Capabilities(KeelPluginHandle owner, unsigned* capabilities) {
        if (Thread(owner) != KEEL_RESULT_OK)
            return KEEL_RESULT_WRONG_THREAD;

        *capabilities = active->write_caps;
        return active->caps_status;
    }

    static KeelResult Write(KeelPluginHandle owner, KeelEntityHandle entity, KeelSchemaFieldHandle field,
        const void* input, unsigned size) {
        auto& s = *active;
        KeelEntityInfo info{};

        if (Describe(owner, entity, &info))
            return KEEL_RESULT_NOT_FOUND;

        const auto found = s.fields.find(field);

        if (found == s.fields.end() || size != Size(found->second.type))
            return KEEL_RESULT_INCOMPATIBLE;

        ++s.writes;
        const auto* bytes = static_cast<const std::byte*>(input);
        s.last_write.assign(bytes, bytes + size);
        const auto callback = s.on_write;

        if (callback)
            callback();

        return s.write_status;
    }

    static KeelResult ToolCapabilities(KeelPluginHandle owner, unsigned* flags) {
        if (Thread(owner) != KEEL_RESULT_OK)
            return KEEL_RESULT_WRONG_THREAD;

        const auto callback = active->on_tool_caps;

        if (callback)
            callback();

        *flags = active->tool_caps;
        return active->tool_caps_status;
    }

    static KeelResult Tool(KeelPluginHandle owner, KeelEntityHandle entity, unsigned kind,
        const KeelEntityTeleport* request, const char* model) {
        auto& s = *active;
        KeelEntityInfo info{};

        if (Describe(owner, entity, &info))
            return KEEL_RESULT_NOT_FOUND;

        ++s.tool_calls;
        s.tool_kind = kind;
        const auto callback = s.on_tool;

        if (callback)
            callback();
        // Copy after callbacks, proving caller input storage remains valid.
        if (request)
            s.last_teleport = *request;

        if (model)
            s.last_model = model;

        if (kind == KEELS2_ENTITY_TOOL_REMOVE)
            s.entities.erase(entity);

        return s.tool_status;
    }

    static KeelResult ConstructionReady(KeelPluginHandle owner) {
        const auto thread = Thread(owner);
        return thread == KEEL_RESULT_OK ? active->construction_status : thread;
    }

    static KeelResult Create(KeelPluginHandle owner, const char* name, KeelEntityHandle* output) {
        auto& s = *active;
        *output = 0;
        const auto ready = ConstructionReady(owner);

        if (ready != KEEL_RESULT_OK)
            return ready;

        const std::string before = name;
        *output = s.next++;
        ++s.creates;
        s.entities[*output] = {sizeof(KeelEntityInfo), 6, 0x60006 + s.creates * 0x1000, 0, s.epoch};
        s.pending.insert(*output);
        const auto callback = s.on_create;

        if (callback)
            callback();

        Check(before == name, "factory input is copied before callbacks");
        return KEEL_RESULT_OK;
    }

    static KeelResult DescribePending(KeelPluginHandle, KeelEntityHandle handle, KeelEntityInfo* info) {
        auto& s = *active;
        const auto it = s.entities.find(handle);

        if (!s.pending.contains(handle) || it == s.entities.end() || it->second.epoch != s.epoch)
            return KEEL_RESULT_NOT_FOUND;

        *info = it->second;

        if (s.bad_created_metadata)
            info->size = 0;

        return KEEL_RESULT_OK;
    }

    static KeelResult SetKey(KeelPluginHandle owner, KeelEntityHandle handle, const KeelEntityKeyValue* value) {
        auto& s = *active;
        KeelEntityInfo info{};

        if (DescribePending(owner, handle, &info) != KEEL_RESULT_OK)
            return KEEL_RESULT_NOT_FOUND;

        ++s.keys;
        const auto callback = s.on_key;

        if (callback)
            callback();

        s.last_key = *value;
        s.key_name = value->name;
        s.key_text = value->string_value;
        return KEEL_RESULT_OK;
    }

    static KeelResult TeleportPending(KeelPluginHandle owner, KeelEntityHandle handle, const KeelEntityTeleport* value) {
        auto& s = *active;
        KeelEntityInfo info{};

        if (DescribePending(owner, handle, &info) != KEEL_RESULT_OK)
            return KEEL_RESULT_NOT_FOUND;

        ++s.pending_teleports;
        const auto callback = s.on_pending_teleport;

        if (callback)
            callback();

        s.last_teleport = *value;
        return KEEL_RESULT_OK;
    }

    static KeelResult Spawn(KeelPluginHandle owner, KeelEntityHandle handle, KeelBool* invoked) {
        auto& s = *active;
        *invoked = KEEL_FALSE;
        KeelEntityInfo info{};

        if (DescribePending(owner, handle, &info) != KEEL_RESULT_OK)
            return KEEL_RESULT_NOT_FOUND;

        if (!s.invoke_spawn)
            return s.spawn_status;

        ++s.spawns;
        *invoked = KEEL_TRUE;
        const auto callback = s.on_spawn;

        if (callback)
            callback();

        s.pending.erase(handle);

        if (s.spawn_status != KEEL_RESULT_OK)
            s.entities.erase(handle);

        return s.spawn_status;
    }

    static inline const KeelEntityConstructionApi construction_api{
        sizeof(KeelEntityConstructionApi),
        1,
        ConstructionReady,
        Create,
        DescribePending,
        SetKey,
        TeleportPending,
        Spawn,
        [](KeelPluginHandle, std::uint32_t, KeelEntityHandle*) {
            return KEEL_RESULT_UNSUPPORTED;
        },
        [](KeelPluginHandle, KeelEntityHandle, const char*, KeelEntityAccessCallback, void*) {
            return KEEL_RESULT_UNSUPPORTED;
        }};

    static KeelResult InputCapabilities(KeelPluginHandle, unsigned* direct, unsigned* queued) {
        const auto callback = active->on_input_caps;

        if (callback)
            callback();

        *direct = active->input_direct;
        *queued = active->input_queued;
        return active->input_result;
    }

    static KeelResult
    Input(KeelPluginHandle owner, KeelEntityHandle target, const KeelEntityInputRequest* request, KeelBool* invoked) {
        auto& s = *active;
        *invoked = KEEL_FALSE;

        for (const auto entity : {target,request->activator,request->caller,request->value_entity}) if (entity) {
                KeelEntityInfo info{};

                if (Describe(owner, entity, &info) != KEEL_RESULT_OK)
                    return KEEL_RESULT_NOT_FOUND;
        }

        if (request->queued && request->value.type == KEELS2_INPUT_COLOR)
            return KEEL_RESULT_UNSUPPORTED;

        ++s.input_calls;
        *invoked = s.input_invoked;
        const auto callback = s.on_input;

        if (callback)
            callback();

        s.last_input = *request;
        s.input_name = request->input;
        s.input_text = request->value.type == KEELS2_INPUT_STRING ? request->value.string_value : "";
        return s.input_result;
    }

    static inline const KeelEntityInputApi input_api{sizeof(KeelEntityInputApi), 1, InputCapabilities, Input};
    static inline const KeelEntityToolsApi tools_api{
        sizeof(KeelEntityToolsApi),
        1,
        ToolCapabilities,
        [](KeelPluginHandle p, KeelEntityHandle e, const KeelEntityTeleport* t) {
            return Tool(p, e, 1, t, nullptr);
        },
        [](KeelPluginHandle p, KeelEntityHandle e, const char* model) {
            return Tool(p, e, 2, nullptr, model);
        },
        [](KeelPluginHandle p, KeelEntityHandle e) {
            return Tool(p, e, 4, nullptr, nullptr);
        }};

    static inline const KeelEntityWritesApi writes_api{sizeof(KeelEntityWritesApi), 1, Capabilities, Write};
    static inline const KeelEntitiesApi entity_api{
        sizeof(KeelEntitiesApi), 1, ByIndex, Find, Release, Describe, Equal, Read};

    static inline const KeelSchemaApi schema_api{sizeof(KeelSchemaApi), 1, Resolve, ReleaseField, DescribeField};
    static inline const KeelPlayersApi player_api{sizeof(KeelPlayersApi), 1, nullptr, nullptr, Player};
    static inline const KeelNativeRuntimeApi runtime_api{
        sizeof(KeelNativeRuntimeApi), 1, Thread, nullptr, nullptr, nullptr};

    std::shared_ptr<Service> ServiceFor(std::uint64_t owner = 1, bool construction = false) {
        return std::make_shared<Service>(owner,
                                         entity_api,
                                         schema_api,
                                         player_api,
                                         runtime_api,
                                         &writes_api,
                                         &tools_api,
                                         construction ? &construction_api : nullptr,
                                         &input_api);
    }
};
Fixture* Fixture::active = nullptr;
#include "sdktools_input_fixture.h"
void Construction() {
    Fixture f;
    auto service = f.ServiceFor(1, true);
    service->ConstructionReady();
    auto legacy = f.ServiceFor();
    Reject(
        [&] {
            legacy->ConstructionReady();
        },
        "legacy readiness");

    Reject(
        [&] {
            legacy->Create("prop_dynamic");
        },
        "legacy create");

    Reject(
        [&] {
            service->Create("bad/class");
        },
        "invalid class before factory");

    std::string classname = "prop_dynamic";
    f.on_create = [&] {
        classname = "mutated";
    };
    auto entity = service->Create(classname);
    f.on_create = {};
    Check(entity->Valid() && entity->Pending() && entity->Same(*entity) && f.creates == 1,"owned pending identity");
    auto ordinary = service->Find(4);
    Check(!ordinary->Pending(), "ordinary entity is not pending");
    auto field = service->Resolve("CTestEntity","value",KEELS2_SCHEMA_INT32);
    Reject(
        [&] {
            entity->Integer(*field);
        },
        "pending schema read refused");

    Reject(
        [&] {
            entity->SetInteger(*field, 7);
        },
        "pending schema write refused");

    Reject(
        [&] {
            entity->SetModel("model.vmdl");
        },
        "pending live model operation refused");

    Reject(
        [&] {
            entity->Remove();
        },
        "pending live removal refused");

    char name[] = "model", text[] = "models/test.vmdl";
    KeelEntityKeyValue value{};
    value.size = sizeof(value);
    value.type = KEELS2_ENTITY_KEY_STRING;
    value.name = name;
    value.string_value = text;
    f.on_key = [&] {
        name[0] = text[0] = 'X';
    };
    entity->SetKey(value);
    f.on_key = {};
    Check(f.key_name == "model" && f.key_text == "models/test.vmdl","copied key text survives callback mutation");
    value.name = "sample";
    value.string_value = "";

    for (unsigned kind = 2; kind <= 7; ++kind) {
        value.type = kind;
        value.int_value = 1;
        value.float_value = 2.5f;
        value.vector_value[0] = 3;
        value.vector_value[1] = 4;
        value.vector_value[2] = 5;
        value.color_value[0] = 10;
        value.color_value[1] = 20;
        value.color_value[2] = 30;
        value.color_value[3] = 255;
        entity->SetKey(value);
        Check(f.last_key.type == kind && f.last_key.color_value[3] == 255, "typed key payload");
    }

    Check(f.keys == 7,"all seven key types");
    value.type = KEELS2_ENTITY_KEY_BOOL;
    value.int_value = 2;
    Reject(
        [&] {
            entity->SetKey(value);
        },
        "bad bool refused");

    value.type = KEELS2_ENTITY_KEY_FLOAT;
    value.float_value = std::numeric_limits<float>::infinity();
    Reject(
        [&] {
            entity->SetKey(value);
        },
        "bad float refused");

    value.type = KEELS2_ENTITY_KEY_VECTOR;
    value.vector_value[1] = std::numeric_limits<float>::quiet_NaN();
    Reject(
        [&] {
            entity->SetKey(value);
        },
        "bad vector refused");

    value.type = KEELS2_ENTITY_KEY_INT32;
    value.name = "sample";
    Reject(
        [&] {
            ordinary->SetKey(value);
        },
        "ordinary handle cannot stage keys");

    Check(f.keys == 7,"invalid keys have no host effects");
    std::array<float,3> position{1,2,3};
    f.on_pending_teleport = [&] {
        position[0] = 99;
    };
    entity->Teleport(1, position, {}, {});
    f.on_pending_teleport = {};
    Check(f.pending_teleports == 1 && !f.tool_calls && f.last_teleport.position[0] == 1,
          "pending teleport copies selected values");

    std::thread worker([&] {
        Reject(
            [&] {
                entity->Close();
            },
            "worker cannot cancel pending");
    });
    worker.join();
    Check(entity->Pending(),"failed thread check retains ownership");
    f.invoke_spawn = false;
    f.spawn_status = KEEL_RESULT_NOT_READY;
    bool invoked = true;
    Reject(
        [&] {
            entity->Spawn(invoked);
        },
        "noninvoked spawn failure");

    Check(!invoked && entity->Pending(),"noninvoked remains retryable");
    f.invoke_spawn = true;
    f.spawn_status = KEEL_RESULT_OK;
    entity->Spawn(invoked);
    Check(invoked && entity->Valid() && !entity->Pending(),"same handle transitions to live");
    Reject(
        [&] {
            entity->Spawn(invoked);
        },
        "spawn cannot repeat");

    Check(!invoked && f.spawns == 1, "repeat not invoked");
    Reject(
        [&] {
            entity->SetKey(value);
        },
        "consumed construction cannot stage keys");

    entity.reset();
    Check(f.cancels == 0, "live close drops only handle");
    entity = service->Create("prop_dynamic");
    f.spawn_status = KEEL_RESULT_ENGINE_FAILURE;
    Reject(
        [&] {
            entity->Spawn(invoked);
        },
        "invoked engine failure");

    Check(invoked && !entity->Valid(), "invoked marker survives failure");
    entity.reset();
    f.spawn_status = KEEL_RESULT_OK;
    f.bad_created_metadata = true;
    const auto before_metadata = f.cancels;
    Reject(
        [&] {
            service->Create("prop_dynamic");
        },
        "invalid factory metadata releases owner");

    Check(f.cancels == before_metadata + 1, "metadata failure cancels pending");
    f.bad_created_metadata = false;
    entity = service->Create("prop_dynamic");
    const auto before_close = f.cancels;
    f.on_cancel = [&] {
        entity.reset();
    };
    entity->Close();
    f.on_cancel = {};
    Check(!entity && f.cancels == before_close+1,"close may destroy its own Entity exactly once");
    entity = service->Create("prop_dynamic");
    f.on_key = [&] {
        entity.reset();
    };
    entity->SetKey(value);
    f.on_key = {};
    Check(!entity, "key callback may destroy target");
    entity = service->Create("prop_dynamic");
    f.on_pending_teleport = [&] {
        entity.reset();
    };
    entity->Teleport(1, {1, 2, 3}, {}, {});
    f.on_pending_teleport = {};
    Check(!entity, "teleport callback may destroy target");
    entity = service->Create("prop_dynamic");
    f.on_spawn = [&] {
        entity.reset();
        service.reset();
    };
    entity->Spawn(invoked);
    f.on_spawn = {};
    Check(invoked && !entity && !service, "spawn retains service after both owners close");
    ordinary.reset();
    field.reset();
    service = f.ServiceFor(1, true);
    entity = service->Create("prop_dynamic");
    ++f.epoch;
    Reject(
        [&] {
            entity->SetKey(value);
        },
        "map stale creation refused");

    entity.reset();
    std::vector<std::unique_ptr<Entity>> held;

    for (unsigned i = 0; i < 255; ++i)
        held.push_back(service->Find(4));

    f.on_create = [&] {
        Reject(
            [&] {
                service->Find(4);
            },
            "factory callback sees reserved provider slot");
    };
    entity = service->Create("prop_dynamic");
    f.on_create = {};
    Check(service->EntityCount() == 256, "creation quota reserved before callback");
    entity.reset();
    held.clear();
    unsigned depth{};
    f.on_create = [&] {
        ++depth;

        if (depth == 8)
            Reject(
                [&] {
                    service->Create("prop_dynamic");
                },
                "creation recursion limit");
        else
            held.push_back(service->Create("prop_dynamic"));

        --depth;
    };
    entity = service->Create("prop_dynamic");
    f.on_create = {};
    Check(held.size() == 7, "bounded nested factory callbacks");
    held.clear();
    entity.reset();
    Check(f.pending.empty() && f.entities.empty() && service->EntityCount() == 0,"construction resources cleaned");
}

}

int main() {
    try {
        Inputs();
        Construction();
        Fixture fixture;
        auto service = fixture.ServiceFor();
        auto resolve = [&](unsigned type) {
            return service->Resolve("CTestEntity", "value", type);
        };
        {
            auto entity = service->Find(4), same = service->FromSource(0x23004), other = service->Find(5);
            Check(entity->Valid() && entity->Same(*same) && !entity->Same(*other), "stable entity equality and lookup");
            Check(entity->Describe().source2_handle == 0x23004 && entity->Describe().index == 4, "checked identity metadata");
            const std::int32_t expected[]{255, -128, 255, -32768, 65535, -2147483647 - 1};

            for (unsigned type = 1; type <= 6; ++type) {
                auto field = resolve(type);
                Check(entity->Integer(*field) == expected[type - 1], "integer signedness and char byte values");
                Check(entity->IntegerText(*field) == std::to_string(expected[type-1]), "exact small integer text");
            }

            const char* wide[]{"4294967295", "-9223372036854775808", "18446744073709551615"};

            for (unsigned type = 7; type <= 9; ++type) {
                auto field = resolve(type);
                Reject(
                    [&] {
                        entity->Integer(*field);
                    },
                    "wide integer narrowing refused");

                Check(entity->IntegerText(*field) == wide[type-7], "full-width integer text preserved");
            }

            auto boolean = resolve(12);
            Check(entity->Integer(*boolean) == 1, "boolean read");
            fixture.bad_bool = true;
            Reject(
                [&] {
                    entity->Integer(*boolean);
                },
                "malformed bool refused without reading a C++ bool");

            fixture.bad_bool = false;

            for (auto type : {10u, 11u}) {
                auto field = resolve(type);
                Check(entity->Number(*field) == 1.25f, "typed float32/64 conversion");
                fixture.nonfinite = true;
                Reject(
                    [&] {
                        entity->Number(*field);
                    },
                    "nonfinite or overflowing floating value refused");

                fixture.nonfinite = false;
                Reject(
                    [&] {
                        entity->Integer(*field);
                    },
                    "float not coerced to integer");
            }

            auto vector = resolve(14);
            Check(entity->Vector(*vector) == std::array<float, 3>{1, 2, 3}, "vector components preserved");
            fixture.nonfinite = true;
            Reject(
                [&] {
                    entity->Vector(*vector);
                },
                "nonfinite vector refused");

            fixture.nonfinite = false;
            auto reference = resolve(13);
            Check(entity->SourceHandle(*reference) == 0x45005, "packed reference read");
            auto referenced = service->FromSource(entity->SourceHandle(*reference));
            Check(referenced->Same(*other), "referenced entity independently owned");
            Reject(
                [&] {
                    entity->Integer(*reference);
                },
                "entity references are not integer fields");

            Reject(
                [&] {
                    entity->Number(*boolean);
                },
                "boolean not coerced to float");

            const auto before = fixture.reads;
            Reject(
                [&] {
                    entity->Vector(*boolean);
                },
                "typed mismatch refused before host read");

            Check(fixture.reads == before, "wrong-size host read never attempted");
            auto foreign = fixture.ServiceFor(2)->Resolve("CTestEntity", "value", 6);
            Reject(
                [&] {
                    entity->Integer(*foreign);
                },
                "foreign provider field refused");

            auto meta = resolve(6);
            fixture.profile = "changed-profile";
            Check(meta->Profile() == "fixture-v1" && meta->ClassName() == "CTestEntity" && meta->Name() == "value" &&
                      meta->Size() == 4,
                  "field metadata is copied, not borrowed from mutable host strings");

            fixture.wrong_identity = true;
            Check(!entity->Valid(), "reused handle metadata cannot silently retarget");
            Reject(
                [&] {
                    entity->Integer(*meta);
                },
                "changed identity refused before read");

            fixture.wrong_identity = false;
            bool wrong_thread = false;
            std::thread worker([&] {
                try {
                    entity->Integer(*meta);
                } catch (const Error&) {
                    wrong_thread = true;
                }
            });
            worker.join();
            Check(wrong_thread, "worker-thread read refused");
            fixture.mutation = 3;
            Reject(
                [&] {
                    entity->Integer(*meta);
                },
                "entity invalidated during read returns no value");

            Check(!entity->Valid(), "map epoch invalidates retained handle");
            entity->Close();
            meta->Close();
        }

        Check(fixture.entities.empty() && fixture.fields.empty() && service->EntityCount() == 0 &&
                  service->FieldCount() == 0,
              "all native resources released");

        {
            auto pawn = service->FromPlayer({3, 0, 9}, true), controller = service->FromPlayer({3, 0, 9}, false);
            Check(pawn->Describe().index == 4 && controller->Describe().index == 3, "current player pawn/controller mapping");
            fixture.mutation = 1;
            Reject(
                [&] {
                    service->FromPlayer({3, 0, 9}, true);
                },
                "pawn replacement during lookup refused");

            fixture.pawn = 0x23004;
            fixture.mutation = 2;
            Reject(
                [&] {
                    service->FromPlayer({3, 0, 9}, true);
                },
                "connection replacement during lookup refused");

            fixture.connection = 9;
            Check(service->EntityCount() == 2 && fixture.entities.size() == 2, "failed player mapping releases acquired entity");
        }

        for (unsigned fault = 1; fault <= 8; ++fault) {
            fixture.metadata_fault = fault;
            Reject(
                [&] {
                    resolve(6);
                },
                "invalid metadata rejected");

            Check(fixture.fields.empty() && service->FieldCount() == 0, "invalid metadata cannot leak native handles");
        }

        fixture.metadata_fault = 0;
        Reject(
            [&] {
                service->Find(-1);
            },
            "negative entity index refused");

        Reject(
            [&] {
                service->FromSource(0xffffffff);
            },
            "invalid source handle refused");

        for (const auto* name : {"", "a.b", "pointer->field", "name[0]"})
            Reject(
                [&] {
                    service->Resolve(name, "value", 6);
                },
                "schema path syntax refused");

        Reject(
            [&] {
                resolve(0);
            },
            "unknown schema type refused");

        {
            std::vector<std::unique_ptr<Entity>> entities;

            for (unsigned i = 0; i < 256; ++i)
                entities.push_back(service->Find(4));

            Reject(
                [&] {
                    service->Find(4);
                },
                "entity provider quota enforced");

            entities.pop_back();
            entities.push_back(service->Find(4));
            std::vector<std::unique_ptr<Field>> fields;

            for (unsigned i = 0; i < 128; ++i)
                fields.push_back(resolve(6));

            Reject(
                [&] {
                    resolve(6);
                },
                "field provider quota enforced");

            fields.pop_back();
            fields.push_back(resolve(6));
        }

        Check(fixture.entities.empty() && fixture.fields.empty(), "quota resources release completely");
        {
            auto entity = service->Find(4);

            struct Boundary {
                unsigned type;
                const char* low;
                const char* high;
                const char* below;
                const char* above;
            };
            const Boundary boundaries[]{
                {1,"0","255","-1","256"},{2,"-128","127","-129","128"},{3,"0","255","-1","256"},
                {4,"-32768","32767","-32769","32768"},{5,"0","65535","-1","65536"},
                {6,"-2147483648","2147483647","-2147483649","2147483648"},
                {7,"0","4294967295","-1","4294967296"},
                {8,"-9223372036854775808","9223372036854775807","-9223372036854775809","9223372036854775808"},
                {9,"0","18446744073709551615","-1","18446744073709551616"},{12,"0","1","-1","2"}};

            for (const auto& boundary : boundaries) {
                auto field = resolve(boundary.type);
                const auto before = fixture.writes;
                entity->SetIntegerText(*field, boundary.low);
                entity->SetIntegerText(*field, boundary.high);
                Check(fixture.writes == before + 2 && fixture.last_write.size() == field->Size(),
                      "integer boundary writes preserve field width");

                Reject(
                    [&] {
                        entity->SetIntegerText(*field, boundary.below);
                    },
                    "integer lower overflow refused");

                Reject(
                    [&] {
                        entity->SetIntegerText(*field, boundary.above);
                    },
                    "integer upper overflow refused");

                Check(fixture.writes == before + 2, "range error never reaches host write");
            }

            auto field = resolve(9);
            entity->SetIntegerText(*field,"18446744073709551615");
            std::uint64_t wide{};
            std::memcpy(&wide, fixture.last_write.data(), sizeof(wide));
            Check(wide == UINT64_MAX,"full uint64 bits preserved");
            Reject(
                [&] {
                    entity->SetInteger(*field, -1);
                },
                "negative cell cannot silently wrap to unsigned64");

            for (const auto* invalid : {"","+1"," 1","1 ","0xff","1x","1.0","--1"})
                Reject(
                    [&] {
                        entity->SetIntegerText(*field, invalid);
                    },
                    "strict decimal text syntax");

            field = resolve(8);
            entity->SetIntegerText(*field, "-9223372036854775808");
            std::int64_t signed_wide{};
            std::memcpy(&signed_wide, fixture.last_write.data(), sizeof(signed_wide));
            Check(signed_wide == INT64_MIN,"full signed64 bits preserved");
            field = resolve(11);
            entity->SetNumber(*field, 3.25f);
            double number{};
            std::memcpy(&number, fixture.last_write.data(), sizeof(number));
            Check(number == 3.25, "float32 widened to float64");
            Reject(
                [&] {
                    entity->SetNumber(*field, std::numeric_limits<float>::infinity());
                },
                "infinite write refused");

            field = resolve(14);
            entity->SetVector(*field, {4, 5, 6});
            std::array<float, 3> vector{};
            std::memcpy(vector.data(), fixture.last_write.data(), sizeof(vector));
            Check(vector == std::array<float, 3>{4, 5, 6}, "vector layout preserved");
            Reject(
                [&] {
                    entity->SetVector(*field, {1, 2, std::numeric_limits<float>::quiet_NaN()});
                },
                "nonfinite vector write refused");

            field = resolve(13);
            Reject(
                [&] {
                    entity->SetInteger(*field, 123);
                },
                "integer writes cannot assign entity references");

            field = resolve(6);
            const auto before = fixture.writes;
            auto foreign = fixture.ServiceFor(2)->Resolve("CTestEntity","value",6);
            Reject(
                [&] {
                    entity->SetInteger(*foreign, 1);
                },
                "foreign field writes refused");

            fixture.wrong_identity = true;
            Reject(
                [&] {
                    entity->SetInteger(*field, 1);
                },
                "changed identity prevents write");

            fixture.wrong_identity = false;
            Check(fixture.writes == before,"identity failure does not reach write");
            fixture.write_caps = 0;
            Reject(
                [&] {
                    entity->SetInteger(*field, 1);
                },
                "unsupported capability prevents write");

            fixture.write_caps = 1;
            fixture.caps_status = KEEL_RESULT_UNSUPPORTED;
            Reject(
                [&] {
                    service->WriteCapabilities();
                },
                "unsupported binary capability error");

            fixture.caps_status = KEEL_RESULT_OK;
            std::exception_ptr worker_error;
            std::thread worker([&] {
                try {
                    Reject(
                        [&] {
                            entity->SetInteger(*field, 1);
                        },
                        "worker write refused");
                } catch (...) {
                    worker_error = std::current_exception();
                }
            });
            worker.join();

            if (worker_error)
                std::rethrow_exception(worker_error);

            const auto recursion_start = fixture.writes;
            fixture.on_write = [&] {
                entity->SetInteger(*field, 5);
            };
            Reject(
                [&] {
                    entity->SetInteger(*field, 4);
                },
                "recursive notification bounded");

            fixture.on_write = {};
            Check(fixture.writes == recursion_start + 8,"eight active writes allowed");
            entity->SetInteger(*field,3);
            // Host notification may close the very resources used by this call.
            fixture.on_write = [&] {
                entity.reset();
                field.reset();
            };
            fixture.write_status = KEEL_RESULT_ENGINE_FAILURE;
            Reject(
                [&] {
                    entity->SetInteger(*field, 2);
                },
                "notification failure propagates after resource destruction");

            fixture.on_write = {};
            fixture.write_status = KEEL_RESULT_OK;
            Check(!entity && !field,"notification destroyed borrowed Entity and Field safely");
        }

        {
            auto read_only = std::make_shared<Service>(
                1, Fixture::entity_api, Fixture::schema_api, Fixture::player_api, Fixture::runtime_api);

            auto entity = read_only->Find(4);
            auto field = read_only->Resolve("CTestEntity", "value", 6);
            Check(entity->Integer(*field) == INT32_MIN,"older host still supports reads");
            Reject(
                [&] {
                    entity->SetInteger(*field, 1);
                },
                "older host reports writes unavailable");
        }

        Check(fixture.entities.empty() && fixture.fields.empty() && service->EntityCount() == 0 &&
                  service->FieldCount() == 0,
              "write paths release every resource");

        {
            auto tools = fixture.ServiceFor();
            auto entity = tools->Find(4);
            std::array<float,3> position{1,2,3}, angles{4,5,6}, velocity{7,8,9};
            Check(tools->ToolCapabilities() == 7,"entity tool capabilities");

            for (unsigned flags = 1; flags <= 7; ++flags) {
                entity->Teleport(flags,position,angles,velocity);
                Check(fixture.last_teleport.size == 44 && fixture.last_teleport.flags == flags &&
                    fixture.last_teleport.position[2] == (flags & 1 ? 3 : 0) &&
                    fixture.last_teleport.angles[1] == (flags & 2 ? 5 : 0) &&
                    fixture.last_teleport.velocity[0] == (flags & 4 ? 7 : 0),"selected vectors copied, omitted vectors zero");
            }

            const auto before = fixture.tool_calls;

            for (unsigned flags : {0u, 8u, UINT32_MAX})
                Reject(
                    [&] {
                        entity->Teleport(flags, position, angles, velocity);
                    },
                    "bad teleport flags");

            position[0] = std::numeric_limits<float>::quiet_NaN();
            Reject(
                [&] {
                    entity->Teleport(1, position, angles, velocity);
                },
                "selected NaN refused");

            Check(fixture.tool_calls == before,"invalid vectors never invoke engine");
            entity->Teleport(4, position, angles, velocity);
            position[0] = 1;
            std::string model(511, 'a');
            entity->SetModel(model);
            Check(fixture.last_model == model, "maximum model asset length");

            for (const auto& invalid : {std::string{},
                                        std::string(512, 'b'),
                                        std::string("bad\nasset"),
                                        std::string("a\0b", 3),
                                        std::string("a\x7f")})
                Reject(
                    [&] {
                        entity->SetModel(invalid);
                    },
                    "invalid asset rejected");

            fixture.on_tool = [&] {
                position.fill(99);
                angles.fill(99);
                velocity.fill(99);
                model = "changed";
            };
            entity->Teleport(7,position,angles,velocity);
            Check(fixture.last_teleport.position[0] == 1 && fixture.last_teleport.angles[0] == 4 &&
                      fixture.last_teleport.velocity[0] == 7,
                  "teleport snapshot survives callback edits");

            model = "models/test.vmdl";
            entity->SetModel(model);
            Check(fixture.last_model == "models/test.vmdl" && model == "changed","asset snapshot survives callback edits");
            fixture.on_tool = {};
            fixture.wrong_identity = true;
            Reject(
                [&] {
                    entity->Remove();
                },
                "changed identity refused");

            fixture.wrong_identity = false;
            fixture.tool_caps = 0;
            Reject(
                [&] {
                    entity->Remove();
                },
                "unsupported tool refused");

            fixture.tool_caps = 7;
            fixture.tool_caps_status = KEEL_RESULT_UNSUPPORTED;
            Reject(
                [&] {
                    tools->ToolCapabilities();
                },
                "capability failure propagated");

            fixture.tool_caps_status = KEEL_RESULT_OK;
            std::exception_ptr worker_error;
            std::thread worker([&] {
                try {
                    Reject(
                        [&] {
                            entity->Teleport(7, position, angles, velocity);
                        },
                        "worker teleport refused");

                    Reject(
                        [&] {
                            entity->SetModel("test");
                        },
                        "worker model refused");

                    Reject(
                        [&] {
                            entity->Remove();
                        },
                        "worker remove refused");
                } catch (...) {
                    worker_error = std::current_exception();
                }
            });
            worker.join();

            if (worker_error)
                std::rethrow_exception(worker_error);

            const auto recursion_start = fixture.tool_calls;
            fixture.on_tool = [&] {
                entity->SetModel("reentry");
            };
            Reject(
                [&] {
                    entity->SetModel("initial");
                },
                "recursive entity game call bounded");

            fixture.on_tool = {};
            Check(fixture.tool_calls == recursion_start+8,"eight game calls permitted before recursion rejection");
            entity->Remove();
            Check(!entity->Valid(), "immediate removal may invalidate entity before return");
            entity->Close();
            Reject(
                [&] {
                    entity->Remove();
                },
                "closed entity refused");

            entity = tools->Find(4);
            ++fixture.epoch;
            Reject(
                [&] {
                    entity->SetModel("test");
                },
                "expired map identity refused");
        }

        for (unsigned kind : {1u,2u,4u}) {
            auto tools = fixture.ServiceFor();
            auto entity = tools->Find(4);
            std::weak_ptr<Service> weak = tools;
            fixture.on_tool = [&] {
                entity.reset();
                tools.reset();
            };
            fixture.tool_status = KEEL_RESULT_ENGINE_FAILURE;
            Reject(
                [&] {
                    if (kind == 1)
                        entity->Teleport(7, {1, 2, 3}, {4, 5, 6}, {7, 8, 9});
                    else if (kind == 2)
                        entity->SetModel("models/test.vmdl");
                    else
                        entity->Remove();
                },
                "engine failure after callback destroys entity and service owner");

            Check(!entity && !tools && weak.expired(),"operation holds service only until completion");
            fixture.on_tool = {};
            fixture.tool_status = KEEL_RESULT_OK;
        }

        {
            auto tools = fixture.ServiceFor();
            auto entity = tools->Find(4);
            std::weak_ptr<Service> weak = tools;
            const auto before = fixture.tool_calls;
            fixture.on_tool_caps = [&] {
                entity.reset();
                tools.reset();
            };
            Reject(
                [&] {
                    entity->Remove();
                },
                "entity closed during capability lookup is not invoked");

            fixture.on_tool_caps = {};
            Check(weak.expired() && fixture.tool_calls == before,"capability reentry cleanup retains no stale owner");
        }

        {
            auto legacy = std::make_shared<Service>(
                1, Fixture::entity_api, Fixture::schema_api, Fixture::player_api, Fixture::runtime_api);

            auto entity = legacy->Find(4);
            Reject(
                [&] {
                    legacy->ToolCapabilities();
                },
                "legacy host has no tools");

            Reject(
                [&] {
                    entity->Remove();
                },
                "legacy host cannot remove entities");

            auto bad = Fixture::tools_api;
            bad.remove = nullptr;
            Reject(
                [&] {
                    static_cast<void>(std::make_shared<Service>(1,
                                                                Fixture::entity_api,
                                                                Fixture::schema_api,
                                                                Fixture::player_api,
                                                                Fixture::runtime_api,
                                                                nullptr,
                                                                &bad));
                },
                "incomplete optional table refused");
        }

        Check(fixture.entities.empty() && fixture.fields.empty() && service->EntityCount() == 0,
              "entity game operations release all resources");

        std::cout << "Entity/schema identity, types, metadata, quotas and cleanup checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
