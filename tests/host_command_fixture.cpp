#include "host_fixture.h"
#include <tier1/convar.h>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstdlib>

namespace {
struct Command {
    int argument_start = 0;
    CUtlVectorFixedGrowable<char, 512> text;
    CUtlVectorFixedGrowable<char, 512> unused;
    CUtlVectorFixedGrowable<char*, 64> arguments;
    std::vector<std::string> words;
    explicit Command(const std::vector<std::string>& values, const char* original = nullptr) : words(values) {
        std::string joined;
        for (const auto& value : words) { if (!joined.empty()) joined += ' '; joined += value; }
        const char* input = original ? original : joined.c_str();
        if (words.size() > 64 || std::strlen(input) >= 512) throw std::runtime_error("fixture command limit");
        text.AddMultipleToTail(static_cast<int>(std::strlen(input) + 1), input);
        for (auto& value : words) arguments.AddToTail(value.data());
        if (words.size() > 1) argument_start = static_cast<int>(words[0].size() + 1);
    }
};
static_assert(offsetof(Command, words) == sizeof(CCommand));

class Cvar {
public:
    virtual void Slot00() { std::abort(); }
    virtual void Slot01() { std::abort(); }
    virtual void Slot02() { std::abort(); }
    virtual void Slot03() { std::abort(); }
    virtual void Slot04() { std::abort(); }
    virtual void Slot05() { std::abort(); }
    virtual void Slot06() { std::abort(); }
    virtual void Slot07() { std::abort(); }
    virtual void Slot08() { std::abort(); }
    virtual void Slot09() { std::abort(); }
    virtual void Slot10() { std::abort(); }
    virtual void Slot11() { std::abort(); }
    virtual void Slot12() { std::abort(); }
    virtual void Slot13() { std::abort(); }
    virtual void Slot14() { std::abort(); }
    virtual void Slot15() { std::abort(); }
    virtual void Slot16() { std::abort(); }
    virtual void Slot17() { std::abort(); }
    virtual void Slot18() { std::abort(); }
    virtual void Slot19() { std::abort(); }
    virtual void DispatchConCommand(ConCommandRef, const CCommandContext&, const CCommand&) { ++calls; }
    unsigned calls = 0;
} cvar;

}
extern "C" void SrFixtureWithArguments(uint32_t count, const char* const* values, int slot,
    SrFixtureDispatch callback, void* data) {
    std::vector<std::string> words;
    for (uint32_t i = 0; i < count; ++i) words.emplace_back(values[i]);
    Command command(words);
    const auto* native = reinterpret_cast<const CCommand*>(&command);
    CCommandContext context(CT_NO_TARGET, CPlayerSlot(slot));
    callback(data, &context, native, static_cast<uint32_t>(native->ArgC()), native->ArgV());
}
extern "C" void SrFixtureWithCommand(const char* text, int slot, SrFixtureDispatch callback, void* data) {
    std::istringstream parser(text);
    std::vector<std::string> words;
    std::string word;
    while (parser >> std::quoted(word)) words.push_back(word);
    Command command(words, text);
    const auto* native = reinterpret_cast<const CCommand*>(&command);
    CCommandContext context(CT_NO_TARGET, CPlayerSlot(slot));
    callback(data, &context, native, static_cast<uint32_t>(native->ArgC()), native->ArgV());
}
extern "C" int SrFixtureCaller(const void* context) {
    return static_cast<const CCommandContext*>(context)->GetPlayerSlot().Get();
}
extern "C" void* SrFixtureCvar() { return &cvar; }
extern "C" bool SrFixtureDispatchConCommand(uint32_t count, const char* const* values, int slot) {
    const auto before = cvar.calls;
    SrFixtureWithArguments(count, values, slot, [](void*, const void* context, const void* command,
        uint32_t, const char* const*) {
        auto* table = *reinterpret_cast<void***>(&cvar);
        using Dispatch = void (*)(Cvar*, ConCommandRef, const CCommandContext&, const CCommand&);
        const auto dispatch = reinterpret_cast<Dispatch>(static_cast<void* volatile*>(table)[20]);
        dispatch(&cvar, ConCommandRef(1, 1), *static_cast<const CCommandContext*>(context),
            *static_cast<const CCommand*>(command));
    }, nullptr);
    return cvar.calls != before;
}
