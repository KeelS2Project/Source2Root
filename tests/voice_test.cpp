#include "foundation.h"
#include <cstdlib>
#include <functional>
#include <iostream>

static void Require(bool value, const char* message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

class Host final : public sr::GameHost {
public:
    sr::Voice voice{*this};
    std::map<int, sr::Player> players;
    std::map<std::pair<int,int>, bool> listening;
    std::vector<std::pair<int,int>> writes;
    std::function<void()> on_read;
    int fail_write = -1, fail_lookup = -1;
    bool unavailable = false, throw_write = false;
    Host() {
        for (int i = 0; i < 3; ++i)
            players[i] = {i,
                          static_cast<std::uint64_t>(10 + i),
                          76561197960265851ULL + i,
                          true,
                          false,
                          "Player",
                          70 + i,
                          2,
                          true};
    }

    KeelResult Lookup(int slot, sr::Player& player) override {
        if (slot == fail_lookup)
            return KEEL_RESULT_ENGINE_FAILURE;

        if (!players.contains(slot))
            return KEEL_RESULT_NOT_FOUND;

        player = players.at(slot);
        return KEEL_RESULT_OK;
    }

    KeelResult NextPlayer(int after, sr::Player& player) override {
        auto it = players.upper_bound(after);

        if (it == players.end())
            return KEEL_RESULT_NOT_FOUND;

        player = it->second;
        return KEEL_RESULT_OK;
    }

    KeelResult GetListening(const sr::Player& receiver, const sr::Player& sender, bool& value) override {
        if (unavailable)
            return KEEL_RESULT_UNSUPPORTED;

        if (auto callback = std::exchange(on_read, {}))
            callback();

        value = Read(receiver.slot, sender.slot);
        return KEEL_RESULT_OK;
    }

    KeelResult SetListening(const sr::Player& receiver, const sr::Player& sender, bool value) override {
        Require(players.contains(receiver.slot) && players.at(receiver.slot).SameConnection(receiver) &&
            players.contains(sender.slot) && players.at(sender.slot).SameConnection(sender),"write must use current sessions");

        if (throw_write)
            throw std::runtime_error("fixture write");

        if (receiver.slot == fail_write)
            return KEEL_RESULT_ENGINE_FAILURE;

        writes.emplace_back(receiver.slot,sender.slot);
        Require(voice.Filter(receiver.slot,sender.slot,value)==KEEL_RESULT_OK,"own write filter");
        listening[{receiver.slot, sender.slot}] = value;
        return KEEL_RESULT_OK;
    }

    bool Read(int receiver, int sender) const {
        auto it = listening.find({receiver, sender});
        return it == listening.end() || it->second;
    }

    void External(int receiver,int sender,bool value) {
        Require(voice.Filter(receiver,sender,value)==KEEL_RESULT_OK,"external listening request");
        listening[{receiver,sender}]=value;
    }

    KeelResult Reply(const sr::Player*, const std::string&) override {
        return KEEL_RESULT_OK;
    }

    void Log(const std::string&) override {}

    KeelResult RegisterCommand(const std::string&) override {
        return KEEL_RESULT_OK;
    }

    KeelResult RemoveCommand(const std::string&) override {
        return KEEL_RESULT_OK;
    }

    KeelResult ListenEvent(const std::string&) override {
        return KEEL_RESULT_OK;
    }

    KeelResult RemoveEvent(const std::string&) override {
        return KEEL_RESULT_OK;
    }

    KeelResult RenderMenu(const sr::Player&, const std::string&, int) override {
        return KEEL_RESULT_OK;
    }

    KeelResult AcquireProvider(const std::string&, unsigned) override {
        return KEEL_RESULT_UNSUPPORTED;
    }

    KeelResult ReleaseProvider(const std::string&, unsigned) override {
        return KEEL_RESULT_UNSUPPORTED;
    }
};
int main() {
    Host h;
    std::string error;
    auto sender = h.players.at(0);
    h.listening[{1,0}]=false;
    Require(h.voice.Set(1,sender,true,error),"mute all receivers");

    for (int i = 0; i < 3; ++i)
        Require(!h.Read(i, 0), "all receivers muted");

    h.External(2, 0, false);
    h.External(1, 0, true);
    Require(!h.Read(1,0),"mute filters later game request");
    Require(h.voice.Set(2,sender,true,error),"second owner mute");
    Require(h.voice.Release(1,error) && h.voice.Muted(sender) && !h.Read(1,0),"one owner cannot clear another restriction");
    Require(h.voice.Release(2,error) && !h.voice.Muted(sender),"last owner releases");
    Require(h.Read(0,0) && h.Read(1,0) && !h.Read(2,0),"latest observed external requests restored");
    h.fail_write=1;
    Require(!h.voice.Set(1, sender, true, error) && h.voice.Muted(sender) && error.starts_with("Mute recorded;"),
            "partial mute remains recorded with explicit failure");

    h.fail_write = -1;
    Require(h.voice.Refresh(error) && !h.Read(1, 0), "restriction retry succeeds");
    h.fail_write = 1;
    Require(!h.voice.Release(1, error) && error.find("retry required") != std::string::npos,
            "release refuses unfinished restoration");

    h.External(1, 0, false);
    h.fail_write = -1;
    Require(h.voice.Release(1,error) && !h.Read(1,0),"repeated release adopts latest pending request");
    Require(h.voice.Set(1,sender,true,error),"mute before transient failure");
    h.fail_lookup = 1;
    Require(!h.voice.Release(1, error), "transient receiver lookup retains restore obligation");
    h.fail_lookup = -1;
    h.writes.clear();
    Require(h.voice.Release(1, error) && h.writes.size() == 1, "only pending receiver retried");
    Require(h.voice.Set(1,sender,true,error),"mute before replaced receiver");
    ++h.players.at(1).connection;
    h.writes.clear();
    Require(h.voice.Release(1,error),"replaced receiver can be retired");

    for (const auto& [receiver, from] : h.writes)
        Require(receiver != 1, "no restore against replacement receiver");

    h.on_read = [&] {
        ++h.players.at(0).connection;
    };
    Require(!h.voice.Set(1, sender, true, error) && !h.voice.Muted(sender),
            "sender replacement during initial read prevents ownership");

    sender = h.players.at(0);
    Require(h.voice.Set(1, sender, true, error), "mute current sender");
    h.voice.Disconnected(0, sender.connection - 1);
    Require(h.voice.Muted(sender), "old disconnect cannot clear new owner");
    h.throw_write = true;
    Require(!h.voice.Release(1, error), "throwing restore retained");
    h.throw_write = false;
    h.External(2, 0, true);
    Require(h.voice.Release(1,error) && h.Read(2,0),"write recursion guard restored after exception");
    Require(h.voice.Set(1,sender,true,error),"mute before native pause");
    Require(h.voice.Suspend(error) && h.Read(2,0) && !h.voice.Muted(sender),"native pause restores owned listening state");
    h.External(2,0,false);
    Require(h.voice.Resume(error) && h.voice.Muted(sender),"native resume reapplies owned requests");
    Require(h.voice.Release(1,error) && !h.Read(2,0),"later unmute preserves external state changed while paused");
    Require(h.voice.Set(1,sender,true,error),"mute before refused pause");
    h.fail_write = 1;
    Require(!h.voice.Suspend(error) && h.voice.Muted(sender), "refused pause retains mute intent");
    h.fail_write = -1;
    Require(h.voice.Refresh(error) && !h.Read(1, 0), "refused pause enforcement recovers");
    Require(h.voice.Suspend(error),"native pause retry");
    ++h.players.at(0).connection;
    h.writes.clear();
    Require(h.voice.Resume(error) && h.writes.empty(), "resume does not follow a replacement sender");
    sender=h.players.at(0);
    h.unavailable = true;
    Require(!h.voice.Set(1, sender, true, error) && !h.voice.Muted(sender), "unsupported engine does not record mute");
    h.unavailable = false;
    Require(h.voice.Set(1, sender, true, error), "mute before map cleanup");
    h.fail_write = 2;
    Require(!h.voice.Release(0, error), "all-owner cleanup retains failure");
    h.fail_write = -1;
    Require(h.voice.Release(0, error), "all-owner cleanup retry");
    Require(h.voice.Set(1,sender,true,error),"mute before sender disconnect");
    h.players.erase(0);
    h.writes.clear();
    Require(h.voice.Refresh(error) && h.writes.empty(), "disconnected sender retired without writes");
    std::cout << "Owned voice filtering and recoverable restoration tests passed.\n";
}
