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

enum SQLRequest { NoSQLRequest = 0 }
typedef SQLCallback = function void(SQLRequest request, any data, const char[] error);

// Executes one statement on a private worker connection to this script's named
// SQLite database. This does not join a synchronous SQL_Begin transaction.
// No parameter placeholders or transaction-control SQL; use SQL_Prepare and
// bound parameters for dynamic values until async bindings are available.
// Results: at most 256 rows, 32 columns, 256 KiB text; each value <=4095 bytes.
// Callback runs once on the server thread (after resume if paused). Empty error
// means success. Close the request to release results or cancel its callback.
// Cancellation is cooperative: a write already committed cannot be undone.
// Reload/unload cancels all requests. Queue full returns NoSQLRequest/GetLastError.
native SQLRequest SQL_QuerySQLiteAsync(const char[] name, const char[] query, SQLCallback callback, any data = 0);
native bool SQL_CloseRequest(SQLRequest request);
native bool SQL_RequestReady(SQLRequest request);
// Counts/IsNull/Changes return -1 on error; typed getters return false and set
// GetLastError. Row and column indices are zero-based. Result handles stay owned
// by the submitting script until explicitly closed or its generation unloads.
native int SQL_ResultRows(SQLRequest request);
native int SQL_ResultColumns(SQLRequest request);
native int SQL_ResultIsNull(SQLRequest request, int row, int column);
native bool SQL_ResultInt(SQLRequest request, int row, int column, int& value);
native bool SQL_ResultFloat(SQLRequest request, int row, int column, float& value);
native bool SQL_ResultString(SQLRequest request, int row, int column, char[] output, int capacity);
native int SQL_ResultChanges(SQLRequest request);
native bool SQL_ResultInsertId(SQLRequest request, char[] output, int capacity);
