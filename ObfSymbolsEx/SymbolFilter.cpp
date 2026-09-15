// SymbolFilter.cpp - Report line/group filtering rules and matching logic.
#include "SymbolFilter.h"

#include <algorithm>
#include <cwctype>

void SymbolFilter::AddClassRule(FilterMode mode, const std::wstring& word) {
    _classRules.push_back({mode, word});
}

void SymbolFilter::AddMethodRule(FilterMode mode, const std::wstring& word) {
    _methodRules.push_back({mode, word});
}

bool SymbolFilter::ContainsCaseInsensitive(const std::wstring& haystack, const std::wstring& needle) {
    if (needle.empty()) {
        return true;
    }

    auto toLower = [](std::wstring s) {
        std::transform(s.begin(), s.end(), s.begin(),
            [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        return s;
    };

    return toLower(haystack).find(toLower(needle)) != std::wstring::npos;
}

bool SymbolFilter::EvaluateRules(const std::vector<FilterRule>& rules, const std::wstring& text) {
    // Step 1: if any Include rule exists, `text` must match at least one.
    bool hasInclude = false;
    bool includeMatched = false;
    for (const auto& rule : rules) {
        if (rule.mode == FilterMode::Include) {
            hasInclude = true;
            if (ContainsCaseInsensitive(text, rule.word)) {
                includeMatched = true;
            }
        }
    }
    if (hasInclude && !includeMatched) {
        return false;
    }

    // Step 2: any Exclude rule match drops `text`, regardless of step 1.
    for (const auto& rule : rules) {
        if (rule.mode == FilterMode::Exclude && ContainsCaseInsensitive(text, rule.word)) {
            return false;
        }
    }

    return true;
}

bool SymbolFilter::ShouldKeepClass(const std::wstring& className) const {
    return EvaluateRules(_classRules, className);
}

bool SymbolFilter::ShouldKeepLine(const std::wstring& lineText) const {
    return EvaluateRules(_methodRules, lineText);
}

std::wstring SymbolFilter::Describe() const {
    if (!IsActive()) {
        return L"";
    }

    std::wstring result;
    auto appendRules = [&](const wchar_t* label, const std::vector<FilterRule>& rules) {
        for (const auto& rule : rules) {
            if (!result.empty()) {
                result += L", ";
            }
            result += label;
            result += (rule.mode == FilterMode::Include) ? L"+" : L"-";
            result += rule.word;
        }
    };
    appendRules(L"fc", _classRules);
    appendRules(L"fm", _methodRules);
    return result;
}
