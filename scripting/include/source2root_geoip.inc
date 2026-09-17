#if defined _source2root_geoip_included
    #endinput
#endif
#define _source2root_geoip_included
#include <source2root>

enum GeoDatabase { NoGeoDatabase = 0 }
enum GeoRecord { NoGeoRecord = 0 }
enum GeoField {
    Geo_CountryCode, Geo_CountryName, Geo_RegisteredCountryCode, Geo_RegisteredCountryName,
    Geo_ContinentCode, Geo_ContinentName, Geo_RegionCode, Geo_RegionName,
    Geo_City, Geo_PostalCode, Geo_TimeZone, Geo_Organization
}
enum GeoNumber { Geo_Latitude, Geo_Longitude, Geo_AccuracyRadius }
enum GeoDistanceUnit { Geo_Kilometers, Geo_Miles }
typedef GeoCallback = function void(GeoDatabase database, any data, const char[] error);

// Operators supply licensed .mmdb files under data/extensions/source2root.geoip.
// No production database or automatic download is included. Filenames must be
// simple names, not paths. At most8database handles and8jobs per provider;
// each file <=512MiB. Copies check cancellation and a30second budget between
// reads (not an absolute OS-I/O deadline), then map a private immutable snapshot.
// Load/reload runs off the game thread. Callback runs on the server thread,
// waits while its script is paused, and is suppressed by close/script unload.
native GeoDatabase GeoIP_Open(const char[] filename, GeoCallback callback, any data = 0);
native bool GeoIP_Close(GeoDatabase database);
// Existing lookups continue during reload; failure retains the old database.
// A load/callback must finish before the same handle can reload again.
native bool GeoIP_Reload(GeoDatabase database, GeoCallback callback, any data = 0);
native bool GeoIP_IsReady(GeoDatabase database);
native bool GeoIP_IsLoading(GeoDatabase database);
native bool GeoIP_Error(GeoDatabase database, char[] output, int capacity);
native bool GeoIP_DatabaseType(GeoDatabase database, char[] output, int capacity);
// Database build epoch as decimal text, preserving timestamps above32bits.
native bool GeoIP_DatabaseTime(GeoDatabase database, char[] output, int capacity);

// Local IPv4/IPv6 literals only; no DNS. Accepts IPv4:port or [IPv6]:port;
// IPv4-mapped IPv6 is normalized. A bare IPv6 suffix is never treated as a port.
// No record/error returns NoGeoRecord. Localized fields use the requested
// language (e.g. en, fr, pt-BR), with English fallback. Country and registered
// country stay distinct. An absent field returns false and clears its output.
// Records own copied values, so they survive database close/reload. Handles
// belong to the creating script. Geographic coordinates are approximate.
native GeoRecord GeoIP_Lookup(GeoDatabase database, const char[] address, const char[] language = "en");
native bool GeoIP_CloseRecord(GeoRecord record);
native bool GeoIP_Text(GeoRecord record, GeoField field, char[] output, int capacity);
native bool GeoIP_Number(GeoRecord record, GeoNumber field, float& value);
// ASN is unsigned32bits and returned as decimal text to avoid cell overflow.
native bool GeoIP_ASN(GeoRecord record, char[] output, int capacity);
// Great-circle distance on a mean-radius sphere, not an ellipsoid survey.
native bool GeoIP_Distance(float latitude1, float longitude1, float latitude2, float longitude2, GeoDistanceUnit unit, float& distance);
