#include "reader.h"
#include <source2root/extension.hpp>
#include <source2root/work_queue.hpp>
#include <bit>
#include <utility>

namespace {
using namespace keels2::authoring;
using source2root::NativeCall;
namespace geo = source2root::geoip;

class GeoIP final : public source2root::Extension {
public:
    static constexpr PluginInfo Info{"Source2Root GeoIP", "KeelS2 Project", "1.0.0", "Local IPv4 and IPv6 geographic database lookups"};
    static constexpr PluginRequirement Requirements[]{{"Source2Root", "1.0.0", DependencyRequirement::exact}};
    GeoIP() : Extension("source2root.geoip") {}
    void OnGameFrame(bool, bool, bool) override { if (queue_) queue_->Dispatch(); }
private:
    static constexpr unsigned DatabaseType = 1, RecordType = 2;
    struct Database {
        GeoIP* extension;
        std::filesystem::path source, snapshots;
        std::shared_ptr<geo::Reader> reader;
        std::unique_ptr<source2root::WorkQueue::Ticket> ticket;
        SrCallback callback = 0;
        std::int32_t handle = 0;
        std::string error;
        ~Database() { ticket.reset(); if (callback) extension->CancelCallback(callback); }
    };
    using DatabaseRef = std::shared_ptr<Database>;
    std::vector<std::weak_ptr<Database>> databases_;
    std::unique_ptr<source2root::WorkQueue> queue_; // joins before captured state dies

