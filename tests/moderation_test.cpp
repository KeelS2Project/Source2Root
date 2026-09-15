#include "foundation.h"
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>

static void Require(bool value, const char* text) { if (!value) { std::cerr << text << '\n'; std::exit(1); } }
class Host final : public sr::GameHost {
public:
    struct Message { int slot; std::string text; };
    std::map<int, sr::Player> players;
    std::vector<Message> messages, kicks;
    std::vector<std::string> logs;
    std::map<int, unsigned> attempts;
    std::function<void(const sr::Player&)> on_kick;
    bool immediate = true;
    int fail_slot = -2;
    sr::Timestamp now{};
    std::string menu;
    KeelResult Lookup(int slot, sr::Player& out) override { if (!players.contains(slot)) return KEEL_RESULT_NOT_FOUND; out = players.at(slot); return KEEL_RESULT_OK; }
    KeelResult NextPlayer(int after, sr::Player& out) override { auto it = players.upper_bound(after); if (it == players.end()) return KEEL_RESULT_NOT_FOUND; out = it->second; return KEEL_RESULT_OK; }
    sr::Timestamp UtcNow() const override { return now; }
    KeelResult Reply(const sr::Player* player, const std::string& text) override {
        Require(!player || (players.contains(player->slot) && player->SameConnection(players.at(player->slot))), "reply may not follow a replacement connection");
        messages.push_back({player ? player->slot : -1, text}); return KEEL_RESULT_OK;
    }
    KeelResult KickPlayer(const sr::Player& player, const std::string& reason) override {
        Require(players.contains(player.slot) && player.SameConnection(players.at(player.slot)), "disconnect requires current connection");
        ++attempts[player.slot]; if (player.slot == fail_slot) return KEEL_RESULT_ENGINE_FAILURE;
        kicks.push_back({player.slot, reason});
        if (immediate) { players.erase(player.slot); if (on_kick) on_kick(player); }
        return KEEL_RESULT_OK;
    }
    void Log(const std::string& text) override { logs.push_back(text); std::cout << text << '\n'; Require(text.find("FAILED:") == std::string::npos, text.c_str()); }
    KeelResult RegisterCommand(const std::string&) override { return KEEL_RESULT_OK; }
    KeelResult RemoveCommand(const std::string&) override { return KEEL_RESULT_OK; }
    KeelResult ListenEvent(const std::string&) override { return KEEL_RESULT_OK; }
    KeelResult RemoveEvent(const std::string&) override { return KEEL_RESULT_OK; }
    KeelResult RenderMenu(const sr::Player&, const std::string& html, int) override { menu = html; return KEEL_RESULT_OK; }
    KeelResult AcquireProvider(const std::string&, unsigned) override { return KEEL_RESULT_UNSUPPORTED; }
    KeelResult ReleaseProvider(const std::string&, unsigned) override { return KEEL_RESULT_UNSUPPORTED; }
    unsigned Count(int slot) const { unsigned result = 0; for (const auto& message : messages) result += message.slot == slot; return result; }
    bool Has(int slot, const std::string& part) const { for (const auto& message : messages) if (message.slot == slot && message.text.find(part) != std::string::npos) return true; return false; }
    void Clear() { messages.clear(); kicks.clear(); logs.clear(); attempts.clear(); }
};
int main(int argc, char** argv) {
    Require(argc == 4, "moderation_test runtime scripts root");
    const std::filesystem::path root(argv[3]), scripts(argv[2]);
    std::filesystem::remove_all(root); std::filesystem::create_directories(root / "configs");
    const auto admin_file = root / "configs/admins.cfg";
    const std::string assignments = R"("Admins" { "Root" { "identity" "[U:1:123]" "group" "root" }
        "Mod" { "identity" "[U:1:124]" "group" "moderator" } "Equal" { "identity" "[U:1:125]" "group" "root" } })";
    std::ofstream(admin_file) << assignments;
    std::ofstream(root / "configs/admin_groups.cfg") << R"("Groups" { "root" { "immunity" "100" }
        "moderator" { "immunity" "10" "permissions" { "admin.help" "1" "admin.menu" "1" "admin.kick" "1" "admin.ban" "1" "admin.unban" "1" } } })";
    auto install = [&](const char* id) {
        auto directory = root / "plugins" / id; std::filesystem::create_directories(directory);
        std::filesystem::copy_file(scripts / (std::string(id) + ".smx"), directory / "main.smx", std::filesystem::copy_options::overwrite_existing);
        std::ofstream(directory / "plugin.json") << "{\"schema\":1,\"id\":\"" << id << "\",\"name\":\"" << id << "\",\"author\":\"tests\",\"version\":\"1.0.0\",\"api\":2,\"entry\":\"main.smx\",\"enabled\":true,\"dependencies\":[]}";
        return directory / "plugin.json";
    };
    Host host; std::string error;
    Require(sr::ParseUtc("2026-09-15T12:00:00Z", host.now, error), "fixed wall clock");
    std::map<int, sr::Player> initial = {
        {3,{3,13,76561197960265851ULL,true,false,"Peter Brev",70,3,true}},
        {4,{4,14,76561197960265852ULL,true,false,"Alex",71,2,true}},
        {5,{5,15,76561197960265853ULL,true,false,"Alexandra",72,3,true}},
        {6,{6,16,76561197960265854ULL,true,false,"Kiddo",73,2,false}},
        {7,{7,17,0,false,true,"Bot",74,2,true}}};
    unsigned generation = 100;
    auto restore = [&] { host.players = initial; for (auto& [slot, player] : host.players) player.connection = ++generation; };
    restore();
    auto app = std::make_unique<sr::Foundation>(host, argv[1], root);
    host.on_kick = [&](const sr::Player& player) { app->Disconnected(player.slot, player.connection); };
    auto load = [&] { Require(app->Load(install("admin")) && app->Load(install("moderation")), "load actual moderation script"); };
    load();
    auto run = [&](int slot, const std::string& text, sr::Origin origin = sr::Origin::ClientConsole) {
        host.Clear(); app->Dispatch(slot == -1 ? sr::Origin::ServerConsole : origin, slot, text);
    };
    auto single = [&](int slot, const char* text) { Require(host.Count(slot) == 1 && host.Has(slot, text), "one issuer result with expected content"); };
    auto input = [&](sr::MenuInput value) { return app->MenuInput(host.players.at(3), app->CurrentMenu(host.players.at(3)), value); };
    auto select = [&](unsigned index) { for (unsigned i=0;i<index;++i) Require(input(sr::MenuInput::Down), "menu navigation"); Require(input(sr::MenuInput::Select), "menu selection"); };
    auto tick = [&](unsigned seconds = 1) { host.now += std::chrono::seconds(seconds); app->Tick(sr::Foundation::Clock::now() + std::chrono::seconds((host.now.time_since_epoch().count() % 1000000))); };
    Require(app->Load(install("moderation_api")), "startup mutation guards and API fixture");
    run(3,"sr_banapi"); single(3,"moderation API bounds passed");
    run(3,"sr_banapi timer"); Require(!host.logs.empty() && host.logs.front().find("timers across maps require NoPlayer") != std::string::npos,"player-targeted persistent timers are rejected");
    run(3,"sr_banapi kick"); Require(host.kicks.size()==1 && host.kicks[0].text=="One self-kick result" && host.Count(3)==0,"public self-disconnect receipt");
    run(-1,"sr_banapi stored"); single(-1,"stored kick receipt refused"); Require(host.messages.size()==1,"a later callback cannot impersonate a disconnected actor");
    Require(app->Unload("moderation_api"), "retiring mutation guards"); restore();
    run(3,"sr_help"); Require(host.Has(3,"sr_ban <target-or-SteamID> <minutes> [reason]") && host.Has(3,"sr_unban <SteamID>"), "help includes running moderation usage");
    for (auto origin : {sr::Origin::ClientConsole,sr::Origin::PublicChat,sr::Origin::SilentChat}) {
        run(6,origin == sr::Origin::ClientConsole ? "sr_kick @me" : origin == sr::Origin::PublicChat ? "!kick @me" : "/kick @me",origin);
        single(6,"You do not have access"); Require(host.kicks.empty(), "consistent command permissions");
    }
    for (const auto& command : {"sr_kick", "sr_ban #73", "sr_ban #73 -1", "sr_ban #73 5256001", "sr_ban @all 1", "sr_ban @bots 1", "sr_ban #74 1", "sr_unban bad", "sr_unban [U:1:123] extra", "sr_kick Al", "sr_kick #73 \"\"", "sr_ban #73 1 \"\""}) {
        run(3,command); Require(host.Count(3)==1 && host.kicks.empty(),"bad command arguments fail privately");
    }
    run(3,"sr_kick #72"); single(3,"immunity"); Require(host.kicks.empty(),"root cannot kick equal root");
    run(4,"sr_ban [U:1:123] 0"); single(4,"immunity"); Require(host.kicks.empty(),"offline identity uses strict immunity");
    run(3,"sr_kick \"Kiddo\" Clear reason"); single(3,"Kicked Kiddo");
    Require(host.kicks.size()==1 && host.kicks[0].text=="Clear reason" && host.Has(4,"Peter Brev kicked Kiddo"),"kick supports dead player and full reason");
    restore(); run(3,"sr_kick @me");
    Require(host.Count(3)==0 && host.kicks.size()==1 && host.kicks[0].text=="Kicked Peter Brev" && host.Has(4,"Peter Brev kicked Peter Brev"),"immediate self-kick keeps actor and uses disconnect as sole issuer result");
    restore(); host.fail_slot=3; run(3,"sr_kick @me"); single(3,"Player disconnect failed"); Require(host.kicks.empty() && !host.Count(4),"failed self-kick has no successful receipt/activity"); host.fail_slot=-2;
    run(3,"sr_kick @all");
    Require(host.kicks.size()==4 && host.kicks.back().slot==3 && host.kicks.back().text.find("Kicked 4 players; 1 target failed:")==0 && host.Count(3)==0 && host.Has(5,"Peter Brev kicked 4 players"),"group self-kick executes last with one aggregate disconnect result");
    restore(); run(-1,"sr_kick #72 Console reason"); single(-1,"Kicked Alexandra"); Require(host.kicks.size()==1,"console immunity exception"); restore();
    run(3,"sr_ban [U:1:999] 1 Offline reason"); single(3,"Banned 76561197960266727 for 1 minute"); Require(host.kicks.empty(),"offline ban persists without disconnect");
    run(3,"sr_unban STEAM_0:1:499"); single(3,"Removed the ban for 76561197960266727");
    run(3,"sr_unban [U:1:999]"); single(3,"No stored ban"); Require(host.messages.size()==1,"absent unban does not announce an action");
    host.fail_slot=6; run(3,"sr_ban #73 1 Expiring test");
    host.Clear(); tick(61); Require(host.attempts.empty(), "expired stored ban does not enforce");
    run(-1,"sr_unban [U:1:126]");
    host.fail_slot=6; run(3,"sr_ban #73 0 Native retry reason"); single(3,"Ban saved for");
    Require(host.Has(3,"disconnect pending") && host.kicks.empty(),"persisted ban remains successful when initial disconnect fails");
    host.Clear(); tick(); Require(host.attempts[6]==1,"enforcement retries persisted failed disconnect");
    for (int i=0;i<4;++i) tick(); Require(host.attempts[6]==1,"retry backoff avoids per-frame repeated disconnect");
    host.fail_slot=-2; tick(); Require(host.attempts[6]==2 && host.kicks.size()==1,"enforcement recovers on same banned connection");
    run(-1,"sr_unban [U:1:126]"); restore();
    run(3,"sr_ban @me 0 Self test"); Require(host.Count(3)==0 && host.kicks.size()==1 && host.kicks[0].text.find("Banned 76561197960265851 permanently")==0 && host.Has(4,"Peter Brev banned 76561197960265851"),"self ban persists and preserves attribution after immediate disconnect");
    run(-1,"sr_unban [U:1:123]"); restore();
    host.immediate=false; run(-1,"sr_ban [U:1:126] 0 Deferred"); Require(host.kicks.size()==1,"deferred native disconnect accepted");
    host.Clear(); tick(); tick(); Require(host.kicks.empty(),"accepted ban enforcement not duplicated on pending disconnect");
    ++host.players.at(6).connection; tick(); Require(host.kicks.size()==1,"new banned connection enforces again");
    app->MapChanged(); ++host.players.at(6).connection; host.Clear(); tick(); Require(host.kicks.size()==1,"ban enforcement timer survives map transition");
    Require(app->Pause("moderation"),"pause moderation"); ++host.players.at(6).connection; host.Clear(); tick(5); Require(host.kicks.empty(),"paused moderation does not enforce");
    Require(app->Resume("moderation"),"resume moderation"); tick(2); Require(host.kicks.size()==1,"resumed timer enforces new connection");
    Require(app->Unload("moderation"),"unload enforcement"); ++host.players.at(6).connection; host.Clear(); tick(5); Require(host.kicks.empty(),"unloaded moderation owns no enforcement callback");
    Require(app->Load(install("moderation")),"reload moderation"); tick(2); Require(host.kicks.size()==1,"reloaded plugin enforces persisted ban");
    run(-1,"sr_unban [U:1:126]"); host.immediate=true; restore();
    run(3,"sr_admin"); select(3); Require(host.menu.find("Kick a player")!=std::string::npos,"admin routes kick menu with absent slap plugin");
    ++host.players.at(6).connection; select(3); single(3,"Player is no longer available"); Require(host.kicks.empty(),"stale kick menu cannot target replacement");
    run(3,"sr_admin"); select(4); select(1); select(3); single(3,"Banned 76561197960265854 for 30 minutes"); Require(host.kicks.size()==1,"ban menu duration and stable target");
    run(3,"sr_admin"); select(5); select(0); single(3,"Removed the ban for 76561197960265854");
    for (int id=1000;id<1010;++id) run(-1,"sr_ban [U:1:"+std::to_string(id)+"] 0 Page test");
    run(3,"sr_admin"); select(5); select(8); select(1); single(3,"Removed the ban for 76561197960266737");
    run(3,"sr_admin"); select(5);
    std::ofstream(admin_file)<<"\"Admins\" {}"; app->ReloadPermissions(); Require(!input(sr::MenuInput::Select) && host.kicks.empty(),"revoked unban menu permission blocks selection");
    std::ofstream(admin_file)<<assignments; app->ReloadPermissions();
    Require(app->Shutdown(),"shutdown before native platform restart"); app.reset();
    app=std::make_unique<sr::Foundation>(host,argv[1],root); load();
    run(3,"sr_unban [U:1:1000]"); single(3,"Removed the ban for 76561197960266728");
    const auto bans = root / "data/bans.json";
    std::ofstream(bans,std::ios::app)<<" "; run(3,"sr_ban [U:1:2000] 0 Change test");
    single(3,"changed externally"); Require(host.kicks.empty() && host.Count(4)==0,"external ban edit prevents a false success");
    Require(app->Shutdown(),"clean moderation shutdown");
    app.reset(); std::ofstream(bans) << "{malformed";
    app=std::make_unique<sr::Foundation>(host,argv[1],root);
    Require(app->Load(install("admin")) && !app->Load(install("moderation")), "malformed stored bans prevent moderation startup without disabling other plugins");
    for (const auto& status : app->Status()) if (status.id=="moderation") Require(status.commands==0 && status.handles==0,"refused moderation startup retains no commands or timers");
    host.Clear(); tick(); Require(host.kicks.empty(),"no enforcement from refused startup");
    Require(app->Shutdown(),"malformed-store shutdown remains clean");
    std::ifstream unchanged(bans); std::string raw{std::istreambuf_iterator<char>(unchanged),{}};
    Require(raw=="{malformed","malformed database is left intact");
    std::cout<<"Bundled moderation, identity, immediate disconnect, persistence, menus and enforcement tests passed.\n";
}
