#include "entities.h"
#include <bit>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <thread>
#include <vector>

using namespace source2root::sdktools;
static void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template <typename Function> static void Reject(Function function, const char* message) {
    bool failed = false; try { function(); } catch (const Error&) { failed = true; }
    Check(failed, message);
}
namespace {
struct Fixture {
    struct FieldRecord { unsigned type; std::string classname, name; };
    static Fixture* active;
    std::thread::id thread = std::this_thread::get_id();
    std::uint64_t next = 1, epoch = 1, connection = 9;
    std::uint32_t pawn = 0x23004;
    std::map<KeelEntityHandle, KeelEntityInfo> entities;
    std::map<KeelSchemaFieldHandle, FieldRecord> fields;
    unsigned mutation = 0, metadata_fault = 0, reads = 0;
    bool wrong_identity = false, bad_bool = false, nonfinite = false;
    KeelResult available = KEEL_RESULT_OK;
    std::string profile = "fixture-v1";
    Fixture() { active = this; }
    ~Fixture() { active = nullptr; }
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
        auto& s = *active; *output = 0;
        if (s.available != KEEL_RESULT_OK) return s.available;
        if (source != 0x12003 && source != 0x23004 && source != 0x45005) return KEEL_RESULT_NOT_FOUND;
        const auto id = s.next++;
        s.entities[id] = {sizeof(KeelEntityInfo), source == 0x12003 ? 3 : source == 0x23004 ? 4 : 5, source, 0, s.epoch};
        *output = id;
        if (s.mutation == 1) s.pawn = 0x24004;
        if (s.mutation == 2) ++s.connection;
        s.mutation = 0;
        return KEEL_RESULT_OK;
    }
    static KeelResult ByIndex(KeelPluginHandle owner, std::int32_t index, KeelEntityHandle* output) {
        return Find(owner, index == 3 ? 0x12003 : index == 4 ? 0x23004 : index == 5 ? 0x45005 : 0, output);
    }
    static KeelResult Release(KeelPluginHandle, KeelEntityHandle handle) { return active->entities.erase(handle) ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND; }
    static KeelResult Describe(KeelPluginHandle, KeelEntityHandle handle, KeelEntityInfo* info) {
        auto& s = *active;
        if (s.available != KEEL_RESULT_OK) return s.available;
        const auto found = s.entities.find(handle);
        if (found == s.entities.end() || found->second.epoch != s.epoch) return KEEL_RESULT_NOT_FOUND;
        *info = found->second;
        if (s.wrong_identity) ++info->source2_handle;
        return KEEL_RESULT_OK;
    }
    static KeelResult Equal(KeelPluginHandle owner, KeelEntityHandle left, KeelEntityHandle right, KeelBool* equal) {
        KeelEntityInfo a{}, b{};
        if (Describe(owner, left, &a) || Describe(owner, right, &b)) return KEEL_RESULT_NOT_FOUND;
        *equal = a.source2_handle == b.source2_handle && a.epoch == b.epoch;
        return KEEL_RESULT_OK;
    }
    static KeelResult Resolve(KeelPluginHandle, const KeelSchemaFieldSpec* spec, KeelSchemaFieldHandle* handle) {
        auto& s = *active;
        if (std::string(spec->class_name) != "CTestEntity" || std::string(spec->field_name) != "value") return KEEL_RESULT_NOT_FOUND;
        *handle = s.next++; s.fields[*handle] = {spec->value_type, spec->class_name, spec->field_name}; return KEEL_RESULT_OK;
    }
    static KeelResult ReleaseField(KeelPluginHandle, KeelSchemaFieldHandle handle) { return active->fields.erase(handle) ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND; }
    static KeelResult DescribeField(KeelPluginHandle, KeelSchemaFieldHandle handle, KeelSchemaFieldInfo* info) {
        auto& s = *active; const auto found = s.fields.find(handle);
        if (found == s.fields.end()) return KEEL_RESULT_NOT_FOUND;
        const auto& value = found->second; const auto size = Size(value.type);
        *info = {sizeof(*info), KEELS2_SCHEMA_MODULE_SERVER, value.type, size, size == 12 ? 4u : size,
            16, 0, value.classname.c_str(), value.name.c_str(), "server", s.profile.c_str()};
        if (s.metadata_fault == 1) info->value_size = 1024;
        if (s.metadata_fault == 2) info->value_type = 999;
        if (s.metadata_fault == 3) info->value_alignment = 0;
        if (s.metadata_fault == 4) info->offset = -1;
        if (s.metadata_fault == 5) info->class_name = "OtherClass";
        if (s.metadata_fault == 6) info->field_name = nullptr;
        if (s.metadata_fault == 7) info->module_name = nullptr;
        if (s.metadata_fault == 8) info->compatibility_profile = "";
        return KEEL_RESULT_OK;
    }
    template <typename T> static void Store(void* output, T value) { std::memcpy(output, &value, sizeof(value)); }
    static KeelResult Read(KeelPluginHandle owner, KeelEntityHandle entity, KeelSchemaFieldHandle field, void* output, unsigned size) {
        auto& s = *active; KeelEntityInfo info{};
        if (Describe(owner, entity, &info)) return KEEL_RESULT_NOT_FOUND;
        const auto found = s.fields.find(field);
        if (found == s.fields.end() || size != Size(found->second.type)) return KEEL_RESULT_INCOMPATIBLE;
        ++s.reads;
        switch (found->second.type) {
            case 1: case 3: Store(output, std::uint8_t{255}); break;
            case 2: Store(output, std::int8_t{-128}); break;
            case 4: Store(output, std::int16_t{-32768}); break;
            case 5: Store(output, std::uint16_t{65535}); break;
            case 6: Store(output, std::int32_t{-2147483647 - 1}); break;
            case 7: Store(output, std::numeric_limits<std::uint32_t>::max()); break;
            case 8: Store(output, std::numeric_limits<std::int64_t>::min()); break;
            case 9: Store(output, std::numeric_limits<std::uint64_t>::max()); break;
            case 10: Store(output, s.nonfinite ? std::numeric_limits<float>::infinity() : 1.25f); break;
            case 11: Store(output, s.nonfinite ? std::numeric_limits<double>::max() : 1.25); break;
            case 12: Store(output, static_cast<std::uint8_t>(s.bad_bool ? 2 : 1)); break;
            case 13: Store(output, std::uint32_t{0x45005}); break;
            case 14: { const float vector[]{1, 2, s.nonfinite ? std::numeric_limits<float>::quiet_NaN() : 3}; std::memcpy(output, vector, sizeof(vector)); break; }
        }
        if (s.mutation == 3) { ++s.epoch; s.mutation = 0; }
        return KEEL_RESULT_OK;
    }
    static KeelResult Player(KeelPluginHandle, const KeelPlayerConnection* expected, KeelPlayerInfo* info) {
        auto& s = *active;
        if (expected->slot != 3 || expected->generation != s.connection) return KEEL_RESULT_NOT_FOUND;
        *info = {}; info->size = sizeof(*info); info->slot = 3; info->connection = s.connection;
        info->flags = KEELS2_PLAYER_CONNECTED; info->controller_handle = 0x12003; info->pawn_handle = s.pawn;
        return KEEL_RESULT_OK;
    }
    static inline const KeelEntitiesApi entity_api{sizeof(KeelEntitiesApi), 1, ByIndex, Find, Release, Describe, Equal, Read};
    static inline const KeelSchemaApi schema_api{sizeof(KeelSchemaApi), 1, Resolve, ReleaseField, DescribeField};
    static inline const KeelPlayersApi player_api{sizeof(KeelPlayersApi), 1, nullptr, nullptr, Player};
    static inline const KeelNativeRuntimeApi runtime_api{sizeof(KeelNativeRuntimeApi), 1, Thread, nullptr, nullptr, nullptr};
    std::shared_ptr<Service> ServiceFor(std::uint64_t owner = 1) { return std::make_shared<Service>(owner, entity_api, schema_api, player_api, runtime_api); }
};
Fixture* Fixture::active = nullptr;
}
int main() {
    try {
        Fixture fixture;
        auto service = fixture.ServiceFor();
        auto resolve = [&](unsigned type) { return service->Resolve("CTestEntity", "value", type); };
        {
            auto entity = service->Find(4), same = service->FromSource(0x23004), other = service->Find(5);
            Check(entity->Valid() && entity->Same(*same) && !entity->Same(*other), "stable entity equality and lookup");
            Check(entity->Describe().source2_handle == 0x23004 && entity->Describe().index == 4, "checked identity metadata");
            const std::int32_t expected[]{255, -128, 255, -32768, 65535, -2147483647 - 1};
            for (unsigned type = 1; type <= 6; ++type) {
                auto field = resolve(type); Check(entity->Integer(*field) == expected[type-1], "integer signedness and char byte values");
                Check(entity->IntegerText(*field) == std::to_string(expected[type-1]), "exact small integer text");
            }
            const char* wide[]{"4294967295", "-9223372036854775808", "18446744073709551615"};
            for (unsigned type = 7; type <= 9; ++type) {
                auto field = resolve(type); Reject([&] { entity->Integer(*field); }, "wide integer narrowing refused");
                Check(entity->IntegerText(*field) == wide[type-7], "full-width integer text preserved");
            }
            auto boolean = resolve(12); Check(entity->Integer(*boolean) == 1, "boolean read");
            fixture.bad_bool = true; Reject([&] { entity->Integer(*boolean); }, "malformed bool refused without reading a C++ bool"); fixture.bad_bool = false;
            for (auto type : {10u, 11u}) {
                auto field = resolve(type); Check(entity->Number(*field) == 1.25f, "typed float32/64 conversion");
                fixture.nonfinite = true; Reject([&] { entity->Number(*field); }, "nonfinite or overflowing floating value refused"); fixture.nonfinite = false;
                Reject([&] { entity->Integer(*field); }, "float not coerced to integer");
            }
            auto vector = resolve(14); Check(entity->Vector(*vector) == std::array<float,3>{1,2,3}, "vector components preserved");
            fixture.nonfinite = true; Reject([&] { entity->Vector(*vector); }, "nonfinite vector refused"); fixture.nonfinite = false;
            auto reference = resolve(13); Check(entity->SourceHandle(*reference) == 0x45005, "packed reference read");
            auto referenced = service->FromSource(entity->SourceHandle(*reference)); Check(referenced->Same(*other), "referenced entity independently owned");
            Reject([&] { entity->Integer(*reference); }, "entity references are not integer fields");
            Reject([&] { entity->Number(*boolean); }, "boolean not coerced to float");
            const auto before = fixture.reads;
            Reject([&] { entity->Vector(*boolean); }, "typed mismatch refused before host read"); Check(fixture.reads == before, "wrong-size host read never attempted");
            auto foreign = fixture.ServiceFor(2)->Resolve("CTestEntity", "value", 6);
            Reject([&] { entity->Integer(*foreign); }, "foreign provider field refused");
            auto meta = resolve(6); fixture.profile = "changed-profile";
            Check(meta->Profile() == "fixture-v1" && meta->ClassName() == "CTestEntity" && meta->Name() == "value" && meta->Size() == 4,
                "field metadata is copied, not borrowed from mutable host strings");
            fixture.wrong_identity = true; Check(!entity->Valid(), "reused handle metadata cannot silently retarget");
            Reject([&] { entity->Integer(*meta); }, "changed identity refused before read"); fixture.wrong_identity = false;
            bool wrong_thread = false;
            std::thread worker([&] { try { entity->Integer(*meta); } catch (const Error&) { wrong_thread = true; } }); worker.join();
            Check(wrong_thread, "worker-thread read refused");
            fixture.mutation = 3; Reject([&] { entity->Integer(*meta); }, "entity invalidated during read returns no value");
            Check(!entity->Valid(), "map epoch invalidates retained handle");
            entity->Close(); meta->Close();
        }
        Check(fixture.entities.empty() && fixture.fields.empty() && service->EntityCount() == 0 && service->FieldCount() == 0, "all native resources released");
        {
            auto pawn = service->FromPlayer({3,0,9}, true), controller = service->FromPlayer({3,0,9}, false);
            Check(pawn->Describe().index == 4 && controller->Describe().index == 3, "current player pawn/controller mapping");
            fixture.mutation = 1; Reject([&] { service->FromPlayer({3,0,9}, true); }, "pawn replacement during lookup refused"); fixture.pawn = 0x23004;
            fixture.mutation = 2; Reject([&] { service->FromPlayer({3,0,9}, true); }, "connection replacement during lookup refused"); fixture.connection = 9;
            Check(service->EntityCount() == 2 && fixture.entities.size() == 2, "failed player mapping releases acquired entity");
        }
        for (unsigned fault = 1; fault <= 8; ++fault) {
            fixture.metadata_fault = fault; Reject([&] { resolve(6); }, "invalid metadata rejected");
            Check(fixture.fields.empty() && service->FieldCount() == 0, "invalid metadata cannot leak native handles");
        }
        fixture.metadata_fault = 0;
        Reject([&] { service->Find(-1); }, "negative entity index refused");
        Reject([&] { service->FromSource(0xffffffff); }, "invalid source handle refused");
        for (const auto* name : {"", "a.b", "pointer->field", "name[0]"}) Reject([&] { service->Resolve(name, "value", 6); }, "schema path syntax refused");
        Reject([&] { resolve(0); }, "unknown schema type refused");
        {
            std::vector<std::unique_ptr<Entity>> entities;
            for (unsigned i = 0; i < 256; ++i) entities.push_back(service->Find(4));
            Reject([&] { service->Find(4); }, "entity provider quota enforced");
            entities.pop_back(); entities.push_back(service->Find(4));
            std::vector<std::unique_ptr<Field>> fields;
            for (unsigned i = 0; i < 128; ++i) fields.push_back(resolve(6));
            Reject([&] { resolve(6); }, "field provider quota enforced");
            fields.pop_back(); fields.push_back(resolve(6));
        }
        Check(fixture.entities.empty() && fixture.fields.empty(), "quota resources release completely");
        std::cout << "Entity/schema identity, types, metadata, quotas and cleanup checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
