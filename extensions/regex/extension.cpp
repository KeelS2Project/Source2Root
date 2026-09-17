#include "regex.h"
#include <source2root/extension.hpp>
#include <algorithm>

namespace {
using namespace keels2::authoring;
using source2root::NativeCall;
namespace rx = source2root::regex;
class Regex final : public source2root::Extension {
public:
    static constexpr PluginInfo Info{"Source2Root Regex", "KeelS2 Project", "1.0.0", "Bounded PCRE2 patterns, captures and replacement"};
    static constexpr PluginRequirement Requirements[]{{"Source2Root", "1.0.0", DependencyRequirement::exact}};
    Regex() : Extension("source2root.regex") {}
private:
    static constexpr unsigned PatternType = 1, ResultType = 2;
    using PatternRef = std::shared_ptr<rx::Pattern>;
    using ResultRef = std::shared_ptr<rx::Result>;
    std::vector<std::weak_ptr<rx::Pattern>> patterns_;
    std::vector<std::weak_ptr<rx::Result>> results_;
    bool OnExtensionStart() override {
        return RegisterNative("Regex_Compile", 5, &Regex::Compile)
            && RegisterNative("Regex_Close", 1, &Regex::Close)
            && RegisterNative("Regex_Match", 6, &Regex::Match)
            && RegisterNative("Regex_CloseResult", 1, &Regex::CloseResult)
            && RegisterNative("Regex_MatchCount", 1, &Regex::Count)
            && RegisterNative("Regex_GroupCount", 1, &Regex::Groups)
            && RegisterNative("Regex_Capture", 5, &Regex::Capture)
            && RegisterNative("Regex_CaptureName", 5, &Regex::Named)
            && RegisterNative("Regex_Offsets", 5, &Regex::Offsets)
            && RegisterNative("Regex_Replace", 9, &Regex::Replace);
    }
    template <typename T> static void Available(std::vector<std::weak_ptr<T>>& values) {
        std::erase_if(values, [](const auto& value) { return value.expired(); });
        if (values.size() >= 64) throw rx::Error("Regex provider handle limit (64 patterns or 64 results) reached.");
    }
    template <typename Function> static std::int32_t Invoke(NativeCall& call, Function function) {
        try { return function(); } catch (const rx::Error& error) { return call.Fail(error.what()); }
    }
    static rx::Pattern& Pattern(NativeCall& call) { return *call.Resource<PatternRef>(call.Int(1), PatternType); }
    static rx::Result& Result(NativeCall& call) { return *call.Resource<ResultRef>(call.Int(1), ResultType); }
    static bool Boolean(NativeCall& call, unsigned index) {
        const auto value = call.Int(index);
        if (value != 0 && value != 1) throw rx::Error("Regex global option must be false or true.");
        return value != 0;
    }
    static void CaptureText(NativeCall& call, const std::string& value) {
        const auto capacity = call.Int(5);
        if (capacity <= 0 || value.size() >= static_cast<unsigned>(capacity)) throw rx::Error("Regex capture output buffer is too small.");
        call.Output(4, capacity, value);
    }
    static void ErrorText(NativeCall& call, unsigned index, unsigned capacity, const rx::Error& error) {
        // The empty write already validated capacity. Preserve the full message
        // in GetLastError even if the explicit diagnostic buffer is smaller.
        call.Output(index, capacity, std::string(error.what()).substr(0, capacity - 1));
    }
    std::int32_t Compile(NativeCall& call) {
        const auto source = call.String(1);
        call.Output(3, call.Int(4), ""); call.OutputCell(5, -1);
        try {
            Available(patterns_);
            auto pattern = std::make_shared<rx::Pattern>(source, call.Int(2));
            patterns_.push_back(pattern);
            return call.Own(PatternType, std::make_unique<PatternRef>(std::move(pattern)));
        } catch (const rx::Error& error) {
            ErrorText(call, 3, call.Int(4), error); call.OutputCell(5, error.offset);
            return call.Fail(error.what());
        }
    }
    std::int32_t Close(NativeCall& call) { call.Close(call.Int(1), PatternType); return 1; }
    std::int32_t CloseResult(NativeCall& call) { call.Close(call.Int(1), ResultType); return 1; }
    std::int32_t Match(NativeCall& call) {
        const auto subject = call.String(2);
        call.Output(5, call.Int(6), "");
        try {
            Available(results_);
            auto result = std::make_shared<rx::Result>(Pattern(call).Match(subject, Boolean(call, 3), call.Int(4)));
            results_.push_back(result);
            return call.Own(ResultType, std::make_unique<ResultRef>(std::move(result)));
        } catch (const rx::Error& error) {
            ErrorText(call, 5, call.Int(6), error); return call.Fail(error.what());
        }
    }
    std::int32_t Count(NativeCall& call) { return static_cast<std::int32_t>(Result(call).Count()); }
    std::int32_t Groups(NativeCall& call) { return static_cast<std::int32_t>(Result(call).Groups()); }
    std::int32_t Capture(NativeCall& call) {
        call.Output(4, call.Int(5), "");
        return Invoke(call, [&] {
            auto value = Result(call).Capture(call.Int(2), call.Int(3));
            if (!value) return 0;
            CaptureText(call, *value); return 1;
        });
    }
    std::int32_t Named(NativeCall& call) {
        const auto name = call.String(3);
        call.Output(4, call.Int(5), "");
        return Invoke(call, [&] {
            auto& result = Result(call); const auto match = call.Int(2);
            auto value = result.Capture(match, result.Named(match, name));
            if (!value) return 0;
            CaptureText(call, *value); return 1;
        });
    }
    std::int32_t Offsets(NativeCall& call) {
        call.OutputCell(4, -1); call.OutputCell(5, -1);
        return Invoke(call, [&] {
            const auto span = Result(call).Offsets(call.Int(2), call.Int(3));
            call.OutputCell(4, span.start); call.OutputCell(5, span.end); return span.start >= 0 ? 1 : 0;
        });
    }
    std::int32_t Replace(NativeCall& call) {
        const auto subject = call.String(2), replacement = call.String(3);
        call.Output(4, call.Int(5), ""); call.Output(7, call.Int(8), "");
        try {
            const auto result = Pattern(call).Replace(subject, replacement, Boolean(call, 6), call.Int(9), call.Int(5));
            call.Output(4, call.Int(5), result.text); return static_cast<std::int32_t>(result.count);
        } catch (const rx::Error& error) {
            ErrorText(call, 7, call.Int(8), error); return call.Fail(error.what(), -1);
        }
    }
};
}
KEELS2_PLUGIN(Regex)
