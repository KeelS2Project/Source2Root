#if defined _source2root_sdktools_included
    #endinput
#endif
#define _source2root_sdktools_included
#include <source2root>

enum Entity { NoEntity = 0 }
enum SchemaField { NoSchemaField = 0 }
enum SchemaType
{
    Schema_Char = 1, Schema_Int8, Schema_UInt8, Schema_Int16, Schema_UInt16,
    Schema_Int32, Schema_UInt32, Schema_Int64, Schema_UInt64,
    Schema_Float32, Schema_Float64, Schema_Bool, Schema_EntityHandle, Schema_Vector3
}
// Game-thread operations through Keel's entity/schema services. Availability,
// field names and types depend on the active game adapter and compatibility
// profile. No raw memory addresses or guessed offsets are exposed. This
// SDKTools layer provides typed reads/writes; it does not implement spawning,
// teleportation, tracing, sound, temp entities or the SourceMod SDKCall API.
// Handles belong to this script generation and are closed on reload/unload.
// Entity identity includes the engine serial and map epoch; retained entities
// never silently retarget when an index is reused. Close stale entities too.
// Provider limits: 256 entities and 128 fields, plus core per-script resources.
// Invalid/wrong-owner/closed script handles are native errors. Missing engine
// objects, unavailable services or incompatible fields return failure and set
// GetLastError. Entity_IsValid is false for stale/unavailable engine objects.
native Entity Entity_Find(int index);
// Source2 packed handle bits, NOT a SourcePawn Entity handle. -1 is invalid.
// Do not persist packed handles across maps; retain an owned Entity instead.
native Entity Entity_FromHandle(int sourceHandle);
// Returns current pawn (default) or controller; revalidates the connection and
// selected entity after lookup. The result does not track subsequent respawns.
native Entity Entity_FromPlayer(Player player, bool pawn = true);
native bool Entity_Close(Entity entity);
native bool Entity_IsValid(Entity entity);
native int Entity_Index(Entity entity); // -1 on engine failure
native bool Entity_SourceHandle(Entity entity, int& sourceHandle);
native bool Entity_Same(Entity left, Entity right);
// ASCII schema names <=255 bytes; type must match exactly. Scalar fields only;
// strings, arrays, pointers and embedded service structures are not exposed.
native SchemaField Schema_Find(const char[] className, const char[] fieldName, SchemaType type);
native bool Schema_Close(SchemaField field);
native SchemaType Schema_FieldType(SchemaField field);
native int Schema_FieldSize(SchemaField field);
native bool Schema_FieldInfo(SchemaField field, char[] className, int classCapacity,
    char[] fieldName, int fieldCapacity, char[] profile, int profileCapacity);
// Int accepts scalar signed/unsigned integers and bool. Char returns its byte
// value (0..255). Values outside signed32 range fail; use exact decimal text for
// full-width int64/uint64. No floating-to-integer or integer-to-float coercion.
// Typed outputs are zeroed on failure; string output is empty, source handle -1.
native bool Entity_ReadInt(Entity entity, SchemaField field, int& value);
native bool Entity_ReadIntegerText(Entity entity, SchemaField field, char[] value, int capacity);
// Float accepts float32/float64, rejecting nonfinite/out-of-range values. Float64
// is rounded to SourcePawn float32. Vectors contain three finite float32 values.
native bool Entity_ReadFloat(Entity entity, SchemaField field, float& value);
native bool Entity_ReadVector(Entity entity, SchemaField field, float value[3]);
native bool Entity_ReadSourceHandle(Entity entity, SchemaField field, int& sourceHandle);
// Opens a separately owned entity referenced by an entity-handle field. Returns
// NoEntity for the engine's invalid reference (-1), or on a lookup/read failure.
native Entity Entity_ReadEntity(Entity entity, SchemaField field);

#define ENTITY_WRITE_NUMERIC_FIELDS 1
// Optional host keels2.entity_writes API1. Output mask is zero on failure.
// Binary capabilities do not guarantee that a particular entity can be written.
native bool Entity_GetWriteCapabilities(int& capabilities);
// Exact schema width/signedness; overflow and bool values other than 0/1 fail.
// The adapter marks changed entities for network replication. This does not
// invoke game-specific setters or change callbacks. Use dedicated game actions
// when their semantics are required. No pointer, handle, string or array writes.
// Failure during an engine notification can occur after the field was written.
// Close/reload callbacks can invalidate resources while a write is completing.
native bool Entity_WriteInt(Entity entity, SchemaField field, int value);
// Decimal full-width signed/unsigned64, narrowed only after range validation.
// No whitespace, leading '+', hexadecimal or trailing characters.
native bool Entity_WriteIntegerText(Entity entity, SchemaField field, const char[] value);
// Float64 fields receive the exact widening of SourcePawn's finite float32.
native bool Entity_WriteFloat(Entity entity, SchemaField field, float value);
native bool Entity_WriteVector(Entity entity, SchemaField field, const float value[3]);
