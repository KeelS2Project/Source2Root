#include "client.h"
#include <source2root/extension.hpp>
#include <source2root/http.h>
#include <source2root/work_queue.hpp>
#include <atomic>
#include <thread>

namespace {
using namespace keels2::authoring;
using source2root::NativeCall;
namespace web = source2root::http;

class HTTP final : public source2root::Extension {
public:
    static constexpr PluginInfo Info{
        "Source2Root HTTP", "KeelS2 Project", "1.0.0", "HTTP transfers for scripts and native extensions"};

    static constexpr PluginRequirement Requirements[]{{"Source2Root", "1.0.0", DependencyRequirement::exact}};
    HTTP() : Extension(SR_HTTP_SERVICE, SR_HTTP_API_VERSION, &api_),
        api_{sizeof(api_), SR_HTTP_API_VERSION, this, &HTTP::PerformNative} {}

    void OnGameFrame(bool, bool, bool) override {
        if (queue_)
            queue_->Dispatch();
    }

private:
    enum class State { Building, Sent, Complete, Canceled };

    struct Request {
        HTTP* extension;
        web::Request input;
        web::Response response;
        std::unique_ptr<source2root::WorkQueue::Ticket> ticket;
        SrCallback callback = 0;
        State state = State::Building;
        std::int32_t handle = 0;
        std::string error;
        ~Request() {
            ticket.reset();

            if (callback)
                extension->CancelCallback(callback);
        }
    };
    using Ref = std::shared_ptr<Request>;
    static constexpr unsigned RequestType = 1;
    const SrHttpApi api_;
    web::Client client_; // global curl lifetime encloses all queued worker jobs
    std::thread::id server_thread_;
    std::atomic<unsigned> native_calls_{0};
    std::vector<std::weak_ptr<Request>> requests_;
    std::unique_ptr<source2root::WorkQueue> queue_;
    bool PrepareExtensionUnload() override {
        if (native_calls_.load())
            return false;

        if (queue_) {
            queue_->Dispatch();

            if (queue_->Pending())
                return false;

            queue_.reset();
        }

        return true;
    }

    bool OnExtensionStart() override {
        server_thread_ = std::this_thread::get_id();
        return RegisterNative("HTTP_Create", 2, &HTTP::Create)
            && RegisterNative("HTTP_Close", 1, &HTTP::Close)
            && RegisterNative("HTTP_SetHeader", 3, &HTTP::SetHeader)
            && RegisterNative("HTTP_SetBody", 2, &HTTP::SetBody)
            && RegisterNative("HTTP_AppendBytes", 3, &HTTP::AppendBytes)
            && RegisterNative("HTTP_Form", 3, &HTTP::Form)
            && RegisterNative("HTTP_FormFile", 4, &HTTP::FormFile)
            && RegisterNative("HTTP_Limits", 4, &HTTP::Limits)
            && RegisterNative("HTTP_TrustFile", 2, &HTTP::TrustFile)
            && RegisterNative("HTTP_Send", 3, &HTTP::Send)
            && RegisterNative("HTTP_Cancel", 1, &HTTP::Cancel)
            && RegisterNative("HTTP_IsDone", 1, &HTTP::Done)
            && RegisterNative("HTTP_Error", 3, &HTTP::ErrorText)
            && RegisterNative("HTTP_Status", 1, &HTTP::Status)
            && RegisterNative("HTTP_Size", 1, &HTTP::Size)
            && RegisterNative("HTTP_ReadText", 4, &HTTP::ReadText)
            && RegisterNative("HTTP_ReadBytes", 4, &HTTP::ReadBytes)
            && RegisterNative("HTTP_HeaderCount", 1, &HTTP::HeaderCount)
            && RegisterNative("HTTP_HeaderName", 4, &HTTP::HeaderName)
            && RegisterNative("HTTP_HeaderValue", 4, &HTTP::HeaderValue)
            && RegisterNative("HTTP_URL", 3, &HTTP::URL);
    }

    static Ref Value(NativeCall& call) {
        return call.Resource<Ref>(call.Int(1), RequestType);
    }

    static Request& Building(NativeCall& call) {
        auto& request = *Value(call);

        if (request.state != State::Building)
            throw web::Error("HTTP request is immutable after submission.");

        return request;
    }

