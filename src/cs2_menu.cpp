#include "cs2_menu.h"

#include <google/protobuf/message.h>

#include <array>
#include <stdexcept>

namespace sr {
namespace {

const google::protobuf::FieldDescriptor* Field(const google::protobuf::Message& message,
                                              const char* name,
                                              google::protobuf::FieldDescriptor::CppType type,
                                              bool repeated = false) {
    const auto* field = message.GetDescriptor()->FindFieldByName(name);
    if (!field || field->cpp_type() != type || field->is_repeated() != repeated)
        throw std::runtime_error(std::string("CS2 protobuf field mismatch: ") + name);
    return field;
}

using FD = google::protobuf::FieldDescriptor;

void SetInt(google::protobuf::Message& message, const char* name, int value) {
    message.GetReflection()->SetInt32(&message, Field(message, name, FD::CPPTYPE_INT32), value);
}

void SetString(google::protobuf::Message& message, const char* name, const std::string& value) {
    message.GetReflection()->SetString(&message, Field(message, name, FD::CPPTYPE_STRING), value);
}

}

void Cs2MenuBackend::Observe(const CNetMessage* message) {
    if (!message) return;
    const auto* proto = static_cast<const google::protobuf::Message*>(message->AsProto());
    if (!proto || proto->GetDescriptor()->name() != "CMsgSource1LegacyGameEventList") return;
    try {
        const auto* descriptors = Field(*proto, "descriptors", FD::CPPTYPE_MESSAGE, true);
        const auto* reflection = proto->GetReflection();
        for (int i = 0; i < reflection->FieldSize(*proto, descriptors); ++i) {
            const auto& descriptor = reflection->GetRepeatedMessage(*proto, descriptors, i);
            const auto* fields = descriptor.GetReflection();
            if (fields->GetString(descriptor, Field(descriptor, "name", FD::CPPTYPE_STRING)) !=
                "show_survival_respawn_status") continue;
            const int id = fields->GetInt32(descriptor, Field(descriptor, "eventid", FD::CPPTYPE_INT32));
            const auto* key_field = Field(descriptor, "keys", FD::CPPTYPE_MESSAGE, true);
            std::vector<Key> keys;
            bool text = false, duration = false, user = false;
            const int count = fields->FieldSize(descriptor, key_field);
            if (id < 0 || count < 3 || count > 32) throw std::runtime_error("invalid menu event descriptor");
            for (int j = 0; j < count; ++j) {
                const auto& key = fields->GetRepeatedMessage(descriptor, key_field, j);
                const auto* kr = key.GetReflection();
                Key entry{kr->GetString(key, Field(key, "name", FD::CPPTYPE_STRING)),
                          kr->GetInt32(key, Field(key, "type", FD::CPPTYPE_INT32))};
                if (entry.type < 0 || entry.type > 7) throw std::runtime_error("unsupported legacy event key type");
                if (entry.name == "loc_token") text = entry.type == 1;
                if (entry.name == "duration") duration = entry.type >= 3 && entry.type <= 5;
                if (entry.name == "userid") user = entry.type >= 3 && entry.type <= 5;
                keys.push_back(std::move(entry));
            }
            if (!text || !duration || !user) throw std::runtime_error("menu event lacks typed text/duration/user keys");
            keys_ = std::move(keys);
            event_id_ = id;
            error_.clear();
            return;
        }
        MapChanged();
        error_ = "CS2 did not advertise show_survival_respawn_status";
    } catch (const std::exception& error) {
        MapChanged();
        error_ = error.what();
    }
}

KeelResult Cs2MenuBackend::Render(int slot, const std::string& html) {
    if (slot < 0 || slot >= ABSOLUTE_PLAYER_LIMIT || html.size() > 2048)
        return KEEL_RESULT_INVALID_ARGUMENT;
    if (!messages_ || !events_ || !Ready()) return KEEL_RESULT_NOT_READY;
    INetworkMessageInternal* definition = nullptr;
    CNetMessage* allocated = nullptr;
    KeelResult result = KEEL_RESULT_OK;
    try {
        definition = messages_->FindNetworkMessage("CMsgSource1LegacyGameEvent");
        if (!definition) throw std::runtime_error("legacy game-event network message unavailable");
        allocated = definition->AllocateMessage();
        if (!allocated) throw std::runtime_error("legacy game-event allocation failed");
        auto* proto = static_cast<google::protobuf::Message*>(allocated->AsProto());
        if (!proto || proto->GetDescriptor()->name() != "CMsgSource1LegacyGameEvent")
            throw std::runtime_error("legacy game-event network message type mismatch");
        proto->Clear();
        SetString(*proto, "event_name", "show_survival_respawn_status");
        SetInt(*proto, "eventid", event_id_);
        const auto* key_field = Field(*proto, "keys", FD::CPPTYPE_MESSAGE, true);
        for (const auto& key : keys_) {
            auto* entry = proto->GetReflection()->AddMessage(proto, key_field);
            SetInt(*entry, "type", key.type);
            const int value = key.name == "duration" ? (html.empty() ? 0 : 1) : key.name == "userid" ? slot : 0;
            switch (key.type) {
            case 0: break;
            case 1: SetString(*entry, "val_string", key.name == "loc_token" ? html : ""); break;
            case 2: entry->GetReflection()->SetFloat(entry, Field(*entry, "val_float", FD::CPPTYPE_FLOAT), 0); break;
            case 3: SetInt(*entry, "val_long", value); break;
            case 4: SetInt(*entry, "val_short", value); break;
            case 5: SetInt(*entry, "val_byte", value); break;
            case 6: entry->GetReflection()->SetBool(entry, Field(*entry, "val_bool", FD::CPPTYPE_BOOL), false); break;
            case 7: entry->GetReflection()->SetUInt64(entry, Field(*entry, "val_uint64", FD::CPPTYPE_UINT64), 0); break;
            }
        }
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
    if (allocated) {
        try { messages_->DeallocateNetMessageAbstract(definition, allocated); }
        catch (...) { error_ = "CS2 menu message release failed"; result = KEEL_RESULT_ENGINE_FAILURE; }
    }
    return result;
}

}
