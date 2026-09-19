#include "hooks.h"
#include <keels2/detail/authoring_status.hpp>
#include <exception>

namespace source2root::dhooks {
namespace {
void Check(KeelResult result, const char* operation) {
    if (result != KEEL_RESULT_OK)
        throw Error(std::string(operation) + ": " + keels2::detail::ResultDescription(result) + ".");
}

bool Consistent(const KeelEntityInfo& value) {
    return value.size == sizeof(value) && !value.reserved && value.index >= 0 && value.epoch &&
        value.source2_handle != KEELS2_INVALID_SOURCE2_ENTITY_HANDLE;
}
}

EntityLease::~EntityLease() {
    if (handle) {
        service->entities_.release(service->owner_, handle);
        --service->entity_count_;
    }
}

void Service::EntityServices(const KeelEntityAccessApi& access,
                             const KeelEntitiesApi& entities,
                             const KeelPlayersApi& players) {
    Thread();

    if (access_.visit || entity_count_)
        throw Error("Entity services are already configured.");

    if (access.size != sizeof(access) || access.api_version != KEELS2_ENTITY_ACCESS_API_VERSION || !access.visit ||
        entities.size != sizeof(entities) || entities.api_version != KEELS2_ENTITIES_API_VERSION ||
        !entities.find_by_source2_handle || !entities.describe || !entities.release ||
        players.size != sizeof(players) || players.api_version != KEELS2_PLAYERS_API_VERSION || !players.validate_connection)
        throw Error("Incompatible checked entity services.");

    access_ = access;
    entities_ = entities;
    players_ = players;
}

std::shared_ptr<EntityLease> Service::AcquireEntity(std::uint32_t source) {
    Thread();

    if (!access_.visit || source == KEELS2_INVALID_SOURCE2_ENTITY_HANDLE)
        throw Error("Entity reference is invalid or unavailable.");

    if (entity_count_ == 256)
        throw Error("SDKCall entity limit (256) reached.");

    auto lease = std::make_shared<EntityLease>();
    lease->service = shared_from_this();
    KeelEntityHandle handle = 0;
    Check(entities_.find_by_source2_handle(owner_, source, &handle), "Find SDKCall entity");

    if (!handle)
        throw Error("Host returned an empty entity handle.");

    lease->handle = handle;
    ++entity_count_;
    lease->identity.size = sizeof(lease->identity);
    Check(entities_.describe(owner_, handle, &lease->identity), "Describe SDKCall entity");

    if (!Consistent(lease->identity) || lease->identity.source2_handle != source)
        throw Error("Host returned a different or invalid entity identity.");

    return lease;
}

void Service::ValidateEntity(const EntityLease& entity) const {
    Thread();
    KeelEntityInfo current{sizeof(current), -1, KEELS2_INVALID_SOURCE2_ENTITY_HANDLE, 0, 0};
    Check(entities_.describe(owner_, entity.handle, &current), "Validate SDKCall entity");

    if (!Consistent(current) || current.index != entity.identity.index || current.epoch != entity.identity.epoch ||
        current.source2_handle != entity.identity.source2_handle)
        throw Error("SDKCall entity identity changed.");
}

KeelPlayerInfo Service::Player(const KeelPlayerConnection& player) const {
    Thread();

    if (!players_.validate_connection || player.slot < 0 || player.reserved || !player.generation)
        throw Error("SDKCall player connection is invalid or unavailable.");

    KeelPlayerInfo info{};
    info.size = sizeof(info);
    Check(players_.validate_connection(owner_, &player, &info), "Validate SDKCall player");

    if (info.size != sizeof(info) || info.reserved || info.slot != player.slot ||
        info.connection != player.generation || !(info.flags & KEELS2_PLAYER_CONNECTED))
        throw Error("SDKCall player connection changed.");

    return info;
}

void Service::InvokeEntities(const TargetData& target, const std::vector<KeelHookValue>& arguments,
    const std::vector<BufferSpec>& bounds, const std::vector<KeelEntityAccessSpec>& entities,
    const std::vector<unsigned>& slots, KeelHookValue& result) {
    Thread();

    if (!access_.visit || entities.empty() || entities.size() != slots.size())
        throw Error("Checked entity calls are unavailable.");

    struct Context {
        Service& service;
        const TargetData& target;
        std::vector<KeelHookValue> arguments;
        const std::vector<BufferSpec>& bounds;
        const std::vector<unsigned>& slots;
        KeelHookValue& result;
        std::exception_ptr error;
        bool called = false;
    } context{*this, target, arguments, bounds, slots, result, {}};
    const auto invoke = [](void* raw, void* const* pointers, unsigned count) noexcept -> KeelResult {
        auto& value = *static_cast<Context*>(raw);

        try {
            if (value.called || !pointers || count != value.slots.size())
                throw Error("Invalid entity callback from host.");

            value.called = true;

            for (unsigned i = 0; i < count; ++i) {
                if (!pointers[i])
                    throw Error("Host returned a null entity pointer.");

                value.arguments[value.slots[i] - 1].scalar.pointer = pointers[i];
            }

            value.service.Invoke(value.target, 0, value.arguments, value.bounds, value.result);
            return KEEL_RESULT_OK;
        } catch (...) {
            value.error = std::current_exception();
            return KEEL_RESULT_ENGINE_FAILURE;
        }
    };
    const auto status = access_.visit(owner_, entities.data(), static_cast<unsigned>(entities.size()), invoke, &context);

    if (context.error)
        std::rethrow_exception(context.error);

    Check(status, "Invoke checked entity call");

    if (!context.called)
        throw Error("Host omitted the entity call callback.");
}
}
