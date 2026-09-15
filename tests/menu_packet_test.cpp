#include "cs2_menu.h"
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
std::array<void*, 128> message_table{}, messages_table{}, definition_table{}, events_table{};
Interface messages{messages_table.data()}, definition{definition_table.data()}, events{events_table.data()};
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
void* AsProto(const Packet* packet) { return mode == 3 ? nullptr : packet->proto; }
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
        auto* advertised = Add(*list, "descriptors");
        Int(*advertised, "eventid", 413);
        Text(*advertised, "name", "show_survival_respawn_status");
        for (const auto& [name, type] : {std::pair{"duration", 4}, {"loc_token", 1}, {"userid", 3}}) {
            auto* key = Add(*advertised, "keys");
            Text(*key, "name", name); Int(*key, "type", type);
        }
        Install<&CNetMessage::AsProto>(message_table, &AsProto);
        Install<&INetworkMessages::FindNetworkMessage>(messages_table, &Find);
        Install<&INetworkMessages::DeallocateNetMessageAbstract>(messages_table, &Release);
        Install<&INetworkMessageInternal::AllocateMessage>(definition_table, &Allocate);
        using PostMethod = void (IGameEventSystem::*)(CSplitScreenSlot, bool, int, const uint64*,
            INetworkMessageInternal*, const CNetMessage*, unsigned long, NetChannelBufType_t);
        Install<static_cast<PostMethod>(&IGameEventSystem::PostEventAbstract)>(events_table, &Post);
        sr::Cs2MenuBackend backend(reinterpret_cast<INetworkMessages*>(&messages), reinterpret_cast<IGameEventSystem*>(&events));
        Check(backend.Render(3, "menu") == KEEL_RESULT_NOT_READY, "unadvertised menu cannot guess an event ID");
        Packet advertisement{message_table.data(), list.get()};
        backend.Observe(reinterpret_cast<const CNetMessage*>(&advertisement));
        Check(backend.Ready(), "actual protocol advertisement learned");
        const std::string html = "<font color='#FFF'>Menu 100% &amp; text</font>";
        Check(backend.Render(3, html) == KEEL_RESULT_OK && decoded->ParseFromString(wire), "render and decode wire payload");
        Check(Int(*decoded, "eventid") == 413 && Text(*decoded, "event_name") == "show_survival_respawn_status" &&
              Int(Key(*decoded, 0), "val_short") == 1 && Text(Key(*decoded, 1), "val_string") == html &&
              Int(Key(*decoded, 2), "val_long") == 3, "wire respects advertised key order and types");
        Check(recipients[0] == 8, "private menu targets only slot 3");
        for (std::size_t i = 1; i < recipients.size(); ++i) Check(recipients[i] == 0, "no high-slot broadcast");
        Check(backend.Render(3, "") == KEEL_RESULT_OK && decoded->ParseFromString(wire) &&
              Int(Key(*decoded, 0), "val_short") == 0 && Text(Key(*decoded, 1), "val_string").empty(), "clear expires visible menu");
        const int last = ABSOLUTE_PLAYER_LIMIT - 1;
        Check(backend.Render(last, html) == KEEL_RESULT_OK &&
              recipients[static_cast<unsigned>(last) / 64] == (uint64{1} << (last % 64)), "recipient mask reaches last slot");
        for (mode = 1; mode <= 4; ++mode) {
            Check(backend.Render(3, html) == KEEL_RESULT_ENGINE_FAILURE, "engine failures reported");
            Check(allocations == releases, "no message allocation leak on failed send");
        }
        mode = 0;
        Check(backend.Render(-1, html) == KEEL_RESULT_INVALID_ARGUMENT &&
              backend.Render(3, std::string(2049, 'x')) == KEEL_RESULT_INVALID_ARGUMENT, "renderer bounds");
        backend.MapChanged();
        Check(!backend.Ready() && backend.Render(3, html) == KEEL_RESULT_NOT_READY, "map change invalidates event ID");
        auto* invalid = list->GetReflection()->MutableRepeatedMessage(list.get(), list->GetDescriptor()->FindFieldByName("descriptors"), 0);
        auto* key = invalid->GetReflection()->MutableRepeatedMessage(invalid, invalid->GetDescriptor()->FindFieldByName("keys"), 1);
        Int(*key, "type", 7);
        backend.Observe(reinterpret_cast<const CNetMessage*>(&advertisement));
        Check(!backend.Ready(), "incompatible advertised text type refused");
        std::cout << "pinned CS2 wire protocol, recipients, typed advertisement, clear and message lifetime passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
