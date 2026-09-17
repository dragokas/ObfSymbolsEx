// CommandLineParser.h - Parses ObfSymbolsEx.exe's whole command line:
// the two required positional arguments (input, output) plus any number of
// optional report-filtering switches from the 4th argument onward.
#pragma once

#include <string>

#include "SymbolFilter.h"

// Parsed, validated command-line arguments.
struct CommandLineOptions {
    std::wstring inputPath;     // argv[1]: .pdb, or a PE image (.exe/.dll/...)
    std::wstring outputPath;    // argv[2]: base .sym output path
    SymbolFilter filter;        // -fc+/-fc-/-fm+/-fm- rules from argv[3..]
    std::wstring symbolServer;  // -srv=... override, empty if not given
};

// Parses argv into `options`. Positions 1 and 2 (1-based) are the required
// input and output paths; any further positions may each be one of:
//   -fc+WORD / -fc-WORD   class-name filter, include/exclude
//   -fm+WORD / -fm-WORD   whole-line filter, include/exclude
//   -srv=URL              symbol server to download a PE input's PDB from
// repeated any number of times (except -srv=, which is used only once), in
// any order. WORD may optionally be wrapped in double quotes ("WORD"),
// which are stripped.
class CommandLineParser {
public:
    // Returns true and fills `options` on success. On failure, returns
    // false and fills `errorMessage` with a human-readable reason (too few
    // positional arguments, an unrecognized switch, or a filter switch
    // missing its WORD).
    static bool Parse(int argc, wchar_t* argv[], CommandLineOptions& options, std::wstring& errorMessage);

    // Prints the "Usage: ..." block (including the filter switches) to
    // stdout.
    static void PrintUsage();
};
