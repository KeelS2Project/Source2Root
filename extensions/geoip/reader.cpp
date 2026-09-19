#include "reader.h"
#include <maxminddb.h>
#if defined(_WIN32)
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <numbers>
#include <random>
#include <vector>

namespace source2root::geoip {
namespace {
constexpr std::uint64_t MaxDatabase = 512ull * 1024 * 1024;
bool Alnum(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

void Coordinates(double latitude, double longitude) {
    if (!std::isfinite(latitude) || !std::isfinite(longitude) || latitude < -90 || latitude > 90 || longitude < -180 ||
        longitude > 180)
        throw Error("GeoIP coordinates are outside their finite latitude/longitude ranges.");
}

sockaddr_storage Address(const std::string& input) {
    if (input.empty() || input.size() > 128 || input.find_first_of("% \t\r\n") != std::string::npos ||
        input.find('\0') != std::string::npos)
        throw Error("GeoIP requires an IPv4 or IPv6 literal.");

    std::string ip = input, port;
    bool has_port = false, bracketed = false;

    if (input.front() == '[') {
        bracketed = true;
        const auto end = input.find(']');

        if (end == std::string::npos)
            throw Error("Invalid bracketed GeoIP address.");

        ip = input.substr(1, end - 1);

        if (end + 1 != input.size()) {
            if (input[end + 1] != ':')
                throw Error("Invalid GeoIP port.");

            has_port = true;
            port = input.substr(end + 2);
        }
    } else if (std::count(input.begin(), input.end(), ':') == 1) {
        const auto colon = input.find(':');
        ip = input.substr(0, colon);
        port = input.substr(colon + 1);
        has_port = true;
    }

    if (has_port) {
        unsigned value = 0;
        const auto parsed = std::from_chars(port.data(), port.data() + port.size(), value);

        if (port.empty() || parsed.ec != std::errc{} || parsed.ptr != port.data() + port.size() || value > 65535)
            throw Error("Invalid GeoIP port.");
    }

    sockaddr_storage storage{};
    auto* v4 = reinterpret_cast<sockaddr_in*>(&storage);

    if (!bracketed && inet_pton(AF_INET, ip.c_str(), &v4->sin_addr) == 1) {
        v4->sin_family = AF_INET;
        return storage;
    }

    storage = {};
    auto* v6 = reinterpret_cast<sockaddr_in6*>(&storage);

    if (inet_pton(AF_INET6, ip.c_str(), &v6->sin6_addr) != 1)
        throw Error("GeoIP requires a valid literal; hostnames and zone IDs are not supported.");

    v6->sin6_family = AF_INET6;
    const auto* bytes = reinterpret_cast<const unsigned char*>(&v6->sin6_addr);

    if (std::all_of(bytes,
                    bytes + 10,
                    [](auto c) {
                        return c == 0;
                    }) &&
        bytes[10] == 255 && bytes[11] == 255) {
        std::array<unsigned char, 4> mapped{};
        std::copy(bytes + 12, bytes + 16, mapped.begin());
        storage = {};
        v4 = reinterpret_cast<sockaddr_in*>(&storage);
        v4->sin_family = AF_INET;
        std::memcpy(&v4->sin_addr, mapped.data(), mapped.size());
    }

    return storage;
}

MMDB_entry_data_s Value(MMDB_entry_s& entry, std::initializer_list<const char*> keys) {
    std::vector<const char*> path(keys);
    path.push_back(nullptr);
    MMDB_entry_data_s value{};
    const auto status = MMDB_aget_value(&entry, &value, path.data());

    if (status == MMDB_LOOKUP_PATH_DOES_NOT_MATCH_DATA_ERROR)
        return {};

    if (status != MMDB_SUCCESS)
        throw Error("GeoIP database value is malformed.");

    return value;
}

std::optional<std::string> Text(MMDB_entry_s& entry, std::initializer_list<const char*> keys) {
    const auto value = Value(entry, keys);

    if (!value.has_data)
        return {};

    if (value.type != MMDB_DATA_TYPE_UTF8_STRING || value.data_size > 1024 ||
        std::memchr(value.utf8_string, 0, value.data_size))
        throw Error("Invalid or oversized GeoIP string field.");

    return std::string(value.utf8_string, value.data_size);
}

std::optional<float> Decimal(MMDB_entry_s& entry, std::initializer_list<const char*> keys) {
    const auto value = Value(entry, keys);

    if (!value.has_data)
        return {};

    double number;

    if (value.type == MMDB_DATA_TYPE_DOUBLE)
        number = value.double_value;
    else if (value.type == MMDB_DATA_TYPE_FLOAT)
        number = value.float_value;
    else if (value.type == MMDB_DATA_TYPE_UINT16)
        number = value.uint16;
    else if (value.type == MMDB_DATA_TYPE_UINT32)
        number = value.uint32;
    else
        throw Error("Invalid GeoIP numeric field.");

    if (!std::isfinite(number) || std::abs(number) > 1.0e9)
        throw Error("GeoIP numeric field is out of range.");

    return static_cast<float>(number);
}
}

void ValidateName(const std::string& filename) {
    if (filename.size() < 6 || filename.size() > 64 || !filename.ends_with(".mmdb") || !Alnum(filename.front()) ||
        filename.find("..") != std::string::npos || !std::all_of(filename.begin(), filename.end(), [](auto c) {
            return Alnum(c) || c == '_' || c == '-' || c == '.';
        }))
        throw Error("GeoIP database names require a simple .mmdb filename (at most 64 bytes).");
}

double Distance(double a, double b, double c, double d, bool miles) {
    Coordinates(a, b);
    Coordinates(c, d);
    const auto radians = std::numbers::pi / 180.0;
    const auto x = std::sin((c - a) * radians / 2), y = std::sin((d - b) * radians / 2);
    const auto h = std::clamp(x * x + std::cos(a * radians) * std::cos(c * radians) * y * y, 0.0, 1.0);
    return 2 * (miles ? 3958.7613 : 6371.0088) * std::atan2(std::sqrt(h), std::sqrt(1 - h));
}

struct Reader::Impl {
    std::filesystem::path directory;
    MMDB_s database{};
    bool open = false;
    ~Impl() {
        if (open)
            MMDB_close(&database);

        std::error_code ignored;

        if (!directory.empty()) {
            std::filesystem::remove(directory / "database.mmdb", ignored);
            std::filesystem::remove(directory, ignored);
        }
    }
};
Reader::Reader(const std::filesystem::path& file,
               const std::filesystem::path& snapshots,
               const std::atomic_bool& canceled)
    : impl_(std::make_unique<Impl>()) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    const auto check = [&] {
        if (canceled.load(std::memory_order_relaxed))
            throw Error("GeoIP database load canceled.");

        if (std::chrono::steady_clock::now() >= deadline)
            throw Error("GeoIP database copy exceeded its time budget.");
    };
    check();

