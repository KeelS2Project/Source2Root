#if defined _source2root_database_included
    #endinput
#endif
#define _source2root_database_included

#include <source2root>

enum Database { NoDatabase = 0 }
enum DBStatement { NoStatement = 0 }
enum SQLStep { SQL_Error = -1, SQL_Done = 0, SQL_Row = 1 }

native Database SQL_OpenSQLite(const char[] name);
native bool SQL_Close(Database database);
native DBStatement SQL_Prepare(Database database, const char[] query);
native bool SQL_BindInt(DBStatement statement, int parameter, int value);
native bool SQL_BindFloat(DBStatement statement, int parameter, float value);
native bool SQL_BindString(DBStatement statement, int parameter, const char[] value);
native bool SQL_BindNull(DBStatement statement, int parameter);
native SQLStep SQL_Step(DBStatement statement);
native bool SQL_Reset(DBStatement statement);
native bool SQL_Finalize(DBStatement statement);
native int SQL_ColumnCount(DBStatement statement);
native int SQL_IsNull(DBStatement statement, int column);
native bool SQL_GetInt(DBStatement statement, int column, int& value);
native bool SQL_GetFloat(DBStatement statement, int column, float& value);
native bool SQL_GetString(DBStatement statement, int column, char[] output, int capacity);
native bool SQL_Begin(Database database);
native bool SQL_Commit(Database database);
native bool SQL_Rollback(Database database);
native int SQL_AffectedRows(Database database);
native bool SQL_InsertId(Database database, char[] output, int capacity);
