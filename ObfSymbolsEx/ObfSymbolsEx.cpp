// ObfSymbolsEx.cpp : Parse PDB files and extract function symbols
//

#include "PdbSymbolExtractor.h"
#include "CommandLineParser.h"
#include "SymbolFilter.h"
#include "PdbSymbolDownloader.h"
#include "Version.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <iomanip>
#include <sstream>

// Helper function to convert wstring to string
std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

// Inverse of WStringToString(), used to run a -fm filter (which stores its
// WORD as std::wstring) against a report line that has already been
// formatted into a narrow std::string for writing.
std::wstring StringToWString(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

// Fixed width of the VISIBILITY column: "PUBLIC"/"PRIVATE" are the only two
// values that ever appear, so this never needs to be computed from data.
static const int kVisibilityWidth = 7; // strlen("PRIVATE")

// Column widths for the two remaining fields whose value space is small and
// bounded -- SIZE=N (a byte count) and CALLING_CONVENTION (one of a handful
// of known conventions) -- computed from the widest value that actually
// occurs in this run, so lines align exactly without over- or
// under-reserving space. REAL_NAME/SIGNATURE/RETURN_TYPE/VTABLE_CLASS are
// deliberately NOT aligned this way: those can run to hundreds of
// characters for a template-heavy symbol, and forcing every other line to
// pad out to match would make the file harder to read, not easier (see
// OUTPUT_FORMAT.md's "no column alignment" note) -- VISIBILITY, SIZE, and
// CALLING_CONVENTION don't have that problem, since their value space is
// small and bounded by construction.
//
// Widths are always computed from the FULL (pre-filter) symbol set, not
// whatever -fc/-fm leave behind -- so column alignment doesn't shift
// depending on which filters happen to be active on a given run.
struct ColumnWidths {
    int sizeFieldWidth;         // width of the whole "SIZE=NNN" token
    int callingConventionWidth;
};

static ColumnWidths ComputeColumnWidths(const std::vector<FunctionSymbol>& symbols) {
    size_t maxSizeDigits = 1;
    size_t maxCallingConventionLen = 0;
    for (const auto& symbol : symbols) {
        maxSizeDigits = (std::max)(maxSizeDigits, std::to_string(symbol.length).size());
        maxCallingConventionLen = (std::max)(maxCallingConventionLen, WStringToString(symbol.callingConvention).size());
    }

    ColumnWidths widths;
    widths.sizeFieldWidth = static_cast<int>(5 + maxSizeDigits); // "SIZE=" is 5 characters
    widths.callingConventionWidth = static_cast<int>(maxCallingConventionLen);
    return widths;
}

// Number of hex digits std::hex would print for `value` (no leading zeros,
// "0" prints as one digit) -- used to size the "0x..." token in the
// SLOT/OFFSET/RVA alignment below the same way std::to_string().size() sizes
// a decimal one above.
static size_t HexDigitCount(ULONGLONG value) {
    std::ostringstream oss;
    oss << std::hex << value;
    return oss.str().size();
}

// Column widths for server_vtable.sym's/server_vtable_obfuscated.sym's SLOT
// line: SLOT=N, OFFSET=0x..., RVA=0x..., and SIZE=N all have a naturally
// bounded value space for a given PDB (a slot index can't exceed the
// largest vtable's slot count; RVA/SIZE can't exceed the binary's own
// address space / largest function), so -- like VISIBILITY/SIZE/
// CALLING_CONVENTION in ComputeColumnWidths() above -- they are safe to
// column-align without the "arbitrarily long template name" problem that
// keeps METHOD/METHOD_CLASS/etc. unpadded.
struct VTableColumnWidths {
    int slotFieldWidth;    // width of the whole "SLOT=NNN" token
    int offsetFieldWidth;  // width of the whole "OFFSET=0x..." token
    int rvaFieldWidth;     // width of the whole "RVA=0x..." token (or "RVA=-1")
    int sizeFieldWidth;    // width of the whole "SIZE=NNN" token (or "SIZE=-1")
};

static VTableColumnWidths ComputeVTableColumnWidths(const std::vector<VTableSymbol>& vtables,
                                                     const std::vector<FunctionSymbol>& symbols,
                                                     DWORD targetPointerSize) {
    // SLOT ranges over [0, slotCount) for every table; OFFSET = SLOT *
    // pointerSize -- both are bounded purely by the largest table's slot
    // count, with no dependency on which methods end up matched to a slot.
    // `vtables` alone isn't the full picture: WriteVTablesToFile() also adds
    // a synthetic table for any (class, shape) pair a virtual method
    // exposes but DIA never gave a concrete SymTagVTable for, and a
    // synthetic table's slot count can exceed every concrete table's -- so
    // `maxSlot` must be tightened by both `vtables` and `symbols` before
    // `maxOffset` is derived from it (deriving `maxOffset` independently,
    // per source, risks under-counting it relative to the true `maxSlot`
    // exactly when a synthetic table turns out to be the largest one).
    ULONGLONG maxSlot = 0;
    for (const auto& table : vtables) {
        if (table.slotCount == 0) continue;
        maxSlot = (std::max)(maxSlot, static_cast<ULONGLONG>(table.slotCount - 1));
    }
    for (const auto& symbol : symbols) {
        if (!symbol.isVirtual || symbol.vtableIndex < 0 || symbol.vtableShapeId == 0 || symbol.vtableSlotCount == 0) {
            continue;
        }
        maxSlot = (std::max)(maxSlot, static_cast<ULONGLONG>(symbol.vtableSlotCount - 1));
    }
    // Every table in this PDB shares the same target pointer size (passed
    // in from the PDB's own machine type, not guessed), so the largest
    // possible OFFSET is simply the largest possible SLOT times it.
    ULONGLONG maxOffset = maxSlot * static_cast<ULONGLONG>(targetPointerSize);

    // RVA and SIZE, for whichever method ends up matched to a slot, can only
    // ever be drawn from the same pool WriteVTablesToFile() populates
    // exactMethods/shapeMethods from -- computing the max over that same
    // pool gives the exact upper bound of what could ever be printed,
    // without duplicating the match-selection logic itself.
    ULONGLONG maxRva = 0;
    ULONGLONG maxSize = 0;
    for (const auto& symbol : symbols) {
        if (!symbol.isVirtual || symbol.vtableIndex < 0 || symbol.vtableShapeId == 0) {
            continue;
        }
        maxRva = (std::max)(maxRva, static_cast<ULONGLONG>(symbol.rva));
        maxSize = (std::max)(maxSize, static_cast<ULONGLONG>(symbol.length));
    }

    VTableColumnWidths widths;
    widths.slotFieldWidth = static_cast<int>(5 + (std::max<size_t>)(1, std::to_string(maxSlot).size())); // "SLOT="
    widths.offsetFieldWidth = static_cast<int>(7 + 2 + (std::max<size_t>)(1, HexDigitCount(maxOffset))); // "OFFSET=" + "0x"
    widths.rvaFieldWidth = static_cast<int>((std::max<size_t>)(4 + 2 + HexDigitCount(maxRva), 6)); // "RVA=" + "0x", or "RVA=-1"
    widths.sizeFieldWidth = static_cast<int>((std::max<size_t>)(5 + std::to_string(maxSize).size(), 7)); // "SIZE=" + digits, or "SIZE=-1"
    return widths;
}

// Formats one server.sym mapping-file line (everything after the shared
// column-aligned prefix through SOURCE_FILE=...), with no trailing newline.
// Used both to actually write server.sym and, unchanged, to run -fm against
// server_obfuscated.sym's symbols -- see WriteObfuscatedSymbolsToFile().
static std::string FormatMappingLine(const FunctionSymbol& symbol, const ColumnWidths& widths) {
    const char* visibility = symbol.isPublic ? "PUBLIC" : "PRIVATE";
    std::string obfuscatedName = WStringToString(symbol.obfuscatedName);
    std::string name = WStringToString(symbol.name);
    std::string signature = WStringToString(symbol.signature);
    std::string returnType = WStringToString(symbol.returnType);
    std::string callingConvention = WStringToString(symbol.callingConvention);
    std::string sizeField = "SIZE=" + std::to_string(symbol.length);

    // Format:
    // PUBLIC/PRIVATE ADDRESS SIZE=N OBFUSCATED_NAME CALLING_CONVENTION NAME(SIGNATURE) -> RETURN_TYPE
    // IS_VIRTUAL VTABLE_OFFSET VTABLE_INDEX VTABLE_CLASS VTABLE_INTRO VTABLE_SHAPE THIS_ADJUST ... SOURCE_FILE
    //
    // VISIBILITY, SIZE=N, and CALLING_CONVENTION are left-padded to a
    // per-run column width (see ComputeColumnWidths()) so short values
    // ("PUBLIC", "SIZE=8", "__cdecl") line up with long ones
    // ("PRIVATE", "SIZE=29382", "__thiscall") across the whole file.
    // ADDRESS and everything from OBFUSCATED_NAME onward are left
    // unpadded -- ADDRESS varies with the binary's own size, and
    // REAL_NAME/SIGNATURE/RETURN_TYPE/VTABLE_CLASS can run far too long
    // for column alignment to help.
    std::ostringstream line;
    line << std::left << std::setw(kVisibilityWidth) << visibility << " "
        << "0x" << std::hex << std::uppercase << symbol.rva << std::dec << " "
        << std::left << std::setw(widths.sizeFieldWidth) << sizeField << " "
        << obfuscatedName << " "
        << std::left << std::setw(widths.callingConventionWidth) << callingConvention << " "
        << name << signature << " -> " << returnType << " "
        << "IS_VIRTUAL=" << (symbol.isVirtual ? 1 : 0) << " "
        << "VTABLE_OFFSET=";
    if (symbol.vtableOffset >= 0) {
        line << "0x" << std::hex << std::uppercase << symbol.vtableOffset << std::dec;
    } else {
        line << "-1";
    }
    line << " VTABLE_INDEX=" << symbol.vtableIndex
        << " VTABLE_CLASS=" << WStringToString(symbol.className)
        << " VTABLE_INTRO=" << (symbol.isIntroducingVirtual ? 1 : 0)
        << " VTABLE_SHAPE=" << symbol.vtableShapeId
        << " VTABLE_SLOTS=" << symbol.vtableSlotCount
        << " THIS_ADJUST=" << symbol.thisAdjust
        << " KIND=" << WStringToString(symbol.symbolKind)
        << " THUNK=" << (symbol.isThunk ? 1 : 0)
        << " THUNK_ORDINAL=" << symbol.thunkOrdinal
        << " THUNK_TARGET_RVA=0x" << std::hex << std::uppercase << symbol.thunkTargetRva << std::dec
        << " SOURCE_FILE=" << WStringToString(symbol.sourceLocation);
    return line.str();
}

// Write symbols to output file
//
// Filtering: -fc tests symbol.className; -fm tests the whole formatted
// line (FormatMappingLine()), which is why -fm can match "PUBLIC"/
// "PRIVATE" or anything else on the line, not just the method name.
bool WriteSymbolsToFile(const std::string& outputPath, const std::vector<FunctionSymbol>& symbols,
                        const SymbolFilter& filter) {
    std::ofstream outFile(outputPath);
    if (!outFile.is_open()) {
        std::cerr << "Failed to open output file: " << outputPath << std::endl;
        return false;
    }

    ColumnWidths widths = ComputeColumnWidths(symbols);

    for (const auto& symbol : symbols) {
        if (!filter.ShouldKeepClass(symbol.className)) {
            continue;
        }

        std::string line = FormatMappingLine(symbol, widths);
        if (!filter.ShouldKeepLine(StringToWString(line))) {
            continue;
        }

        outFile << line << std::endl;
    }

    outFile.close();
    return true;
}

// Write obfuscated-only symbols to output file (without real names)
//
// Filtering: -fc tests symbol.className, same as WriteSymbolsToFile(). -fm
// is also evaluated against FormatMappingLine()'s real-name text (not this
// file's own sparser line) -- the obfuscated line has no method name,
// signature, or source file left to search, so testing its own text would
// make -fm effectively unusable here; testing the same real text as
// server.sym instead keeps the two files' surviving symbol sets identical
// for any given filter.
bool WriteObfuscatedSymbolsToFile(const std::string& outputPath, const std::vector<FunctionSymbol>& symbols,
                                  const SymbolFilter& filter) {
    std::ofstream outFile(outputPath);
    if (!outFile.is_open()) {
        std::cerr << "Failed to open obfuscated output file: " << outputPath << std::endl;
        return false;
    }

    ColumnWidths widths = ComputeColumnWidths(symbols);

    for (const auto& symbol : symbols) {
        if (!filter.ShouldKeepClass(symbol.className)) {
            continue;
        }
        if (!filter.ShouldKeepLine(StringToWString(FormatMappingLine(symbol, widths)))) {
            continue;
        }

        const char* visibility = symbol.isPublic ? "PUBLIC" : "PRIVATE";
        std::string obfuscatedName = WStringToString(symbol.obfuscatedName);
        std::string callingConvention = WStringToString(symbol.callingConvention);
        std::string sizeField = "SIZE=" + std::to_string(symbol.length);

        // Format: PUBLIC/PRIVATE ADDRESS SIZE=N CALLING_CONVENTION OBFUSCATED_NAME
        // IS_VIRTUAL VTABLE_OFFSET VTABLE_INDEX VTABLE_CLASS VTABLE_INTRO VTABLE_SHAPE THIS_ADJUST
        // Note: Real function name, signature, return type, and source
        // location are all omitted -- a return type (e.g. a specific class
        // pointer) or a source file path can hint at a function's purpose
        // or the project's structure just as much as its name can. Calling
        // convention is placed right before the obfuscated name -- the same
        // position it takes before the real name in the mapping file -- so
        // the obfuscated name plays the "name of the function" role here.
        // VISIBILITY, SIZE=N, and CALLING_CONVENTION are column-aligned the
        // same way as in the mapping file -- see ComputeColumnWidths().
        outFile << std::left << std::setw(kVisibilityWidth) << visibility << " "
            << "0x" << std::hex << std::uppercase << symbol.rva << std::dec << " "
            << std::left << std::setw(widths.sizeFieldWidth) << sizeField << " "
            << std::left << std::setw(widths.callingConventionWidth) << callingConvention << " "
            << obfuscatedName << " "
            << "IS_VIRTUAL=" << (symbol.isVirtual ? 1 : 0) << " "
            << "VTABLE_OFFSET=";
        if (symbol.vtableOffset >= 0) {
            outFile << "0x" << std::hex << std::uppercase << symbol.vtableOffset << std::dec;
        } else {
            outFile << "-1";
        }
        outFile << " VTABLE_INDEX=" << symbol.vtableIndex
            << " VTABLE_CLASS=" << WStringToString(symbol.className)
            << " VTABLE_INTRO=" << (symbol.isIntroducingVirtual ? 1 : 0)
            << " VTABLE_SHAPE=" << symbol.vtableShapeId
            << " VTABLE_SLOTS=" << symbol.vtableSlotCount
            << " THIS_ADJUST=" << symbol.thisAdjust
            << " KIND=" << WStringToString(symbol.symbolKind)
            << " THUNK=" << (symbol.isThunk ? 1 : 0)
            << " THUNK_ORDINAL=" << symbol.thunkOrdinal
            << " THUNK_TARGET_RVA=0x" << std::hex << std::uppercase << symbol.thunkTargetRva << std::dec
            << std::endl;
    }

    outFile.close();
    return true;
}

// Returns the bare, unqualified name from a possibly-namespace/class-
// qualified symbol name, e.g. "vgui::TreeView::SetLabelEditingAllowed" ->
// "SetLabelEditingAllowed". A free function's name has no "::" in it at
// all, so it's already its own bare name.
static std::string UnqualifiedName(const std::string& qualifiedName) {
    size_t lastSeparator = qualifiedName.rfind("::");
    return (lastSeparator == std::string::npos) ? qualifiedName : qualifiedName.substr(lastSeparator + 2);
}

// Writes a simplified report -- just "Name(Signature) -> ReturnType", one
// line per symbol, nothing else -- to `outputPath`. Feeds both
// server_simple_sort_by_class.sym and server_simple_sort_by_name.sym; the
// two differ only in sort order, selected by `sortByMethodName`:
//   false: alphabetical order of the fully-qualified name. Since a class's
//          methods all share its "Namespace::Class::" prefix, this both
//          groups and orders by class -- server_simple_sort_by_class.sym.
//   true:  alphabetical order of just the bare method name (see
//          UnqualifiedName()), ignoring whatever class/namespace it
//          belongs to -- server_simple_sort_by_name.sym.
//
// Filtering: -fc tests symbol.className, same as WriteSymbolsToFile(). -fm
// tests this report's own line text (name+signature+return type is all
// there is here, so unlike WriteObfuscatedSymbolsToFile() there's no
// sparser sibling text to worry about keeping in sync).
bool WriteSimpleReportToFile(const std::string& outputPath, const std::vector<FunctionSymbol>& symbols,
                             const SymbolFilter& filter, bool sortByMethodName) {
    struct SimpleLine {
        std::string sortKey;
        std::string text;
    };
    std::vector<SimpleLine> lines;
    lines.reserve(symbols.size());

    for (const auto& symbol : symbols) {
        if (!filter.ShouldKeepClass(symbol.className)) {
            continue;
        }

        std::string name = WStringToString(symbol.name);
        std::string text = name + WStringToString(symbol.signature) + " -> " + WStringToString(symbol.returnType);
        if (!filter.ShouldKeepLine(StringToWString(text))) {
            continue;
        }

        lines.push_back({ sortByMethodName ? UnqualifiedName(name) : name, std::move(text) });
    }

    std::stable_sort(lines.begin(), lines.end(), [](const SimpleLine& a, const SimpleLine& b) {
        return a.sortKey < b.sortKey;
    });

    std::ofstream outFile(outputPath);
    if (!outFile.is_open()) {
        std::cerr << "Failed to open output file: " << outputPath << std::endl;
        return false;
    }
    for (const auto& line : lines) {
        outFile << line.text << std::endl;
    }
    outFile.close();
    return true;
}

// Write a reconstructed VTable map. DIA does not necessarily emit a
// SymTagVTable child for every C++ class that has virtual methods. Therefore
// this writer combines concrete SymTagVTable records with synthetic table
// descriptors reconstructed from the virtual-method metadata itself.
// Synthetic tables are explicitly marked SOURCE=METHOD_METADATA and never
// pretend to have a concrete DIA VTable symbol ID or RVA.
//
// Filtering: -fc tests table.className -- a non-match skips the table's
// header AND every one of its slot lines (the whole group). -fm tests each
// SLOT= line individually, always against its real-names text (name,
// signature, return type, class, source file) regardless of
// `includeRealNames`, so server_vtable.sym and server_vtable_obfuscated.sym
// (the two calls this function gets, one per file) keep exactly the same
// slots for a given filter.
bool WriteVTablesToFile(const std::string& outputPath,
                        const std::vector<VTableSymbol>& vtables,
                        const std::vector<FunctionSymbol>& symbols,
                        bool includeRealNames,
                        DWORD targetPointerSize,
                        const SymbolFilter& filter) {
    std::ofstream outFile(outputPath);
    if (!outFile.is_open()) {
        std::cerr << "Failed to open vtable output file: " << outputPath << std::endl;
        return false;
    }

    VTableColumnWidths widths = ComputeVTableColumnWidths(vtables, symbols, targetPointerSize);

    struct MethodKey {
        DWORD classId;
        DWORD shapeId;
        LONG slot;

        bool operator==(const MethodKey& other) const {
            return classId == other.classId && shapeId == other.shapeId && slot == other.slot;
        }
    };

    struct MethodKeyHash {
        size_t operator()(const MethodKey& key) const noexcept {
            size_t h = static_cast<size_t>(key.classId) * 0x9E3779B1u;
            h ^= static_cast<size_t>(key.shapeId) + 0x85EBCA6Bu + (h << 6) + (h >> 2);
            h ^= static_cast<size_t>(static_cast<uint32_t>(key.slot)) + 0xC2B2AE35u + (h << 6) + (h >> 2);
            return h;
        }
    };

    std::unordered_map<MethodKey, std::vector<const FunctionSymbol*>, MethodKeyHash> exactMethods;
    std::unordered_map<MethodKey, std::vector<const FunctionSymbol*>, MethodKeyHash> shapeMethods;

    for (const auto& symbol : symbols) {
        if (!symbol.isVirtual || symbol.vtableIndex < 0 || symbol.vtableShapeId == 0) {
            continue;
        }

        exactMethods[{symbol.classParentId, symbol.vtableShapeId, symbol.vtableIndex}].push_back(&symbol);
        shapeMethods[{0, symbol.vtableShapeId, symbol.vtableIndex}].push_back(&symbol);
    }

    // Start with concrete DIA VTable records.
    std::vector<VTableSymbol> outputTables = vtables;
    const size_t concreteTableCount = outputTables.size();

    // Add a synthetic table for every (class, shape) pair exposed by virtual
    // methods when no matching concrete VTable with a usable shape exists.
    // This is what makes classes such as CBaseEntity visible even when DIA
    // omitted their SymTagVTable child.
    for (const auto& symbol : symbols) {
        if (!symbol.isVirtual || symbol.vtableIndex < 0 ||
            symbol.vtableShapeId == 0 || symbol.vtableSlotCount == 0 ||
            symbol.className.empty()) {
            continue;
        }

        bool alreadyPresent = false;
        for (const auto& table : outputTables) {
            if (table.classParentId == symbol.classParentId &&
                table.shapeId == symbol.vtableShapeId &&
                table.slotCount != 0) {
                alreadyPresent = true;
                break;
            }
        }

        if (!alreadyPresent) {
            VTableSymbol table{};
            table.symbolId = 0;
            table.rva = 0;
            table.classParentId = symbol.classParentId;
            table.shapeId = symbol.vtableShapeId;
            table.slotCount = symbol.vtableSlotCount;
            // The PDB's own target pointer size (4 or 8), determined once
            // from the PDB machine type during extraction and passed in
            // directly -- the same value every VTABLE_OFFSET/VTABLE_INDEX
            // conversion in this run already used. A synthetic table has no
            // concrete vtable of its own to read a pointer size from, but
            // there is nothing to guess: every slot offset in this same PDB
            // uses this one pointer size.
            table.pointerSize = targetPointerSize;
            table.className = symbol.className;
            outputTables.push_back(std::move(table));
        }
    }

    // Stable, readable ordering: class name first, then shape ID. Concrete
    // DIA tables stay before synthetic ones for otherwise identical keys.
    std::stable_sort(outputTables.begin(), outputTables.end(),
        [](const VTableSymbol& a, const VTableSymbol& b) {
            if (a.className != b.className) return a.className < b.className;
            if (a.shapeId != b.shapeId) return a.shapeId < b.shapeId;
            if ((a.symbolId != 0) != (b.symbolId != 0)) return a.symbolId != 0;
            return a.symbolId < b.symbolId;
        });

    auto chooseMethod = [](const std::vector<const FunctionSymbol*>& candidates) -> const FunctionSymbol* {
        // Prefer a thunk because it can be the actual callable entry stored
        // in a concrete vtable slot. If there is no thunk, use the function.
        for (const FunctionSymbol* candidate : candidates) {
            if (candidate->isThunk)
                return candidate;
        }
        return candidates.empty() ? nullptr : candidates.front();
    };

    size_t syntheticCount = 0;
    for (const auto& table : outputTables) {
        const bool synthetic = (table.symbolId == 0 && table.rva == 0 && table.slotCount != 0);
        if (synthetic)
            ++syntheticCount;

        // -fc: skip the whole table (header + every slot line) when its
        // class doesn't match. Stats above still count every table found,
        // regardless of this filter.
        if (!filter.ShouldKeepClass(table.className)) {
            continue;
        }

        // Identity fields first (CLASS/CLASS_ID/SHAPE/SLOTS -- what a human
        // is almost always looking for), then the source-tracking fields
        // (VTABLE_SOURCE/ID/RVA -- whether this is a concrete DIA_VTABLE or
        // a reconstructed METHOD_METADATA table, and its own low-level
        // identifiers) last. "SOURCE=" is renamed "VTABLE_SOURCE=" now that
        // it no longer immediately follows a standalone "VTABLE" line-type
        // marker at the start of the line -- the line is still identifiable
        // as a header (vs. a "SLOT=..." line) by starting with "CLASS=".
        outFile << "CLASS=";
        if (includeRealNames) outFile << WStringToString(table.className);
        else outFile << "-";

        outFile << " CLASS_ID=" << table.classParentId
            << " SHAPE=" << table.shapeId
            << " SLOTS=" << table.slotCount
            << " VTABLE_SOURCE=" << (synthetic ? "METHOD_METADATA" : "DIA_VTABLE")
            << " ID=";
        if (synthetic) outFile << "-";
        else outFile << table.symbolId;

        outFile << " RVA=";
        if (synthetic) outFile << "-";
        else outFile << "0x" << std::hex << std::uppercase << table.rva << std::dec;

        outFile << std::endl;

        for (DWORD slot = 0; slot < table.slotCount; ++slot) {
            const FunctionSymbol* method = nullptr;
            const char* matchType = "NONE";

            auto exactIt = exactMethods.find({table.classParentId, table.shapeId, static_cast<LONG>(slot)});
            if (exactIt != exactMethods.end() && !exactIt->second.empty()) {
                method = chooseMethod(exactIt->second);
                matchType = method && method->isThunk ? "CLASS_SHAPE_THUNK" : "CLASS_SHAPE";
            } else {
                auto shapeIt = shapeMethods.find({0, table.shapeId, static_cast<LONG>(slot)});
                if (shapeIt != shapeMethods.end() && !shapeIt->second.empty()) {
                    method = chooseMethod(shapeIt->second);
                    matchType = method && method->isThunk ? "SHAPE_THUNK" : "SHAPE";
                }
            }

            // SLOT/OFFSET/RVA/SIZE are column-aligned to a per-run width
            // (see ComputeVTableColumnWidths()) -- build each whole
            // "FIELD=VALUE" token first, then left-pad it as one unit so
            // shorter values (SLOT=9, RVA=-1) line up with longer ones
            // (SLOT=238, RVA=0x1034D3E0) in the field that follows.
            std::string slotField = "SLOT=" + std::to_string(slot);
            std::ostringstream offsetStream;
            offsetStream << "OFFSET=0x" << std::hex << std::uppercase
                << (static_cast<ULONGLONG>(slot) * table.pointerSize);
            std::string offsetField = offsetStream.str();

            std::ostringstream outLine;
            outLine << std::left << std::setw(widths.slotFieldWidth) << slotField << " "
                << std::left << std::setw(widths.offsetFieldWidth) << offsetField;

            // Filter-test text: always built with the real method identity
            // (name/signature/return type/class/source file), independent
            // of `includeRealNames` -- see this function's doc comment.
            std::ostringstream filterLine;
            filterLine << slotField << " " << offsetField;

            if (method) {
                std::string callingConvention = WStringToString(method->callingConvention);

                std::ostringstream rvaStream;
                rvaStream << "RVA=0x" << std::hex << std::uppercase << method->rva;
                std::string rvaField = rvaStream.str();
                std::string sizeField = "SIZE=" + std::to_string(method->length);

                outLine << " " << std::left << std::setw(widths.rvaFieldWidth) << rvaField
                    << " " << std::left << std::setw(widths.sizeFieldWidth) << sizeField;

                // Calling convention sits right before whichever field plays
                // "the name of the function" on this line: the obfuscated
                // name here (no real name in this file), or the real
                // METHOD= name below when includeRealNames is set.
                if (!includeRealNames) {
                    outLine << " " << callingConvention;
                }
                outLine << " OBFUSCATED=" << WStringToString(method->obfuscatedName)
                    << " MATCH=" << matchType
                    << " KIND=" << WStringToString(method->symbolKind)
                    << " THUNK=" << (method->isThunk ? 1 : 0)
                    << " THUNK_ORDINAL=" << method->thunkOrdinal
                    << " THUNK_TARGET_RVA=0x" << std::hex << std::uppercase << method->thunkTargetRva << std::dec
                    << " THIS_ADJUST=" << method->thisAdjust;

                if (includeRealNames) {
                    outLine << " " << callingConvention
                        << " METHOD=" << WStringToString(method->name)
                        << WStringToString(method->signature)
                        << " METHOD_RETURN_TYPE=" << WStringToString(method->returnType)
                        << " METHOD_CLASS=" << WStringToString(method->className)
                        << " SOURCE_FILE=" << WStringToString(method->sourceLocation);
                }

                filterLine << " " << rvaField << " " << sizeField << " " << callingConvention
                    << " OBFUSCATED=" << WStringToString(method->obfuscatedName)
                    << " MATCH=" << matchType
                    << " KIND=" << WStringToString(method->symbolKind)
                    << " THUNK=" << (method->isThunk ? 1 : 0)
                    << " THUNK_ORDINAL=" << method->thunkOrdinal
                    << " THUNK_TARGET_RVA=0x" << std::hex << std::uppercase << method->thunkTargetRva << std::dec
                    << " THIS_ADJUST=" << method->thisAdjust
                    << " METHOD=" << WStringToString(method->name)
                    << WStringToString(method->signature)
                    << " METHOD_RETURN_TYPE=" << WStringToString(method->returnType)
                    << " METHOD_CLASS=" << WStringToString(method->className)
                    << " SOURCE_FILE=" << WStringToString(method->sourceLocation);
            } else {
                outLine << " " << std::left << std::setw(widths.rvaFieldWidth) << "RVA=-1"
                    << " " << std::left << std::setw(widths.sizeFieldWidth) << "SIZE=-1"
                    << " OBFUSCATED=- MATCH=NONE";
                if (includeRealNames)
                    outLine << " METHOD=UNKNOWN";

                filterLine << " RVA=-1 SIZE=-1 OBFUSCATED=- MATCH=NONE METHOD=UNKNOWN";
            }

            if (filter.ShouldKeepLine(StringToWString(filterLine.str()))) {
                outFile << outLine.str() << std::endl;
            }
        }

        outFile << std::endl;
    }

    std::cout << "VTable output: " << concreteTableCount
        << " DIA records + " << syntheticCount
        << " method-metadata tables" << std::endl;

    outFile.close();
    return true;
}

// A group of virtual methods sharing one class identity. Shared between the
// class-centric VTable listing and the inheritance report, so both agree on
// exactly which classes are reported.
struct VTableClassGroup {
    DWORD classParentId = 0;
    std::wstring className;
    std::vector<const FunctionSymbol*> methods;
};

// Groups every virtual method with a valid VTABLE_INDEX and known class name
// by its owning class. Deliberately independent of SymTagVTable: a class is
// included whenever DIA gives at least one such method.
std::vector<VTableClassGroup> CollectVTableClassGroups(const std::vector<FunctionSymbol>& symbols) {
    std::vector<VTableClassGroup> groups;
    for (const auto& symbol : symbols) {
        if (!symbol.isVirtual || symbol.vtableIndex < 0 || symbol.className.empty())
            continue;

        auto it = std::find_if(groups.begin(), groups.end(), [&](const VTableClassGroup& group) {
            if (symbol.classParentId != 0 && group.classParentId != 0)
                return group.classParentId == symbol.classParentId;
            return group.className == symbol.className;
        });

        if (it == groups.end()) {
            VTableClassGroup group;
            group.classParentId = symbol.classParentId;
            group.className = symbol.className;
            group.methods.push_back(&symbol);
            groups.push_back(std::move(group));
        } else {
            it->methods.push_back(&symbol);
        }
    }

    std::sort(groups.begin(), groups.end(), [](const VTableClassGroup& a, const VTableClassGroup& b) {
        if (a.className != b.className) return a.className < b.className;
        return a.classParentId < b.classParentId;
    });

    return groups;
}

// Write a class-centric view of every virtual method. This file deliberately
// does not depend on SymTagVTable: a class is included whenever DIA gives a
// virtual method with a valid VTABLE_INDEX and class name.
//
// Filtering: -fc tests group.className -- a non-match skips the whole group
// (header + every method line). -fm tests each "[N] ..." method line
// individually.
bool WriteVTableClassesToFile(const std::string& outputPath,
                              const std::vector<FunctionSymbol>& symbols,
                              const SymbolFilter& filter) {
    std::ofstream outFile(outputPath);
    if (!outFile.is_open()) {
        std::cerr << "Failed to open VTable classes output file: " << outputPath << std::endl;
        return false;
    }

    std::vector<VTableClassGroup> groups = CollectVTableClassGroups(symbols);

    // The bracketed vtable index is column-aligned across the WHOLE file
    // (not just within one class's own group), matching the same
    // file-wide-max approach as ComputeColumnWidths()/
    // ComputeVTableColumnWidths() -- one width value in play, rather than a
    // different one per class, keeps the rule simple: "[N]" always occupies
    // the same number of characters everywhere in this file. vtableIndex is
    // bounded by the largest vtable's slot count for this PDB, so this is
    // small and safe to pad, unlike the unbounded name/signature that
    // follows it.
    LONG maxVtableIndex = 0;
    for (const auto& group : groups) {
        for (const FunctionSymbol* method : group.methods) {
            maxVtableIndex = (std::max)(maxVtableIndex, method->vtableIndex);
        }
    }
    const int indexFieldWidth = static_cast<int>(2 + std::to_string(maxVtableIndex).size()); // "[" + digits + "]"

    size_t methodCount = 0;
    for (auto& group : groups) {
        std::sort(group.methods.begin(), group.methods.end(),
            [](const FunctionSymbol* a, const FunctionSymbol* b) {
                if (a->vtableIndex != b->vtableIndex) return a->vtableIndex < b->vtableIndex;
                if (a->vtableShapeId != b->vtableShapeId) return a->vtableShapeId < b->vtableShapeId;
                if (a->isThunk != b->isThunk) return !a->isThunk; // function before thunk
                if (a->name != b->name) return a->name < b->name;
                return a->rva < b->rva;
            });

        DWORD maxSlots = 0;
        for (const FunctionSymbol* method : group.methods)
            maxSlots = (std::max)(maxSlots, method->vtableSlotCount);

        // Stats below count every method found, regardless of filtering.
        methodCount += group.methods.size();

        if (!filter.ShouldKeepClass(group.className)) {
            continue;
        }

        outFile << "CLASS " << WStringToString(group.className)
            << " CLASS_ID=" << group.classParentId;
        if (maxSlots != 0)
            outFile << " VTABLE_SLOTS=" << maxSlots;
        outFile << std::endl;

        for (const FunctionSymbol* method : group.methods) {
            std::ostringstream line;
            std::string indexField = "[" + std::to_string(method->vtableIndex) + "]";
            line << "  " << std::left << std::setw(indexFieldWidth) << indexField
                << " RVA=0x" << std::hex << std::uppercase << method->rva << std::dec
                << " " << WStringToString(method->callingConvention)
                << " " << WStringToString(method->name) << WStringToString(method->signature)
                << " -> " << WStringToString(method->returnType)
                << " SHAPE=" << method->vtableShapeId
                << " INTRO=" << (method->isIntroducingVirtual ? 1 : 0)
                << " THIS_ADJUST=" << method->thisAdjust
                << " KIND=" << WStringToString(method->symbolKind);

            if (method->isThunk) {
                line << " THUNK_ORDINAL=" << method->thunkOrdinal
                    << " THUNK_TARGET_RVA=0x" << std::hex << std::uppercase
                    << method->thunkTargetRva << std::dec;
            }

            line << " SOURCE_FILE=" << WStringToString(method->sourceLocation);

            if (filter.ShouldKeepLine(StringToWString(line.str()))) {
                outFile << line.str() << std::endl;
            }
        }

        outFile << std::endl;
    }

    std::cout << "VTable classes output: " << groups.size()
        << " classes, " << methodCount << " virtual method records" << std::endl;

    outFile.close();
    return true;
}

// Recursively writes one base class and its own ancestors as an ASCII tree,
// following exactly the SymTagBaseClass graph DIA reports for this PDB --
// never inferred from source code. `pathClassIds` guards against a class
// re-appearing as its own ancestor; this should not happen for a well-formed
// C++ hierarchy, but PDB data is not proof against anomalies.
//
// Filtering: -fm tests each tree line individually (this whole subtree is
// only reached at all when the group's top-level class already passed -fc
// -- see WriteVTableInheritanceToFile()); a dropped ancestor line does not
// stop traversal into its own children.
static void WriteInheritanceSubtree(std::ofstream& outFile,
                                     DWORD classId,
                                     const std::string& prefix,
                                     bool isLast,
                                     const std::unordered_map<DWORD, ClassHierarchyInfo>& classHierarchy,
                                     std::vector<DWORD>& pathClassIds,
                                     const SymbolFilter& filter) {
    auto it = classHierarchy.find(classId);

    std::string name = (it != classHierarchy.end() && !it->second.className.empty())
        ? WStringToString(it->second.className)
        : ("UnknownClass#" + std::to_string(classId));

    std::string line = prefix + (isLast ? "`-- " : "+-- ") + name;
    if (filter.ShouldKeepLine(StringToWString(line))) {
        outFile << line << std::endl;
    }

    if (it == classHierarchy.end() || it->second.baseClassIds.empty()) {
        return;
    }

    if (std::find(pathClassIds.begin(), pathClassIds.end(), classId) != pathClassIds.end()) {
        std::string cycleLine = prefix + (isLast ? "    " : "|   ") + "(cycle detected in PDB data, stopping)";
        if (filter.ShouldKeepLine(StringToWString(cycleLine))) {
            outFile << cycleLine << std::endl;
        }
        return;
    }
    pathClassIds.push_back(classId);

    const std::string childPrefix = prefix + (isLast ? "    " : "|   ");
    const auto& bases = it->second.baseClassIds;
    for (size_t i = 0; i < bases.size(); ++i) {
        WriteInheritanceSubtree(outFile, bases[i], childPrefix, (i + 1 == bases.size()),
                                 classHierarchy, pathClassIds, filter);
    }

    pathClassIds.pop_back();
}

// Write, for every class that appears in the class-centric VTable report
// (WriteVTableClassesToFile), its full recursive base-class hierarchy as an
// ASCII tree, down to the root class(es). Multiple inheritance produces
// multiple sibling branches at the same level; a base reached through more
// than one path (diamond inheritance) is printed once per path, since each
// occurrence is a distinct inheritance route worth seeing on its own.
//
// This is derived entirely from this PDB's own DIA SymTagBaseClass data
// (see PdbSymbolExtractor::ExtractSymbolsFromPdb); nothing here reads or
// infers anything from source code.
//
// Filtering: -fc tests group.className -- a non-match skips the class and
// its entire hierarchy tree. -fm then applies per tree line, same as
// WriteInheritanceSubtree() above.
bool WriteVTableInheritanceToFile(const std::string& outputPath,
                                   const std::vector<FunctionSymbol>& symbols,
                                   const std::unordered_map<DWORD, ClassHierarchyInfo>& classHierarchy,
                                   const SymbolFilter& filter) {
    std::ofstream outFile(outputPath);
    if (!outFile.is_open()) {
        std::cerr << "Failed to open VTable inheritance output file: " << outputPath << std::endl;
        return false;
    }

    std::vector<VTableClassGroup> groups = CollectVTableClassGroups(symbols);

    for (const auto& group : groups) {
        if (!filter.ShouldKeepClass(group.className)) {
            continue;
        }

        outFile << "CLASS " << WStringToString(group.className)
            << " CLASS_ID=" << group.classParentId << std::endl;

        auto it = classHierarchy.find(group.classParentId);
        if (it == classHierarchy.end() || it->second.baseClassIds.empty()) {
            std::string line = "  (no base classes reported by DIA for this class)";
            if (filter.ShouldKeepLine(StringToWString(line))) {
                outFile << line << std::endl;
            }
        } else {
            std::vector<DWORD> path;
            path.push_back(group.classParentId);
            const auto& bases = it->second.baseClassIds;
            for (size_t i = 0; i < bases.size(); ++i) {
                WriteInheritanceSubtree(outFile, bases[i], "  ", (i + 1 == bases.size()),
                                         classHierarchy, path, filter);
            }
        }

        outFile << std::endl;
    }

    std::cout << "VTable inheritance output: " << groups.size() << " classes" << std::endl;

    outFile.close();
    return true;
}

int wmain(int argc, wchar_t* argv[]) {
    std::wcout << L"ObfSymbolsEx v" OBFSYMBOLSEX_VERSION L" - PDB Function Symbol Extractor" << std::endl;
    std::wcout << L"=============================================" << std::endl << std::endl;

    CommandLineOptions options;
    std::wstring parseError;
    if (!CommandLineParser::Parse(argc, argv, options, parseError)) {
        std::wcerr << L"Error: " << parseError << std::endl << std::endl;
        CommandLineParser::PrintUsage();
        return 1;
    }

    std::wstring pdbPath = options.inputPath;
    std::string outputPath = WStringToString(options.outputPath);

    std::wcout << L"Input: " << pdbPath << std::endl;
    std::wcout << L"Output file: " << options.outputPath << std::endl;
    if (options.filter.IsActive()) {
        std::wcout << L"Filters: " << options.filter.Describe() << std::endl;
    }
    std::wcout << std::endl;

    // For a PE input (.exe/.dll/.ocx/.sys/... -- detected by magic), always
    // try to download its matching PDB from a symbol server first. Only
    // when that fails do we fall back to PdbSymbolExtractor's own
    // loadDataForExe(), which asks DIA to locate the PDB itself.
    if (PdbSymbolDownloader::IsPeFile(pdbPath)) {
        std::wcout << L"Looking up symbols for this PE file on a symbol server..." << std::endl;
        std::wstring downloadedPdbPath;
        std::wstring downloadError;
        if (PdbSymbolDownloader::TryDownloadPdb(pdbPath, options.symbolServer, downloadedPdbPath, downloadError)) {
            pdbPath = downloadedPdbPath;
        } else {
            std::wcout << L"Symbol server lookup failed (" << downloadError << L"); falling back to loadDataForExe." << std::endl;
        }
        std::wcout << std::endl;
    }

    // Create extractor (DIA SDK is initialized in constructor)
    PdbSymbolExtractor extractor;

    // Check if initialization was successful
    if (!extractor.IsInitialized()) {
        std::wcerr << L"Failed to initialize PDB symbol extractor" << std::endl;
        std::wcerr << L"Error: " << extractor.GetLastError() << std::endl;
        return 1;
    }

    // Extract symbols from PDB
    std::vector<FunctionSymbol> symbols;
    std::vector<VTableSymbol> vtables;
    std::unordered_map<DWORD, ClassHierarchyInfo> classHierarchy;
    DWORD targetPointerSize = 0;
    if (!extractor.ExtractSymbols(pdbPath, symbols, vtables, classHierarchy, targetPointerSize)) {
        std::wcerr << L"Failed to extract symbols from PDB file" << std::endl;
        std::wcerr << L"Error: " << extractor.GetLastError() << std::endl;
        return 1;
    }

    // Sort symbols by address
    std::sort(symbols.begin(), symbols.end(), [](const FunctionSymbol& a, const FunctionSymbol& b) {
        return a.rva < b.rva;
    });

    std::wcout << L"Writing to output files..." << std::endl;

    // Write full mapping file (with real names)
    if (!WriteSymbolsToFile(outputPath, symbols, options.filter)) {
        std::wcerr << L"Failed to write symbols to output file" << std::endl;
        return 1;
    }

    // Generate obfuscated-only filename
    std::string obfuscatedPath = outputPath;
    size_t dotPos = obfuscatedPath.rfind('.');
    if (dotPos != std::string::npos) {
        obfuscatedPath.insert(dotPos, "_obfuscated");
    } else {
        obfuscatedPath += "_obfuscated";
    }

    // Write obfuscated-only file (without real names)
    if (!WriteObfuscatedSymbolsToFile(obfuscatedPath, symbols, options.filter)) {
        std::wcerr << L"Failed to write obfuscated symbols file" << std::endl;
        return 1;
    }

    std::wcout << L"Successfully wrote mapping file to " << argv[2] << std::endl;
    std::wcout << L"Successfully wrote obfuscated file to " << std::wstring(obfuscatedPath.begin(), obfuscatedPath.end()) << std::endl;

    // Simplified "Name(Signature) -> ReturnType" reports, sorted by class
    // and by bare method name respectively.
    std::string simpleSortByClassPath = outputPath;
    size_t simpleSortByClassDotPos = simpleSortByClassPath.rfind('.');
    if (simpleSortByClassDotPos != std::string::npos) {
        simpleSortByClassPath.insert(simpleSortByClassDotPos, "_simple_sort_by_class");
    } else {
        simpleSortByClassPath += "_simple_sort_by_class";
    }

    if (!WriteSimpleReportToFile(simpleSortByClassPath, symbols, options.filter, false)) {
        std::wcerr << L"Failed to write simple sort-by-class report" << std::endl;
        return 1;
    }

    std::string simpleSortByNamePath = outputPath;
    size_t simpleSortByNameDotPos = simpleSortByNamePath.rfind('.');
    if (simpleSortByNameDotPos != std::string::npos) {
        simpleSortByNamePath.insert(simpleSortByNameDotPos, "_simple_sort_by_name");
    } else {
        simpleSortByNamePath += "_simple_sort_by_name";
    }

    if (!WriteSimpleReportToFile(simpleSortByNamePath, symbols, options.filter, true)) {
        std::wcerr << L"Failed to write simple sort-by-name report" << std::endl;
        return 1;
    }

    std::wcout << L"Successfully wrote simple sort-by-class file to " << std::wstring(simpleSortByClassPath.begin(), simpleSortByClassPath.end()) << std::endl;
    std::wcout << L"Successfully wrote simple sort-by-name file to " << std::wstring(simpleSortByNamePath.begin(), simpleSortByNamePath.end()) << std::endl;

    // Generate a reconstructed VTable filename.
    std::string vtablePath = outputPath;
    size_t vtableDotPos = vtablePath.rfind('.');
    if (vtableDotPos != std::string::npos) {
        vtablePath.insert(vtableDotPos, "_vtable");
    } else {
        vtablePath += "_vtable";
    }

    if (!WriteVTablesToFile(vtablePath, vtables, symbols, true, targetPointerSize, options.filter)) {
        std::wcerr << L"Failed to write VTable mapping file" << std::endl;
        return 1;
    }

    // Also emit an obfuscated-only VTable map.
    std::string vtableObfuscatedPath = outputPath;
    size_t vtableObfDotPos = vtableObfuscatedPath.rfind('.');
    if (vtableObfDotPos != std::string::npos) {
        vtableObfuscatedPath.insert(vtableObfDotPos, "_vtable_obfuscated");
    } else {
        vtableObfuscatedPath += "_vtable_obfuscated";
    }

    if (!WriteVTablesToFile(vtableObfuscatedPath, vtables, symbols, false, targetPointerSize, options.filter)) {
        std::wcerr << L"Failed to write obfuscated VTable mapping file" << std::endl;
        return 1;
    }

    // Class-centric virtual-method hierarchy, independent of SymTagVTable.
    std::string vtableClassesPath = outputPath;
    size_t vtableClassesDotPos = vtableClassesPath.rfind('.');
    if (vtableClassesDotPos != std::string::npos) {
        vtableClassesPath.insert(vtableClassesDotPos, "_vtable_classes");
    } else {
        vtableClassesPath += "_vtable_classes";
    }

    if (!WriteVTableClassesToFile(vtableClassesPath, symbols, options.filter)) {
        std::wcerr << L"Failed to write VTable classes file" << std::endl;
        return 1;
    }

    // Recursive base-class hierarchy (an ASCII tree per class), for every
    // class that appears in the VTable classes file above.
    std::string vtableInheritancePath = outputPath;
    size_t vtableInheritanceDotPos = vtableInheritancePath.rfind('.');
    if (vtableInheritanceDotPos != std::string::npos) {
        vtableInheritancePath.insert(vtableInheritanceDotPos, "_vtable_inheritance");
    } else {
        vtableInheritancePath += "_vtable_inheritance";
    }

    if (!WriteVTableInheritanceToFile(vtableInheritancePath, symbols, classHierarchy, options.filter)) {
        std::wcerr << L"Failed to write VTable inheritance file" << std::endl;
        return 1;
    }

    std::wcout << L"Successfully wrote VTable file to " << std::wstring(vtablePath.begin(), vtablePath.end()) << std::endl;
    std::wcout << L"Successfully wrote obfuscated VTable file to " << std::wstring(vtableObfuscatedPath.begin(), vtableObfuscatedPath.end()) << std::endl;
    std::wcout << L"Successfully wrote VTable classes file to " << std::wstring(vtableClassesPath.begin(), vtableClassesPath.end()) << std::endl;
    std::wcout << L"Successfully wrote VTable inheritance file to " << std::wstring(vtableInheritancePath.begin(), vtableInheritancePath.end()) << std::endl;
    return 0;
}
