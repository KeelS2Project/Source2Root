#include "foundation.h"
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>

static void Require(bool value, const char* text) { if (!value) { std::cerr << text << '\n'; std::exit(1); } }
class Host final : public sr::GameHost {
public:
    struct Message { int slot; std::string text; };
    std::map<int,sr::Player> players;
    std::vector<Message> messages;
    std::vector<std::string> logs, changes;
    std::vector<int> restarts;
    std::set<std::string> installed{"de_dust2", "de_mirage"};
    KeelResult map_result = KEEL_RESULT_OK, restart_result = KEEL_RESULT_OK;
    std::string menu;
    std::function<void(const std::string&)> on_log;
    std::function<void()> on_change;
    KeelResult Lookup(int slot,sr::Player& out) override { if (!players.contains(slot)) return KEEL_RESULT_NOT_FOUND; out=players.at(slot); return KEEL_RESULT_OK; }
    KeelResult NextPlayer(int after,sr::Player& out) override { auto it=players.upper_bound(after); if (it==players.end()) return KEEL_RESULT_NOT_FOUND; out=it->second; return KEEL_RESULT_OK; }
    KeelResult MapInstalled(const std::string& name,bool& value) override { value=installed.contains(name); return map_result; }
    KeelResult ChangeMap(const std::string& name) override {
        if(map_result!=KEEL_RESULT_OK) return map_result;
        if(!installed.contains(name)) return KEEL_RESULT_NOT_FOUND;
        changes.push_back(name); if(on_change) on_change(); return KEEL_RESULT_OK;
    }
    KeelResult RestartRound(int seconds) override { if(restart_result!=KEEL_RESULT_OK) return restart_result; restarts.push_back(seconds); return KEEL_RESULT_OK; }
    KeelResult Reply(const sr::Player* player,const std::string& text) override {
        Require(!player || players.at(player->slot).SameConnection(*player),"current reply connection");
        messages.push_back({player?player->slot:-1,text}); return KEEL_RESULT_OK;
    }
    void Log(const std::string& text) override { logs.push_back(text); std::cout<<text<<'\n'; Require(text.find("FAILED:")==std::string::npos,"public server API guard"); if(on_log) on_log(text); }
    KeelResult RegisterCommand(const std::string&) override { return KEEL_RESULT_OK; }
    KeelResult RemoveCommand(const std::string&) override { return KEEL_RESULT_OK; }
    KeelResult ListenEvent(const std::string&) override { return KEEL_RESULT_OK; }
    KeelResult RemoveEvent(const std::string&) override { return KEEL_RESULT_OK; }
    KeelResult RenderMenu(const sr::Player&,const std::string& value) override { menu=value; return KEEL_RESULT_OK; }
    KeelResult AcquireProvider(const std::string&,unsigned) override { return KEEL_RESULT_UNSUPPORTED; }
    KeelResult ReleaseProvider(const std::string&,unsigned) override { return KEEL_RESULT_UNSUPPORTED; }
    bool Has(int slot,const std::string& text) const { for(const auto& m:messages) if(m.slot==slot && m.text.find(text)!=std::string::npos) return true; return false; }
    unsigned Count(int slot) const { unsigned n=0; for(const auto& m:messages) n+=m.slot==slot; return n; }
};
int main(int argc,char** argv) {
    Require(argc==4,"server_test runtime scripts root");
    const std::filesystem::path root(argv[3]),scripts(argv[2]);
    std::filesystem::remove_all(root); std::filesystem::create_directories(root/"configs");
    const auto admins=root/"configs/admins.cfg", allowed=root/"configs/allowed_maps.txt";
    const std::string assignments=R"("Admins" { "Root" { "identity" "[U:1:123]" "group" "root" } })";
    std::ofstream(admins)<<assignments;
    std::ofstream(root/"configs/admin_groups.cfg")<<R"("Groups" { "root" { "immunity" "100" } })";
    std::ofstream(allowed)<<"// unrestricted\n";
    std::ofstream(root/"configs/map_menu.txt")<<"de_dust2\nde_missing\nde_mirage\n";
    auto install=[&](const char* id) {
        auto directory=root/"plugins"/id; std::filesystem::create_directories(directory);
        std::filesystem::copy_file(scripts/(std::string(id)+".smx"),directory/"main.smx",std::filesystem::copy_options::overwrite_existing);
        std::ofstream(directory/"plugin.json")<<"{\"schema\":1,\"id\":\""<<id<<"\",\"name\":\""<<id<<"\",\"author\":\"tests\",\"version\":\"1.0.0\",\"api\":2,\"entry\":\"main.smx\",\"enabled\":true,\"dependencies\":[]}";
        return directory/"plugin.json";
    };
    Host h; h.players={{3,{3,13,76561197960265851ULL,true,false,"Peter Brev",70,3,true}},
        {4,{4,14,76561197960265852ULL,true,false,"Kiddo",71,2,true}}};
    sr::Foundation app(h,argv[1],root);
    Require(app.Load(install("admin")) && app.Load(install("server")),"load bundled server commands");
    auto run=[&](int slot,const std::string& text,sr::Origin origin=sr::Origin::ClientConsole) {
        h.messages.clear(); h.logs.clear(); auto result=app.Dispatch(slot==-1?sr::Origin::ServerConsole:origin,slot,text); app.Tick(sr::Foundation::Clock::now()); return result;
    };
    auto single=[&](int slot,const char* text) { Require(h.Count(slot)==1 && h.Has(slot,text),"one private server-command result"); };
    auto select=[&](unsigned index) { for(unsigned i=0;i<index;++i) Require(app.MenuInput(h.players.at(3),app.CurrentMenu(h.players.at(3)),sr::MenuInput::Down),"navigate"); Require(app.MenuInput(h.players.at(3),app.CurrentMenu(h.players.at(3)),sr::MenuInput::Select),"select"); app.Tick(sr::Foundation::Clock::now()); };
    run(3,"sr_help"); Require(h.Has(3,"sr_map <map>") && h.Has(3,"sr_restart [seconds]"),"help lists both server commands");
    for(auto origin:{sr::Origin::ClientConsole,sr::Origin::PublicChat,sr::Origin::SilentChat}) {
        run(4,origin==sr::Origin::ClientConsole?"sr_map de_dust2":origin==sr::Origin::PublicChat?"!map de_dust2":"/map de_dust2",origin);
        single(4,"You do not have access"); Require(h.changes.empty(),"unauthorized map rejected");
    }
    for(const auto& command:{"sr_map", "sr_map de_dust2 extra", "sr_map ../de_dust2", "sr_map DE_DUST2", "sr_map de_missing", "sr_map \"de_dust2;quit\"", "sr_map \"\""}) { run(3,command); Require(h.Count(3)==1 && h.changes.empty(),"invalid map remains private and inert"); }
    run(3,"sr_map de_dust2"); single(3,"Requested a map change to de_dust2"); Require(h.changes.back()=="de_dust2" && h.Has(4,"ADMIN: Requested a map change"),"map shared activity");
    app.SetSetting("sr_show_activity","0"); run(-1,"sr_restart"); single(-1,"Requested a round restart in 1 second"); Require(h.restarts.back()==1 && h.messages.size()==1,"default restart with activity disabled");
    for(const auto& command:{"sr_restart 0","sr_restart 61","sr_restart -1","sr_restart 1.5","sr_restart 999999999999999","sr_restart 1 extra"}) { auto before=h.restarts.size(); run(3,command); Require(h.restarts.size()==before && h.Count(3)==1,"invalid restart delay rejected"); }
    run(3,"/restart 60",sr::Origin::SilentChat); single(3,"60 seconds"); Require(h.restarts.back()==60,"silent restart maximum");
    h.restart_result=KEEL_RESULT_UNSUPPORTED; run(3,"sr_restart 5"); single(3,"unavailable on this game host"); h.restart_result=KEEL_RESULT_OK;
    std::ofstream(allowed)<<"de_mirage\n"; auto changes=h.changes.size(); run(3,"sr_map de_dust2"); single(3,"not allowed"); Require(h.changes.size()==changes,"current allowlist enforced");
    run(3,"sr_admin"); select(3); Require(h.menu.find("de_mirage")!=std::string::npos && h.menu.find("de_dust2")==std::string::npos,"map menu uses allowlist");
    std::ofstream(allowed)<<"de_dust2\n"; select(0); single(3,"not allowed"); Require(h.changes.size()==changes,"selection rechecks changed allowlist by map name");
    run(3,"sr_admin"); select(3); std::ofstream(admins)<<"\"Admins\" {}"; app.ReloadPermissions();
    Require(!app.MenuInput(h.players.at(3),app.CurrentMenu(h.players.at(3)),sr::MenuInput::Select),"menu permission rechecked"); Require(h.changes.size()==changes,"revoked menu cannot change map");
    std::ofstream(admins)<<assignments; app.ReloadPermissions();
    std::ofstream(allowed)<<"\n"; run(3,"sr_admin"); select(3); Require(h.menu.find("de_dust2")!=std::string::npos && h.menu.find("de_missing")==std::string::npos,"unrestricted menu filters configured installed choices");
    select(1); Require(h.changes.back()=="de_mirage","map selection uses stored name");
    run(3,"sr_admin"); select(4); select(1); Require(h.restarts.back()==5,"restart menu tagged seconds");
    std::ofstream(allowed)<<"bad;quit\n"; changes=h.changes.size(); run(3,"sr_map de_dust2"); single(3,"Map lists accept"); Require(h.changes.size()==changes,"malformed allowlist fails closed");
    std::filesystem::remove(allowed); run(3,"sr_map de_dust2"); single(3,"Could not open allowed_maps.txt"); Require(h.changes.size()==changes,"deleted allowlist does not authorize maps");
    std::ofstream(allowed)<<"\n";
    h.on_change=[&]{app.MapChanged();}; run(3,"sr_map de_dust2"); single(3,"Requested a map change"); h.on_change={};
    Require(app.Pause("server"),"pause server commands"); changes=h.changes.size(); run(3,"sr_map de_dust2"); Require(h.changes.size()==changes,"paused command cannot execute"); Require(app.Resume("server"),"resume server commands");
    changes=h.changes.size(); app.Dispatch(sr::Origin::ClientConsole,3,"sr_map de_dust2");
    Require(h.changes.size()==changes,"map request waits until callback ends and next frame");
    app.Dispatch(sr::Origin::ClientConsole,3,"sr_map de_mirage"); Require(h.Has(3,"already pending"),"only one global pending request");
    Require(app.Pause("server"),"pause before queued execution"); app.Tick(sr::Foundation::Clock::now());
    Require(h.changes.size()==changes && app.Resume("server"),"pause cancels queued map request");
    app.Dispatch(sr::Origin::ClientConsole,3,"sr_map de_dust2"); Require(app.PreparePause(),"native pause cancels request");
    app.NativeResumed(); app.Tick(sr::Foundation::Clock::now()); Require(h.changes.size()==changes,"native resume does not revive cancelled map");
    app.Dispatch(sr::Origin::ClientConsole,3,"sr_map de_dust2"); h.installed.erase("de_dust2"); app.Tick(sr::Foundation::Clock::now());
    Require(h.changes.size()==changes,"engine rechecks installation at execution"); h.installed.insert("de_dust2");
    app.Dispatch(sr::Origin::ClientConsole,3,"sr_map de_dust2"); Require(app.Unload("server"),"unload queued owner");
    app.Tick(sr::Foundation::Clock::now()); Require(h.changes.size()==changes && app.Load(install("server")),"unload cancels queued action without stale VM");
    std::ofstream(allowed)<<"// full legacy bound\n"; { std::ofstream list(allowed,std::ios::app); for(unsigned i=0;i<1024;i++) list<<"map_"<<i<<'\n'; }
    h.installed.insert("map_1023"); run(3,"sr_map map_1023"); Require(h.changes.back()=="map_1023","full 1024-entry legacy allowlist executes under runtime deadline");
    std::ofstream(allowed)<<"\n"; { std::ofstream list(root/"configs/map_menu.txt"); for(int i=0;i<10;++i) { auto name="custom_"+std::to_string(i); list<<name<<'\n'; h.installed.insert(name); } }
    run(3,"sr_admin"); select(3); Require(h.menu.find("custom_0")!=std::string::npos,"first map page");
    select(8); Require(h.menu.find("custom_8")!=std::string::npos && h.menu.find("Previous maps")!=std::string::npos,"second map page");
    select(1); Require(h.changes.back()=="custom_9","paged selection retains map name");
    app.Dispatch(sr::Origin::ClientConsole,3,"sr_map de_dust2"); changes=h.changes.size(); app.MapChanged(); app.Tick(sr::Foundation::Clock::now());
    Require(h.changes.size()==changes,"intervening map transition cancels queued request");
    std::ofstream(root/"configs/snapshot.txt",std::ios::binary)<<"\xef\xbb\xbf" "first\r\n\r\nlast";
    std::ofstream(root/"configs/oversized.txt")<<std::string(131073,'x');
    std::ofstream(root/"configs/binary.txt",std::ios::binary).write("a\0b",3);
    std::ofstream(root/"secret")<<"private";
    std::error_code symlink_error; std::filesystem::create_symlink(root/"secret",root/"configs/outside.txt",symlink_error);
    h.on_log=[&](const std::string& text){if(text.find("replace snapshot file")!=std::string::npos) std::ofstream(root/"configs/snapshot.txt")<<"replacement";};
    Require(app.Load(install("server_api")),"load server API fixture");
    run(3,"sr_server_api"); bool done=false; for(const auto& log:h.logs) done|=log.find("server API checks passed")!=std::string::npos; Require(done,"public config and mechanics API checks ran");
    run(3,"sr_config_fault"); bool stale=false; for(const auto& log:h.logs) stale|=log.find("stale, foreign or wrong-type handle")!=std::string::npos; Require(stale,"closed config handle rejected");
    changes=h.changes.size(); run(3,"sr_queue_fault"); Require(h.changes.size()==changes,"failed requesting callback cancels queue");
    Require(app.Unload("server_api") && app.Shutdown(),"unload retires snapshots and server commands");
    std::cout<<"Bundled map/restart, current policy, menus and owned configuration snapshots passed.\n";
}
