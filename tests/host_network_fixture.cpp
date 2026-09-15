#include "host_fixture.h"
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
    std::unique_ptr<Message> list, output;
    std::array<void*, 128> messages_table{}, definition_table{}, packet_table{}, events_table{};
    Interface messages{messages_table.data()}, definition{definition_table.data()}, events{events_table.data()};
    Packet outgoing{packet_table.data(), nullptr}, advertised{packet_table.data(), nullptr};
    std::string html;
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
INetworkMessageInternal* Find(void*, const char* name) {
    Check(name && std::strcmp(name, "CMsgSource1LegacyGameEvent") == 0);
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
    if (message == reinterpret_cast<CNetMessage*>(&network->advertised)) return;
    Check(slot.Get() == -1 && !local && count == ABSOLUTE_PLAYER_LIMIT && mask && mask[0] == 8 &&
        size == 0 && buffer == BUF_RELIABLE && message == reinterpret_cast<CNetMessage*>(&network->outgoing));
    for (unsigned i = 1; i < (ABSOLUTE_PLAYER_LIMIT + 63) / 64; ++i) Check(!mask[i]);
    auto& output = *network->output;
    const auto* reflection = output.GetReflection();
    const auto* fields = output.GetDescriptor();
    Check(reflection->GetInt32(output, fields->FindFieldByName("eventid")) == 413);
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
        network->list = create("CMsgSource1LegacyGameEventList");
        network->output = create("CMsgSource1LegacyGameEvent");
        auto* entry = Add(*network->list, "descriptors");
        Integer(*entry, "eventid", 413); Text(*entry, "name", "show_survival_respawn_status");
        for (const auto& [name, type] : {std::pair{"duration", 4}, {"loc_token", 1}, {"userid", 3}}) {
            auto* key = Add(*entry, "keys"); Text(*key, "name", name); Integer(*key, "type", type);
        }
        Install<&CNetMessage::AsProto>(network->packet_table, &AsProto);
        Install<&INetworkMessages::FindNetworkMessage>(network->messages_table, &Find);
        Install<&INetworkMessages::DeallocateNetMessageAbstract>(network->messages_table, &Release);
        Install<&INetworkMessageInternal::AllocateMessage>(network->definition_table, &Allocate);
        Install<static_cast<PostMethod>(&IGameEventSystem::PostEventAbstract)>(network->events_table, &Post);
        network->outgoing.proto = network->output.get(); network->advertised.proto = network->list.get();
        return true;
    } catch (...) { if (network) std::destroy_at(network); network = nullptr; return false; }
}
extern "C" void* SrNetworkInterface(const char* name) {
    if (!network || !name) return nullptr;
    if (std::strcmp(name, NETWORKMESSAGES_INTERFACE_VERSION) == 0) return &network->messages;
    if (std::strcmp(name, GAMEEVENTSYSTEM_INTERFACE_VERSION) == 0) return &network->events;
    return nullptr;
}
extern "C" void SrNetworkAdvertise() {
    Check(network != nullptr);
    reinterpret_cast<IGameEventSystem*>(&network->events)->PostEventAbstract(CSplitScreenSlot(-1), false, 0, nullptr,
        nullptr, reinterpret_cast<const CNetMessage*>(&network->advertised), 0, BUF_RELIABLE);
}
extern "C" const char* SrNetworkMenuText() { return network ? network->html.c_str() : ""; }
extern "C" bool SrNetworkStop() {
    const bool clear = !network || !network->allocations;
    if (network) std::destroy_at(network);
    network = nullptr;
    google::protobuf::ShutdownProtobufLibrary();
    return clear;
}
