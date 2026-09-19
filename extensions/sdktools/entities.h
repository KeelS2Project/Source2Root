#pragma once

#include <keels2/entities.h>
#include <keels2/entity_writes.h>
#include <keels2/entity_tools.h>
#include <keels2/entity_construction.h>
#include <keels2/entity_input.h>
#include <keels2/native_runtime.h>
#include <keels2/players.h>
#include <array>
#include <memory>
#include <stdexcept>
#include <string>

namespace source2root::sdktools {
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Service;

class Field final {
public:
    ~Field();
    Field(const Field&) = delete;
    Field& operator=(const Field&) = delete;
    unsigned Type() const {
        return type_;
    }

    unsigned Size() const {
        return size_;
    }

    const std::string& ClassName() const {
        return class_;
    }

    const std::string& Name() const {
        return name_;
    }

    const std::string& Profile() const {
        return profile_;
    }

    void Close();

private:
    friend class Service;
    friend class Entity;
    Field(std::shared_ptr<Service> service, KeelSchemaFieldHandle handle);
    std::shared_ptr<Service> service_;
    KeelSchemaFieldHandle handle_ = 0;
    unsigned type_ = 0, size_ = 0;
    std::string class_, name_, profile_;
};

class Entity final {
public:
    ~Entity();
    Entity(const Entity&) = delete;
    Entity& operator=(const Entity&) = delete;
    KeelEntityInfo Describe() const;
    bool Valid() const;
    bool Pending() const;
    void SetKey(const KeelEntityKeyValue& value) const;
    void Spawn(bool& invoked) const;
    void Input(const char* name, const KeelEntityInputValue& value, bool& invoked,
        const Entity* activator = nullptr, const Entity* caller = nullptr, const Entity* value_entity = nullptr,
        bool queued = false, float delay = 0) const;

    bool Same(const Entity& other) const;
    std::int32_t Integer(const Field& field) const;
    std::string IntegerText(const Field& field) const;
    float Number(const Field& field) const;
    std::array<float, 3> Vector(const Field& field) const;
    std::uint32_t SourceHandle(const Field& field) const;
    void Teleport(unsigned flags, const std::array<float,3>& position,
        const std::array<float,3>& angles, const std::array<float,3>& velocity) const;

    void SetModel(const std::string& model) const;
    void Remove() const;
    void SetInteger(const Field& field, std::int32_t value) const;
    void SetIntegerText(const Field& field, const std::string& value) const;
    void SetNumber(const Field& field, float value) const;
    void SetVector(const Field& field, const std::array<float, 3>& value) const;
    void Close();

private:
    friend class Service;
    Entity(std::shared_ptr<Service> service, KeelEntityHandle handle);
    void Read(const Field& field, void* output, unsigned size) const;
    void Tool(unsigned kind, const KeelEntityTeleport* request, const char* model) const;
    void Write(const Field& field, const void* value, unsigned size) const;
    std::shared_ptr<Service> service_;
    KeelEntityHandle handle_ = 0;
    KeelEntityInfo identity_{};
    bool constructed_ = false;
};
// Pure service adapter: no game headers, raw engine addresses or schema offsets
// exposed to scripts. Its caller keeps the host services alive. All operations
// run on the game thread; native resources retain this adapter through cleanup.
// Construct Service in a shared_ptr before acquiring resources.
class Service final : public std::enable_shared_from_this<Service> {
public:
    Service(KeelPluginHandle plugin,
            const KeelEntitiesApi& entities,
            const KeelSchemaApi& schema,
            const KeelPlayersApi& players,
            const KeelNativeRuntimeApi& runtime,
            const KeelEntityWritesApi* writes = nullptr,
            const KeelEntityToolsApi* tools = nullptr,
            const KeelEntityConstructionApi* construction = nullptr,
            const KeelEntityInputApi* input = nullptr);

    std::array<unsigned,2> InputCapabilities();
    void ConstructionReady() const;
    std::unique_ptr<Entity> Create(const std::string& classname);
    unsigned WriteCapabilities() const;
    unsigned ToolCapabilities() const;
    std::unique_ptr<Entity> Find(int index);
    std::unique_ptr<Entity> FromSource(std::uint32_t handle);
    std::unique_ptr<Entity> FromPlayer(const KeelPlayerConnection& player, bool pawn);
    std::unique_ptr<Field> Resolve(const std::string& classname, const std::string& name, unsigned type);
    unsigned EntityCount() const {
        return entity_count_;
    }

    unsigned FieldCount() const {
        return field_count_;
    }

private:
    friend class Entity;
    friend class Field;
    void Thread() const;
    std::unique_ptr<Entity> Adopt(KeelEntityHandle handle);
    KeelEntityInfo
    Describe(KeelEntityHandle handle, const KeelEntityInfo& expected, bool constructed, bool* pending = nullptr) const;
    KeelPlayerInfo Player(const KeelPlayerConnection& player) const;
    KeelPluginHandle plugin_;
    const KeelEntitiesApi entities_;
    const KeelSchemaApi schema_;
    const KeelPlayersApi players_;
    const KeelNativeRuntimeApi runtime_;
    const KeelEntityWritesApi writes_;
    const KeelEntityToolsApi tools_;
    const KeelEntityConstructionApi construction_;
    const KeelEntityInputApi input_;
    unsigned active_tools_ = 0;
    unsigned active_writes_ = 0;
    unsigned entity_count_ = 0, field_count_ = 0;
};
}
