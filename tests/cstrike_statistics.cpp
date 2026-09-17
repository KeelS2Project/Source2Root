#include "statistics.h"
#include "players.h"
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <thread>

namespace {
using source2root::cstrike::Statistics;
using source2root::cstrike::Statistic;
void Check(bool result, const char* message) { if (!result) throw std::runtime_error(message); }
template <typename F> void Reject(F call) {
    bool failed = false;
    try { call(); } catch (const source2root::cstrike::Error&) { failed = true; }
    Check(failed, "statistics call should return a domain error");
}
struct Fixture {
    static Fixture* current;
    static constexpr KeelPluginHandle owner = 5;
    const std::thread::id thread = std::this_thread::get_id();
    std::uint64_t next = 1, epoch = 1, connection = 19;
    std::map<KeelEntityHandle,std::uint64_t> entities;
    std::map<KeelSchemaFieldHandle,std::string> fields;
    std::int32_t score = -3, mvps = 2;
    unsigned writes = 0, caps = 1, mutation = 0;
    bool missing_field = false, bad_type = false;
    KeelResult write_status = KEEL_RESULT_OK;
    std::function<void()> callback;
    Fixture() { current = this; }
    ~Fixture() { current = nullptr; }
    static KeelResult Thread(KeelPluginHandle plugin) {
        if (plugin != owner) return KEEL_RESULT_NOT_READY;
        return std::this_thread::get_id() == current->thread ? KEEL_RESULT_OK : KEEL_RESULT_WRONG_THREAD;
    }
    static KeelResult Player(KeelPluginHandle plugin, const KeelPlayerConnection* player, KeelPlayerInfo* info) {
        if (Thread(plugin)) return KEEL_RESULT_WRONG_THREAD;
        if (player->slot != 2 || player->generation != current->connection) return KEEL_RESULT_NOT_FOUND;
        *info = {}; info->size = sizeof(*info); info->slot = 2; info->connection = current->connection;
        info->flags = KEELS2_PLAYER_CONNECTED; info->controller_handle = 0x12003; info->pawn_handle = 0x23004;
        return KEEL_RESULT_OK;
    }
    static KeelResult Find(KeelPluginHandle plugin, unsigned source, KeelEntityHandle* out) {
        if (Thread(plugin)) return KEEL_RESULT_WRONG_THREAD;
        if (source != 0x12003) return KEEL_RESULT_NOT_FOUND;
        auto& f = *current; *out = f.next++; f.entities[*out] = f.epoch;
        if (f.mutation == 1) { ++f.connection; f.mutation = 0; }
        return KEEL_RESULT_OK;
    }
    static KeelResult ByIndex(KeelPluginHandle plugin, int index, KeelEntityHandle* out) {
        return Find(plugin,index == 3 ? 0x12003 : 0,out);
    }
    static KeelResult Release(KeelPluginHandle plugin, KeelEntityHandle entity) {
        return plugin == owner && current->entities.erase(entity) ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND;
    }
    static KeelResult Describe(KeelPluginHandle plugin, KeelEntityHandle entity, KeelEntityInfo* out) {
        const auto found = current->entities.find(entity);
        if (plugin != owner || found == current->entities.end() || found->second != current->epoch) return KEEL_RESULT_NOT_FOUND;
        *out = {sizeof(*out),3,0x12003,0,current->epoch}; return KEEL_RESULT_OK;
    }
    static KeelResult Equal(KeelPluginHandle plugin, KeelEntityHandle a, KeelEntityHandle b, KeelBool* out) {
        KeelEntityInfo x{},y{};
        if (Describe(plugin,a,&x) || Describe(plugin,b,&y)) return KEEL_RESULT_NOT_FOUND;
        *out = KEEL_TRUE; return KEEL_RESULT_OK;
    }
    static KeelResult Resolve(KeelPluginHandle plugin, const KeelSchemaFieldSpec* spec, KeelSchemaFieldHandle* out) {
        auto& f = *current;
        if (Thread(plugin)) return KEEL_RESULT_WRONG_THREAD;
        const std::string name = spec->field_name;
        if (f.missing_field || std::string(spec->class_name) != "CCSPlayerController" ||
            (name != "m_iScore" && name != "m_iMVPs")) return KEEL_RESULT_NOT_FOUND;
        if (spec->value_type != KEELS2_SCHEMA_INT32) return KEEL_RESULT_INCOMPATIBLE;
        *out = f.next++; f.fields[*out] = name; return KEEL_RESULT_OK;
    }
    static KeelResult ReleaseField(KeelPluginHandle plugin, KeelSchemaFieldHandle field) {
        return plugin == owner && current->fields.erase(field) ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND;
    }
    static KeelResult DescribeField(KeelPluginHandle plugin, KeelSchemaFieldHandle field, KeelSchemaFieldInfo* out) {
        const auto found = current->fields.find(field);
        if (plugin != owner || found == current->fields.end()) return KEEL_RESULT_NOT_FOUND;
        *out = {sizeof(*out),KEELS2_SCHEMA_MODULE_SERVER,KEELS2_SCHEMA_INT32,4,4,16,0,
            "CCSPlayerController",found->second.c_str(),"server","fixture"};
        if (current->bad_type) out->value_type = KEELS2_SCHEMA_UINT32;
        return KEEL_RESULT_OK;
    }
    static KeelResult Read(KeelPluginHandle plugin, KeelEntityHandle entity, KeelSchemaFieldHandle field, void* value, unsigned size) {
        KeelEntityInfo info{};
        if (Describe(plugin,entity,&info)) return KEEL_RESULT_NOT_FOUND;
        const auto found = current->fields.find(field);
        if (found == current->fields.end() || size != 4) return KEEL_RESULT_INCOMPATIBLE;
        const auto result = found->second == "m_iScore" ? current->score : current->mvps;
        std::memcpy(value,&result,4);
        if (current->mutation == 2) { ++current->epoch; current->mutation = 0; }
        return KEEL_RESULT_OK;
    }
    static KeelResult Caps(KeelPluginHandle plugin, unsigned* out) {
        if (Thread(plugin)) return KEEL_RESULT_WRONG_THREAD;
        *out = current->caps; return KEEL_RESULT_OK;
    }
    static KeelResult Write(KeelPluginHandle plugin, KeelEntityHandle entity, KeelSchemaFieldHandle field, const void* value, unsigned size) {
        KeelEntityInfo info{};
        if (Describe(plugin,entity,&info)) return KEEL_RESULT_NOT_FOUND;
        const auto found = current->fields.find(field);
        if (found == current->fields.end() || size != 4) return KEEL_RESULT_INCOMPATIBLE;
        auto& result = found->second == "m_iScore" ? current->score : current->mvps;
        std::memcpy(&result,value,4); ++current->writes;
        const auto callback = current->callback;
        if (callback) callback();
        return current->write_status;
    }
    static inline const KeelEntitiesApi entities_api{sizeof(KeelEntitiesApi),1,ByIndex,Find,Release,Describe,Equal,Read};
    static inline const KeelSchemaApi schema_api{sizeof(KeelSchemaApi),1,Resolve,ReleaseField,DescribeField};
    static inline const KeelPlayersApi players_api{sizeof(KeelPlayersApi),1,nullptr,nullptr,Player};
    static inline const KeelNativeRuntimeApi runtime_api{sizeof(KeelNativeRuntimeApi),1,Thread,nullptr,nullptr,nullptr};
    static inline const KeelEntityWritesApi writes_api{sizeof(KeelEntityWritesApi),1,Caps,Write};
    std::shared_ptr<source2root::sdktools::Service> Service(bool writable = true) {
        return std::make_shared<source2root::sdktools::Service>(owner,entities_api,schema_api,players_api,runtime_api,writable ? &writes_api : nullptr);
    }
    void Empty() const { Check(entities.empty() && fields.empty(), "statistics temporary resources released"); }
};
Fixture* Fixture::current = nullptr;
}
void RunStatisticsChecks() {
    Fixture fixture;
    const KeelPlayerConnection player{2,0,19};
    auto service = fixture.Service(); Statistics statistics(service);
    Check(statistics.Get(player,Statistic::score) == -3 && statistics.Get(player,Statistic::mvps) == 2, "controller field mapping");
    for (const auto value : {std::numeric_limits<std::int32_t>::min(),0,std::numeric_limits<std::int32_t>::max()}) {
        statistics.Set(player,Statistic::score,value);
        Check(statistics.Get(player,Statistic::score) == value && statistics.Get(player,Statistic::mvps) == 2, "score changes only score and preserves signed range");
    }
    for (const auto value : {0,std::numeric_limits<std::int32_t>::max()}) {
        statistics.Set(player,Statistic::mvps,value); Check(statistics.Get(player,Statistic::mvps) == value, "MVP boundaries");
    }
    fixture.Empty(); const auto written = fixture.writes;
    Reject([&] { statistics.Set(player,Statistic::mvps,-1); });
    Reject([&] { statistics.Set(player,static_cast<Statistic>(99),1); });
    Reject([&] { statistics.Get(player,static_cast<Statistic>(99)); });
    fixture.missing_field = true; Reject([&] { statistics.Get(player,Statistic::score); });
    Reject([&] { statistics.Set(player,Statistic::score,0); }); fixture.missing_field = false;
    fixture.bad_type = true; Reject([&] { statistics.Get(player,Statistic::score); }); fixture.bad_type = false;
    fixture.caps = 0; Reject([&] { statistics.Set(player,Statistic::score,0); }); fixture.caps = 1;
    Statistics readonly(fixture.Service(false)); Check(readonly.Get(player,Statistic::score) == fixture.score, "old host supports statistic reads");
    Reject([&] { readonly.Set(player,Statistic::score,0); });
    Reject([&] { statistics.Get({2,0,20},Statistic::score); });
    fixture.mutation = 1; Reject([&] { statistics.Set(player,Statistic::score,0); }); fixture.connection = 19;
    fixture.mutation = 2; Reject([&] { statistics.Get(player,Statistic::score); });
    Check(fixture.writes == written, "invalid statistics calls do not write"); fixture.Empty();
    fixture.write_status = KEEL_RESULT_ENGINE_FAILURE;
    Reject([&] { statistics.Set(player,Statistic::score,7); });
    Check(fixture.score == 7, "failure after mutation does not promise rollback"); fixture.write_status = KEEL_RESULT_OK;
    const auto before_recursion = fixture.writes;
    fixture.callback = [&] { statistics.Set(player,Statistic::score,8); };
    Reject([&] { statistics.Set(player,Statistic::score,8); }); fixture.callback = {};
    Check(fixture.writes == before_recursion + 8, "statistic notification recursion is bounded"); fixture.Empty();
    statistics.Set(player,Statistic::score,9);
    auto destroyable = std::make_unique<Statistics>(service);
    fixture.callback = [&] { destroyable.reset(); };
    destroyable->Set(player,Statistic::score,10); fixture.callback = {};
    Check(!destroyable && fixture.score == 10, "temporary resources retain service after facade destruction"); fixture.Empty();
    std::exception_ptr failure;
    std::thread worker([&] { try { Reject([&] { statistics.Get(player,Statistic::score); });
        Reject([&] { statistics.Set(player,Statistic::score,0); }); } catch (...) { failure = std::current_exception(); } });
    worker.join(); if (failure) std::rethrow_exception(failure);
    Reject([&] { Statistics invalid(nullptr); }); fixture.Empty();
}