    if (std::filesystem::symlink_status(file).type() != std::filesystem::file_type::regular)
        throw Error("GeoIP database is missing, not a regular file or a symbolic link.");

    const auto size = std::filesystem::file_size(file);
    const auto stamp = std::filesystem::last_write_time(file);

    if (!size || size > MaxDatabase)
        throw Error("GeoIP databases must be nonempty and at most 512 MiB.");

    std::filesystem::create_directories(snapshots);

    if (std::filesystem::is_symlink(snapshots))
        throw Error("GeoIP snapshot directory must not be a symbolic link.");

    std::random_device random;

    for (unsigned i = 0; i < 32 && impl_->directory.empty(); ++i) {
        const auto candidate = snapshots / ("snapshot-" + std::to_string(random()) + "-" + std::to_string(random()));

        if (std::filesystem::create_directory(candidate))
            impl_->directory = candidate;
    }

    if (impl_->directory.empty())
        throw Error("GeoIP snapshot allocation failed.");

    std::filesystem::permissions(impl_->directory, std::filesystem::perms::owner_all);
    const auto copy = impl_->directory / "database.mmdb";
    std::ifstream source(file, std::ios::binary);
    std::ofstream target(copy, std::ios::binary);

    if (!source || !target)
        throw Error("GeoIP database copy could not be opened.");

    std::array<char, 64 * 1024> buffer{};
    std::uint64_t copied = 0;

    while (source) {
        check();
        source.read(buffer.data(), buffer.size());
        const auto length = source.gcount();
        copied += length;

        if (copied > MaxDatabase)
            throw Error("GeoIP database exceeds 512 MiB.");

        target.write(buffer.data(), length);

        if (!target)
            throw Error("GeoIP database copy failed.");
    }

    if (source.bad() || copied != size || std::filesystem::file_size(file) != size ||
        std::filesystem::last_write_time(file) != stamp)
        throw Error("GeoIP database changed or could not be read; retry after replacing it.");

    target.close();

    if (!target)
        throw Error("GeoIP database copy failed.");

    check();
    const auto status = MMDB_open(copy.string().c_str(), MMDB_MODE_MMAP, &impl_->database);

