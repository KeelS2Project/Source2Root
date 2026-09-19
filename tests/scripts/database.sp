#include <source2root_database>

Database database;

bool Execute(const char[] sql)
{
    DBStatement statement = SQL_Prepare(database, sql);

    if (statement == NoStatement)
        return false;

    bool ok = SQL_Step(statement) == SQL_Done;
    return SQL_Finalize(statement) && ok;
}

public bool OnPluginStart()
{
    database = SQL_OpenSQLite("acceptance");

    if (database == NoDatabase || !Execute("CREATE TABLE IF NOT EXISTS checks(n INTEGER, text TEXT, real REAL, empty TEXT)")
        || !Execute("DELETE FROM checks") || !SQL_Begin(database))
        return false;

    DBStatement statement = SQL_Prepare(database, "INSERT INTO checks VALUES(?,?,?,?)");

    if (statement == NoStatement || !SQL_BindInt(statement, 1, 42)
        || !SQL_BindString(statement, 2, "quoted '; SQL remains data")
        || !SQL_BindFloat(statement, 3, 1.25) || !SQL_BindNull(statement, 4)
        || SQL_Step(statement) != SQL_Done || SQL_AffectedRows(database) != 1
        || !SQL_Finalize(statement) || !SQL_Commit(database))
        return false;

    char inserted[32];

    if (!SQL_InsertId(database, inserted, sizeof(inserted)) || inserted[0] != '1')
        return false;

    statement = SQL_Prepare(database, "SELECT n,text,real,empty FROM checks");
    int number;
    float fraction;
    char text[128];

    if (statement == NoStatement || SQL_ColumnCount(statement) != 4 || SQL_Step(statement) != SQL_Row
        || !SQL_GetInt(statement, 0, number) || number != 42
        || !SQL_GetFloat(statement, 2, fraction) || fraction != 1.25
        || !SQL_GetString(statement, 1, text, sizeof(text)) || text[0] != 'q'
        || SQL_IsNull(statement, 3) != 1 || SQL_Step(statement) != SQL_Done
        || !SQL_Reset(statement) || SQL_Step(statement) != SQL_Row || !SQL_Finalize(statement))
        return false;

    if (SQL_Prepare(database, "SELECT * FROM does_not_exist") != NoStatement)
        return false;

    GetLastError(text, sizeof(text));

    if (text[0] == 0 || SQL_OpenSQLite("../escape") != NoDatabase)
        return false;

    if (!SQL_Begin(database) || !Execute("DELETE FROM checks") || !SQL_Rollback(database))
        return false;

    statement = SQL_Prepare(database, "SELECT n FROM checks");

    if (!SQL_Close(database) || SQL_Step(statement) != SQL_Row || !SQL_GetInt(statement, 0, number) || number != 42 ||
        !SQL_Reset(statement))
        return false;

    LogMessage("SQLITE_SCRIPT_OK");
    return true;
}
