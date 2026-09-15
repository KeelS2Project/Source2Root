#pragma once

#include <keels2/plugin.h>
#include <engine/igameeventsystem.h>
#include <networksystem/inetworkmessages.h>

#include <string>
#include <vector>

namespace sr {

class Cs2MenuBackend {
public:
    Cs2MenuBackend(INetworkMessages* messages, IGameEventSystem* events)
        : messages_(messages), events_(events) {}
    void Observe(const CNetMessage* message);
    KeelResult Render(int slot, const std::string& html);
    void MapChanged() { event_id_ = -1; keys_.clear(); }
    bool Ready() const { return event_id_ >= 0; }
    const std::string& Error() const { return error_; }
private:
    struct Key { std::string name; int type; };
    INetworkMessages* messages_;
    IGameEventSystem* events_;
    std::vector<Key> keys_;
    int event_id_ = -1;
    std::string error_ = "waiting for the server's legacy game-event list; reconnect a client";
};

}
