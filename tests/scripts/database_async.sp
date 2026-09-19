#include <source2root_database>

#if defined DATABASE_CONFIGURED
SQLRequest Configured(const char[] sql, SQLCallback callback, any data = 0)
{
    SQLQuery query = SQL_CreateQuery(sql);

    if (query == NoSQLQuery)
        return NoSQLRequest;

    SQLRequest request = SQL_ExecuteAsync("acceptance", query, callback, data);
    SQL_CloseQuery(query);
    return request;
}
#endif
SQLRequest Submit()
{
#if defined DATABASE_CONFIGURED
#if defined DATABASE_POSTGRESQL
    SQLQuery query = SQL_CreateQuery("SELECT $1::integer,$2::double precision,$3::text,$4::text,2147483648");
#else
    SQLQuery query = SQL_CreateQuery("SELECT ?,?,?,?,2147483648");
#endif
    if (query == NoSQLQuery || !SQL_QueryBindInt(query, 1, 42) || !SQL_QueryBindFloat(query, 2, 1.25)
        || !SQL_QueryBindString(query, 3, "quoted value") || !SQL_QueryBindNull(query, 4))
        return NoSQLRequest;

    SQLRequest request = SQL_ExecuteAsync("acceptance", query, Completed, 73);
    SQL_QueryBindInt(query, 1, 99);
    SQL_CloseQuery(query);
    return request;
#else
    return SQL_QuerySQLiteAsync("async", "SELECT 42,1.25,'quoted value',NULL,2147483648", Completed, 73);
#endif
}

public bool OnPluginStart()
{
    SQLRequest request = Submit();

    if (request == NoSQLRequest || SQL_RequestReady(request) || SQL_ResultRows(request) != -1)
        return false;
#if defined DATABASE_CONFIGURED
    request = Configured("SELECT * FROM missing_table", Failed, 13);
#else
    request = SQL_QuerySQLiteAsync("async", "SELECT * FROM missing_table", Failed, 13);
#endif
    if (request == NoSQLRequest)
        return false;
#if defined DATABASE_CONFIGURED
    request = Configured("SELECT 9", Canceled);
#else
    request = SQL_QuerySQLiteAsync("async", "SELECT 9", Canceled);
#endif
    return request != NoSQLRequest && SQL_CloseRequest(request)
        && RegisterCommand("sr_async", "", Again)
        && RegisterCommand("sr_async_slow", "", Slow);
}

public void Completed(SQLRequest request, any data, const char[] error)
{
    int number;
    float fraction;
    char text[64];

    if (data != 73 || error[0] || !SQL_RequestReady(request) || SQL_ResultRows(request) != 1
        || SQL_ResultColumns(request) != 5 || !SQL_ResultInt(request, 0, 0, number) || number != 42
        || !SQL_ResultFloat(request, 0, 1, fraction) || fraction != 1.25
        || !SQL_ResultString(request, 0, 2, text, sizeof(text)) || text[0] != 'q'
        || SQL_ResultIsNull(request, 0, 3) != 1 || SQL_ResultInt(request, 0, 4, number)
        || SQL_ResultInt(request, 1, 0, number) || SQL_ResultChanges(request) < 0
        || !SQL_ResultInsertId(request, text, sizeof(text)) || text[0] != '0')
        LogMessage("SQL_ASYNC_FAILED");
    else
        LogMessage("SQL_ASYNC_OK");

    SQL_CloseRequest(request);
}

public void Failed(SQLRequest request, any data, const char[] error)
{
    if (data == 13 && error[0] && SQL_RequestReady(request) && SQL_ResultRows(request) == -1)
        LogMessage("SQL_ASYNC_ERROR_OK");
    else
        LogMessage("SQL_ASYNC_FAILED");

    SQL_CloseRequest(request);
}

public
void Canceled(SQLRequest request, any data, const char[] error)
{
    LogMessage("SQL_ASYNC_UNEXPECTED");
}

public
void Again(Player caller, const char[] arguments)
{
    Submit();
}

public void Slow(Player caller, const char[] arguments)
{
#if defined DATABASE_CONFIGURED
    Configured("SELECT 7", Canceled);
#else
    SQL_QuerySQLiteAsync(
        "async",
        "WITH RECURSIVE forever(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM forever) SELECT sum(x) FROM forever",
        Canceled);
#endif
}
