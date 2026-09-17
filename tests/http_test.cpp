#include "client.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <thread>

using namespace source2root::http;
static void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template <typename Function> static void Reject(Function function, const char* message) {
    try { function(); } catch (const Error&) { return; }
    throw std::runtime_error(message);
}
static std::string Env(const char* name) { const auto* value = std::getenv(name); if (!value) throw std::runtime_error(name); return value; }
int main() {
    try {
        Client client;
        const auto root = Env("SR_HTTP_URL"), secure = Env("SR_HTTP_TLS_URL");
        const auto files = std::filesystem::path(Env("SR_HTTP_FIXTURE_FILES"));
        const auto send = [&](Request request) { return client.Perform(std::move(request), [] { return false; }); };
        const auto request = [&](const char* path) { Request input; input.url = root + path; return input; };
        auto result = send(request("/hello"));
        Check(result.status == 200 && result.body == "hello é" && result.url == root + "/hello", "HTTP status/body/final URL");
        unsigned repeated = 0;
        for (const auto& [name, value] : result.headers) if (name == "x-repeated") { Check(value == (++repeated == 1 ? "first" : "second"), "repeated header order"); }
        Check(repeated == 2, "repeated response headers preserved");
        Check(send(request("/status")).status == 404, "HTTP failure status is a completed response");
        Check(send(request("/binary")).body == std::string("A\0\xffZ", 4), "binary response preserved");
        auto input = request("/hello"); input.method = Method::Head;
        Check(send(input).body.empty(), "HEAD has no body");
        for (const auto method : {Method::Post, Method::Put, Method::Patch, Method::Delete}) {
            input = request("/echo"); input.method = method; input.body = std::string("raw\0\xff" "data", 9);
            Check(send(input).body == input.body, "binary body uploaded without truncation");
        }
        input = request("/echo"); input.method = Method::Post;
        input.parts = {{"text", "hello", "", "", {}}, {"binary", std::string("x\0y", 3), "data.bin", "application/octet-stream", {}},
            {"upload", "", "upload.bin", "application/octet-stream", files / "upload.bin"}};
        result = send(input);
        Check(result.body.find("name=\"text\"") != result.body.npos && result.body.find("hello") != result.body.npos &&
            result.body.find(std::string("x\0y", 3)) != result.body.npos && result.body.find(std::string("file\0\xffpayload", 13)) != result.body.npos,
            "multipart text/binary/file fields preserved");
        input = request("/redirect-307"); input.method = Method::Post; input.body = "payload";
        Check(send(input).body == "POST:payload", "307 preserves method and body");
        input.url = root + "/redirect-303";
        Check(send(input).body == "GET:", "303 drops POST body");
        input.url = root + "/redirect-upload-cross";
        Reject([&] { send(input); }, "cross-origin upload redirect refused");
        input = request("/redirect-same"); input.headers = {{"Authorization", "Bearer secret"}, {"X-Secret", "private"}, {"Cookie", "session=value"}};
        Check(send(input).body.find("private") != std::string::npos, "same-origin headers retained");
        input.url = root + "/redirect-cross";
        result = send(input);
        Check(result.body.find("authorization") == result.body.npos && result.body.find("x-secret") == result.body.npos && result.body.find("cookie") == result.body.npos,
            "cross-origin redirects drop every custom header");
        input.redirects = 0;
        Check(send(input).status == 302, "zero redirects exposes original response");
        Reject([&] { send(request("/redirect-loop")); }, "redirect loop bounded");
        Reject([&] { send(request("/redirect-protocol")); }, "non-HTTP redirect refused");
        for (const auto* path : {"/large", "/gzip", "/too-many-headers", "/long-header", "/short"})
            Reject([&] { send(request(path)); }, "oversized/decompressed/header/partial response rejected");
        input = request("/hello"); input.response_limit = 8;
        Check(send(input).body.size() == 8, "exact response cap accepted");
        input.response_limit = 7; Reject([&] { send(input); }, "one-byte body overflow rejected");
        input = request("/hello"); input.url = secure + "/hello"; input.ca_file = Env("SR_HTTP_CA");
        Check(send(input).status == 200, "valid private TLS CA accepted");
        input.ca_file = Env("SR_HTTP_BAD_CA"); Reject([&] { send(input); }, "wrong CA rejected");
        input.ca_file = Env("SR_HTTP_CA"); input.url = Env("SR_HTTP_WRONG_NAME_URL") + "/hello";
        Reject([&] { send(input); }, "TLS hostname mismatch rejected");
        input.url = secure + "/downgrade"; Reject([&] { send(input); }, "HTTPS downgrade redirect rejected");
        input = request("/delay"); input.timeout_ms = 100;
        const auto began = std::chrono::steady_clock::now();
        Reject([&] { send(input); }, "request timeout enforced");
        Check(std::chrono::steady_clock::now() - began < std::chrono::seconds(2), "local timeout terminates promptly");
        std::atomic_bool canceled{false}; std::atomic_bool was_canceled{false};
        std::thread worker([&] { try { client.Perform(request("/delay"), [&] { return canceled.load(); }); } catch (const Error&) { was_canceled = true; } });
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); canceled = true; worker.join();
        Check(was_canceled, "active transfer cancellation");
        Reject([&] { client.Perform(request("/hello"), [] { return true; }); }, "pre-canceled request performs no work");
        for (const auto* url : {"file:///tmp/data", "ftp://localhost/a", "http://user:pass@localhost/", "http://localhost/a\r\nInjected:yes", "http://[fe80::1%25eth0]/"}) {
            input = request("/hello"); input.url = url; Reject([&] { input.Validate(); }, "invalid URL refused");
        }
        for (const auto* header : {"Host", "Content-Length", "Transfer-Encoding", "Proxy-Authorization", "Connection"})
            Reject([&] { ValidateHeader(header, "value"); }, "framing/proxy header refused");
        Reject([&] { ValidateHeader("X-Value", "bad\r\nheader"); }, "header injection refused");
        input = request("/echo"); input.method = Method::Post; input.body.assign(BodyLimit + 1, 'x');
        Reject([&] { input.Validate(); }, "oversized input refused");
        input = request("/echo"); input.method = Method::Post; input.parts = {{"file", "", "missing.bin", "", files / "missing.bin"}};
        Reject([&] { send(input); }, "missing upload file refused");
        std::atomic<unsigned> completed{0}; std::vector<std::thread> parallel;
        for (int i = 0; i < 4; ++i) parallel.emplace_back([&] { if (send(request("/hello")).status == 200) ++completed; });
        for (auto& thread : parallel) thread.join();
        Check(completed == 4, "independent concurrent private transfers");
        std::cout << "HTTP backend request/body/form/TLS/redirect/bounds/cancellation checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
