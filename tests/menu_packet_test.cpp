#include "cs2_menu.h"
#include "gameevents.pb.h"
#include <keels2/keelhook.hpp>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/dynamic_message.h>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
using Message = google::protobuf::Message;
struct Interface { void** table; };
struct Packet { void** table; Message* proto; };
std::array<void*, 128> message_table{}, messages_table{}, definition_table{}, events_table{}, manager_table{}, event_table{};
Interface messages{messages_table.data()}, definition{definition_table.data()}, events{events_table.data()};
Interface manager{manager_table.data()}, native_event{event_table.data()};
Message* wrong_proto = nullptr;
unsigned creations = 0, frees = 0;
int event_id = 413, duration = -1, player = -1;
std::string token;
Packet outgoing{message_table.data(), nullptr};
unsigned allocations = 0, releases = 0, sends = 0;
int mode = 0;
std::string wire;
std::array<uint64, (ABSOLUTE_PLAYER_LIMIT + 63) / 64> recipients{};
void Check(bool condition, const char* text) { if (!condition) throw std::runtime_error(text); }

template <auto Method, typename Function>
void Install(std::array<void*, 128>& table, Function function) {
    auto index = keels2::kh::VirtualIndex<Method>();
    Check(index && *index < table.size(), "pinned virtual method index");
    static_assert(sizeof(function) == sizeof(void*));
    std::memcpy(&table[*index], &function, sizeof(function));
}
void* AsProto(const Packet* packet) { return mode == 3 ? nullptr : mode == 11 ? wrong_proto : packet->proto; }
INetworkMessageInternal* Find(void*, const char* name) {
    Check(std::strcmp(name, "CMsgSource1LegacyGameEvent") == 0, "exact event message lookup required");
    return mode == 1 ? nullptr : reinterpret_cast<INetworkMessageInternal*>(&definition);
}
CNetMessage* Allocate(void*) {
    if (mode == 2) return nullptr;
    ++allocations;
    return reinterpret_cast<CNetMessage*>(&outgoing);
}
void Release(void*, INetworkMessageInternal* type, CNetMessage* packet) {
    Check(type == reinterpret_cast<INetworkMessageInternal*>(&definition) &&
          packet == reinterpret_cast<CNetMessage*>(&outgoing), "message released through allocation owner");
    ++releases;
    if (mode == 10) throw std::runtime_error("injected packet release failure");
}
void Post(void*, CSplitScreenSlot slot, bool local, int count, const uint64* mask,
    INetworkMessageInternal* type, const CNetMessage* packet, unsigned long size, NetChannelBufType_t buffer) {
    Check(slot.Get() == -1 && !local && count == ABSOLUTE_PLAYER_LIMIT && mask &&
          type == reinterpret_cast<INetworkMessageInternal*>(&definition) &&
          packet == reinterpret_cast<CNetMessage*>(&outgoing) && size == 0 && buffer == BUF_RELIABLE,
          "targeted reliable game-event delivery");
    if (mode == 4) throw std::runtime_error("injected engine send failure");
    ++sends;
    std::memcpy(recipients.data(), mask, sizeof(recipients));
    Check(outgoing.proto->SerializeToString(&wire), "serialize actual upstream protocol");
}
void Int(Message& message, const char* field, int value) {
    message.GetReflection()->SetInt32(&message, message.GetDescriptor()->FindFieldByName(field), value);
}
int Int(const Message& message, const char* field) {
    return message.GetReflection()->GetInt32(message, message.GetDescriptor()->FindFieldByName(field));
}
void Text(Message& message, const char* field, const char* value) {
    message.GetReflection()->SetString(&message, message.GetDescriptor()->FindFieldByName(field), value);
}
std::string Text(const Message& message, const char* field) {
    return message.GetReflection()->GetString(message, message.GetDescriptor()->FindFieldByName(field));
}
Message* Add(Message& message, const char* field) {
    return message.GetReflection()->AddMessage(&message, message.GetDescriptor()->FindFieldByName(field));
}
const Message& Key(const Message& message, int index) {
    return message.GetReflection()->GetRepeatedMessage(message, message.GetDescriptor()->FindFieldByName("keys"), index);
}
IGameEvent* Create(void*, const char* name, bool force, int* cookie) {
    Check(std::strcmp(name, "show_survival_respawn_status") == 0 && force && !cookie, "native menu event lookup");
    if (mode == 12) throw std::runtime_error("injected event creation failure");
    if (mode == 5) return nullptr;
    ++creations;
    token.clear(); duration = player = -1;
    return reinterpret_cast<IGameEvent*>(&native_event);
}
void EventText(void*, const GameEventKeySymbol_t& key, const char* value) {
    Check(std::strcmp(key.GetString(), "loc_token") == 0, "native event text key");
    if (mode == 6) throw std::runtime_error("injected setter failure");
    token = value;
}
void EventInt(void*, const GameEventKeySymbol_t& key, int value) {
    Check(std::strcmp(key.GetString(), "duration") == 0, "only duration uses a numeric setter");
    duration = value;
}
void EventPlayer(void*, const GameEventKeySymbol_t& key, CPlayerSlot slot) {
    Check(std::strcmp(key.GetString(), "userid") == 0, "typed native player key");
    player = slot.Get();
}
void Free(void*, IGameEvent* event) {
    Check(event == reinterpret_cast<IGameEvent*>(&native_event), "event released by its manager");
    ++frees;
    if (mode == 9) throw std::runtime_error("injected event release failure");
}
bool Serialize(void*, IGameEvent* event, CNetMessagePB<CMsgSource1LegacyGameEvent>* packet) {
    Check(event == reinterpret_cast<IGameEvent*>(&native_event) &&
          reinterpret_cast<void*>(packet) == &outgoing, "native event serializer receives allocated packet");
    if (mode == 7) return false;
    if (mode == 8) throw std::runtime_error("injected serialization failure");
    Check(duration >= 0 && player >= 0, "native fields set before serialization");
    auto& proto = *outgoing.proto;
    Int(proto, "eventid", event_id); Text(proto, "event_name", "show_survival_respawn_status");
    auto* user = Add(proto, "keys"); Int(*user, "type", 3); Int(*user, "val_long", player);
    auto* text = Add(proto, "keys"); Int(*text, "type", 1); Text(*text, "val_string", token.c_str());
    auto* expires = Add(proto, "keys"); Int(*expires, "type", 3); Int(*expires, "val_long", duration);
    auto* pawn = Add(proto, "keys"); Int(*pawn, "type", 3); Int(*pawn, "val_long", 10000 + player);
    return true;
}

}

