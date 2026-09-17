#include "regex.h"
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>

namespace source2root::regex {
namespace {
using Clock = std::chrono::steady_clock;
template <class T, void (*Free)(T*)> using Ptr = std::unique_ptr<T, decltype(Free)>;
struct Budget {
    std::size_t used = 0, limit;
    explicit Budget(std::size_t limit) : limit(limit) {}
    struct alignas(std::max_align_t) Header { std::size_t size; };
    static void* Allocate(PCRE2_SIZE size, void* data) noexcept {
        auto& budget = *static_cast<Budget*>(data);
        if (size > budget.limit - budget.used || sizeof(Header) > budget.limit - budget.used - size) return nullptr;
        const auto total = size + sizeof(Header);
        auto* header = static_cast<Header*>(std::malloc(total));
        if (!header) return nullptr;
        header->size = total; budget.used += total;
        return header + 1;
    }
    static void Release(void* pointer, void* data) noexcept {
        if (!pointer) return;
        auto* header = static_cast<Header*>(pointer) - 1;
        static_cast<Budget*>(data)->used -= header->size;
        std::free(header);
    }
};
std::string Message(int code) {
    std::array<PCRE2_UCHAR, 256> message{};
    if (pcre2_get_error_message(code, message.data(), message.size()) < 0) return "Regex operation failed.";
    return "Regex: " + std::string(reinterpret_cast<const char*>(message.data()));
}
void Text(std::string_view value) {
    if (value.size() > TextLimit || value.find('\0') != std::string_view::npos)
        throw Error("Regex text must be at most 4095 bytes without NUL characters.");
}
PCRE2_SPTR Bytes(std::string_view value) {
    return reinterpret_cast<PCRE2_SPTR>(value.empty() ? "" : value.data());
}
struct Operation {
    Budget budget{2 * 1024 * 1024};
    Ptr<pcre2_general_context, pcre2_general_context_free> general{nullptr, pcre2_general_context_free};
    Ptr<pcre2_match_context, pcre2_match_context_free> context{nullptr, pcre2_match_context_free};
    Ptr<pcre2_match_data, pcre2_match_data_free> data{nullptr, pcre2_match_data_free};
    const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(20);
    unsigned steps = 0;
    bool exhausted = false, too_many = false;
    explicit Operation(const pcre2_code* code) {
        general.reset(pcre2_general_context_create(Budget::Allocate, Budget::Release, &budget));
        if (general) { context.reset(pcre2_match_context_create(general.get())); data.reset(pcre2_match_data_create_from_pattern(code, general.get())); }
        if (!context || !data) throw Error("Regex operation memory limit reached.");
        pcre2_set_match_limit(context.get(), 100000);
        pcre2_set_depth_limit(context.get(), 128);
        pcre2_set_heap_limit(context.get(), 1024);
        pcre2_set_callout(context.get(), [](pcre2_callout_block*, void* raw) noexcept {
            auto& self = *static_cast<Operation*>(raw);
            if (++self.steps > 100000 || ((self.steps & 255) == 0 && Clock::now() > self.deadline)) {
                self.exhausted = true; return PCRE2_ERROR_CALLOUT;
            }
            return 0;
        }, this);
        pcre2_set_substitute_callout(context.get(), [](pcre2_substitute_callout_block* block, void* raw) noexcept {
            auto& self = *static_cast<Operation*>(raw);
            if (block->subscount > MatchLimit) { self.too_many = true; return -1; }
            return 0;
        }, this);
    }
    void Check(int rc) const {
        if (too_many) throw Error("Regex global replacement limit (256) reached; output discarded.");
        if (exhausted) throw Error("Regex operation work budget reached.");
        if (rc < 0 && rc != PCRE2_ERROR_NOMATCH) throw Error(Message(rc));
    }
};
}
struct Pattern::Impl {
    Budget budget{1024 * 1024};
    Ptr<pcre2_general_context, pcre2_general_context_free> general{nullptr, pcre2_general_context_free};
    Ptr<pcre2_code, pcre2_code_free> code{nullptr, pcre2_code_free};
    unsigned groups = 0;
    std::vector<std::pair<std::string, unsigned>> names;
};
Pattern::Pattern(std::string_view pattern, std::uint32_t flags) : impl_(std::make_unique<Impl>()) {
    Text(pattern);
    if (flags & ~511u) throw Error("Unknown regex compile flags.");
    auto& self = *impl_;
    self.general.reset(pcre2_general_context_create(Budget::Allocate, Budget::Release, &self.budget));
    if (!self.general) throw Error("Regex compile memory limit reached.");
    Ptr<pcre2_compile_context, pcre2_compile_context_free> context(pcre2_compile_context_create(self.general.get()), pcre2_compile_context_free);
    if (!context) throw Error("Regex compile memory limit reached.");
    pcre2_set_parens_nest_limit(context.get(), 64);
    pcre2_set_max_pattern_length(context.get(), TextLimit);
    pcre2_set_max_pattern_compiled_length(context.get(), 256 * 1024);
    pcre2_set_compile_extra_options(context.get(), PCRE2_EXTRA_NEVER_CALLOUT);
    auto deadline = Clock::now() + std::chrono::milliseconds(20);
    pcre2_set_compile_recursion_guard(context.get(), [](std::uint32_t depth, void* raw) noexcept {
        return depth > 64 || Clock::now() > *static_cast<const Clock::time_point*>(raw) ? 1 : 0;
    }, &deadline);
    const std::uint32_t options[] = {PCRE2_CASELESS, PCRE2_MULTILINE, PCRE2_DOTALL, PCRE2_EXTENDED,
        PCRE2_ANCHORED, PCRE2_DOLLAR_ENDONLY, PCRE2_UNGREEDY, PCRE2_UTF, PCRE2_UCP};
    std::uint32_t native = PCRE2_AUTO_CALLOUT | PCRE2_NEVER_BACKSLASH_C;
    for (unsigned i = 0; i < std::size(options); ++i) if (flags & (1u << i)) native |= options[i];
    int error = 0; PCRE2_SIZE offset = 0;
    self.code.reset(pcre2_compile(Bytes(pattern), pattern.size(), native, &error, &offset, context.get()));
    if (!self.code) throw Error(Message(error), offset <= pattern.size() ? static_cast<int>(offset) : -1);
    if (Clock::now() > deadline) throw Error("Regex compilation time budget reached.");
    pcre2_pattern_info(self.code.get(), PCRE2_INFO_CAPTURECOUNT, &self.groups);
    if (self.groups > CaptureLimit) throw Error("Regex capture group limit (64) reached.");
    std::uint32_t count = 0, width = 0; PCRE2_SPTR names = nullptr;
    pcre2_pattern_info(self.code.get(), PCRE2_INFO_NAMECOUNT, &count);
    pcre2_pattern_info(self.code.get(), PCRE2_INFO_NAMEENTRYSIZE, &width);
    pcre2_pattern_info(self.code.get(), PCRE2_INFO_NAMETABLE, &names);
    for (unsigned i = 0; i < count; ++i, names += width)
        self.names.emplace_back(reinterpret_cast<const char*>(names + 2), (names[0] << 8) | names[1]);
}
Pattern::~Pattern() = default;
Result Pattern::Match(std::string_view subject, bool all, unsigned offset) const {
    Text(subject);
    if (offset > subject.size()) throw Error("Regex byte offset is outside subject.");
    Result result; result.subject_ = subject; result.groups_ = impl_->groups; result.names_ = impl_->names;
    Operation op(impl_->code.get());
    PCRE2_SIZE start = offset; std::uint32_t options = 0;
    do {
        const auto rc = pcre2_match(impl_->code.get(), Bytes(subject), subject.size(), start, options, op.data.get(), op.context.get());
        op.Check(rc);
        if (rc == PCRE2_ERROR_NOMATCH) break;
        if (rc == 0) throw Error("Regex capture storage is insufficient.");
        if (result.Count() == MatchLimit) throw Error("Regex global match limit (256) reached; results discarded.");
        const auto* spans = pcre2_get_ovector_pointer(op.data.get());
        std::vector<Span> match(result.groups_ + 1);
        for (unsigned i = 0; i < static_cast<unsigned>(rc); ++i) {
            if (spans[i * 2] == PCRE2_UNSET) continue;
            if (spans[i * 2] > spans[i * 2 + 1] || spans[i * 2 + 1] > subject.size()) throw Error("Regex produced invalid capture offsets.");
            match[i] = {static_cast<int>(spans[i * 2]), static_cast<int>(spans[i * 2 + 1])};
        }
        result.matches_.push_back(std::move(match));
    } while (all && pcre2_next_match(op.data.get(), &start, &options));
    return result;
}
Replacement Pattern::Replace(std::string_view subject, std::string_view replacement, bool all, unsigned offset, unsigned capacity) const {
    Text(subject); Text(replacement);
    if (offset > subject.size()) throw Error("Regex byte offset is outside subject.");
    if (!capacity || capacity > TextLimit + 1) throw Error("Regex output capacity must be 1..4096.");
    Operation op(impl_->code.get()); std::array<PCRE2_UCHAR, TextLimit + 1> output{}; PCRE2_SIZE length = capacity;
    const auto rc = pcre2_substitute(impl_->code.get(), Bytes(subject), subject.size(), offset,
        PCRE2_SUBSTITUTE_UNSET_EMPTY | (all ? PCRE2_SUBSTITUTE_GLOBAL : 0), op.data.get(), op.context.get(),
        Bytes(replacement), replacement.size(), output.data(), &length);
    op.Check(rc);
    if (rc < 0) throw Error(Message(rc));
    return {std::string(reinterpret_cast<const char*>(output.data()), length), static_cast<unsigned>(rc)};
}
Span Result::Offsets(unsigned match, unsigned group) const {
    if (match >= matches_.size() || group > groups_) throw Error("Regex match or group index is out of range.");
    return matches_[match][group];
}
std::optional<std::string> Result::Capture(unsigned match, unsigned group) const {
    const auto span = Offsets(match, group);
    if (span.start < 0) return std::nullopt;
    return subject_.substr(span.start, span.end - span.start);
}
unsigned Result::Named(unsigned match, std::string_view name) const {
    if (match >= matches_.size()) throw Error("Regex match index is out of range.");
    std::optional<unsigned> first;
    for (const auto& [candidate, group] : names_) if (candidate == name) {
        if (!first) first = group;
        if (Offsets(match, group).start >= 0) return group;
    }
    if (!first) throw Error("Unknown regex capture name.");
    return *first;
}
}
