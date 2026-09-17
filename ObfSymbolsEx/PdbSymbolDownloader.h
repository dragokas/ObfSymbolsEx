// PdbSymbolDownloader.h - Locates and downloads a PE file's matching PDB
// from a symbol server, following the standard Microsoft symbol-server URL
// convention:
// https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/symbol-path
#pragma once

#include <string>

// This is always tried BEFORE PdbSymbolExtractor's own loadDataForExe()
// fallback: given a .exe/.dll/.ocx/.sys/... file, read its embedded
// CodeView debug directory (PDB filename + GUID/age), and download the
// matching PDB from a symbol server into the directory ObfSymbolsEx.exe
// itself runs from. Only if this fails (no network, PDB not published, no
// debug directory at all, ...) should the caller fall back to letting DIA
// locate the PDB on its own via loadDataForExe().
class PdbSymbolDownloader {
public:
    // True if `path` begins with a valid MZ/PE header, checked by magic
    // (DOS header + PE signature) rather than by file extension -- so
    // .exe, .dll, .ocx, .sys, .cpl, ... all qualify equally.
    static bool IsPeFile(const std::wstring& path);

    // Attempts to find and download the PDB matching the PE file at
    // `peFilePath`. `symbolServerOverride` is the value of an optional
    // -srv=... command line switch, empty if not given.
    //
    // Candidate symbol servers are tried in this order until one has the
    // file:
    //   1. `symbolServerOverride`, if non-empty
    //   2. every srv*/symsrv* entry in the _NT_SYMBOL_PATH environment
    //      variable, if set (see the symbol-path format linked above)
    //   3. the default public Microsoft symbol server
    //      (https://msdl.microsoft.com/download/symbols)
    //
    // Before contacting any server, this looks for a same-named PDB next to
    // the running ObfSymbolsEx.exe and next to `peFilePath` itself, and
    // reuses the first one whose own embedded GUID actually matches this PE
    // (parsed directly from the PDB's MSF header) -- a same-named PDB left
    // over from a different build is never silently substituted. (Age is
    // deliberately not part of this check: it's a rewrite counter for the
    // PDB's own container, not a timestamp, and commonly differs from the
    // binary's embedded Age for an otherwise-correct PDB.) On a fresh
    // download, the PDB is saved next to ObfSymbolsEx.exe.
    //
    // Every server URL this tries (and whether it succeeded, failed, or
    // was skipped because a matching PDB was already present) is printed
    // to stdout as it happens.
    //
    // On success, fills `outPdbPath` with the local PDB's full path and
    // returns true. On any failure (no CodeView debug directory, no server
    // has the file, network error, ...), fills `errorMessage` with a
    // human-readable reason and returns false -- callers should then fall
    // back to loadDataForExe.
    static bool TryDownloadPdb(const std::wstring& peFilePath,
                                const std::wstring& symbolServerOverride,
                                std::wstring& outPdbPath,
                                std::wstring& errorMessage);
};
