#include "reader.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <thread>
#include <limits>
#include <vector>

using namespace source2root::geoip;
static void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

template <typename Operation> static void Reject(Operation operation, const char* message) {
    bool failed = false;

    try {
        operation();
    } catch (const Error&) {
        failed = true;
    }

    Check(failed, message);
}

static const auto& Text(const Record& record, Field field) {
    return record.text[static_cast<unsigned>(field)];
}

int main(int argc, char** argv) {
    try {
        Check(argc == 3, "geoip_test upstream-test-data private-fixture");
        const std::filesystem::path data(argv[1]), root(argv[2]), snapshots = root / "snapshots", input = root / "city.mmdb";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        const std::atomic_bool canceled{false}, stopped{true};

        for (const auto* bad : {"", "../city.mmdb", "dir/city.mmdb", ".hidden.mmdb", "city", "city..mmdb"})
            Reject(
                [&] {
                    ValidateName(bad);
                },
                "invalid database filename refused");

        ValidateName("GeoLite2-City.mmdb");
        Reject(
            [&] {
                Reader reader(data / "GeoIP2-City-Test.mmdb", snapshots, stopped);
            },
            "pre-canceled load does no work");

        Reject(
            [&] {
                Reader reader(root / "missing.mmdb", snapshots, canceled);
            },
            "missing database refused");

        std::ofstream(input) << "malformed";
        Reject(
            [&] {
                Reader reader(input, snapshots, canceled);
            },
            "malformed database refused");

        Check(std::filesystem::is_empty(snapshots), "failed load removes partial snapshot");
        std::filesystem::copy_file(data / "GeoIP2-City-Test.mmdb", input, std::filesystem::copy_options::overwrite_existing);
        std::optional<Record> retained;
        {
            Reader reader(input, snapshots, canceled);
            Check(reader.Type() == "GeoIP2-City" && reader.Epoch() > 0, "metadata preserved");
            auto london = reader.Lookup("81.2.69.160:27015", "fr");
            Check(london && Text(*london, Field::CountryCode) == "GB" &&
                      Text(*london, Field::RegisteredCountryCode) == "US" && Text(*london, Field::City) == "Londres" &&
                      Text(*london, Field::RegionCode) == "GB-ENG",
                  "localized IPv4 and distinct country fields");

            Check(london->numbers[0] && std::abs(*london->numbers[0] - 51.5142f) < 0.0001f &&
                london->numbers[1] && std::abs(*london->numbers[1] + 0.0931f) < 0.0001f && london->numbers[2] == 100.0f,
                "latitude longitude and accuracy radius");

            Check(Text(*reader.Lookup("[2001:218::1]:27015", "ja"), Field::CountryName) == "日本",
                  "IPv6 bracketed port and UTF8 locale");

            Check(Text(*reader.Lookup("2001:218::1:1234"), Field::CountryCode) == "JP", "bare IPv6 suffix remains part of address");
            Check(Text(*reader.Lookup("::ffff:81.2.69.160", "unavailable"), Field::City) == "London",
                  "mapped IPv4 and English fallback");

            Check(!reader.Lookup("127.0.0.1") && !reader.Lookup("192.0.2.1"), "no record is distinct from failure");

            for (const auto* bad : {"localhost", "https://81.2.69.160", "81.2.69.160:65536", "81.2.69.160:", "81.2.69.160:bad",
                "[81.2.69.160]", "[2001:218::1]junk", "2001:218::1%eth0", "81.2.69.160 ", "[2001:218::1", "[2001:218::1]:-1"})
                Reject(
                    [&] {
                        reader.Lookup(bad);
                    },
                    "hostname or invalid address/port refused");

            Reject(
                [&] {
                    reader.Lookup("81.2.69.160", "en/../fr");
                },
                "invalid locale refused");

            std::ofstream(input, std::ios::trunc) << "replacement";
            retained = reader.Lookup("81.2.69.160");
            Check(retained && Text(*retained, Field::City) == "London",
                  "original file truncation cannot invalidate private mapping");

            std::atomic<unsigned> successful{0};
            std::vector<std::thread> threads;

            for (int i = 0; i < 4; ++i)
                threads.emplace_back([&] {
                    for (int j = 0; j < 100; ++j)
                        if (Text(*reader.Lookup("81.2.69.160"), Field::City) == "London")
                            ++successful;
                });

            for (auto& thread : threads)
                thread.join();

            Check(successful == 400, "immutable reader supports independent concurrent lookups");
        }

        Check(std::filesystem::is_empty(snapshots) && Text(*retained, Field::City) == "London",
              "records outlive reader and snapshot is removed");

        {
            Reader reader(data / "GeoIP2-Country-Test.mmdb", snapshots, canceled);
            const auto value = reader.Lookup("81.2.69.160");
            Check(value && Text(*value, Field::CountryCode) == "GB" && !Text(*value, Field::City) && !value->numbers[0],
                  "partial country database leaves unsupported fields absent");
        }

        {
            Reader reader(data / "GeoLite2-ASN-Test.mmdb", snapshots, canceled);
            const auto value = reader.Lookup("1.0.0.1");
            Check(value && value->asn == 15169 && Text(*value, Field::Organization) == "Google Inc." &&
                      !Text(*value, Field::CountryCode),
                  "ASN database and absent geography");
        }

        Check(Distance(0, 0, 0, 0, false) == 0 && std::abs(Distance(0, 0, 0, 180, false) - 20015.1144) < 0.001,
            "zero coordinates and antipodal distance are valid");

        Check(std::abs(Distance(0, 0, 0, 1, false) / Distance(0, 0, 0, 1, true) - 1.609344) < 0.000001, "distance units");
        Reject(
            [&] {
                Distance(91, 0, 0, 0, false);
            },
            "latitude bound");

        Reject(
            [&] {
                Distance(0, 0, 0, std::numeric_limits<double>::infinity(), false);
            },
            "nonfinite distance refused");

        Check(std::filesystem::is_empty(snapshots), "all successful readers release private snapshots");
        {
            const auto large = root / "large.mmdb";
            std::ofstream(large).close();
            std::filesystem::resize_file(large, 512ull * 1024 * 1024 + 1);
            Reject(
                [&] {
                    Reader reader(large, snapshots, canceled);
                },
                "oversized sparse database refused before copy");

            std::filesystem::remove(large);
        }
#if !defined(_WIN32)
        const auto link = root / "linked.mmdb";
        std::filesystem::create_symlink(data / "GeoIP2-City-Test.mmdb", link);
        Reject(
            [&] {
                Reader reader(link, snapshots, canceled);
            },
            "database symlinks refused");
#endif
        unsigned corrupt = 0;

        for (const auto& file : std::filesystem::recursive_directory_iterator(data.parent_path() / "bad-data")) {
            if (!file.is_regular_file() || file.path().extension() != ".mmdb")
                continue;

            try {
                Reader reader(file.path(), snapshots, canceled);

                for (const auto* address : {"1.1.1.1", "81.2.69.160", "2001:218::1", "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"}) {
                    try {
                        reader.Lookup(address);
                    } catch (const Error&) { /* malformed entries are bounded errors */
                    }
                }
            } catch (const Error&) { /* malformed metadata is rejected during open */ }

            ++corrupt;
        }

        Check(corrupt >= 10 && std::filesystem::is_empty(snapshots), "upstream malformed corpus leaves no leaked snapshots");
        std::cout << "Malformed corpus files exercised: " << corrupt << '\n';
        std::cout << "GeoIP backend literal addresses, snapshots, fields, locales, ASN and distance passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
