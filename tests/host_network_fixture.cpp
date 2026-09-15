#include "host_fixture.h"
#include "gameevents.pb.h"
#include <igameevents.h>
#include <engine/igameeventsystem.h>
#include <networksystem/inetworkmessages.h>
#include <keels2/keelhook.hpp>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/dynamic_message.h>
#include <array>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace {
using Message = google::protobuf::Message;
struct Interface { void** table; };
struct Packet { void** table; Message* proto; };
using PostMethod = void (IGameEventSystem::*)(CSplitScreenSlot, bool, int, const uint64*,
    INetworkMessageInternal*, const CNetMessage*, unsigned long, NetChannelBufType_t);

struct NetworkFixture {
    google::protobuf::DescriptorPool pool;
    google::protobuf::DynamicMessageFactory factory{&pool};
    std::unique_ptr<Message> output;
    std::array<void*, 128> messages_table{}, definition_table{}, packet_table{}, events_table{}, manager_table{}, event_table{};
    Interface messages{messages_table.data()}, definition{definition_table.data()}, events{events_table.data()};
    Interface manager{manager_table.data()}, event{event_table.data()};
    Packet outgoing{packet_table.data(), nullptr};
    std::string html, token;
    int duration = -1, player = -1;
    unsigned active_events = 0;
    unsigned allocations = 0;
};
alignas(NetworkFixture) std::array<std::byte, sizeof(NetworkFixture)> network_storage{};
NetworkFixture* network = nullptr;

void Check(bool value) { if (!value) throw std::runtime_error("native menu network fixture contract"); }
void ValidateInterface(void*) {}
template <auto Method, typename Function>
void Install(std::array<void*, 128>& table, Function function) {
    const auto slot = keels2::kh::VirtualIndex<Method>();
    Check(slot && *slot < table.size());
    std::memcpy(&table[*slot], &function, sizeof(function));
}
void* AsProto(const Packet* packet) { return packet->proto; }
INetworkMessageInternal* FindName(void*, const char*) { return nullptr; }
INetworkMessageInternal* Find(void*, int id) {
    Check(id == GE_Source1LegacyGameEvent);
    return reinterpret_cast<INetworkMessageInternal*>(&network->definition);
}
CNetMessage* Allocate(void*) {
    ++network->allocations;
    return reinterpret_cast<CNetMessage*>(&network->outgoing);
}
void Release(void*, INetworkMessageInternal* definition, CNetMessage* message) {
    Check(network->allocations && definition == reinterpret_cast<INetworkMessageInternal*>(&network->definition) &&
        message == reinterpret_cast<CNetMessage*>(&network->outgoing));
    --network->allocations;
}
void Post(void*, CSplitScreenSlot slot, bool local, int count, const uint64* mask,
    INetworkMessageInternal*, const CNetMessage* message, unsigned long size, NetChannelBufType_t buffer) {
    Check(slot.Get() == -1 && !local && count == ABSOLUTE_PLAYER_LIMIT && mask && mask[0] == 8 &&
        size == 0 && buffer == BUF_RELIABLE && message == reinterpret_cast<CNetMessage*>(&network->outgoing));
    for (unsigned i = 1; i < (ABSOLUTE_PLAYER_LIMIT + 63) / 64; ++i) Check(!mask[i]);
    auto& output = *network->output;
    const auto* reflection = output.GetReflection();
    const auto* fields = output.GetDescriptor();
    Check(reflection->GetInt32(output, fields->FindFieldByName("eventid")) == 413 && network->active_events == 1);
    const auto& key = reflection->GetRepeatedMessage(output, fields->FindFieldByName("keys"), 1);
    network->html = key.GetReflection()->GetString(key, key.GetDescriptor()->FindFieldByName("val_string"));
}
void Integer(Message& message, const char* name, int value) {
    message.GetReflection()->SetInt32(&message, message.GetDescriptor()->FindFieldByName(name), value);
}
void Text(Message& message, const char* name, const char* value) {
    message.GetReflection()->SetString(&message, message.GetDescriptor()->FindFieldByName(name), value);
}
Message* Add(Message& message, const char* name) {
    return message.GetReflection()->AddMessage(&message, message.GetDescriptor()->FindFieldByName(name));
}
IGameEvent* Create(void*, const char* name, bool force, int* cookie) {
    Check(std::strcmp(name, "show_survival_respawn_status") == 0 && force && !cookie && !network->active_events);
    ++network->active_events;
    network->token.clear(); network->duration = network->player = -1;
    return reinterpret_cast<IGameEvent*>(&network->event);
}
void EventText(void*, const GameEventKeySymbol_t& key, const char* value) {
    Check(std::strcmp(key.GetString(), "loc_token") == 0); network->token = value;
}
void EventInt(void*, const GameEventKeySymbol_t& key, int value) {
    Check(std::strcmp(key.GetString(), "duration") == 0); network->duration = value;
}
void EventPlayer(void*, const GameEventKeySymbol_t& key, CPlayerSlot value) {
    Check(std::strcmp(key.GetString(), "userid") == 0); network->player = value.Get();
}
void Free(void*, IGameEvent* event) {
    Check(event == reinterpret_cast<IGameEvent*>(&network->event) && network->active_events == 1);
    --network->active_events;
}
bool Serialize(void*, IGameEvent* event, CNetMessagePB<CMsgSource1LegacyGameEvent>* packet) {
    Check(event == reinterpret_cast<IGameEvent*>(&network->event) &&
        reinterpret_cast<void*>(packet) == &network->outgoing && network->active_events == 1 &&
        network->duration >= 0 && network->player == 3);
    auto& output = *network->output;
    Integer(output, "eventid", 413); Text(output, "event_name", "show_survival_respawn_status");
    auto* duration = Add(output, "keys"); Integer(*duration, "type", 3); Integer(*duration, "val_long", network->duration);
    auto* token = Add(output, "keys"); Integer(*token, "type", 1); Text(*token, "val_string", network->token.c_str());
    auto* player = Add(output, "keys"); Integer(*player, "type", 3); Integer(*player, "val_long", network->player);
    return true;
}

}

