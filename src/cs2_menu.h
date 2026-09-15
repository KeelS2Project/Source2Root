#pragma once

#include <keels2/plugin.h>
#include <engine/igameeventsystem.h>
#include <networksystem/inetworkmessages.h>
#include <igameevents.h>

#include <string>

namespace sr {

class Cs2MenuBackend {
public:
    Cs2MenuBackend(INetworkMessages* messages, IGameEventSystem* events)
        : messages_(messages), events_(events) {}
    KeelResult Render(IGameEventManager2* manager, int slot, const std::string& html, int duration_ms);
    const std::string& Error() const { return error_; }
private:
    INetworkMessages* messages_;
    IGameEventSystem* events_;
    std::string error_;
};

}
