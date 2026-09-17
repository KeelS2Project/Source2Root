#include <source2root_geoip>
GeoDatabase city;
GeoRecord saved;

public bool OnPluginStart()
{
    if (GeoIP_Open("../city.mmdb", Unexpected)) return false;
    city = GeoIP_Open("city.mmdb", Loaded, 7);
    GeoDatabase missing = GeoIP_Open("missing.mmdb", Missing);
    GeoDatabase canceled = GeoIP_Open("city.mmdb", Unexpected);
    if (!city || !missing || !canceled || GeoIP_IsReady(city) || !GeoIP_IsLoading(city) || !GeoIP_Close(canceled)) return false;
    return RegisterCommand("sr_geo_reload", "", Reload) && RegisterCommand("sr_geo_pending", "", Pending) &&
        RegisterCommand("sr_geo_close", "", Close) && RegisterCommand("sr_geo_wrong", "", WrongType);
}
public void Unexpected(GeoDatabase database, any data, const char[] error) { LogMessage("GEO_UNEXPECTED"); }
public void Missing(GeoDatabase database, any data, const char[] error)
{
    if (!error[0] || GeoIP_IsReady(database)) LogMessage("GEO_FAILED_MISSING");
    else LogMessage("GEO_MISSING_OK");
    GeoIP_Close(database);
}
public void Loaded(GeoDatabase database, any data, const char[] error)
{
    char value[256], epoch[32]; float latitude, distance;
    if (database != city || data != 7 || error[0] || !GeoIP_IsReady(city) || GeoIP_IsLoading(city) ||
        !GeoIP_DatabaseType(city, value, sizeof(value)) || value[0] != 'G' ||
        !GeoIP_DatabaseTime(city, epoch, sizeof(epoch)) || epoch[0] == '0') { LogMessage("GEO_FAILED_LOAD"); return; }
    saved = GeoIP_Lookup(city, "81.2.69.160:27015", "fr");
    if (!saved || !GeoIP_Text(saved, Geo_CountryCode, value, sizeof(value)) || value[0] != 'G' ||
        !GeoIP_Text(saved, Geo_City, value, sizeof(value)) || value[0] != 'L' ||
        !GeoIP_Number(saved, Geo_Latitude, latitude) || latitude < 51.0 || latitude > 52.0 ||
        GeoIP_ASN(saved, value, sizeof(value)) || value[0] || GeoIP_Lookup(city, "localhost") ||
        GeoIP_Lookup(city, "127.0.0.1") || !GeoIP_Distance(0.0, 0.0, 0.0, 0.0, Geo_Kilometers, distance) || distance != 0.0 ||
        GeoIP_Text(saved, view_as<GeoField>(99), value, sizeof(value))) { LogMessage("GEO_FAILED_FIELDS"); return; }
    GeoRecord v6 = GeoIP_Lookup(city, "[2001:218::1]:27015", "ja");
    if (!v6 || !GeoIP_Text(v6, Geo_CountryCode, value, sizeof(value)) || value[0] != 'J') LogMessage("GEO_FAILED_V6");
    else LogMessage("GEO_SCRIPT_OK");
    GeoIP_CloseRecord(v6);
}
public void Reload(Player player, const char[] arguments)
{
    int expected;
    if (!ParseInt(arguments, expected, 0, 1) || !GeoIP_Reload(city, Reloaded, expected) || !GeoIP_IsReady(city)) LogMessage("GEO_FAILED_RELOAD_START");
}
public void Reloaded(GeoDatabase database, any data, const char[] error)
{
    char value[256];
    GeoRecord current = GeoIP_Lookup(city, "81.2.69.160");
    if (database != city || !current || !GeoIP_Text(saved, Geo_City, value, sizeof(value))) LogMessage("GEO_FAILED_SNAPSHOT");
    else if (data == 0 && error[0] && GeoIP_Text(current, Geo_City, value, sizeof(value))) LogMessage("GEO_RELOAD_ERROR_OK");
    else if (data == 1 && !error[0] && !GeoIP_Text(current, Geo_City, value, sizeof(value)) && !value[0]) {
        LogMessage("GEO_RELOAD_OK");
        if (!GeoIP_Reload(database, Chained)) LogMessage("GEO_FAILED_CHAIN_START");
    }
    else LogMessage("GEO_FAILED_RELOAD");
    GeoIP_CloseRecord(current);
}
public void Chained(GeoDatabase database, any data, const char[] error)
{
    if (database != city || error[0] || !GeoIP_IsReady(city) || GeoIP_IsLoading(city)) LogMessage("GEO_FAILED_CHAIN");
    else LogMessage("GEO_RELOAD_CHAIN_OK");
}
public void WrongType(Player player, const char[] arguments)
{
    char value[256];
    GeoIP_Text(view_as<GeoRecord>(city), Geo_City, value, sizeof(value));
    LogMessage("GEO_FAILED_HANDLE_ACCEPTED");
}
public void Pending(Player player, const char[] arguments)
{
    if (!GeoIP_Reload(city, Unexpected)) LogMessage("GEO_FAILED_PENDING");
}
public void Close(Player player, const char[] arguments)
{
    GeoIP_Close(city);
    char value[256];
    if (!GeoIP_Text(saved, Geo_City, value, sizeof(value))) LogMessage("GEO_FAILED_RECORD_LIFETIME");
    GeoIP_CloseRecord(saved);
}
