#include <source2root_database>

public bool OnPluginStart()
{
    // The operator enables the postgresql profile and permits this plugin ID.
    SQLQuery query = SQL_CreateQuery("SELECT $1::text, $2::integer + 1");
    if (query == NoSQLQuery) return false;
    if (!SQL_QueryBindString(query, 1, "Hello from PostgreSQL") || !SQL_QueryBindInt(query, 2, 41))
    {
        SQL_CloseQuery(query);
        return false;
    }
    SQLRequest request = SQL_ExecuteAsync("postgresql", query, Completed);
    SQL_CloseQuery(query);
    return request != NoSQLRequest;
}
public void Completed(SQLRequest request, any data, const char[] error)
{
    if (error[0]) LogMessage(error);
    else
    {
        char message[128];
        int number;
        if (SQL_ResultString(request, 0, 0, message, sizeof(message)) && SQL_ResultInt(request, 0, 1, number))
        {
            char output[160];
            Format(output, sizeof(output), "%s: %d", message, number);
            LogMessage(output);
        }
    }
    SQL_CloseRequest(request);
}
