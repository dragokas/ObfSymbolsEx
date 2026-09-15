// SymbolFilter.h - Report line/group filtering rules (-fc/-fm switches) and
// the matching logic that applies them.
#pragma once

#include <string>
#include <vector>

// Whether a -fc/-fm rule includes or excludes matching text.
enum class FilterMode {
    Include,   // + : only text containing WORD survives this rule's field
    Exclude    // - : text containing WORD is dropped
};

// One parsed -fc+/-fc-/-fm+/-fm- switch.
struct FilterRule {
    FilterMode mode;
    std::wstring word;   // substring to test for, case-insensitive
};

// Holds every -fc/-fm rule parsed from the command line and implements the
// include-then-exclude matching logic used to decide which report lines
// and vtable-class groups survive into the output files.
//
// For either field (class name or whole line), a piece of text is kept
// according to this two-step rule:
//   1. If any Include (+) rule was given for that field, the text must
//      contain at least one Include rule's word (case-insensitive
//      substring) to survive this step. With no Include rules, everything
//      survives this step.
//   2. Any text that contains an Exclude (-) rule's word is then dropped,
//      regardless of step 1.
// Both steps are always case-insensitive substring checks, never a
// whole-word match. Multiple + rules widen what's kept (OR); multiple -
// rules widen what's removed (OR).
//
// -fc rules test a class name (server.sym/server_obfuscated.sym's own
// VTABLE_CLASS, or a vtable group/table's header class name elsewhere) --
// matching decides whether an entire symbol/line, or an entire group
// (header + members), is considered at all.
// -fm rules test a whole formatted report line -- matching decides whether
// that one line is written, independent of -fc.
class SymbolFilter {
public:
    void AddClassRule(FilterMode mode, const std::wstring& word);
    void AddMethodRule(FilterMode mode, const std::wstring& word);

    bool HasClassRules() const { return !_classRules.empty(); }
    bool HasMethodRules() const { return !_methodRules.empty(); }
    bool IsActive() const { return HasClassRules() || HasMethodRules(); }

    // -fc: true if a symbol/group whose class name is `className` should
    // be kept.
    bool ShouldKeepClass(const std::wstring& className) const;

    // -fm: true if `lineText` (a fully formatted report line) should be
    // kept.
    bool ShouldKeepLine(const std::wstring& lineText) const;

    // Human-readable summary of the active rules, e.g. for a console
    // banner; empty string if no rules are active.
    std::wstring Describe() const;

private:
    std::vector<FilterRule> _classRules;
    std::vector<FilterRule> _methodRules;

    static bool EvaluateRules(const std::vector<FilterRule>& rules, const std::wstring& text);
    static bool ContainsCaseInsensitive(const std::wstring& haystack, const std::wstring& needle);
};
