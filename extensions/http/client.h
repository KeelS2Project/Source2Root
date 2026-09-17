#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace source2root::http {
inline constexpr unsigned BodyLimit = 1024 * 1024, HeaderLimit = 64 * 1024;
enum class Method { Get, Head, Post, Put, Patch, Delete };
class Error : public std::runtime_error { public: using std::runtime_error::runtime_error; };
struct Part { std::string name, data, filename, content_type; std::filesystem::path source; };
struct Request {
    std::string url;
    Method method = Method::Get;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    std::vector<Part> parts;
    std::filesystem::path ca_file;
    unsigned timeout_ms = 10000, response_limit = 256 * 1024, redirects = 3;
    void Validate() const;
};
struct Response {
    unsigned status = 0;
    std::string url, body;
    std::vector<std::pair<std::string, std::string>> headers;
};
void ValidateHeader(std::string_view name, std::string_view value);
void ValidateFilename(std::string_view name);
// Initialize on the owner thread, keep alive until all worker calls finish.
// Perform uses private curl handles and supports independent concurrent callers.
class Client final {
public:
    Client();
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    Response Perform(Request request, const std::function<bool()>& canceled) const;
};
}