    static const web::Response& Response(NativeCall& call) {
        auto& request = *Value(call);

        if (request.state != State::Complete || !request.error.empty())
            throw web::Error("HTTP response is not available.");

        return request.response;
    }

    template <typename Function> static std::int32_t Invoke(NativeCall& call, Function function, int failure = 0) {
        try {
            return function();
        } catch (const web::Error& error) {
            return call.Fail(error.what(), failure);
        }
    }

    template <typename Function> static std::int32_t Edit(NativeCall& call, Function function) {
        return Invoke(call, [&] {
            auto& request = Building(call);
            auto input = request.input;
            function(input);
            input.Validate();
            request.input = std::move(input);
            return 1;
        });
    }

    std::int32_t Create(NativeCall& call) {
        return Invoke(call, [&] {
            std::erase_if(requests_, [](const auto& request) {
                return request.expired();
            });

            if (requests_.size() >= 64)
                throw web::Error("HTTP provider request limit (64) reached.");

            auto request = std::make_shared<Request>();
            request->extension = this;
            request->input.url = call.String(1);
            request->input.method = static_cast<web::Method>(call.Int(2));
            request->input.Validate();
            requests_.push_back(request);
            request->handle = call.Own(RequestType, std::make_unique<Ref>(request));
            return request->handle;
        });
    }

    std::int32_t Close(NativeCall& call) {
        call.Close(call.Int(1), RequestType);
        return 1;
    }

    std::int32_t SetHeader(NativeCall& call) {
        return Edit(call, [&](auto& input) {
            auto name = call.String(2);
            const auto value = call.String(3);
            web::ValidateHeader(name, value);

            for (auto& ch : name)
                if (ch >= 'A' && ch <= 'Z')
                    ch += 'a' - 'A';

            for (auto& header : input.headers)
                if (header.first == name) {
                    header.second = value;
                    return;
                }

            input.headers.emplace_back(name, value);
        });
    }

    std::int32_t SetBody(NativeCall& call) {
        return Edit(call, [&](auto& input) {
            input.body = call.String(2);
        });
    }

    std::int32_t AppendBytes(NativeCall& call) {
        return Edit(call, [&](auto& input) {
            const auto bytes = call.Array(2, call.Int(3));

            for (const auto byte : bytes) {
                if (byte < 0 || byte > 255)
                    throw web::Error("HTTP upload byte is outside 0..255.");

                input.body.push_back(static_cast<char>(byte));
            }
        });
    }

    std::int32_t Form(NativeCall& call) {
        return Edit(call, [&](auto& input) {
            input.parts.push_back({call.String(2), call.String(3), "", "", {}});
        });
    }

    std::int32_t FormFile(NativeCall& call) {
        return Edit(call, [&](auto& input) {
            const auto filename = call.String(3);
            web::ValidateFilename(filename);
            input.parts.push_back(
                {call.String(2), "", filename, call.String(4), std::filesystem::path(call.DataPath()) / filename});
        });
    }

    std::int32_t Limits(NativeCall& call) {
        return Edit(call, [&](auto& input) {
            input.timeout_ms = call.Int(2);
            input.response_limit = call.Int(3);
            input.redirects = call.Int(4);
        });
    }

    std::int32_t TrustFile(NativeCall& call) {
        return Edit(call, [&](auto& input) {
            const auto filename = call.String(2);

            if (filename.empty())
                input.ca_file.clear();
            else {
                web::ValidateFilename(filename);
                input.ca_file = std::filesystem::path(call.ConfigPath()) / filename;
            }
        });
    }

