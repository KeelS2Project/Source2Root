#include <source2root_geoip>
GeoDatabase database;

public bool OnPluginStart()
{
    // Supply your licensed database in data/extensions/source2root.geoip.
    database = GeoIP_Open("GeoLite2-City.mmdb", Loaded);
    return database != NoGeoDatabase &&
           RegisterCommand("sr_geoip", "admin.status", Locate, "Look up an IPv4 or IPv6 address.");
}

public void Loaded(GeoDatabase handle, any data, const char[] error)
{
    if (error[0])
        LogMessage(error);
    else
        LogMessage("Geographic database ready.");
}

public void Locate(Player player, const char[] arguments)
{
    if (!GeoIP_IsReady(database))
    {
        ReplyToCommand(player, "Location lookup is unavailable.");
        return;
    }

    GeoRecord record = GeoIP_Lookup(database, arguments);

    if (record == NoGeoRecord)
    {
        ReplyToCommand(player, "No location found for that address.");
        return;
    }

    char country[256], city[256], message[600];

    if (!GeoIP_Text(record, Geo_CountryName, country, sizeof(country)))
        Format(country, sizeof(country), "Unknown country");

    if (!GeoIP_Text(record, Geo_City, city, sizeof(city)))
        Format(city, sizeof(city), "Unknown city");

    Format(message, sizeof(message), "%s, %s (approximate location)", city, country);
    ReplyToCommand(player, message);
    GeoIP_CloseRecord(record);
}