int main(int argc, char** argv) {
    try {
        Check(argc == 2, "menu_packet_test upstream-descriptors.pb");
        std::ifstream input(argv[1], std::ios::binary);
        google::protobuf::FileDescriptorSet descriptors;
        Check(descriptors.ParseFromIstream(&input), "parse pinned SDK descriptors");
        google::protobuf::DescriptorPool pool;
        for (const auto& file : descriptors.file()) Check(pool.BuildFile(file), "build upstream schema");
        google::protobuf::DynamicMessageFactory factory(&pool);
        auto create = [&](const char* name) {
            auto* descriptor = pool.FindMessageTypeByName(name);
            Check(descriptor, "upstream event protocol exists");
            return std::unique_ptr<Message>(factory.GetPrototype(descriptor)->New());
        };
        auto list = create("CMsgSource1LegacyGameEventList");
        auto event = create("CMsgSource1LegacyGameEvent");
        auto decoded = create("CMsgSource1LegacyGameEvent");
        outgoing.proto = event.get();
        wrong_proto = list.get();
        Install<&CNetMessage::AsProto>(message_table, &AsProto);
        Install<&INetworkMessages::FindNetworkMessage>(messages_table, &Find);
        Install<&INetworkMessages::DeallocateNetMessageAbstract>(messages_table, &Release);
        Install<&INetworkMessageInternal::AllocateMessage>(definition_table, &Allocate);
        using PostMethod = void (IGameEventSystem::*)(CSplitScreenSlot, bool, int, const uint64*,
            INetworkMessageInternal*, const CNetMessage*, unsigned long, NetChannelBufType_t);
        Install<static_cast<PostMethod>(&IGameEventSystem::PostEventAbstract)>(events_table, &Post);
        sr::Cs2MenuBackend backend(reinterpret_cast<INetworkMessages*>(&messages), reinterpret_cast<IGameEventSystem*>(&events));
        Install<&IGameEventManager2::CreateEvent>(manager_table, &Create);
        Install<&IGameEventManager2::FreeEvent>(manager_table, &Free);
        Install<&IGameEventManager2::SerializeEvent>(manager_table, &Serialize);
        Install<&IGameEvent::SetString>(event_table, &EventText);
        Install<&IGameEvent::SetInt>(event_table, &EventInt);
        using SetPlayerMethod = void (IGameEvent::*)(const GameEventKeySymbol_t&, CPlayerSlot);
        Install<static_cast<SetPlayerMethod>(&IGameEvent::SetPlayer)>(event_table, &EventPlayer);
        auto* native = reinterpret_cast<IGameEventManager2*>(&manager);
        Check(backend.Render(nullptr, 3, "menu") == KEEL_RESULT_NOT_READY, "missing native manager refused");
        const std::string html = "<font color='#FFF'>Menu 100% &amp; text</font>";
        Check(backend.Render(native, 3, html) == KEEL_RESULT_OK && decoded->ParseFromString(wire), "render and decode wire payload");
        Check(Int(*decoded, "eventid") == 413 && Text(*decoded, "event_name") == "show_survival_respawn_status" &&
              Int(Key(*decoded, 2), "val_long") == 1 && Text(Key(*decoded, 1), "val_string") == html &&
              Int(Key(*decoded, 0), "val_long") == 3 &&
              Int(Key(*decoded, 3), "val_long") == 10003, "wire uses engine key order, types and pawn identity without an advertisement");
        Check(recipients[0] == 8, "private menu targets only slot 3");
        for (std::size_t i = 1; i < recipients.size(); ++i) Check(recipients[i] == 0, "no high-slot broadcast");
        Check(backend.Render(native, 3, "") == KEEL_RESULT_OK && decoded->ParseFromString(wire) &&
              Int(Key(*decoded, 2), "val_long") == 0 && Text(Key(*decoded, 1), "val_string").empty(), "clear expires visible menu");
        const int last = ABSOLUTE_PLAYER_LIMIT - 1;
        Check(backend.Render(native, last, html) == KEEL_RESULT_OK &&
              recipients[static_cast<unsigned>(last) / 64] == (uint64{1} << (last % 64)), "recipient mask reaches last slot");
        for (mode = 1; mode <= 12; ++mode) {
            Check(backend.Render(native, 3, html) == KEEL_RESULT_ENGINE_FAILURE, "engine failures reported");
            Check(allocations == releases && creations == frees, "event and message resources released on every failure");
        }
        mode = 0;
        Check(backend.Render(native, -1, html) == KEEL_RESULT_INVALID_ARGUMENT &&
              backend.Render(native, 3, std::string(2049, 'x')) == KEEL_RESULT_INVALID_ARGUMENT, "renderer bounds");
        event_id = 614;
        Check(backend.Render(native, 3, html) == KEEL_RESULT_OK && decoded->ParseFromString(wire) &&
              Int(*decoded, "eventid") == 614 && backend.Error().empty(), "engine descriptor changes and recovery need no advertisement");
        Check(allocations == releases && creations == frees, "balanced native resource lifetimes");
        std::cout << "native menu serialization, typed player identity, recipients, clear and failure cleanup passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