    std::int32_t Send(NativeCall& call) {
        return Invoke(call, [&] {
            Building(call);
            const auto request = Value(call);

            if (!queue_)
                queue_ = std::make_unique<source2root::WorkQueue>(2, 32);

            const auto data = call.Int(3);
            const auto input = request->input;
            const std::weak_ptr<Request> weak = request;
            auto response = std::make_shared<web::Response>();
            const auto callback = call.Callback(2);

            try {
                request->ticket = queue_->Submit(
                    [this, input, response](const auto& canceled) {
                        *response = client_.Perform(input, [&] {
                            return canceled.load(std::memory_order_relaxed);
                        });
                    },
                    [this, weak, response, data](auto failure) {
                        const auto request = weak.lock();

                        if (!request)
                            return true;

                        if (request->state == State::Sent) {
                            try {
                                if (failure)
                                    std::rethrow_exception(failure);
                            } catch (const std::exception& error) {
                                request->error = std::string(error.what()).substr(0, 1024);
                            } catch (...) {
                                request->error = "HTTP request failed.";
                            }

                            if (!failure)
                                request->response = std::move(*response);

                            request->state = State::Complete;
                        }

                        auto ticket = std::move(request->ticket);
                        const auto callback = std::exchange(request->callback, 0);
                        const auto status = DeliverCallback(callback, {request->handle, data}, request->error.c_str());

                        if (status == KEEL_RESULT_BUSY) {
                            request->ticket = std::move(ticket);
                            request->callback = callback;
                            return false;
                        }

                        return true;
                    });

                if (!request->ticket)
                    throw web::Error("HTTP worker queue is full.");
            } catch (...) {
                CancelCallback(callback);
                throw;
            }

            request->callback = callback;
            request->state = State::Sent;
            request->input = {};
            return 1;
        });
    }

    std::int32_t Cancel(NativeCall& call) {
        const auto request = Value(call);

        if (!request->ticket)
            return 0;

        request->ticket.reset();

        if (request->callback)
            CancelCallback(std::exchange(request->callback, 0));

        request->state = State::Canceled;
        request->error = "HTTP request canceled.";
        request->response = {};
        return 1;
    }

    std::int32_t Done(NativeCall& call) {
        const auto state = Value(call)->state;
        return state == State::Complete || state == State::Canceled;
    }

    std::int32_t ErrorText(NativeCall& call) {
        call.Output(2, call.Int(3), Value(call)->error);
        return 1;
    }

    std::int32_t Status(NativeCall& call) {
        return Invoke(call, [&] {
            return static_cast<int>(Response(call).status);
        });
    }

    std::int32_t Size(NativeCall& call) {
        return Invoke(
            call,
            [&] {
                return static_cast<int>(Response(call).body.size());
            },
            -1);
    }

    std::int32_t ReadText(NativeCall& call) {
        call.Output(2, call.Int(3), "");
        return Invoke(
            call,
            [&] {
                const auto& body = Response(call).body;
                const auto offset = call.Int(4), capacity = call.Int(3);

                if (offset < 0 || static_cast<unsigned>(offset) > body.size())
                    throw web::Error("HTTP body offset is out of range.");

                if (static_cast<unsigned>(offset) < body.size() && capacity < 2)
                    throw web::Error("HTTP text output needs room for data and its terminator.");

                auto chunk = body.substr(offset, capacity - 1);

                if (chunk.find('\0') != chunk.npos)
                    throw web::Error("HTTP body contains NUL bytes; use HTTP_ReadBytes.");

                call.Output(2, capacity, chunk);
                return static_cast<int>(chunk.size());
            },
            -1);
    }

    std::int32_t ReadBytes(NativeCall& call) {
        call.OutputArray(2, call.Int(3), {});
        return Invoke(
            call,
            [&] {
                const auto& body = Response(call).body;
                const auto offset = call.Int(4), capacity = call.Int(3);

                if (offset < 0 || static_cast<unsigned>(offset) > body.size())
                    throw web::Error("HTTP body offset is out of range.");

                const auto count = std::min<std::size_t>(capacity, body.size() - offset);
                std::vector<std::int32_t> bytes(count);

                for (unsigned i = 0; i < count; ++i)
                    bytes[i] = static_cast<unsigned char>(body[offset + i]);

                call.OutputArray(2, capacity, bytes);
                return static_cast<int>(count);
            },
            -1);
    }

    std::int32_t HeaderCount(NativeCall& call) {
        return Invoke(
            call,
            [&] {
                return static_cast<int>(Response(call).headers.size());
            },
            -1);
    }

    std::int32_t Header(NativeCall& call, bool name) {
        call.Output(3, call.Int(4), "");
        return Invoke(call, [&] {
            const auto& headers = Response(call).headers;
            const auto index = call.Int(2);

            if (index < 0 || static_cast<unsigned>(index) >= headers.size())
                throw web::Error("HTTP header index is out of range.");

            call.Output(3, call.Int(4), name ? headers[index].first : headers[index].second);
            return 1;
        });
    }

    std::int32_t HeaderName(NativeCall& call) {
        return Header(call, true);
    }

