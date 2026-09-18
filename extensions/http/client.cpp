#include "client.h"
#include <curl/curl.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <fstream>
#include <memory>
#include <mutex>

namespace source2root::http {
namespace {
using Clock = std::chrono::steady_clock;
std::mutex runtime_mutex;
unsigned runtime_users = 0;
using Easy = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
using Multi = std::unique_ptr<CURLM, decltype(&curl_multi_cleanup)>;
using Url = std::unique_ptr<CURLU, decltype(&curl_url_cleanup)>;
using Headers = std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>;
using Mime = std::unique_ptr<curl_mime, decltype(&curl_mime_free)>;
void Curl(CURLcode code) { if (code != CURLE_OK) throw Error("HTTP transfer failed: " + std::string(curl_easy_strerror(code))); }
void MultiCheck(CURLMcode code) { if (code != CURLM_OK) throw Error("HTTP transfer manager failed."); }
std::string Lower(std::string_view value) {
    std::string result(value);
    for (auto& ch : result) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    return result;
}
bool Controls(std::string_view value, bool tab = false) {
    return std::any_of(value.begin(), value.end(), [tab](unsigned char ch) { return (ch < 32 && !(tab && ch == 9)) || ch == 127; });
}
bool Token(std::string_view value) {
    if (value.empty()) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
            std::string_view("!#$%&'*+-.^_`|~").find(ch) != std::string_view::npos;
    });
}
std::string Get(CURLU* url, CURLUPart part, unsigned flags = 0) {
    char* value = nullptr;
    if (curl_url_get(url, part, &value, flags) != CURLUE_OK) return {};
    std::unique_ptr<char, decltype(&curl_free)> owner(value, curl_free);
    return value;
}
struct Address { std::string normalized, origin, scheme; };
Address Parse(std::string_view text) {
    if (text.empty() || text.size() > 4095 || Controls(text) || text.find(' ') != text.npos) throw Error("HTTP URL is invalid or too long.");
    Url url(curl_url(), curl_url_cleanup);
    if (!url || curl_url_set(url.get(), CURLUPART_URL, std::string(text).c_str(), CURLU_DISALLOW_USER) != CURLUE_OK)
        throw Error("HTTP URL must be absolute and must not contain user credentials.");
    const auto scheme = Lower(Get(url.get(), CURLUPART_SCHEME)), host = Lower(Get(url.get(), CURLUPART_HOST));
    if ((scheme != "http" && scheme != "https") || host.empty() || !Get(url.get(), CURLUPART_ZONEID).empty())
        throw Error("Only HTTP/HTTPS URLs without IPv6 zone identifiers are supported.");
    curl_url_set(url.get(), CURLUPART_FRAGMENT, nullptr, 0);
    auto normalized = Get(url.get(), CURLUPART_URL);
    if (normalized.empty() || normalized.size() > 4095) throw Error("Normalized HTTP URL exceeds its limit.");
    return {std::move(normalized), scheme + "://" + host + ":" + Get(url.get(), CURLUPART_PORT, CURLU_DEFAULT_PORT), scheme};
}
std::string Read(const std::filesystem::path& file, unsigned maximum) {
    try {
        const auto status = std::filesystem::symlink_status(file);
        if (!std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) throw Error("HTTP input file must be a regular file, not a symlink.");
        if (std::filesystem::file_size(file) > maximum) throw Error("HTTP input file exceeds the transfer limit.");
        std::ifstream stream(file, std::ios::binary);
        if (!stream) throw Error("HTTP input file is unavailable.");
        std::string value; char buffer[16384];
        while (stream) {
            stream.read(buffer, sizeof(buffer)); const auto size = static_cast<unsigned>(stream.gcount());
            if (size > maximum - value.size()) throw Error("HTTP input file grew beyond the transfer limit.");
            value.append(buffer, size);
        }
        if (!stream.eof()) throw Error("HTTP input file could not be read.");
        return value;
    } catch (const std::filesystem::filesystem_error&) { throw Error("HTTP input file is unavailable."); }
}
struct Operation {
    const std::function<bool()>& canceled;
    const Clock::time_point deadline;
    unsigned body_limit, body_bytes = 0, header_bytes = 0;
    Response response;
    std::exception_ptr failure;
    void Check() const {
        if (canceled && canceled()) throw Error("HTTP request canceled.");
        if (Clock::now() >= deadline) throw Error("HTTP request deadline exceeded.");
    }
    template <typename Function> static std::size_t Receive(char* bytes, std::size_t size, std::size_t count, void* raw, Function function) noexcept {
        auto& self = *static_cast<Operation*>(raw);
        try {
            self.Check();
            if (count > HeaderLimit || size > 1) throw Error("HTTP callback block exceeds limits.");
            const auto length = size * count;
            function(self, std::string_view(bytes, length)); return length;
        } catch (...) { self.failure = std::current_exception(); return 0; }
    }
    static std::size_t Body(char* bytes, std::size_t size, std::size_t count, void* raw) noexcept {
        return Receive(bytes, size, count, raw, [](Operation& self, std::string_view value) {
            if (value.size() > self.body_limit - self.body_bytes) throw Error("HTTP response body exceeds its limit.");
            self.body_bytes += value.size(); self.response.body.append(value);
        });
    }
    static std::size_t Header(char* bytes, std::size_t size, std::size_t count, void* raw) noexcept {
        return Receive(bytes, size, count, raw, [](Operation& self, std::string_view line) {
            if (line.size() > HeaderLimit - self.header_bytes) throw Error("HTTP response headers exceed 64KiB.");
            self.header_bytes += line.size();
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.remove_suffix(1);
            if (line.starts_with("HTTP/")) { self.response.headers.clear(); return; }
            if (line.empty()) return;
            const auto colon = line.find(':');
            if (colon == line.npos || colon > 128 || !Token(line.substr(0, colon))) throw Error("HTTP response header is malformed.");
            auto value = line.substr(colon + 1);
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
            while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
            if (value.size() > 4095 || Controls(value, true) || self.response.headers.size() >= 128) throw Error("HTTP response header exceeds limits.");
            self.response.headers.emplace_back(Lower(line.substr(0, colon)), value);
        });
    }
};
}
void ValidateHeader(std::string_view name, std::string_view value) {
    if (name.size() > 128 || !Token(name) || value.size() > 4095 || Controls(value, true)) throw Error("HTTP request header is invalid.");
    const auto lower = Lower(name);
    for (const auto* blocked : {"host", "content-length", "transfer-encoding", "connection", "expect", "trailer", "upgrade", "te"})
        if (lower == blocked) throw Error("HTTP framing headers are managed by the transfer service.");
    if (lower.starts_with("proxy-")) throw Error("HTTP proxy headers are not supported.");
}
void ValidateFilename(std::string_view name) {
    if (name.empty() || name.size() > 64 || name.front() == '.' || name.find("..") != name.npos ||
        !std::all_of(name.begin(), name.end(), [](unsigned char ch) {
            return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '_';
        })) throw Error("HTTP file name must be a simple name of at most 64 characters.");
}
void Request::Validate() const {
    Parse(url);
    if (method < Method::Get || method > Method::Delete || timeout_ms < 100 || timeout_ms > 60000 ||
        !response_limit || response_limit > BodyLimit || redirects > 5) throw Error("HTTP method or request limits are invalid.");
    if (headers.size() > 32 || parts.size() > 32 || body.size() > BodyLimit) throw Error("HTTP request exceeds its size or item limits.");
    if ((!body.empty() || !parts.empty()) && (method == Method::Get || method == Method::Head)) throw Error("HTTP GET/HEAD requests cannot carry a body.");
    if (!parts.empty() && (!body.empty() || method != Method::Post)) throw Error("HTTP multipart forms require POST without a raw body.");
    unsigned header_bytes = 0, input_bytes = body.size();
    for (const auto& [name, value] : headers) { ValidateHeader(name, value); header_bytes += name.size() + value.size() + 4; }
    if (header_bytes > 16384) throw Error("HTTP request headers exceed 16KiB.");
    for (const auto& part : parts) {
        if (part.name.empty() || part.name.size() > 128 || Controls(part.name) || part.name.find('"') != part.name.npos ||
            part.filename.size() > 128 || Controls(part.filename) || part.content_type.size() > 128 || Controls(part.content_type))
            throw Error("HTTP multipart field metadata is invalid.");
        if (!part.source.empty() && (!part.data.empty() || part.filename.empty())) throw Error("HTTP file part requires a filename and no inline data.");
        if (part.data.size() > BodyLimit - input_bytes) throw Error("HTTP upload data exceeds 1MiB.");
        input_bytes += part.data.size();
    }
}
Client::Client() {
    std::lock_guard lock(runtime_mutex);
    if (!runtime_users) Curl(curl_global_init(CURL_GLOBAL_DEFAULT));
    ++runtime_users;
}
Client::~Client() { std::lock_guard lock(runtime_mutex); if (!--runtime_users) curl_global_cleanup(); }
Response Client::Perform(Request request, const std::function<bool()>& canceled) const {
    request.Validate();
    Operation operation{canceled, Clock::now() + std::chrono::milliseconds(request.timeout_ms), request.response_limit};
    operation.Check();
    unsigned upload_bytes = request.body.size();
    for (auto& part : request.parts) {
        operation.Check();
        if (!part.source.empty()) { part.data = Read(part.source, BodyLimit - upload_bytes); part.source.clear(); }
        if (part.data.size() > BodyLimit - upload_bytes) throw Error("HTTP upload data exceeds 1MiB.");
        upload_bytes += part.data.size();
    }
    const auto ca = request.ca_file.empty() ? std::string{} : Read(request.ca_file, BodyLimit);
    if (!request.ca_file.empty() && ca.empty()) throw Error("HTTP trust file is empty.");
    auto address = Parse(request.url);
    for (unsigned hop = 0;; ++hop) {
        operation.Check(); operation.response = {};
        // Input lists/forms outlive easy-handle cleanup, including error paths.
        Headers headers(nullptr, curl_slist_free_all);
        Mime mime(nullptr, curl_mime_free);
        std::array<char, CURL_ERROR_SIZE> detail{};
        Easy easy(curl_easy_init(), curl_easy_cleanup); Multi multi(curl_multi_init(), curl_multi_cleanup);
        if (!easy || !multi) throw Error("HTTP transfer allocation failed.");
        auto set = [&](CURLoption option, auto value) { Curl(curl_easy_setopt(easy.get(), option, value)); };
        set(CURLOPT_ERRORBUFFER, detail.data());
        set(CURLOPT_URL, address.normalized.c_str()); set(CURLOPT_PROTOCOLS_STR, "http,https");
        set(CURLOPT_FOLLOWLOCATION, 0L); set(CURLOPT_PROXY, ""); set(CURLOPT_NETRC, static_cast<long>(CURL_NETRC_IGNORED));
        set(CURLOPT_NOSIGNAL, 1L); set(CURLOPT_SSL_VERIFYPEER, 1L); set(CURLOPT_SSL_VERIFYHOST, 2L);
        set(CURLOPT_SSLVERSION, static_cast<long>(CURL_SSLVERSION_TLSv1_2));
        curl_blob trust{const_cast<char*>(ca.data()), ca.size(), CURL_BLOB_COPY};
        if (!ca.empty()) set(CURLOPT_CAINFO_BLOB, &trust);
        set(CURLOPT_USERAGENT, "Source2Root/1.0.0"); set(CURLOPT_ACCEPT_ENCODING, "");
        const auto remaining = std::max<long>(1, std::chrono::duration_cast<std::chrono::milliseconds>(operation.deadline - Clock::now()).count());
        set(CURLOPT_TIMEOUT_MS, remaining); set(CURLOPT_CONNECTTIMEOUT_MS, std::min<long>(5000, remaining));
        set(CURLOPT_LOW_SPEED_LIMIT, 1L); set(CURLOPT_LOW_SPEED_TIME, 10L);
        set(CURLOPT_WRITEFUNCTION, &Operation::Body); set(CURLOPT_WRITEDATA, &operation);
        set(CURLOPT_HEADERFUNCTION, &Operation::Header); set(CURLOPT_HEADERDATA, &operation);
        const auto append = [&](const std::string& value) {
            auto* updated = curl_slist_append(headers.get(), value.c_str());
            if (!updated) throw Error("HTTP header allocation failed.");
            headers.release(); headers.reset(updated);
        };
        append("Expect:");
        for (const auto& [name, value] : request.headers) append(name + (value.empty() ? ";" : ": " + value));
        set(CURLOPT_HTTPHEADER, headers.get());
        if (!request.parts.empty()) {
            mime.reset(curl_mime_init(easy.get())); if (!mime) throw Error("HTTP form allocation failed.");
            for (const auto& value : request.parts) {
                auto* part = curl_mime_addpart(mime.get()); if (!part) throw Error("HTTP form field allocation failed.");
                Curl(curl_mime_name(part, value.name.c_str())); Curl(curl_mime_data(part, value.data.data(), value.data.size()));
                if (!value.filename.empty()) Curl(curl_mime_filename(part, value.filename.c_str()));
                if (!value.content_type.empty()) Curl(curl_mime_type(part, value.content_type.c_str()));
            }
            set(CURLOPT_MIMEPOST, mime.get());
        } else if (request.method != Method::Get && request.method != Method::Head) {
            set(CURLOPT_POSTFIELDS, request.body.data()); set(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
        }
        static constexpr const char* methods[]{"GET", "HEAD", "POST", "PUT", "PATCH", "DELETE"};
        if (request.method == Method::Head) set(CURLOPT_NOBODY, 1L);
        set(CURLOPT_CUSTOMREQUEST, methods[static_cast<unsigned>(request.method)]);
        MultiCheck(curl_multi_add_handle(multi.get(), easy.get()));
        struct Attachment { CURLM* multi; CURL* easy; ~Attachment() { curl_multi_remove_handle(multi, easy); } } attachment{multi.get(), easy.get()};
        int running = 0;
        do {
            operation.Check(); MultiCheck(curl_multi_perform(multi.get(), &running));
            if (operation.failure) std::rethrow_exception(operation.failure);
            if (running) { int events = 0; MultiCheck(curl_multi_poll(multi.get(), nullptr, 0, 50, &events)); }
        } while (running);
        int pending = 0; auto* message = curl_multi_info_read(multi.get(), &pending);
        if (!message || message->msg != CURLMSG_DONE) throw Error("HTTP transfer completion is missing.");
        if (message->data.result != CURLE_OK && detail.front())
            throw Error("HTTP transfer failed: " + std::string(detail.data()));
        Curl(message->data.result); operation.Check();
        long status = 0; Curl(curl_easy_getinfo(easy.get(), CURLINFO_RESPONSE_CODE, &status));
        operation.response.status = status; operation.response.url = address.normalized;
        const bool redirect = status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
        if (!redirect || !request.redirects) return std::move(operation.response);
        char* location = nullptr; Curl(curl_easy_getinfo(easy.get(), CURLINFO_REDIRECT_URL, &location));
        if (!location) return std::move(operation.response);
        if (hop >= request.redirects) throw Error("HTTP redirect limit reached.");
        auto next = Parse(location);
        if (address.scheme == "https" && next.scheme != "https") throw Error("HTTP redirect cannot downgrade HTTPS.");
        if ((status == 303 && request.method != Method::Head) || ((status == 301 || status == 302) && request.method == Method::Post)) {
            request.method = Method::Get; request.body.clear(); request.parts.clear();
            std::erase_if(request.headers, [](const auto& header) { return Lower(header.first).starts_with("content-"); });
        }
        if (next.origin != address.origin) {
            if (request.method != Method::Get && request.method != Method::Head) throw Error("HTTP upload redirects must remain on the same origin.");
            request.headers.clear();
        }
        address = std::move(next);
    }
}
}
