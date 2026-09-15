#include "cs2_menu.h"
#include "gameevents.pb.h"

#include <google/protobuf/message.h>

#include <array>
#include <stdexcept>

namespace sr {

KeelResult Cs2MenuBackend::Render(IGameEventManager2* manager, int slot, const std::string& html, int duration_ms) {
    if (slot < 0 || slot >= ABSOLUTE_PLAYER_LIMIT || html.size() > 2048 ||
        (!html.empty() && (duration_ms <= 0 || duration_ms > 120000))) {
        error_ = "invalid menu recipient, text length or duration";
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    if (!messages_ || !events_ || !manager) {
        error_ = "CS2 menu transport is not ready";
        return KEEL_RESULT_NOT_READY;
    }
    INetworkMessageInternal* definition = nullptr;
    CNetMessage* allocated = nullptr;
    IGameEvent* event = nullptr;
    KeelResult result = KEEL_RESULT_OK;
    try {
        event = manager->CreateEvent("show_survival_respawn_status", true);
        if (!event) throw std::runtime_error("CS2 menu game event is unavailable");
        event->SetString("loc_token", html.empty() ? " " : html.c_str());
        event->SetInt("duration", html.empty() ? 0 : (duration_ms + 999) / 1000);
        event->SetPlayer("userid", CPlayerSlot(slot));
        definition = messages_->FindNetworkMessageById(GE_Source1LegacyGameEvent);
        if (!definition) throw std::runtime_error("legacy game-event network message unavailable");
        allocated = definition->AllocateMessage();
        if (!allocated) throw std::runtime_error("legacy game-event allocation failed");
        auto* proto = static_cast<google::protobuf::Message*>(allocated->AsProto());
        if (!proto || proto->GetDescriptor()->name() != "CMsgSource1LegacyGameEvent")
            throw std::runtime_error("legacy game-event network message type mismatch");
        proto->Clear();
        if (!manager->SerializeEvent(event, allocated->ToPB<CMsgSource1LegacyGameEvent>()))
            throw std::runtime_error("CS2 menu event serialization failed");
        std::array<uint64, (ABSOLUTE_PLAYER_LIMIT + 63) / 64> recipients{};
        recipients[static_cast<std::size_t>(slot) / 64] |= uint64{1} << (slot % 64);
        events_->PostEventAbstract(CSplitScreenSlot(-1), false, ABSOLUTE_PLAYER_LIMIT,
                                  recipients.data(), definition, allocated, 0, BUF_RELIABLE);
        error_.clear();
    } catch (const std::exception& error) {
        error_ = error.what();
        result = KEEL_RESULT_ENGINE_FAILURE;
    } catch (...) {
        error_ = "CS2 menu renderer threw an unknown exception";
        result = KEEL_RESULT_ENGINE_FAILURE;
    }
    if (event) {
        try { manager->FreeEvent(event); }
        catch (...) { error_ = "CS2 menu event release failed"; result = KEEL_RESULT_ENGINE_FAILURE; }
    }
    if (allocated) {
        try { messages_->DeallocateNetMessageAbstract(definition, allocated); }
        catch (...) { error_ = "CS2 menu message release failed"; result = KEEL_RESULT_ENGINE_FAILURE; }
    }
    return result;
}

}
