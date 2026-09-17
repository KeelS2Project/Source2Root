#include "settings.h"
#include <fstream>
#include <iostream>
#include <limits>

using namespace source2root::db;
static void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template <typename Operation> static void Reject(Operation operation, const char* message) {
    bool rejected = false;
    try { operation(); } catch (const Error& error) {
        Check(std::string(error.what()).find("secret-sentinel") == std::string::npos, "credentials must not appear in diagnostics");
        rejected = true;
    }
    Check(rejected, message);
}
int main(int argc, char** argv) {
    try {
        Check(argc == 2, "database_settings fixture directory");
        const auto file = std::filesystem::path(argv[1]) / "databases.json";
        std::filesystem::create_directories(file.parent_path());
        auto write = [&](const std::string& text) { std::ofstream(file) << text; };
        write(R"({"schema":1,"connections":{"local":{"driver":"sqlite","database":"shared","allow_plugins":["example"]}}})");
        Check(ReadSettings(file, "local", "example").database == "shared", "configured SQLite storage");
        Reject([&] { ReadSettings(file, "local", "stranger"); }, "plugin allow list enforced");
        Reject([&] { ReadSettings(file, "missing", "example"); }, "unknown profile rejected");
        auto mysql = [&](const std::string& fields) {
            write("{\"schema\":1,\"connections\":{\"network\":{\"driver\":\"mysql\",\"database\":\"game\",\"user\":\"game\",\"allow_plugins\":[\"example\"]" + fields + "}}}");
        };
        mysql("");
        const auto settings = ReadSettings(file, "network", "example");
        Check(settings.tls && settings.timeout == 3 && settings.port == 3306, "verified TLS and finite timeouts by default");
        for (const auto* field : {",\"host\":\"remote.example\",\"tls\":false", ",\"port\":65536", ",\"timeout\":0",
            ",\"timeout\":31", ",\"verify_certificate\":false", ",\"ca\":\"relative.pem\"", ",\"password\":\"secret-sentinel\\n\"",
            ",\"user\":\"duplicate\"", ",\"tls\":\"false\""}) {
            mysql(field);
            Reject([&] { ReadSettings(file, "network", "example"); }, "invalid configuration rejected");
        }
        mysql(",\"tls\":false,\"host\":\"127.0.0.1\"");
        Check(!ReadSettings(file, "network", "example").tls, "explicit local plaintext allowed");
        write("{\"password\":\"secret-sentinel\"");
        Reject([&] { ReadSettings(file, "network", "example"); }, "malformed secret configuration reported safely");
        write(std::string(65537, 'a'));
        Reject([&] { ReadSettings(file, "network", "example"); }, "bounded config reads");
        QueryInput input{"SELECT ?,?", {}};
        input.Bind(2, std::string("quoted '; SQL stays data"));
        Reject([&] { input.Validate(); }, "binding holes refused");
        input.Bind(1, std::monostate{});
        input.Validate();
        Reject([&] { input.Bind(0, 7); }, "parameter indices start at one");
        Reject([&] { input.Bind(65, 7); }, "parameter count bounded");
        Reject([&] { input.Bind(1, std::numeric_limits<double>::infinity()); }, "nonfinite values refused");
        Reject([&] { input.Bind(1, std::string("a\0b", 3)); }, "binary string not silently truncated");
        for (int i = 1; i <= 17; ++i) input.Bind(i, std::string(4095, 'a'));
        Reject([&] { input.Validate(); }, "total parameter payload bounded");
        std::cout << "Database configuration, credentials and parameter limits passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
