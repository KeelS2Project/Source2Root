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
    std::map<std::pair<int,int>,bool> listening;
    std::vector<Message> messages;
    std::vector<std::string> logs;
    sr::Foundation* app = nullptr;
    int fail_voice = -2, fail_reply = -2, fail_lookup = -2;
    std::string menu;
    KeelResult Lookup(int slot,sr::Player& out) override { if (slot==fail_lookup) return KEEL_RESULT_ENGINE_FAILURE; if (!players.contains(slot)) return KEEL_RESULT_NOT_FOUND; out=players.at(slot); return KEEL_RESULT_OK; }
    KeelResult NextPlayer(int after,sr::Player& out) override { auto it=players.upper_bound(after); if (it==players.end()) return KEEL_RESULT_NOT_FOUND; out=it->second; return KEEL_RESULT_OK; }
    KeelResult GetListening(const sr::Player& receiver,const sr::Player& sender,bool& value) override { value=Read(receiver.slot,sender.slot); return KEEL_RESULT_OK; }
    KeelResult SetListening(const sr::Player& receiver,const sr::Player& sender,bool value) override {
        Require(players.contains(receiver.slot) && players.at(receiver.slot).SameConnection(receiver) && players.contains(sender.slot) && players.at(sender.slot).SameConnection(sender),"current listening sessions");
        if (receiver.slot==fail_voice) return KEEL_RESULT_ENGINE_FAILURE;
        Require(app->FilterVoice(receiver.slot,sender.slot,value)==KEEL_RESULT_OK,"native listening filter");
        listening[{receiver.slot,sender.slot}]=value; return KEEL_RESULT_OK;
    }
    bool Read(int receiver,int sender) const { auto it=listening.find({receiver,sender}); return it==listening.end() || it->second; }
    KeelResult Reply(const sr::Player* player,const std::string& text) override {
        if (player && player->slot==fail_reply) return KEEL_RESULT_ENGINE_FAILURE;
        Require(!player || players.at(player->slot).SameConnection(*player),"reply retains current connection");
        messages.push_back({player?player->slot:-1,text}); return KEEL_RESULT_OK;
    }
    void Log(const std::string& text) override { logs.push_back(text); std::cout<<text<<'\n'; Require(text.find("FAILED:")==std::string::npos,"fixture API guard"); }
    KeelResult RegisterCommand(const std::string&) override { return KEEL_RESULT_OK; }
    KeelResult RemoveCommand(const std::string&) override { return KEEL_RESULT_OK; }
    KeelResult ListenEvent(const std::string&) override { return KEEL_RESULT_OK; }
    KeelResult RemoveEvent(const std::string&) override { return KEEL_RESULT_OK; }
    KeelResult RenderMenu(const sr::Player&,const std::string& value, int) override { menu=value; return KEEL_RESULT_OK; }
    KeelResult AcquireProvider(const std::string&,unsigned) override { return KEEL_RESULT_UNSUPPORTED; }
    KeelResult ReleaseProvider(const std::string&,unsigned) override { return KEEL_RESULT_UNSUPPORTED; }
    unsigned Count(int slot) const { unsigned result=0; for (const auto& message:messages) result+=message.slot==slot; return result; }
    bool Has(int slot,const std::string& text) const { for (const auto& message:messages) if (message.slot==slot && message.text.find(text)!=std::string::npos) return true; return false; }
};
int main(int argc,char** argv) {
    Require(argc==4,"communications_test runtime scripts root");
    const std::filesystem::path root(argv[3]),scripts(argv[2]);
    std::filesystem::remove_all(root); std::filesystem::create_directories(root/"configs");
    const auto admins=root/"configs/admins.cfg";
    const std::string assignments=R"("Admins" { "Root" { "identity" "[U:1:123]" "group" "root" } "Mod" { "identity" "[U:1:124]" "group" "moderator" } "Equal" { "identity" "[U:1:125]" "group" "root" } })";
    std::ofstream(admins)<<assignments;
    std::ofstream(root/"configs/admin_groups.cfg")<<R"("Groups" { "root" { "immunity" "100" } "moderator" { "immunity" "10" "permissions" { "admin.mute" "1" } } })";
    auto install=[&](const char* id) {
        auto directory=root/"plugins"/id; std::filesystem::create_directories(directory);
        std::filesystem::copy_file(scripts/(std::string(id)+".smx"),directory/"main.smx",std::filesystem::copy_options::overwrite_existing);
        std::ofstream(directory/"plugin.json")<<"{\"schema\":1,\"id\":\""<<id<<"\",\"name\":\""<<id<<"\",\"author\":\"tests\",\"version\":\"1.0.0\",\"api\":2,\"entry\":\"main.smx\",\"enabled\":true,\"dependencies\":[]}";
        return directory/"plugin.json";
    };
    Host h; h.players={
        {3,{3,13,76561197960265851ULL,true,false,"Peter Brev",70,3,true}},
        {4,{4,14,76561197960265852ULL,true,false,"Alex",71,2,true}},
        {5,{5,15,76561197960265853ULL,true,false,"Equal",72,3,true}},
        {6,{6,16,76561197960265854ULL,true,false,"Kiddo",73,2,false}},
        {7,{7,17,0,false,true,"Bot",74,2,true}}};
    sr::Foundation app(h,argv[1],root); h.app=&app;
    Require(app.Load(install("admin")) && app.Load(install("communications")) && app.Load(install("communications_owner")),"load bundled communications and public-API fixture");
    auto run=[&](int slot,const std::string& text,sr::Origin origin=sr::Origin::ClientConsole) {
        h.messages.clear(); h.logs.clear(); return app.Dispatch(slot==-1?sr::Origin::ServerConsole:origin,slot,text);
    };
    auto single=[&](int slot,const char* text) { Require(h.Count(slot)==1 && h.Has(slot,text),"one private issuer result"); };
    auto input=[&](sr::MenuInput value) { return app.MenuInput(h.players.at(3),app.CurrentMenu(h.players.at(3)),value); };
    auto select=[&](unsigned index) { for(unsigned i=0;i<index;++i) Require(input(sr::MenuInput::Down),"menu navigation"); Require(input(sr::MenuInput::Select),"menu selection"); };
    run(3,"sr_help"); Require(h.Has(3,"sr_mute <target>") && h.Has(3,"sr_say <message>"),"help routes new commands");
    for (const auto& text:{"sr_mute","sr_unmute #73 extra","sr_gag","sr_ungag #73 extra","sr_silence","sr_unsilence #73 extra","sr_say","sr_say \"\""}) { run(3,text); Require(h.Count(3)==1,"argument failures remain private"); }
    for(auto origin:{sr::Origin::ClientConsole,sr::Origin::PublicChat,sr::Origin::SilentChat}) {
        run(6,origin==sr::Origin::ClientConsole?"sr_gag #71":origin==sr::Origin::PublicChat?"!gag #71":"/gag #71",origin); single(6,"You do not have access");
    }
    run(3,"sr_mute #72"); single(3,"immunity"); Require(h.Read(4,5),"root cannot mute equal immunity");
    run(3,"sr_mute #73"); single(3,"Muted Kiddo"); Require(!h.Read(3,6) && h.Has(4,"Peter Brev muted Kiddo"),"voice applies to dead target and uses activity");
    run(3,"sr_unmute #73"); single(3,"Unmuted Kiddo"); Require(h.Read(3,6),"unmute restores listening");
    run(3,"sr_gag #73"); single(3,"Gagged Kiddo");
    h.fail_lookup=6; Require(run(6,"unresolved chat",sr::Origin::PublicChat),"unresolved chat identity fails closed"); h.fail_lookup=-2;
    Require(run(6,"ordinary text",sr::Origin::PublicChat) && h.messages.empty(),"gag hides ordinary chat without spam");
    Require(run(6,"!unknown",sr::Origin::PublicChat),"gag hides unknown public command");
    Require(run(6,"/unknown",sr::Origin::SilentChat),"silent unknown remains hidden"); single(6,"Unknown command");
    run(3,"sr_ungag #73"); single(3,"Ungagged Kiddo"); Require(!run(6,"ordinary text",sr::Origin::PublicChat),"ungag restores normal chat");
    run(3,"sr_silence #73"); single(3,"Silenced Kiddo"); Require(!h.Read(3,6) && run(6,"hidden",sr::Origin::PublicChat),"silence sets voice and chat");
    run(3,"sr_unsilence #73"); single(3,"Unsilenced Kiddo"); Require(h.Read(3,6) && !run(6,"visible",sr::Origin::PublicChat),"unsilence restores both");
    run(3,"sr_gag @me"); single(3,"Gagged Peter Brev");
    run(3,"sr_say hidden message"); single(3,"cannot send an announcement while gagged"); Require(h.messages.size()==1,"gagged announcement not delivered");
    Require(run(3,"!ungag @me",sr::Origin::PublicChat),"self-ungag command typed while gagged stays hidden"); single(3,"Ungagged Peter Brev");
    run(3,"sr_say A complete announcement"); single(3,"Announcement sent: A complete announcement");
    Require(h.Count(4)==1 && h.Has(4,"Peter Brev: A complete announcement") && h.Count(7)==0,"announcement excludes issuer and bots");
    h.fail_reply=4; run(3,"sr_say Partial delivery"); single(3,"Announcement reached 2 players; 1 delivery failed"); h.fail_reply=-2;
    run(3,"sr_comms_owner #73 m"); run(3,"sr_unmute #73"); single(3,"another plugin still restricts voice"); Require(h.messages.size()==1 && !h.Read(3,6),"unmute does not claim another owner's restriction was lifted");
    run(3,"sr_comms_owner #73 g"); run(3,"sr_ungag #73"); single(3,"another plugin still restricts chat");
    Require(app.Unload("communications_owner") && h.Read(3,6) && !run(6,"released",sr::Origin::PublicChat),"owner unload restores its voice and chat");
    run(3,"sr_admin"); select(3);
    Require(input(sr::MenuInput::Back) && h.menu.find("Source2Root administration")!=std::string::npos,
        "communications menu returns to administration");
    run(3,"sr_admin"); select(3); Require(h.menu.find("Mute a player")!=std::string::npos,"main menu routes mute");
    ++h.players.at(6).connection; select(3); single(3,"Player is no longer available"); Require(h.Read(3,6),"stale menu does not mute replacement");
    run(3,"sr_admin"); select(5); select(3); single(3,"Gagged Kiddo");
    run(3,"sr_admin"); select(6);
    std::ofstream(admins)<<"\"Admins\" {}"; app.ReloadPermissions(); Require(!input(sr::MenuInput::Select),"menu permission rechecked");
    std::ofstream(admins)<<assignments; app.ReloadPermissions();
    run(3,"sr_mute #73"); Require(app.Pause("communications"),"pause communications");
    Require(!h.Read(3,6) && run(6,"still gagged",sr::Origin::PublicChat),"script pause retains owned restrictions");
    Require(app.Resume("communications"),"resume communications");
    h.fail_voice=4; Require(!app.Unload("communications"),"unload retains failed voice cleanup");
    bool retiring=false; for(const auto& status:app.Status()) if(status.id=="communications") retiring=status.state==sr::PluginState::Retiring;
    Require(retiring && !run(6,"gag released",sr::Origin::PublicChat),"retiring cleanup removes gag while retaining voice restoration");
    h.fail_voice=-2; Require(app.Unload("communications") && h.Read(4,6),"retry unload restores voice");
    Require(app.Load(install("communications")),"reload fresh communications");
    h.fail_voice=4; run(3,"sr_mute #73"); single(3,"Mute recorded;"); Require(h.Count(5)==0,"partial mute failure has no success announcement");
    h.fail_voice=-2; app.Tick(sr::Foundation::Clock::now()+std::chrono::seconds(2)); Require(!h.Read(4,6),"periodic retry completes partial mute");
    run(3,"sr_gag #73"); app.MapChanged(); Require(h.Read(4,6) && !run(6,"map cleared",sr::Origin::PublicChat),"map transition clears owned restrictions");
    run(-1,"sr_mute @all"); single(-1,"Muted 5 players"); Require(!h.Read(4,5),"console group immunity exception");
    Require(app.Shutdown() && h.Read(4,5),"platform shutdown restores all voice state");
    std::cout<<"Bundled communication commands, gag dispatch, ownership and cleanup tests passed.\n";
}
