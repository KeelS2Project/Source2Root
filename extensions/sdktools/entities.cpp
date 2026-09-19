#include "entities.h"
#include <keels2/detail/authoring_status.hpp>
#include <keels2/detail/entity_input_copy.hpp>
#include <algorithm>
#include <charconv>
#include <utility>
#include <cmath>
#include <cstring>
#include <limits>
#include <variant>

namespace source2root::sdktools {
namespace {
void Check(KeelResult result, const char* operation) {
    if (result != KEEL_RESULT_OK) throw Error(std::string(operation) + ": " + keels2::detail::ResultDescription(result) + ".");
}
unsigned ValueSize(unsigned type) {
    switch (type) {
        case KEELS2_SCHEMA_CHAR: case KEELS2_SCHEMA_INT8: case KEELS2_SCHEMA_UINT8: case KEELS2_SCHEMA_BOOL: return 1;
        case KEELS2_SCHEMA_INT16: case KEELS2_SCHEMA_UINT16: return 2;
        case KEELS2_SCHEMA_INT32: case KEELS2_SCHEMA_UINT32: case KEELS2_SCHEMA_FLOAT32: case KEELS2_SCHEMA_ENTITY_HANDLE: return 4;
        case KEELS2_SCHEMA_INT64: case KEELS2_SCHEMA_UINT64: case KEELS2_SCHEMA_FLOAT64: return 8;
        case KEELS2_SCHEMA_VECTOR3: return 12;
        default: throw Error("Unknown schema value type.");
    }
}
void Name(const std::string& value) {
    if (value.empty() || value.size() > 255 || std::any_of(value.begin(), value.end(), [](unsigned char c) {
        return !((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == ':');
    })) throw Error("Schema names require 1..255 ASCII letters, digits, underscores or colons.");
}
std::string Copy(const char* value) {
    if (!value) throw Error("Host returned missing schema metadata.");
    std::size_t length = 0;
    while (length <= 255 && value[length]) ++length;
    if (length > 255 || !length) throw Error("Host returned invalid schema metadata.");
    return {value, length};
}
bool Consistent(const KeelEntityInfo& value) {
    return value.size == sizeof(value) && value.index >= 0 && !value.reserved && value.epoch &&
        value.source2_handle != KEELS2_INVALID_SOURCE2_ENTITY_HANDLE;
}
bool Identity(const KeelEntityInfo& left, const KeelEntityInfo& right) {
    return Consistent(left) && Consistent(right) && left.index == right.index &&
        left.source2_handle == right.source2_handle && left.epoch == right.epoch;
}
template <typename T> T Load(const void* data) { T value; std::memcpy(&value, data, sizeof(value)); return value; }
using IntegerValue = std::variant<std::int64_t, std::uint64_t>;
IntegerValue DecodeInteger(unsigned type, const void* data) {
    switch (type) {
        case KEELS2_SCHEMA_CHAR: case KEELS2_SCHEMA_UINT8: return std::uint64_t{Load<std::uint8_t>(data)};
        case KEELS2_SCHEMA_INT8: return std::int64_t{Load<std::int8_t>(data)};
        case KEELS2_SCHEMA_UINT16: return std::uint64_t{Load<std::uint16_t>(data)};
        case KEELS2_SCHEMA_INT16: return std::int64_t{Load<std::int16_t>(data)};
        case KEELS2_SCHEMA_UINT32: return std::uint64_t{Load<std::uint32_t>(data)};
        case KEELS2_SCHEMA_INT32: return std::int64_t{Load<std::int32_t>(data)};
        case KEELS2_SCHEMA_UINT64: return Load<std::uint64_t>(data);
        case KEELS2_SCHEMA_INT64: return Load<std::int64_t>(data);
        case KEELS2_SCHEMA_BOOL: {
            const auto value = Load<std::uint8_t>(data);
            if (value > 1) throw Error("Host returned an invalid boolean value.");
            return std::uint64_t{value};
        }
        default: throw Error("Field is not an integer or boolean.");
    }
}
void IntegerType(unsigned type) {
    if (!((type >= KEELS2_SCHEMA_CHAR && type <= KEELS2_SCHEMA_UINT64) || type == KEELS2_SCHEMA_BOOL))
        throw Error("Field is not an integer or boolean.");
}
template <typename T> std::array<std::byte, 8> EncodeInteger(const IntegerValue& value) {
    const auto number = std::visit([](auto source) -> T {
        if (!std::in_range<T>(source)) throw Error("Integer is outside the schema field's range.");
        return static_cast<T>(source);
    }, value);
    std::array<std::byte, 8> bytes{}; std::memcpy(bytes.data(), &number, sizeof(number)); return bytes;
}
std::array<std::byte, 8> IntegerBytes(unsigned type, const IntegerValue& value) {
    switch (type) {
        case KEELS2_SCHEMA_CHAR: case KEELS2_SCHEMA_UINT8: return EncodeInteger<std::uint8_t>(value);
        case KEELS2_SCHEMA_INT8: return EncodeInteger<std::int8_t>(value);
        case KEELS2_SCHEMA_UINT16: return EncodeInteger<std::uint16_t>(value);
        case KEELS2_SCHEMA_INT16: return EncodeInteger<std::int16_t>(value);
        case KEELS2_SCHEMA_UINT32: return EncodeInteger<std::uint32_t>(value);
        case KEELS2_SCHEMA_INT32: return EncodeInteger<std::int32_t>(value);
        case KEELS2_SCHEMA_UINT64: return EncodeInteger<std::uint64_t>(value);
        case KEELS2_SCHEMA_INT64: return EncodeInteger<std::int64_t>(value);
        case KEELS2_SCHEMA_BOOL:
            if (!std::visit([](auto number) { return number == 0 || number == 1; }, value)) throw Error("Boolean field requires zero or one.");
            return EncodeInteger<std::uint8_t>(value);
        default: throw Error("Field is not an integer or boolean.");
    }
}
IntegerValue ParseInteger(const std::string& text) {
    if (text.empty() || text.size() > 21) throw Error("Integer text must contain a decimal integer.");
    const auto parse = [&]<typename T>() -> IntegerValue {
        T value{};
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{} || end != text.data() + text.size()) throw Error("Integer text is invalid or outside 64-bit range.");
        return value;
    };
    return text[0] == '-' ? parse.template operator()<std::int64_t>() : parse.template operator()<std::uint64_t>();
}

}
Service::Service(KeelPluginHandle plugin, const KeelEntitiesApi& entities, const KeelSchemaApi& schema,
        const KeelPlayersApi& players, const KeelNativeRuntimeApi& runtime, const KeelEntityWritesApi* writes, const KeelEntityToolsApi* tools, const KeelEntityConstructionApi* construction, const KeelEntityInputApi* input)
    : plugin_(plugin), entities_(entities), schema_(schema), players_(players), runtime_(runtime), writes_(writes ? *writes : KeelEntityWritesApi{}), tools_(tools ? *tools : KeelEntityToolsApi{}), construction_(construction ? *construction : KeelEntityConstructionApi{}), input_(input ? *input : KeelEntityInputApi{}) {
    if (!plugin || entities.size != sizeof(entities) || entities.api_version != KEELS2_ENTITIES_API_VERSION ||
        !entities.find_by_index || !entities.find_by_source2_handle || !entities.release || !entities.describe || !entities.equal || !entities.read_field ||
        schema.size != sizeof(schema) || schema.api_version != KEELS2_SCHEMA_API_VERSION ||
        !schema.resolve_field || !schema.release_field || !schema.describe_field ||
        players.size != sizeof(players) || players.api_version != KEELS2_PLAYERS_API_VERSION || !players.validate_connection ||
        runtime.size != sizeof(runtime) || runtime.api_version != KEELS2_NATIVE_RUNTIME_API_VERSION || !runtime.check_game_thread)
        throw Error("Incompatible entity/schema/player services.");
    if (writes && (writes->size != sizeof(*writes) || writes->api_version != KEELS2_ENTITY_WRITES_API_VERSION ||
        !writes->capabilities || !writes->write_field)) throw Error("Incompatible entity write service.");
    if (tools && (tools->size != sizeof(*tools) || tools->api_version != KEELS2_ENTITY_TOOLS_API_VERSION ||
        !tools->capabilities || !tools->teleport || !tools->set_model || !tools->remove)) throw Error("Incompatible entity tools service.");
    if (construction && (construction->size != sizeof(*construction) || construction->api_version != KEELS2_ENTITY_CONSTRUCTION_API_VERSION ||
        !construction->ready || !construction->create || !construction->describe || !construction->set ||
        !construction->teleport || !construction->spawn || !construction->observe || !construction->visit))
        throw Error("Incompatible entity construction service.");
    if (input && (input->size != sizeof(*input) || input->api_version != KEELS2_ENTITY_INPUT_API_VERSION ||
        !input->capabilities || !input->dispatch)) throw Error("Incompatible entity input service.");
}
void Service::Thread() const { Check(runtime_.check_game_thread(plugin_), "Entity operation"); }
void Service::ConstructionReady() const {
    const auto keep = shared_from_this(); keep->Thread();
    if (!keep->construction_.ready) throw Error("Entity construction service is unavailable.");
    Check(keep->construction_.ready(keep->plugin_),"Entity construction availability");
}
std::unique_ptr<Entity> Service::Create(const std::string& classname) {
    const auto keep = shared_from_this(); const auto name = classname; keep->Thread();
    if (name.empty() || name.size() > KEELS2_ENTITY_KEY_MAX_NAME ||
        std::any_of(name.begin(),name.end(),[](unsigned char c) {
            return !((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_');
        })) throw Error("Entity classname requires 1..127 ASCII letters, digits or underscores.");
    if (!keep->construction_.create) throw Error("Entity construction service is unavailable.");
    if (keep->entity_count_ >= 256) throw Error("Entity provider limit (256 handles) reached.");
    if (keep->active_tools_ >= 8) throw Error("Entity operation recursion limit (8) reached.");
    struct Hold { unsigned& count; ~Hold() { --count; } } hold{keep->active_tools_}; ++hold.count;
    // Reserve provider capacity and ownership before any factory callback.
    auto entity = std::unique_ptr<Entity>(new Entity(keep,0)); entity->constructed_ = true;
    Check(keep->construction_.create(keep->plugin_,name.c_str(),&entity->handle_),"Create entity");
    if (!entity->handle_) throw Error("Host returned an empty created entity.");
    KeelEntityInfo info{}; info.size = sizeof(info);
    Check(keep->construction_.describe(keep->plugin_,entity->handle_,&info),"Describe created entity");
    if (!Consistent(info)) throw Error("Host returned invalid created entity identity.");
    entity->identity_ = info; return entity;
}
KeelEntityInfo Service::Describe(KeelEntityHandle handle, const KeelEntityInfo& expected, bool constructed, bool* pending) const {
    if (pending) *pending = false;
    Thread(); if (!handle) throw Error("Entity handle is closed.");
    KeelEntityInfo current{}; current.size = sizeof(current);
    auto result = constructed ? construction_.describe(plugin_,handle,&current) : KEEL_RESULT_NOT_FOUND;
    if (constructed && result == KEEL_RESULT_OK) { if (pending) *pending = true; }
    else if (result == KEEL_RESULT_NOT_FOUND) result = entities_.describe(plugin_,handle,&current);
    Check(result,"Validate entity");
    if (!Identity(expected,current)) throw Error("Entity identity changed.");
    return current;
}
unsigned Service::WriteCapabilities() const {
    Thread();
    if (!writes_.capabilities) throw Error("Entity write service is unavailable.");
    unsigned capabilities = 0;
    Check(writes_.capabilities(plugin_, &capabilities), "Entity write capabilities");
    return capabilities & KEELS2_ENTITY_WRITE_NUMERIC_FIELDS;
}
unsigned Service::ToolCapabilities() const {
    const auto keep = shared_from_this();
    keep->Thread();
    if (!keep->tools_.capabilities) throw Error("Entity tools service is unavailable.");
    unsigned capabilities = 0;
    Check(keep->tools_.capabilities(keep->plugin_, &capabilities), "Entity tools capabilities");
    return capabilities & (KEELS2_ENTITY_TOOL_TELEPORT | KEELS2_ENTITY_TOOL_SET_MODEL | KEELS2_ENTITY_TOOL_REMOVE);
}
std::unique_ptr<Entity> Service::Adopt(KeelEntityHandle handle) {
    if (!handle) throw Error("Host returned an empty entity handle.");
    // Allocate the RAII owner before any metadata call; errors release the host
    // handle even when metadata allocation/validation fails.
    std::unique_ptr<Entity> entity;
    try { entity.reset(new Entity(shared_from_this(), handle)); }
    catch (...) { entities_.release(plugin_, handle); throw; }
    KeelEntityInfo info{sizeof(info), -1, KEELS2_INVALID_SOURCE2_ENTITY_HANDLE, 0, 0};
    Check(entities_.describe(plugin_, handle, &info), "Describe entity");
    if (!Consistent(info)) throw Error("Host returned invalid entity identity.");
    entity->identity_ = info;
    return entity;
}
std::unique_ptr<Entity> Service::Find(int index) {
    Thread();
    if (index < 0) throw Error("Entity index must be nonnegative.");
    if (entity_count_ >= 256) throw Error("Entity provider limit (256 handles) reached.");
    KeelEntityHandle handle = 0;
    Check(entities_.find_by_index(plugin_, index, &handle), "Find entity");
    auto entity = Adopt(handle);
    if (entity->identity_.index != index) throw Error("Host returned a different entity index.");
    return entity;
}
std::unique_ptr<Entity> Service::FromSource(std::uint32_t source) {
    Thread();
    if (source == KEELS2_INVALID_SOURCE2_ENTITY_HANDLE) throw Error("Source2 entity handle is invalid.");
    if (entity_count_ >= 256) throw Error("Entity provider limit (256 handles) reached.");
    KeelEntityHandle handle = 0;
    Check(entities_.find_by_source2_handle(plugin_, source, &handle), "Find Source2 entity");
    auto entity = Adopt(handle);
    if (entity->identity_.source2_handle != source) throw Error("Host returned a different Source2 entity handle.");
    return entity;
}
KeelPlayerInfo Service::Player(const KeelPlayerConnection& player) const {
    if (player.slot < 0 || player.reserved || !player.generation) throw Error("Player connection is invalid.");
    KeelPlayerInfo info{}; info.size = sizeof(info);
    Check(players_.validate_connection(plugin_, &player, &info), "Validate player connection");
    if (info.size != sizeof(info) || info.reserved || info.slot != player.slot || info.connection != player.generation ||
        !(info.flags & KEELS2_PLAYER_CONNECTED)) throw Error("Player connection changed or is not connected.");
    return info;
}
std::unique_ptr<Entity> Service::FromPlayer(const KeelPlayerConnection& player, bool pawn) {
    Thread();
    const auto before = Player(player);
    const auto source = pawn ? before.pawn_handle : before.controller_handle;
    auto entity = FromSource(source);
    const auto after = Player(player);
    if (after.controller_handle != before.controller_handle || (pawn && after.pawn_handle != source))
        throw Error("Player controller or pawn changed during entity lookup.");
    return entity;
}
std::unique_ptr<Field> Service::Resolve(const std::string& classname, const std::string& name, unsigned type) {
    Thread(); Name(classname); Name(name);
    const auto size = ValueSize(type);
    if (field_count_ >= 128) throw Error("Schema provider limit (128 field handles) reached.");
    const KeelSchemaFieldSpec spec{sizeof(spec), KEELS2_SCHEMA_MODULE_SERVER, type, 0, classname.c_str(), name.c_str()};
    KeelSchemaFieldHandle handle = 0;
    Check(schema_.resolve_field(plugin_, &spec, &handle), "Resolve schema field");
    if (!handle) throw Error("Host returned an empty schema field handle.");
    std::unique_ptr<Field> field;
    try { field.reset(new Field(shared_from_this(), handle)); }
    catch (...) { schema_.release_field(plugin_, handle); throw; }
    KeelSchemaFieldInfo info{}; info.size = sizeof(info);
    Check(schema_.describe_field(plugin_, handle, &info), "Describe schema field");
    if (info.size != sizeof(info) || info.module != KEELS2_SCHEMA_MODULE_SERVER || info.value_type != type ||
        info.value_size != size || info.value_alignment != (size == 12 ? 4 : size) || info.offset < 0 || info.reserved)
        throw Error("Host returned incompatible schema metadata.");
    Copy(info.module_name);
    field->class_ = Copy(info.class_name); field->name_ = Copy(info.field_name); field->profile_ = Copy(info.compatibility_profile);
    if (field->class_ != classname || field->name_ != name) throw Error("Host returned a different schema field.");
    field->type_ = type; field->size_ = size;
    return field;
}
Field::Field(std::shared_ptr<Service> service, KeelSchemaFieldHandle handle) : service_(std::move(service)), handle_(handle) { ++service_->field_count_; }
Field::~Field() {
    if (handle_) service_->schema_.release_field(service_->plugin_, handle_);
    --service_->field_count_;
}
void Field::Close() {
    service_->Thread();
    if (!handle_) return;
    const auto result = service_->schema_.release_field(service_->plugin_, handle_);
    if (result != KEEL_RESULT_NOT_FOUND && result != KEEL_RESULT_NOT_READY) Check(result, "Release schema field");
    handle_ = 0;
}
Entity::Entity(std::shared_ptr<Service> service, KeelEntityHandle handle) : service_(std::move(service)), handle_(handle) { ++service_->entity_count_; }
Entity::~Entity() {
    const auto keep = service_; const auto handle = std::exchange(handle_,0);
    --keep->entity_count_;
    if (handle) keep->entities_.release(keep->plugin_,handle);
}
void Entity::Close() {
    const auto keep = service_; keep->Thread();
    const auto handle = std::exchange(handle_,0); if (!handle) return;
    // Release may destroy this Entity through a reentrant script callback.
    const auto result = keep->entities_.release(keep->plugin_,handle);
    if (result != KEEL_RESULT_NOT_FOUND && result != KEEL_RESULT_NOT_READY) Check(result,"Release entity");
}
KeelEntityInfo Entity::Describe() const {
    const auto keep = service_; const auto handle = handle_; const auto expected = identity_;
    const bool constructed = constructed_;
    return keep->Describe(handle,expected,constructed);
}
bool Entity::Valid() const { try { Describe(); return true; } catch (const Error&) { return false; } }
bool Entity::Pending() const {
    const auto keep = service_; const auto handle = handle_; const auto expected = identity_; const bool constructed = constructed_;
    bool pending{}; keep->Describe(handle,expected,constructed,&pending); return pending;
}
bool Entity::Same(const Entity& other) const {
    const auto keep = service_; const auto left_handle = handle_, right_handle = other.handle_;
    const auto left_expected = identity_, right_expected = other.identity_;
    const bool left_created = constructed_, right_created = other.constructed_;
    if (keep != other.service_) throw Error("Entities belong to different service owners.");
    bool left_pending{}, right_pending{};
    auto left = keep->Describe(left_handle,left_expected,left_created,&left_pending);
    const auto right = keep->Describe(right_handle,right_expected,right_created,&right_pending);
    if (left_pending || right_pending) {
        left = keep->Describe(left_handle,left_expected,left_created);
        return Identity(left,right);
    }
    KeelBool equal = KEEL_FALSE;
    Check(keep->entities_.equal(keep->plugin_,left_handle,right_handle,&equal),"Compare entities");
    if (equal > KEEL_TRUE || (equal == KEEL_TRUE) != Identity(left,right)) throw Error("Host returned inconsistent entity equality.");
    return equal == KEEL_TRUE;
}
void Entity::SetKey(const KeelEntityKeyValue& input) const {
    const auto keep = service_; const auto handle = handle_; const auto expected = identity_; const bool constructed = constructed_;
    auto value = input;
    const auto text = [](const char* input, unsigned maximum, bool empty) {
        if (!input) throw Error("Missing entity key text.");
        std::size_t length{}; while (length <= maximum && input[length]) ++length;
        if (length > maximum || (!length && !empty)) throw Error("Entity key name or text exceeds its bounds.");
        return std::string(input,length);
    };
    if (value.size != sizeof(value) || value.type < KEELS2_ENTITY_KEY_STRING || value.type > KEELS2_ENTITY_KEY_COLOR)
        throw Error("Invalid entity key type.");
    const auto name = text(value.name,KEELS2_ENTITY_KEY_MAX_NAME,false);
    const auto string = value.type == KEELS2_ENTITY_KEY_STRING ? text(value.string_value,KEELS2_ENTITY_KEY_MAX_STRING,true) : std::string{};
    value.name = name.c_str(); value.string_value = string.c_str();
    if (value.type == KEELS2_ENTITY_KEY_BOOL && value.int_value != 0 && value.int_value != 1) throw Error("Boolean entity key requires zero or one.");
    if (value.type == KEELS2_ENTITY_KEY_FLOAT && !std::isfinite(value.float_value)) throw Error("Entity key float must be finite.");
    if (value.type == KEELS2_ENTITY_KEY_VECTOR || value.type == KEELS2_ENTITY_KEY_ANGLES)
        for (const float number : value.vector_value) if (!std::isfinite(number)) throw Error("Entity key vector must be finite.");
    keep->Thread();
    if (!constructed) throw Error("Entity was not created by this owner.");
    if (keep->active_tools_ >= 8) throw Error("Entity operation recursion limit (8) reached.");
    struct Hold { unsigned& count; ~Hold() { --count; } } hold{keep->active_tools_}; ++hold.count;
    bool pending{}; keep->Describe(handle,expected,true,&pending);
    if (!pending) throw Error("Entity construction is already consumed.");
    Check(keep->construction_.set(keep->plugin_,handle,&value),"Set entity key");
}
void Entity::Spawn(bool& invoked) const {
    invoked = false;
    const auto keep = service_; const auto handle = handle_; const auto expected = identity_; const bool constructed = constructed_;
    keep->Thread();
    if (!constructed) throw Error("Entity was not created by this owner.");
    if (keep->active_tools_ >= 8) throw Error("Entity operation recursion limit (8) reached.");
    struct Hold { unsigned& count; ~Hold() { --count; } } hold{keep->active_tools_}; ++hold.count;
    bool pending{}; keep->Describe(handle,expected,true,&pending);
    if (!pending) throw Error("Entity construction is already consumed.");
    KeelBool called{}; const auto result = keep->construction_.spawn(keep->plugin_,handle,&called);
    invoked = called != KEEL_FALSE;
    if (called > KEEL_TRUE) throw Error("Host returned an invalid spawn invocation marker.");
    Check(result,"Dispatch entity spawn");
}
std::array<unsigned,2> Service::InputCapabilities() {
    const auto keep = shared_from_this(); keep->Thread();
    if (!keep->input_.capabilities) throw Error("Entity input service is unavailable.");
    if (keep->active_tools_ >= 8) throw Error("Entity operation recursion limit (8) reached.");
    struct Hold { unsigned& count; ~Hold() { --count; } } hold{keep->active_tools_}; ++hold.count;
    std::array<unsigned,2> types{};
    Check(keep->input_.capabilities(keep->plugin_,&types[0],&types[1]),"Entity input capabilities");
    if ((types[0] | types[1]) & ~511u) throw Error("Host returned unknown entity input types.");
    return types;
}
void Entity::Input(const char* name, const KeelEntityInputValue& value, bool& invoked,
    const Entity* activator, const Entity* caller, const Entity* value_entity, bool queued, float delay) const {
    invoked = false;
    const auto keep = service_;
    // Capture every participant before any host call: callbacks may destroy
    // this Entity, any participant, and the provider's service_ member.
    const Entity* participants[]{this,activator,caller,value_entity};
    std::array<KeelEntityHandle,4> handles{};
    std::array<KeelEntityInfo,4> expected{};
    for (std::size_t i = 0; i < handles.size(); ++i) if (participants[i]) {
        if (participants[i]->service_ != keep) throw Error("Input entities belong to different service owners.");
        handles[i] = participants[i]->handle_; expected[i] = participants[i]->identity_;
        if (!handles[i]) throw Error("Input entity handle is closed.");
    }
    if ((value.type == KEELS2_INPUT_ENTITY) != (value_entity != nullptr)) throw Error("Input entity payload does not match its type.");
    keels2::detail::EntityInputCopy copy;
    Check(copy.Assign(name,value,queued ? KEEL_TRUE : KEEL_FALSE,delay),"Entity input value");
    keep->Thread();
    if (!keep->input_.dispatch) throw Error("Entity input service is unavailable.");
    if (keep->active_tools_ >= 8) throw Error("Entity operation recursion limit (8) reached.");
    struct Hold { unsigned& count; ~Hold() { --count; } } hold{keep->active_tools_}; ++hold.count;
    for (std::size_t i = 0; i < handles.size(); ++i)
        if (handles[i]) keep->Describe(handles[i],expected[i],false);
    KeelEntityInputRequest request{}; request.size = sizeof(request);
    request.input = copy.name.data(); request.value = copy.value;
    request.activator = handles[1]; request.caller = handles[2]; request.value_entity = handles[3];
    request.queued = queued ? KEEL_TRUE : KEEL_FALSE; request.delay = delay;
    KeelBool called{};
    const auto result = keep->input_.dispatch(keep->plugin_,handles[0],&request,&called);
    invoked = called != KEEL_FALSE;
    if (called > KEEL_TRUE) throw Error("Host returned an invalid input invocation marker.");
    Check(result,"Dispatch entity input");
}
void Entity::Read(const Field& field, void* output, unsigned size) const {
    const auto keep = service_; const auto handle = handle_, property = field.handle_; const auto expected = identity_;
    if (!handle || !property) throw Error("Entity or schema field is closed.");
    if (field.service_ != keep || field.size_ != size) throw Error("Schema field belongs to another owner or has a different type.");
    // Schema access remains live-only, even for a construction-owned handle.
    keep->Describe(handle,expected,false);
    Check(keep->entities_.read_field(keep->plugin_,handle,property,output,size),"Read entity field");
    keep->Describe(handle,expected,false);
}
void Entity::Write(const Field& field, const void* value, unsigned size) const {
    // Capture ownership before entering host calls. The notification may invoke
    // another script callback that closes and destroys this Entity or Field.
    auto service = service_;
    const auto entity = handle_, property = field.handle_;
    const auto expected = identity_;
    if (!entity || !property) throw Error("Entity or schema field is closed.");
    if (field.service_ != service || field.size_ != size) throw Error("Schema field belongs to another owner or has a different type.");
    service->Thread();
    if (service->active_writes_ >= 8) throw Error("Entity write recursion limit (8) reached.");
    struct Hold { unsigned& count; explicit Hold(unsigned& value) : count(value) { ++count; } ~Hold() { --count; } } hold(service->active_writes_);
    if (!(service->WriteCapabilities() & KEELS2_ENTITY_WRITE_NUMERIC_FIELDS)) throw Error("Entity field writes are unsupported by this game build.");
    KeelEntityInfo current{sizeof(current), -1, KEELS2_INVALID_SOURCE2_ENTITY_HANDLE, 0, 0};
    Check(service->entities_.describe(service->plugin_, entity, &current), "Validate entity write");
    if (!Identity(expected, current)) throw Error("Entity identity changed before the write.");
    // Do not access Entity/Field members after this call, even on failure.
    Check(service->writes_.write_field(service->plugin_, entity, property, value, size), "Write entity field");
}
void Entity::Tool(unsigned kind, const KeelEntityTeleport* request, const char* model) const {
    // Any host call may reenter scripts and destroy this Entity or its caller.
    // Capture everything first; retain the service through callback completion.
    auto service = service_;
    const auto entity = handle_;
    const auto expected = identity_; const bool constructed = constructed_;
    if (!entity) throw Error("Entity handle is closed.");
    service->Thread();
    if (service->active_tools_ >= 8) throw Error("Entity operation recursion limit (8) reached.");
    struct Hold { unsigned& count; explicit Hold(unsigned& value) : count(value) { ++count; } ~Hold() { --count; } } hold(service->active_tools_);
    if (constructed && kind == KEELS2_ENTITY_TOOL_TELEPORT) {
        bool pending{}; service->Describe(entity,expected,true,&pending);
        if (pending) { Check(service->construction_.teleport(service->plugin_,entity,request),"Teleport pending entity"); return; }
    }
    if (!(service->ToolCapabilities() & kind)) throw Error("Entity operation is unsupported by this game build.");
    KeelEntityInfo current{sizeof(current), -1, KEELS2_INVALID_SOURCE2_ENTITY_HANDLE, 0, 0};
    Check(service->entities_.describe(service->plugin_,entity,&current), "Validate entity operation");
    if (!Identity(expected,current)) throw Error("Entity identity changed before the operation.");
    if (kind == KEELS2_ENTITY_TOOL_TELEPORT)
        Check(service->tools_.teleport(service->plugin_,entity,request), "Teleport entity");
    else if (kind == KEELS2_ENTITY_TOOL_SET_MODEL)
        Check(service->tools_.set_model(service->plugin_,entity,model), "Set entity model");
    else Check(service->tools_.remove(service->plugin_,entity), "Remove entity");
}
void Entity::Teleport(unsigned flags, const std::array<float,3>& position,
    const std::array<float,3>& angles, const std::array<float,3>& velocity) const {
    if (!flags || (flags & ~7u)) throw Error("Teleport requires position, angles or velocity flags.");
    KeelEntityTeleport request{}; request.size = sizeof(request); request.flags = flags;
    const std::array<float,3>* inputs[]{&position,&angles,&velocity};
    float* outputs[]{request.position,request.angles,request.velocity};
    for (unsigned i = 0; i < 3; ++i) if (flags & (1u<<i))
        for (unsigned j = 0; j < 3; ++j) {
            const float value = (*inputs[i])[j];
            if (!std::isfinite(value)) throw Error("Teleport requires finite selected vectors.");
            outputs[i][j] = value;
        }
    Tool(KEELS2_ENTITY_TOOL_TELEPORT,&request,nullptr);
}
void Entity::SetModel(const std::string& model) const {
    // Copy before host entry; the input can belong to a callback-owned resource.
    const auto asset = model;
    if (asset.empty() || asset.size() > KEELS2_ENTITY_MODEL_MAX_BYTES ||
        std::any_of(asset.begin(),asset.end(),[](unsigned char c) { return c < 32 || c == 127; }))
        throw Error("Model asset requires 1..511 bytes without control characters.");
    Tool(KEELS2_ENTITY_TOOL_SET_MODEL,nullptr,asset.c_str());
}
void Entity::Remove() const { Tool(KEELS2_ENTITY_TOOL_REMOVE,nullptr,nullptr); }
void Entity::SetInteger(const Field& field, std::int32_t value) const {
    const auto bytes = IntegerBytes(field.type_, IntegerValue{std::int64_t{value}});
    Write(field, bytes.data(), field.size_);
}
void Entity::SetIntegerText(const Field& field, const std::string& text) const {
    const auto bytes = IntegerBytes(field.type_, ParseInteger(text));
    Write(field, bytes.data(), field.size_);
}
void Entity::SetNumber(const Field& field, float value) const {
    if (!std::isfinite(value)) throw Error("Field write requires a finite float.");
    if (field.type_ == KEELS2_SCHEMA_FLOAT32) Write(field, &value, sizeof(value));
    else if (field.type_ == KEELS2_SCHEMA_FLOAT64) { const double wide = value; Write(field, &wide, sizeof(wide)); }
    else throw Error("Field is not floating point.");
}
void Entity::SetVector(const Field& field, const std::array<float, 3>& value) const {
    if (field.type_ != KEELS2_SCHEMA_VECTOR3) throw Error("Field is not a vector.");
    if (!std::all_of(value.begin(), value.end(), [](float number) { return std::isfinite(number); })) throw Error("Vector write requires finite coordinates.");
    Write(field, value.data(), sizeof(value));
}
std::int32_t Entity::Integer(const Field& field) const {
    const auto type = field.type_; IntegerType(type);
    alignas(8) std::array<std::byte, 8> bytes{};
    Read(field, bytes.data(), field.size_);
    return std::visit([](auto value) -> std::int32_t {
        if (value > std::numeric_limits<std::int32_t>::max()) throw Error("Integer exceeds SourcePawn cell range; use decimal text.");
        if constexpr (std::is_signed_v<decltype(value)>)
            if (value < std::numeric_limits<std::int32_t>::min()) throw Error("Integer exceeds SourcePawn cell range; use decimal text.");
        return static_cast<std::int32_t>(value);
    }, DecodeInteger(type, bytes.data()));
}
std::string Entity::IntegerText(const Field& field) const {
    const auto type = field.type_; IntegerType(type);
    alignas(8) std::array<std::byte, 8> bytes{};
    Read(field, bytes.data(), field.size_);
    return std::visit([](auto value) { return std::to_string(value); }, DecodeInteger(type, bytes.data()));
}
float Entity::Number(const Field& field) const {
    double value;
    if (field.type_ == KEELS2_SCHEMA_FLOAT32) { float single{}; Read(field, &single, sizeof(single)); value = single; }
    else if (field.type_ == KEELS2_SCHEMA_FLOAT64) Read(field, &value, sizeof(value));
    else throw Error("Field is not floating point.");
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) throw Error("Value exceeds finite SourcePawn float range.");
    return static_cast<float>(value);
}
std::array<float, 3> Entity::Vector(const Field& field) const {
    if (field.type_ != KEELS2_SCHEMA_VECTOR3) throw Error("Field is not a vector.");
    std::array<float, 3> value{}; Read(field, value.data(), sizeof(value));
    if (!std::all_of(value.begin(), value.end(), [](float number) { return std::isfinite(number); })) throw Error("Vector contains a nonfinite coordinate.");
    return value;
}
std::uint32_t Entity::SourceHandle(const Field& field) const {
    if (field.type_ != KEELS2_SCHEMA_ENTITY_HANDLE) throw Error("Field is not an entity handle.");
    std::uint32_t value{}; Read(field, &value, sizeof(value)); return value;
}
}
