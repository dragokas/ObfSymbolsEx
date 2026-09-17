// CommandLineParser.cpp - Parses ObfSymbolsEx.exe's whole command line.
#include "CommandLineParser.h"

#include <cwchar>
#include <iostream>

namespace {

// Strips one leading and one trailing '"' from `word` when both are
// present, so -fc+"My Class" and -fc+MyClass both yield the same word.
// (Windows' own argv parsing already strips quotes from most quoting
// styles before main() ever sees them; this is a safety net for whichever
// ones don't.)
std::wstring StripQuotes(const std::wstring& word) {
    if (word.size() >= 2 && word.front() == L'"' && word.back() == L'"') {
        return word.substr(1, word.size() - 2);
    }
    return word;
}

struct FilterPrefix {
    const wchar_t* text;
    bool isClassFilter;
    FilterMode mode;
};

// Recognizes one of the four filter switch prefixes at the start of `arg`.
// On a match, fills `isClassFilter`/`mode` and `remainder` (everything
// after the prefix, not yet unquoted) and returns true.
bool MatchFilterPrefix(const std::wstring& arg, bool& isClassFilter, FilterMode& mode, std::wstring& remainder) {
    static const FilterPrefix prefixes[] = {
        { L"-fc+", true,  FilterMode::Include },
        { L"-fc-", true,  FilterMode::Exclude },
        { L"-fm+", false, FilterMode::Include },
        { L"-fm-", false, FilterMode::Exclude },
    };

    for (const auto& prefix : prefixes) {
        size_t len = wcslen(prefix.text);
        if (arg.compare(0, len, prefix.text) == 0) {
            isClassFilter = prefix.isClassFilter;
            mode = prefix.mode;
            remainder = arg.substr(len);
            return true;
        }
    }
    return false;
}

const wchar_t kSymbolServerPrefix[] = L"-srv=";

} // namespace

bool CommandLineParser::Parse(int argc, wchar_t* argv[], CommandLineOptions& options, std::wstring& errorMessage) {
    if (argc < 3) {
        errorMessage = L"Missing required <input> and/or <output> arguments.";
        return false;
    }

    options.inputPath = argv[1];
    options.outputPath = argv[2];
    options.filter = SymbolFilter();
    options.symbolServer.clear();

    for (int i = 3; i < argc; ++i) {
        std::wstring arg = argv[i];

        size_t srvPrefixLen = wcslen(kSymbolServerPrefix);
        if (arg.compare(0, srvPrefixLen, kSymbolServerPrefix) == 0) {
            std::wstring server = StripQuotes(arg.substr(srvPrefixLen));
            if (server.empty()) {
                errorMessage = L"-srv= is missing its URL/path: " + arg;
                return false;
            }
            options.symbolServer = server;
            continue;
        }

        bool isClassFilter = false;
        FilterMode mode = FilterMode::Include;
        std::wstring rawWord;

        if (!MatchFilterPrefix(arg, isClassFilter, mode, rawWord)) {
            errorMessage = L"Unrecognized argument: " + arg;
            return false;
        }

        std::wstring word = StripQuotes(rawWord);
        if (word.empty()) {
            errorMessage = L"Filter switch is missing its WORD: " + arg;
            return false;
        }

        if (isClassFilter) {
            options.filter.AddClassRule(mode, word);
        } else {
            options.filter.AddMethodRule(mode, word);
        }
    }

    return true;
}

void CommandLineParser::PrintUsage() {
    std::wcout << L"Usage: ObfSymbolsEx.exe <input.pdb|PE file> <output.sym> [filters...] [-srv=URL]" << std::endl;
    std::wcout << L"Example: ObfSymbolsEx.exe myapp.pdb symbols.sym" << std::endl;
    std::wcout << L"         ObfSymbolsEx.exe myapp.exe symbols.sym  (any PE image: .exe/.dll/.ocx/.sys/...)" << std::endl;
    std::wcout << std::endl;
    std::wcout << L"Optional report filters (repeatable, any order, always case-insensitive substring checks):" << std::endl;
    std::wcout << L"  -fc+WORD   only include classes whose name contains WORD" << std::endl;
    std::wcout << L"  -fc-WORD   exclude classes whose name contains WORD" << std::endl;
    std::wcout << L"  -fm+WORD   only include lines that contain WORD" << std::endl;
    std::wcout << L"  -fm-WORD   exclude lines that contain WORD" << std::endl;
    std::wcout << L"WORD may be quoted, e.g. -fc+\"My Class\". Multiple +rules OR together; so do multiple -rules." << std::endl;
    std::wcout << L"Example: ObfSymbolsEx.exe server.pdb server.sym -fc+CBaseEntity -fm+model -fm-Index" << std::endl;
    std::wcout << std::endl;
    std::wcout << L"Optional symbol server override, for a PE input file whose PDB isn't next to it:" << std::endl;
    std::wcout << L"  -srv=URL   e.g. -srv=https://msdl.microsoft.com/download/symbols" << std::endl;
    std::wcout << L"Without -srv=, the _NT_SYMBOL_PATH environment variable's srv*/symsrv* entries are" << std::endl;
    std::wcout << L"tried (e.g. srv*C:\\MyServerSymbols*https://msdl.microsoft.com/download/symbols), then" << std::endl;
    std::wcout << L"the default Microsoft public symbol server. The downloaded PDB is saved next to" << std::endl;
    std::wcout << L"ObfSymbolsEx.exe; this is always tried first, before falling back to DIA's own PDB search." << std::endl;
}