    if (status != MMDB_SUCCESS)
        throw Error("GeoIP database could not be decoded (" + std::to_string(status) + ").");

    impl_->open = true;

    if (!impl_->database.metadata.database_type || std::strlen(impl_->database.metadata.database_type) > 255)
        throw Error("Invalid GeoIP database metadata.");

    check();
#if !defined(_WIN32)
    // POSIX mappings remain valid after unlink; no database copy survives a
    // crash. Windows keeps its mapped file until Reader destruction instead.
    std::filesystem::remove(copy);
    std::filesystem::remove(impl_->directory);
    impl_->directory.clear();
#endif
}

Reader::~Reader() = default;
std::string Reader::Type() const {
    return impl_->database.metadata.database_type;
}

std::uint64_t Reader::Epoch() const {
    return impl_->database.metadata.build_epoch;
}

std::optional<Record> Reader::Lookup(const std::string& input, const std::string& language) const {
    if (language.empty() || language.size() > 32 || !std::all_of(language.begin(), language.end(), [](auto c) {
            return Alnum(c) || c == '-';
        }))
        throw Error("GeoIP language requires a locale name of at most 32 letters/digits/hyphens.");

    const auto address = Address(input);
    int error = MMDB_SUCCESS;
    auto match = MMDB_lookup_sockaddr(&impl_->database, reinterpret_cast<const sockaddr*>(&address), &error);

    if (error == MMDB_IPV6_LOOKUP_IN_IPV4_DATABASE_ERROR)
        return {};

    if (error != MMDB_SUCCESS)
        throw Error("GeoIP lookup encountered a malformed database.");

    if (!match.found_entry)
        return {};

    Record result;
    auto& entry = match.entry;
    const auto put = [&](Field field, auto value) {
        result.text[static_cast<unsigned>(field)] = std::move(value);
    };
    const auto localized = [&](std::initializer_list<const char*> prefix) {
        std::vector<const char*> path(prefix);
        path.push_back("names");
        path.push_back(language.c_str());
        path.push_back(nullptr);
        MMDB_entry_data_s value{};
        auto read = [&]() -> std::optional<std::string> {
            const auto status = MMDB_aget_value(&entry, &value, path.data());

            if (status == MMDB_LOOKUP_PATH_DOES_NOT_MATCH_DATA_ERROR || (status == MMDB_SUCCESS && !value.has_data))
                return {};

            if (status != MMDB_SUCCESS || value.type != MMDB_DATA_TYPE_UTF8_STRING || value.data_size > 1024 ||
                std::memchr(value.utf8_string, 0, value.data_size))
                throw Error("Invalid localized GeoIP string.");

            return std::string(value.utf8_string, value.data_size);
        };
        auto text = read();

        if (!text && language != "en") {
            path[path.size() - 2] = "en";
            text = read();
        }

        return text;
    };
    put(Field::CountryCode, Text(entry, {"country", "iso_code"}));
    put(Field::CountryName, localized({"country"}));
    put(Field::RegisteredCountryCode, Text(entry, {"registered_country", "iso_code"}));
    put(Field::RegisteredCountryName, localized({"registered_country"}));
    put(Field::ContinentCode, Text(entry, {"continent", "code"}));
    put(Field::ContinentName, localized({"continent"}));
    auto region = Text(entry, {"subdivisions", "0", "iso_code"});

    if (region && result.text[0])
        region = *result.text[0] + "-" + *region;

    put(Field::RegionCode, region);
    put(Field::RegionName, localized({"subdivisions", "0"}));
    put(Field::City, localized({"city"}));
    put(Field::PostalCode, Text(entry, {"postal", "code"}));
    put(Field::TimeZone, Text(entry, {"location", "time_zone"}));
    put(Field::Organization, Text(entry, {"autonomous_system_organization"}));
    result.numbers[0] = Decimal(entry, {"location", "latitude"});
    result.numbers[1] = Decimal(entry, {"location", "longitude"});
    result.numbers[2] = Decimal(entry, {"location", "accuracy_radius"});
    Coordinates(result.numbers[0].value_or(0), result.numbers[1].value_or(0));

    if (result.numbers[2] && *result.numbers[2] < 0)
        throw Error("Invalid GeoIP accuracy radius.");

    const auto asn = Value(entry, {"autonomous_system_number"});

    if (asn.has_data) {
        if (asn.type != MMDB_DATA_TYPE_UINT32)
            throw Error("Invalid GeoIP ASN field.");

        result.asn = asn.uint32;
    }

    return result;
}
}
