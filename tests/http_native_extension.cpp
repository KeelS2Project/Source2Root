#include <source2root/http.h>
#include <keels2/authoring.hpp>
#include <keels2/services.hpp>
#include <atomic>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
using namespace keels2::authoring;

class NativeHTTP final : public keels2::Plugin {
public:
    static constexpr PluginInfo Info{
        "HTTP Native Fixture", "KeelS2 Project", "1.0.0", "Native HTTP service consumer fixture"};

    bool Load() override {
        const void* raw = nullptr;

        if (services_.Connect(HostContext()) != KEEL_RESULT_OK ||
            HostContext().QueryService(SR_HTTP_SERVICE, SR_HTTP_API_VERSION, &raw) != KEEL_RESULT_OK)
            return false;

        api_ = static_cast<const SrHttpApi*>(raw);

        if (!api_ || api_->size != sizeof(*api_) || api_->api_version != SR_HTTP_API_VERSION || !api_->perform)
            return false;

        char error[256]{};
        SrHttpRequest input{};
        input.size = sizeof(input);

        if (api_->perform(
                api_->context,
                &input,
                nullptr,
                [](void*, const SrHttpResponse*) {
                },
                nullptr,
                error,
                sizeof(error)) != KEEL_RESULT_WRONG_THREAD)
            return false;

        worker_ = std::thread([this] {
            try {
                const auto* base = std::getenv("SR_HTTP_URL");

                if (!base)
                    throw std::runtime_error("missing native fixture URL");

                const std::string url = std::string(base) + "/echo";
                SrHttpRequest input{};
                input.size = sizeof(input);
                input.url = url.c_str();
                input.method = SR_HTTP_POST;
                input.timeout_ms = 3000;
                input.response_limit = 4096;
                input.redirects = 3;
                const std::uint8_t bytes[]{'a', 0, 255, 'z'};
                input.body = bytes;
                input.body_size = sizeof(bytes);
                SrHttpHeader headers[]{{"Content-Type", "application/octet-stream"}};
                input.headers = headers;
                input.header_count = 1;

                struct Result {
                    unsigned status = 0;
                    std::string body;
                    bool worker = false;
                    std::thread::id thread;
                } result;
                result.thread = std::this_thread::get_id();
                char error[256]{};
                auto complete = [](void* raw, const SrHttpResponse* response) {
                    auto& result = *static_cast<Result*>(raw);

                    if (response->size != sizeof(*response))
                        throw std::runtime_error("bad response size");

                    result.status = response->status;
                    result.body.assign(reinterpret_cast<const char*>(response->body), response->body_size);
                    result.worker = result.thread == std::this_thread::get_id();
                };

                if (api_->perform(api_->context, &input, nullptr, complete, &result, error, sizeof(error)) != KEEL_RESULT_OK ||
                    result.status != 200 || !result.worker || result.body != std::string("a\0\xffz", 4) || error[0])
                    throw std::runtime_error("native binary transfer failed");

                input.body_size = 1024 * 1024 + 1;

                if (api_->perform(api_->context, &input, nullptr, complete, &result, error, sizeof(error)) ==
                        KEEL_RESULT_OK ||
                    !error[0])
                    throw std::runtime_error("native input bound missing");

                input.body_size = sizeof(bytes);

                if (api_->perform(
                        api_->context,
                        &input,
                        [](void*) {
                            return KEEL_TRUE;
                        },
                        complete,
                        &result,
                        error,
                        sizeof(error)) == KEEL_RESULT_OK)
                    throw std::runtime_error("native cancellation missing");
            } catch (const std::exception& error) {
                failure_ = error.what();
            } catch (...) {
                failure_ = "unknown native fixture failure";
            }

            done_.store(true, std::memory_order_release);
        });
        return true;
    }

    void OnGameFrame(bool, bool, bool) override {
        if (!reported_ && done_.load(std::memory_order_acquire)) {
            reported_ = true;

            if (failure_.empty())
                LogMessage("HTTP_NATIVE_OK");
            else
                LogError("HTTP_NATIVE_FAILED: {}", failure_);
        }
    }

    bool PrepareUnload() override {
        if (worker_.joinable()) {
            if (!done_.load(std::memory_order_acquire))
                return false;

            worker_.join();
        }

        if (api_) {
            if (services_.Release(SR_HTTP_SERVICE, SR_HTTP_API_VERSION) != KEEL_RESULT_OK)
                return false;

            api_ = nullptr;
        }

        return true;
    }

    ~NativeHTTP() override {
        if (worker_.joinable())
            worker_.join();
    }

private:
    keels2::services::Service services_;
    const SrHttpApi* api_ = nullptr;
    std::thread worker_;
    std::atomic_bool done_{false};
    bool reported_ = false;
    std::string failure_;
};
}

KEELS2_PLUGIN(NativeHTTP)
