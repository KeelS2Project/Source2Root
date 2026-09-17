#if defined _source2root_regex_included
    #endinput
#endif
#define _source2root_regex_included
#include <source2root>

enum RegexPattern { NoRegexPattern = 0 }
enum RegexResult { NoRegexResult = 0 }
enum RegexFlags {
    Regex_None = 0, Regex_Caseless = 1, Regex_Multiline = 2, Regex_Dotall = 4,
    Regex_Extended = 8, Regex_Anchored = 16, Regex_DollarEndOnly = 32,
    Regex_Ungreedy = 64, Regex_UTF = 128, Regex_UCP = 256
}
// PCRE2 syntax; flags and handles are specific to Source2Root, not SourceMod.
// Text is NUL-terminated, at most 4095 bytes. Pattern limits: 64 captures,
// nesting 64, compiled size 256KiB, PCRE2 allocations 1MiB. Match allocations
// <=2MiB with a 1MiB interpreter heap, depth 128 and backtracking limit 100000.
// All matches in one operation share 100000 automatic callouts and a 20ms
// cooperative deadline (checked every 256 callouts). Compile checks its 20ms
// budget at recursive entry and completion. These are not hard real-time limits.
// No JIT, pattern callouts or \C byte escapes. UTF validation cannot be skipped.
// At most 64 patterns and 64 results across the provider; script quota also applies.
// Domain failures set GetLastError; invalid/foreign/stale handles raise native errors.
// errorOffset is a byte offset on syntax failure, -1 when unavailable. Diagnostic
// text is truncated to fit the explicit error buffer; GetLastError keeps it all.
native RegexPattern Regex_Compile(const char[] pattern, RegexFlags flags, char[] error, int capacity, int &errorOffset);
native bool Regex_Close(RegexPattern pattern);

// A valid owned result with MatchCount()==0 means no match; NoRegexResult means
// failure. Match indices start at zero. offset is a byte offset into the subject;
// in UTF mode it must be at a character boundary. Global results include at most
// 256 matches; exceeding any limit discards the entire operation's results.
// Results copy the subject and capture spans and survive closing the pattern.
native RegexResult Regex_Match(RegexPattern pattern, const char[] subject, bool all, int offset, char[] error, int capacity);
native bool Regex_CloseResult(RegexResult result);
native int Regex_MatchCount(RegexResult result);
// Declared capture groups, excluding group zero (the whole match). All captures
// belong to one match; repeated groups contain their last captured substring.
native int Regex_GroupCount(RegexResult result);
// Unset optional captures return false/empty; participating empty captures return
// true/empty. Undersized buffers fail with empty output rather than truncate.
native bool Regex_Capture(RegexResult result, int match, int group, char[] output, int capacity);
// Duplicate names (?J) select the first participating group, or return false/empty
// when all are unset. An unknown name sets GetLastError and returns false/empty.
native bool Regex_CaptureName(RegexResult result, int match, const char[] name, char[] output, int capacity);
// End is exclusive. Unset captures return false with both offsets set to -1.
native bool Regex_Offsets(RegexResult result, int match, int group, int &start, int &end);

// Replacement uses $0, $1, ${name}, and $$ for a literal dollar. Backslashes are
// literal; extended replacement escapes/case conversion are disabled. Unset
// captures expand to empty; unknown groups are errors. Returns replacement count
// (0 copies subject unchanged), or -1 with empty output on any failure. Global
// replacement is limited to 256 substitutions. capacity includes the terminator.
// Output may reuse an input buffer; error must be a separate buffer from output.
native int Regex_Replace(RegexPattern pattern, const char[] subject, const char[] replacement,
    char[] output, int capacity, bool all, char[] error, int errorCapacity, int offset = 0);
