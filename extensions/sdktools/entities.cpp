#include "entities.h"
#include <keels2/detail/authoring_status.hpp>
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
        const KeelPlayersApi& players, const KeelNativeRuntimeApi& runtime, const KeelEntityWritesApi* writes)
    : plugin_(plugin), entities_(entities), schema_(schema), players_(players), runtime_(runtime), writes_(writes ? *writes : KeelEntityWritesApi{}) {
    if (!plugin || entities.size != sizeof(entities) || entities.api_version != KEELS2_ENTITIES_API_VERSION ||
        !entities.find_by_index || !entities.find_by_source2_handle || !entities.release || !entities.describe || !entities.equal || !entities.read_field ||
        schema.size != sizeof(schema) || schema.api_version != KEELS2_SCHEMA_API_VERSION ||
        !schema.resolve_field || !schema.release_field || !schema.describe_field ||
        players.size != sizeof(players) || players.api_version != KEELS2_PLAYERS_API_VERSION || !players.validate_connection ||
        runtime.size != sizeof(runtime) || runtime.api_version != KEELS2_NATIVE_RUNTIME_API_VERSION || !runtime.check_game_thread)
        throw Error("Incompatible entity/schema/player services.");
    if (writes && (writes->size != sizeof(*writes) || writes->api_version != KEELS2_ENTITY_WRITES_API_VERSION ||
        !writes->capabilities || !writes->write_field)) throw Error("Incompatible entity write service.");
}
void Service::Thread() const { Check(runtime_.check_game_thread(plugin_), "Entity operation"); }
unsigned Service::WriteCapabilities() const {
    Thread();
    if (!writes_.capabilities) throw Error("Entity write service is unavailable.");
    unsigned capabilities = 0;
    Check(writes_.capabilities(plugin_, &capabilities), "Entity write capabilities");
    return capabilities & KEELS2_ENTITY_WRITE_NUMERIC_FIELDS;
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
    if (handle_) service_->entities_.release(service_->plugin_, handle_);
    --service_->entity_count_;
}
void Entity::Close() {
    service_->Thread();
    if (!handle_) return;
    const auto result = service_->entities_.release(service_->plugin_, handle_);
    if (result != KEEL_RESULT_NOT_FOUND && result != KEEL_RESULT_NOT_READY) Check(result, "Release entity");
    handle_ = 0;
}
KeelEntityInfo Entity::Describe() const {
    service_->Thread();
    if (!handle_) throw Error("Entity handle is closed.");
    KeelEntityInfo current{sizeof(current), -1, KEELS2_INVALID_SOURCE2_ENTITY_HANDLE, 0, 0};
    Check(service_->entities_.describe(service_->plugin_, handle_, &current), "Validate entity");
    if (!Identity(identity_, current)) throw Error("Entity identity changed.");
    return current;
}
bool Entity::Valid() const { try { Describe(); return true; } catch (const Error&) { return false; } }
bool Entity::Same(const Entity& other) const {
    if (service_ != other.service_) throw Error("Entities belong to different service owners.");
    const auto left = Describe(), right = other.Describe();
    KeelBool equal = KEEL_FALSE;
    Check(service_->entities_.equal(service_->plugin_, handle_, other.handle_, &equal), "Compare entities");
    if (equal > KEEL_TRUE || (equal == KEEL_TRUE) != Identity(left, right)) throw Error("Host returned inconsistent entity equality.");
    return equal == KEEL_TRUE;
}
void Entity::Read(const Field& field, void* output, unsigned size) const {
    service_->Thread();
    if (!handle_ || !field.handle_) throw Error("Entity or schema field is closed.");
    if (field.service_ != service_ || field.size_ != size) throw Error("Schema field belongs to another owner or has a different type.");
    Describe();
    Check(service_->entities_.read_field(service_->plugin_, handle_, field.handle_, output, size), "Read entity field");
    Describe();
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
    IntegerType(field.type_);
    alignas(8) std::array<std::byte, 8> bytes{};
    Read(field, bytes.data(), field.size_);
    return std::visit([](auto value) -> std::int32_t {
        if (value > std::numeric_limits<std::int32_t>::max()) throw Error("Integer exceeds SourcePawn cell range; use decimal text.");
        if constexpr (std::is_signed_v<decltype(value)>)
            if (value < std::numeric_limits<std::int32_t>::min()) throw Error("Integer exceeds SourcePawn cell range; use decimal text.");
        return static_cast<std::int32_t>(value);
    }, DecodeInteger(field.type_, bytes.data()));
}
std::string Entity::IntegerText(const Field& field) const {
    IntegerType(field.type_);
    alignas(8) std::array<std::byte, 8> bytes{};
    Read(field, bytes.data(), field.size_);
    return std::visit([](auto value) { return std::to_string(value); }, DecodeInteger(field.type_, bytes.data()));
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
