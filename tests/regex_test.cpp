#include "regex.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

using namespace source2root::regex;
static void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template <typename Operation> static std::string Reject(Operation operation, const char* message) {
    try { operation(); } catch (const Error& error) { return error.what(); }
    throw std::runtime_error(message);
}
int main() {
    try {
        Result retained;
        {
            Pattern p("(?<word>[a-z]+)(?:=(\\d+))?", Caseless);
            retained = p.Match("One=42 two", true);
            Check(retained.Count() == 2 && retained.Groups() == 2 && retained.Capture(0, 0) == "One=42" &&
                retained.Capture(0, retained.Named(0, "word")) == "One" && retained.Capture(0, 2) == "42", "numbered/named captures");
            Check(retained.Offsets(1, 1).start == 7 && retained.Offsets(1, 1).end == 10 &&
                !retained.Capture(1, 2) && retained.Offsets(1, 2).start == -1, "byte offsets and unset optional capture");
            Check(p.Match("123").Count() == 0 && p.Match("one two", false, 4).Capture(0, 0) == "two", "no match and starting offset");
        }
        Check(retained.Capture(1, 1) == "two", "result survives pattern destruction");
        Reject([&] { retained.Capture(2, 0); }, "invalid match index refused");
        Reject([&] { retained.Capture(0, 3); }, "invalid group index refused");
        Reject([&] { retained.Named(0, "absent"); }, "unknown capture name refused");
        Pattern empty("()|(a)");
        const auto empties = empty.Match("a", true);
        Check(empties.Count() == 3 && empties.Capture(0, 1) == "" && !empties.Capture(0, 2) &&
            empties.Capture(1, 2) == "a" && empties.Offsets(2, 0).start == 1, "global empty matches make progress and retain nonempty alternative");
        auto dup = Pattern("(?J)(?<v>a)|(?<v>b)").Match("ab", true);
        Check(dup.Capture(0, dup.Named(0, "v")) == "a" && dup.Capture(1, dup.Named(1, "v")) == "b", "duplicate names select participating group");
        Check(Pattern("^b.$", Multiline | Dotall).Match("a\nb\nc").Count() == 0, "multiline anchors constrain matches");
        Check(Pattern("^b.$", Multiline | Dotall).Match("a\nb\n").Capture(0, 0) == "b\n", "dotall and multiline compile flags");
        Check(Pattern(" a # comment\n b ", Extended).Match("ab").Count() == 1, "extended whitespace/comments");
        Check(Pattern("a", Anchored).Match("ba").Count() == 0 && Pattern("a", Anchored).Match("ba", false, 1).Count() == 1, "anchoring honors offset");
        Check(Pattern("a$", DollarEndOnly).Match("a\n").Count() == 0 && Pattern("a.*b", Ungreedy).Match("abxb").Capture(0, 0) == "ab", "endonly and ungreedy flags");
        Pattern utf("\\w+", Utf | Ucp);
        Check(utf.Match("é世界").Capture(0, 0) == "é世界", "Unicode properties");
        auto unicode = Pattern("", Utf).Match("é界", true);
        Check(unicode.Count() == 3 && unicode.Offsets(1, 0).start == 2 && unicode.Offsets(2, 0).start == 5, "empty UTF matches advance whole characters");
        Reject([&] { utf.Match("é", false, 1); }, "UTF continuation offset refused");
        Reject([&] { utf.Match(std::string(1, '\xff')); }, "invalid UTF subject refused");
        Reject([&] { utf.Replace("é", std::string(1, '\xff')); }, "invalid UTF replacement refused");
        Reject([&] { Pattern bad(std::string(1, '\xff'), Utf); }, "invalid UTF pattern refused");
        Reject([&] { Pattern("(*UTF)\\C"); }, "raw-byte UTF matching refused");
        Reject([&] { Pattern("(?C1)a"); }, "explicit callout refused");
        Reject([&] { Pattern("a", 512); }, "unknown flags refused");
        bool syntax = false;
        try { Pattern("("); } catch (const Error& error) { syntax = error.offset == 1; }
        Check(syntax, "compile diagnostic byte offset");
        Reject([&] { utf.Match("a", false, 2); }, "out-of-range start refused");
        Reject([&] { utf.Match(std::string(4096, 'a')); }, "large subject refused");
        Reject([&] { Pattern(std::string(4096, 'a')); }, "large pattern refused");
        Reject([&] { Pattern(std::string("a\0b", 3)); }, "NUL pattern refused");
        std::string groups;
        for (int i = 0; i < 65; ++i) groups += "()";
        Reject([&] { Pattern p(groups); }, "too many captures refused");
        Reject([&] { Pattern p(std::string(65, '(') + "a" + std::string(65, ')')); }, "deep compile refused");
        Reject([&] { Pattern p("(?:abcdefghij){65535}"); }, "expanded compile size/memory refused");
        Pattern one(".");
        Check(one.Match(std::string(256, 'a'), true).Count() == 256, "exact global match cap accepted");
        Reject([&] { one.Match(std::string(257, 'a'), true); }, "excessive global matches fail without partial result");
        Pattern replace("(?<letter>[a-z])(?<digit>[0-9])?");
        auto changed = replace.Replace("a1 b c2", "${digit}${letter}$$", true);
        Check(changed.count == 3 && changed.text == "1a$ b$ 2c$", "global named and optional replacements");
        Check(replace.Replace("a1 b2", "$0!", false).text == "a1! b2", "whole-match replacement");
        Check(replace.Replace("a1 b2", "$2$1", true, 3).text == "a1 2b", "replacement starting offset preserves prefix");
        Check(replace.Replace("a1", "\\n", false).text == "\\n", "backslashes in replacement remain literal");
        changed = replace.Replace("123", "$1", true);
        Check(changed.count == 0 && changed.text == "123", "zero replacements preserve subject");
        Check(Pattern("", Utf).Replace("é界", "-", true).text == "-é-界-", "global UTF empty substitutions");
        Check(Pattern("a").Replace("a", "", true, 0, 1).text.empty(), "terminator-only output capacity");
        Check(one.Replace(std::string(256, 'a'), "", true).count == 256, "exact global replacement cap accepted");
        Reject([&] { one.Replace(std::string(257, 'a'), "", true); }, "replacement callout refuses excessive substitutions");
        Reject([&] { replace.Replace("a1", "$unknown"); }, "unknown replacement group refused");
        Reject([&] { replace.Replace("a1", "$1", false, 0, 1); }, "output overflow refused");
        Reject([&] { replace.Replace("a1", std::string(4096, 'a')); }, "large replacement refused");
        Reject([&] { replace.Replace("a1", "x", false, 0, 4097); }, "invalid output capacity refused");
        const auto began = std::chrono::steady_clock::now();
        Pattern expensive("(*NO_START_OPT)(a+)+$");
        Reject([&] { expensive.Match(std::string(40, 'a') + '!'); }, "pathological backtracking must fail within budget");
        Reject([&] { expensive.Replace(std::string(40, 'a') + '!', "x", true); }, "replacement matching uses same work limits");
        // Each starting position is cheap enough individually; this specifically
        // exercises the whole-operation budget across unanchored search attempts.
        const auto limit = Reject([&] { Pattern("(*NO_START_OPT)(*NO_AUTO_POSSESS)a+b").Match(std::string(4095, 'a')); }, "unanchored aggregate work bounded");
        Check(limit.find("work budget") != std::string::npos, "aggregate callout budget applies across starting positions");
        Check(std::chrono::steady_clock::now() - began < std::chrono::seconds(2), "expensive operations terminate promptly");
        std::atomic<unsigned> successes{0};
        std::vector<std::thread> threads;
        for (unsigned i = 0; i < 4; ++i) threads.emplace_back([&] {
            for (unsigned j = 0; j < 50; ++j) if (replace.Match("a1 b2", true).Count() == 2) ++successes;
        });
        for (auto& thread : threads) thread.join();
        Check(successes == 200, "immutable pattern uses independent concurrent match contexts");
        std::cout << "Regex compile, captures, Unicode, replacement, ownership and resource bounds passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