    bool PrepareExtensionUnload() override {
        if (queue_) { queue_->Dispatch(); if (queue_->Pending()) return false; queue_.reset(); }
        return true;
    }
    bool OnExtensionStart() override {
        return RegisterNative("GeoIP_Open", 3, &GeoIP::Open)
            && RegisterNative("GeoIP_Close", 1, &GeoIP::Close)
            && RegisterNative("GeoIP_Reload", 3, &GeoIP::Reload)
            && RegisterNative("GeoIP_IsReady", 1, &GeoIP::Ready)
            && RegisterNative("GeoIP_IsLoading", 1, &GeoIP::Loading)
            && RegisterNative("GeoIP_Error", 3, &GeoIP::ErrorText)
            && RegisterNative("GeoIP_DatabaseType", 3, &GeoIP::Type)
            && RegisterNative("GeoIP_DatabaseTime", 3, &GeoIP::Epoch)
            && RegisterNative("GeoIP_Lookup", 3, &GeoIP::Lookup)
            && RegisterNative("GeoIP_CloseRecord", 1, &GeoIP::CloseRecord)
            && RegisterNative("GeoIP_Text", 4, &GeoIP::Text)
            && RegisterNative("GeoIP_Number", 3, &GeoIP::Number)
            && RegisterNative("GeoIP_ASN", 3, &GeoIP::ASN)
            && RegisterNative("GeoIP_Distance", 6, &GeoIP::Distance);
    }
    template <typename Function> std::int32_t Invoke(NativeCall& call, Function function) {
        try { return function(); }
        catch (const geo::Error& error) { return call.Fail(error.what()); }
        catch (const std::filesystem::filesystem_error&) { return call.Fail("GeoIP database directory is unavailable."); }
    }
    static DatabaseRef Db(NativeCall& call) { return call.Resource<DatabaseRef>(call.Int(1), DatabaseType); }
    static geo::Reader& Reader(const DatabaseRef& db) {
        if (!db->reader) throw geo::Error("GeoIP database is not ready.");
        return *db->reader;
    }
    void BeginLoad(NativeCall& call, const DatabaseRef& db) {
        if (db->ticket) throw geo::Error("GeoIP database already has a pending load or callback.");
        if (!queue_) queue_ = std::make_unique<source2root::WorkQueue>(1, 8);
        const auto data = call.Int(3);
        struct Result { std::shared_ptr<geo::Reader> reader; std::string error; bool applied = false; };
        auto result = std::make_shared<Result>();
        const auto source = db->source, snapshots = db->snapshots;
        const std::weak_ptr<Database> weak = db;
        const auto callback = call.Callback(2);
        try {
            db->ticket = queue_->Submit([source, snapshots, result](const auto& canceled) {
                result->reader = std::make_shared<geo::Reader>(source, snapshots, canceled);
            }, [this, weak, result, data](auto failure) {
                const auto db = weak.lock();
                if (!db) return true;
                if (!result->applied) {
                    try { if (failure) std::rethrow_exception(failure); }
                    catch (const std::filesystem::filesystem_error&) { result->error = "GeoIP database file is unavailable."; }
                    catch (const std::exception& error) { result->error = std::string(error.what()).substr(0, 1024); }
                    catch (...) { result->error = "GeoIP database load failed."; }
                    db->error = result->error;
                    if (!failure) db->reader = std::move(result->reader);
                    result->applied = true;
                }
                // Release the old pending state before invoking script code so
                // its callback can close the database or start another reload.
                auto ticket = std::move(db->ticket);
                const auto callback = std::exchange(db->callback, 0);
                const auto status = DeliverCallback(callback, {db->handle, data}, result->error.c_str());
                if (status == KEEL_RESULT_BUSY) {
                    db->ticket = std::move(ticket); db->callback = callback; return false;
                }
                return true;
            });
            if (!db->ticket) throw geo::Error("GeoIP worker queue is full.");
        } catch (...) { CancelCallback(callback); throw; }
        db->callback = callback; db->error.clear();
    }
    std::int32_t Open(NativeCall& call) {
        return Invoke(call, [&] {
            const auto name = call.String(1); geo::ValidateName(name);
            std::erase_if(databases_, [](const auto& db) { return db.expired(); });
            if (databases_.size() >= 8) throw geo::Error("GeoIP database handle limit (8) reached.");
            auto db = std::make_shared<Database>(); db->extension = this;
            const auto root = std::filesystem::path(call.DataPath(true));
            db->source = root / name; db->snapshots = root / ".snapshots";
            databases_.push_back(db);
            db->handle = call.Own(DatabaseType, std::make_unique<DatabaseRef>(db));
            try { BeginLoad(call, db); } catch (...) { call.Close(db->handle, DatabaseType); throw; }
            return db->handle;
        });
    }
    std::int32_t Close(NativeCall& call) { call.Close(call.Int(1), DatabaseType); return 1; }
    std::int32_t Reload(NativeCall& call) { return Invoke(call, [&] { BeginLoad(call, Db(call)); return 1; }); }
    std::int32_t Ready(NativeCall& call) { return Db(call)->reader ? 1 : 0; }
    std::int32_t Loading(NativeCall& call) { return Db(call)->ticket ? 1 : 0; }
    std::int32_t ErrorText(NativeCall& call) { call.Output(2, call.Int(3), Db(call)->error); return 1; }
    std::int32_t Type(NativeCall& call) {
        call.Output(2, call.Int(3), "");
        return Invoke(call, [&] { call.Output(2, call.Int(3), Reader(Db(call)).Type()); return 1; });
    }
    std::int32_t Epoch(NativeCall& call) {
        call.Output(2, call.Int(3), "");
        return Invoke(call, [&] { call.Output(2, call.Int(3), std::to_string(Reader(Db(call)).Epoch())); return 1; });
    }
    std::int32_t Lookup(NativeCall& call) {
        return Invoke(call, [&] {
            auto value = Reader(Db(call)).Lookup(call.String(2), call.String(3));
            return value ? call.Own(RecordType, std::make_unique<geo::Record>(std::move(*value))) : 0;
        });
    }
    std::int32_t CloseRecord(NativeCall& call) { call.Close(call.Int(1), RecordType); return 1; }
    std::int32_t Text(NativeCall& call) {
        call.Output(3, call.Int(4), "");
        return Invoke(call, [&] {
            const auto& record = call.Resource<geo::Record>(call.Int(1), RecordType);
            const auto field = call.Int(2);
            if (field < 0 || field >= static_cast<int>(geo::Field::Count)) throw geo::Error("Unknown GeoIP string field.");
            if (!record.text[field]) return 0;
            call.Output(3, call.Int(4), *record.text[field]); return 1;
        });
    }
    std::int32_t Number(NativeCall& call) {
        call.OutputCell(3, 0);
        return Invoke(call, [&] {
            const auto& record = call.Resource<geo::Record>(call.Int(1), RecordType);
            const auto field = call.Int(2);
            if (field < 0 || field >= static_cast<int>(geo::Number::Count)) throw geo::Error("Unknown GeoIP numeric field.");
            if (!record.numbers[field]) return 0;
            call.OutputCell(3, std::bit_cast<std::int32_t>(*record.numbers[field])); return 1;
        });
    }
    std::int32_t ASN(NativeCall& call) {
        call.Output(2, call.Int(3), "");
        const auto& record = call.Resource<geo::Record>(call.Int(1), RecordType);
        if (!record.asn) return 0;
        call.Output(2, call.Int(3), std::to_string(*record.asn)); return 1;
    }
    std::int32_t Distance(NativeCall& call) {
        call.OutputCell(6, 0);
        return Invoke(call, [&] {
            const auto unit = call.Int(5);
            if (unit != 0 && unit != 1) throw geo::Error("GeoIP distance unit must be kilometers (0) or miles (1).");
            const auto value = geo::Distance(call.Float(1), call.Float(2), call.Float(3), call.Float(4), unit == 1);
            call.OutputCell(6, std::bit_cast<std::int32_t>(static_cast<float>(value))); return 1;
        });
    }
};
}
KEELS2_PLUGIN(GeoIP)
