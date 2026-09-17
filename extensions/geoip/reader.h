#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace source2root::geoip {
class Error : public std::runtime_error { public: using std::runtime_error::runtime_error; };
enum class Field { CountryCode, CountryName, RegisteredCountryCode, RegisteredCountryName,
    ContinentCode, ContinentName, RegionCode, RegionName, City, PostalCode, TimeZone, Organization, Count };
enum class Number { Latitude, Longitude, AccuracyRadius, Count };
struct Record {
    std::array<std::optional<std::string>, static_cast<unsigned>(Field::Count)> text;
    std::array<std::optional<float>, static_cast<unsigned>(Number::Count)> numbers;
    std::optional<std::uint32_t> asn;
};
void ValidateName(const std::string& filename);
double Distance(double latitude1, double longitude1, double latitude2, double longitude2, bool miles);

// Construct on a worker. A bounded private file snapshot keeps mappings valid
// if the operator replaces or truncates the original database. Lookups are
// local literal-IP operations; they never resolve hostnames or contact a server.
class Reader final {
public:
    Reader(const std::filesystem::path& file, const std::filesystem::path& snapshots, const std::atomic_bool& canceled);
    ~Reader();
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;
    std::optional<Record> Lookup(const std::string& address, const std::string& language = "en") const;
    std::string Type() const;
    std::uint64_t Epoch() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