extern "C" bool SrNetworkInitialize(const char* path) {
    try {
        if (network) std::destroy_at(network);
        network = nullptr;
        network = std::construct_at(reinterpret_cast<NetworkFixture*>(network_storage.data()));
        auto validation = &ValidateInterface;
        std::memcpy(&network->messages_table[0], &validation, sizeof(validation));
        std::memcpy(&network->events_table[0], &validation, sizeof(validation));
        std::ifstream input(path, std::ios::binary);
        google::protobuf::FileDescriptorSet descriptors;
        Check(descriptors.ParseFromIstream(&input));
        for (const auto& file : descriptors.file()) Check(network->pool.BuildFile(file));
        const auto create = [](const char* name) {
            const auto* descriptor = network->pool.FindMessageTypeByName(name);
            Check(descriptor != nullptr);
            return std::unique_ptr<Message>(network->factory.GetPrototype(descriptor)->New());
        };
        network->output = create("CMsgSource1LegacyGameEvent");
        Install<&CNetMessage::AsProto>(network->packet_table, &AsProto);
        Install<&INetworkMessages::FindNetworkMessage>(network->messages_table, &FindName);
        Install<&INetworkMessages::FindNetworkMessageById>(network->messages_table, &Find);
        Install<&INetworkMessages::DeallocateNetMessageAbstract>(network->messages_table, &Release);
        Install<&INetworkMessageInternal::AllocateMessage>(network->definition_table, &Allocate);
        Install<static_cast<PostMethod>(&IGameEventSystem::PostEventAbstract)>(network->events_table, &Post);
        Install<&IGameEventManager2::CreateEvent>(network->manager_table, &Create);
        Install<&IGameEventManager2::FreeEvent>(network->manager_table, &Free);
        Install<&IGameEventManager2::SerializeEvent>(network->manager_table, &Serialize);
        Install<&IGameEvent::SetString>(network->event_table, &EventText);
        Install<&IGameEvent::SetInt>(network->event_table, &EventInt);
        using SetPlayerMethod = void (IGameEvent::*)(const GameEventKeySymbol_t&, CPlayerSlot);
        Install<static_cast<SetPlayerMethod>(&IGameEvent::SetPlayer)>(network->event_table, &EventPlayer);
        network->outgoing.proto = network->output.get();
        return true;
    } catch (...) { if (network) std::destroy_at(network); network = nullptr; return false; }
}
extern "C" void* SrNetworkInterface(const char* name) {
    if (!network || !name) return nullptr;
    if (std::strcmp(name, NETWORKMESSAGES_INTERFACE_VERSION) == 0) return &network->messages;
    if (std::strcmp(name, GAMEEVENTSYSTEM_INTERFACE_VERSION) == 0) return &network->events;
    return nullptr;
}
extern "C" void* SrNetworkGameEventManager() { return network ? &network->manager : nullptr; }
extern "C" const char* SrNetworkMenuText() { return network ? network->html.c_str() : ""; }
extern "C" bool SrNetworkStop() {
    const bool clear = !network || (!network->allocations && !network->active_events);
    if (network) std::destroy_at(network);
    network = nullptr;
    google::protobuf::ShutdownProtobufLibrary();
    return clear;
}
