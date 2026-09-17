#pragma once

#include <keels2/entities.h>
#include <keels2/native_runtime.h>
#include <keels2/players.h>
#include <array>
#include <memory>
#include <stdexcept>
#include <string>

namespace source2root::sdktools {
class Error : public std::runtime_error { public: using std::runtime_error::runtime_error; };
class Service;
class Field final {
public:
    ~Field();
    Field(const Field&) = delete;
    Field& operator=(const Field&) = delete;
    unsigned Type() const { return type_; }
    unsigned Size() const { return size_; }
    const std::string& ClassName() const { return class_; }
    const std::string& Name() const { return name_; }
    const std::string& Profile() const { return profile_; }
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
    bool Same(const Entity& other) const;
    std::int32_t Integer(const Field& field) const;
    std::string IntegerText(const Field& field) const;
    float Number(const Field& field) const;
    std::array<float, 3> Vector(const Field& field) const;
    std::uint32_t SourceHandle(const Field& field) const;
    void Close();
private:
    friend class Service;
    Entity(std::shared_ptr<Service> service, KeelEntityHandle handle);
    void Read(const Field& field, void* output, unsigned size) const;
    std::shared_ptr<Service> service_;
    KeelEntityHandle handle_ = 0;
    KeelEntityInfo identity_{};
};
// Pure service adapter: no game headers, raw engine addresses or schema offsets
// exposed to scripts. Its caller keeps the host services alive. All operations
// run on the game thread; native resources retain this adapter through cleanup.
// Construct Service in a shared_ptr before acquiring resources.
class Service final : public std::enable_shared_from_this<Service> {
public:
    Service(KeelPluginHandle plugin, const KeelEntitiesApi& entities, const KeelSchemaApi& schema,
        const KeelPlayersApi& players, const KeelNativeRuntimeApi& runtime);
    std::unique_ptr<Entity> Find(int index);
    std::unique_ptr<Entity> FromSource(std::uint32_t handle);
    std::unique_ptr<Entity> FromPlayer(const KeelPlayerConnection& player, bool pawn);
    std::unique_ptr<Field> Resolve(const std::string& classname, const std::string& name, unsigned type);
    unsigned EntityCount() const { return entity_count_; }
    unsigned FieldCount() const { return field_count_; }
private:
    friend class Entity;
    friend class Field;
    void Thread() const;
    std::unique_ptr<Entity> Adopt(KeelEntityHandle handle);
    KeelPlayerInfo Player(const KeelPlayerConnection& player) const;
    KeelPluginHandle plugin_;
    const KeelEntitiesApi entities_;
    const KeelSchemaApi schema_;
    const KeelPlayersApi players_;
    const KeelNativeRuntimeApi runtime_;
    unsigned entity_count_ = 0, field_count_ = 0;
};
}
