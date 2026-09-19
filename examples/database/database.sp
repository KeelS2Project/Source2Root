#include <source2root_database>

public bool OnPluginStart()
{
    SQLQuery query = SQL_CreateQuery("SELECT ?");

    if (query == NoSQLQuery)
        return false;

    if (!SQL_QueryBindString(query, 1, "Hello from the database"))
    {
        SQL_CloseQuery(query);
        return false;
    }

    SQLRequest request = SQL_ExecuteAsync("local", query, Completed);
    SQL_CloseQuery(query);
    return request != NoSQLRequest;
}

public void Completed(SQLRequest request, any data, const char[] error)
{
    if (error[0])
        LogMessage(error);
    else
    {
        char value[128];

        if (SQL_ResultString(request, 0, 0, value, sizeof(value)))
            LogMessage(value);
    }

    SQL_CloseRequest(request);
}