    std::int32_t HeaderValue(NativeCall& call) {
        return Header(call, false);
    }

    std::int32_t URL(NativeCall& call) {
        call.Output(2, call.Int(3), "");
        return Invoke(call, [&] {
            call.Output(2, call.Int(3), Response(call).url);
            return 1;
        });
    }

    static std::string String(const char* input, unsigned limit = 4095) {
        if (!input)
            return {};

        unsigned count = 0;

        while (count <= limit && input[count])
            ++count;

        if (count > limit)
            throw web::Error("HTTP native string exceeds its limit.");

        return {input, count};
    }

    static KeelResult PerformNative(void* raw, const SrHttpRequest* input, SrHttpCanceled canceled,
        SrHttpComplete complete, void* data, char* error, std::uint32_t capacity) noexcept {
        if (error && capacity)
            error[0] = 0;

        const auto fail = [&](KeelResult status, const char* message) {
            if (error && capacity) {
                const auto length = std::min<std::size_t>(std::strlen(message), capacity - 1);
                std::memcpy(error, message, length);
                error[length] = 0;
            }

            return status;
        };
        auto* self = static_cast<HTTP*>(raw);

        if (!self || !input || input->size != sizeof(*input) || !complete || !error || !capacity || capacity > 4096)
            return fail(KEEL_RESULT_INVALID_ARGUMENT, "Invalid native HTTP request.");

        if (self->server_thread_ == std::this_thread::get_id())
            return fail(KEEL_RESULT_WRONG_THREAD, "Native HTTP transfers require a worker thread.");

        if (self->native_calls_.fetch_add(1) >= 8) {
            self->native_calls_.fetch_sub(1);
            return fail(KEEL_RESULT_BUSY, "Native HTTP concurrency limit (8) reached.");
        }

        struct Active {
            std::atomic<unsigned>& count;
            ~Active() {
                --count;
            }
        } active{self->native_calls_};

        try {
            if (input->header_count > 32 || input->part_count > 32 || input->body_size > web::BodyLimit ||
                (input->header_count && !input->headers) || (input->part_count && !input->parts) || (input->body_size && !input->body))
                throw web::Error("Native HTTP input exceeds limits or has missing data.");

            web::Request request;
            request.url = String(input->url);
            request.method = static_cast<web::Method>(input->method);
            request.timeout_ms = input->timeout_ms;
            request.response_limit = input->response_limit;
            request.redirects = input->redirects;
            request.ca_file = String(input->ca_file);

            if (input->body_size)
                request.body.assign(reinterpret_cast<const char*>(input->body), input->body_size);

            for (unsigned i = 0; i < input->header_count; ++i)
                request.headers.emplace_back(String(input->headers[i].name, 128), String(input->headers[i].value));

            unsigned bytes = input->body_size;

            for (unsigned i = 0; i < input->part_count; ++i) {
                const auto& part = input->parts[i];

                if (part.data_size > web::BodyLimit - bytes || (part.data_size && !part.data))
                    throw web::Error("Native HTTP form data exceeds limits.");

                bytes += part.data_size;
                request.parts.push_back(
                    {String(part.name, 128),
                     part.data_size ? std::string(reinterpret_cast<const char*>(part.data), part.data_size) : "",
                     String(part.filename, 128),
                     String(part.content_type, 128),
                     String(part.source_file)});
            }

            auto response = self->client_.Perform(std::move(request), [&] {
                return canceled && canceled(data) != KEEL_FALSE;
            });
            std::vector<SrHttpHeader> headers;
            headers.reserve(response.headers.size());

            for (const auto& [name, value] : response.headers)
                headers.push_back({name.c_str(), value.c_str()});

            const SrHttpResponse result{sizeof(result),
                                        response.status,
                                        response.url.c_str(),
                                        headers.data(),
                                        static_cast<unsigned>(headers.size()),
                                        reinterpret_cast<const std::uint8_t*>(response.body.data()),
                                        static_cast<unsigned>(response.body.size())};

            complete(data, &result);
            return KEEL_RESULT_OK;
        } catch (const std::exception& exception) {
            return fail(KEEL_RESULT_ENGINE_FAILURE, exception.what());
        } catch (...) {
            return fail(KEEL_RESULT_ENGINE_FAILURE, "Native HTTP operation threw an exception.");
        }
    }
};
}

KEELS2_PLUGIN(HTTP)
