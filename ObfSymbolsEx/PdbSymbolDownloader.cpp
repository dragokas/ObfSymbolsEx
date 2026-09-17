// PdbSymbolDownloader.cpp - Locates and downloads a PE file's matching PDB
// from a symbol server.
#include "PdbSymbolDownloader.h"

#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace {

#pragma pack(push, 1)
struct CvInfoPdb70 {
    DWORD CvSignature; // 'RSDS' == 0x53445352
    GUID Signature;
    DWORD Age;
    // Followed by a null-terminated PDB path/name (variable length, not
    // part of this fixed struct).
};
#pragma pack(pop)

constexpr DWORD kRsdsSignature = 0x53445352; // 'RSDS'

// Reads just enough of the PE file at `path` to find its CodeView (RSDS)
// debug directory entry, and fills `pdbFileName` (basename only, as
// embedded in the PE -- build machines often embed a full path) and
// `identifier`: the 32 hex GUID digits plus hex age, concatenated exactly
// as a symbol server expects them in its URL, e.g.
// "B534385F5F7DE0246E566FFC095804991".
bool ReadCodeViewInfo(const std::wstring& path, std::wstring& pdbFileName, std::wstring& identifier) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;

    IMAGE_DOS_HEADER dosHeader{};
    file.read(reinterpret_cast<char*>(&dosHeader), sizeof(dosHeader));
    if (!file || dosHeader.e_magic != IMAGE_DOS_SIGNATURE) return false;

    file.seekg(dosHeader.e_lfanew, std::ios::beg);
    DWORD peSignature = 0;
    file.read(reinterpret_cast<char*>(&peSignature), sizeof(peSignature));
    if (!file || peSignature != IMAGE_NT_SIGNATURE) return false;

    IMAGE_FILE_HEADER fileHeader{};
    file.read(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader));
    if (!file || fileHeader.SizeOfOptionalHeader == 0) return false;

    std::streampos optionalHeaderStart = file.tellg();

    WORD magic = 0;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (!file) return false;

    bool isPe32Plus = (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC);
    if (!isPe32Plus && magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return false;

    // The 32-bit and 64-bit optional headers only differ in the fields
    // before DataDirectory; compute the DEBUG entry's offset for whichever
    // one this file actually has instead of reading a whole
    // IMAGE_OPTIONAL_HEADER32/64.
    size_t debugDataDirOffset = (isPe32Plus
        ? offsetof(IMAGE_OPTIONAL_HEADER64, DataDirectory)
        : offsetof(IMAGE_OPTIONAL_HEADER32, DataDirectory))
        + sizeof(IMAGE_DATA_DIRECTORY) * IMAGE_DIRECTORY_ENTRY_DEBUG;

    file.seekg(optionalHeaderStart + static_cast<std::streamoff>(debugDataDirOffset), std::ios::beg);
    IMAGE_DATA_DIRECTORY debugDataDir{};
    file.read(reinterpret_cast<char*>(&debugDataDir), sizeof(debugDataDir));
    if (!file || debugDataDir.Size == 0) return false;

    // The section table immediately follows the optional header.
    file.seekg(optionalHeaderStart + static_cast<std::streamoff>(fileHeader.SizeOfOptionalHeader), std::ios::beg);
    std::vector<IMAGE_SECTION_HEADER> sections(fileHeader.NumberOfSections);
    for (auto& section : sections) {
        file.read(reinterpret_cast<char*>(&section), sizeof(section));
        if (!file) return false;
    }

    // Resolve the debug directory ARRAY's own RVA to a file offset via the
    // section table. Each IMAGE_DEBUG_DIRECTORY entry inside that array
    // separately carries its own PointerToRawData (already a file offset),
    // so this is the only RVA->file-offset translation needed here.
    DWORD debugDirRva = debugDataDir.VirtualAddress;
    LONGLONG debugDirFileOffset = -1;
    for (const auto& section : sections) {
        DWORD sectionSize = (std::max)(section.Misc.VirtualSize, section.SizeOfRawData);
        if (debugDirRva >= section.VirtualAddress && debugDirRva < section.VirtualAddress + sectionSize) {
            debugDirFileOffset = static_cast<LONGLONG>(section.PointerToRawData) + (debugDirRva - section.VirtualAddress);
            break;
        }
    }
    if (debugDirFileOffset < 0) return false;

    DWORD entryCount = debugDataDir.Size / sizeof(IMAGE_DEBUG_DIRECTORY);
    file.seekg(static_cast<std::streampos>(debugDirFileOffset), std::ios::beg);
    for (DWORD i = 0; i < entryCount; ++i) {
        IMAGE_DEBUG_DIRECTORY entry{};
        file.read(reinterpret_cast<char*>(&entry), sizeof(entry));
        if (!file) return false;

        if (entry.Type != IMAGE_DEBUG_TYPE_CODEVIEW || entry.SizeOfData <= sizeof(CvInfoPdb70)) {
            continue;
        }

        std::vector<char> raw(entry.SizeOfData);
        std::streampos savedPos = file.tellg();
        file.seekg(entry.PointerToRawData, std::ios::beg);
        file.read(raw.data(), static_cast<std::streamsize>(raw.size()));
        bool readOk = static_cast<bool>(file);
        file.clear();
        file.seekg(savedPos, std::ios::beg);
        if (!readOk) continue;

        CvInfoPdb70 cvInfo{};
        std::memcpy(&cvInfo, raw.data(), sizeof(cvInfo));
        if (cvInfo.CvSignature != kRsdsSignature) continue;

        const char* nameStart = raw.data() + sizeof(CvInfoPdb70);
        size_t nameLen = strnlen(nameStart, raw.size() - sizeof(CvInfoPdb70));
        std::string pdbPathNarrow(nameStart, nameLen);

        size_t lastSlash = pdbPathNarrow.find_last_of("\\/");
        std::string pdbFileNameNarrow = (lastSlash == std::string::npos) ? pdbPathNarrow : pdbPathNarrow.substr(lastSlash + 1);
        if (pdbFileNameNarrow.empty()) continue;

        int wideLen = MultiByteToWideChar(CP_UTF8, 0, pdbFileNameNarrow.c_str(), -1, NULL, 0);
        if (wideLen <= 0) continue;
        std::wstring wide(static_cast<size_t>(wideLen - 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, pdbFileNameNarrow.c_str(), -1, &wide[0], wideLen);
        pdbFileName = wide;

        wchar_t idBuffer[64];
        swprintf_s(idBuffer, L"%08X%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X%X",
                   cvInfo.Signature.Data1, cvInfo.Signature.Data2, cvInfo.Signature.Data3,
                   cvInfo.Signature.Data4[0], cvInfo.Signature.Data4[1], cvInfo.Signature.Data4[2],
                   cvInfo.Signature.Data4[3], cvInfo.Signature.Data4[4], cvInfo.Signature.Data4[5],
                   cvInfo.Signature.Data4[6], cvInfo.Signature.Data4[7], cvInfo.Age);
        identifier = idBuffer;
        return true;
    }

    return false;
}

// The 32-byte magic every MSF-format PDB file starts with.
constexpr char kMsfMagic[32] = {
    'M','i','c','r','o','s','o','f','t',' ','C','/','C','+','+',' ',
    'M','S','F',' ','7','.','0','0','\r','\n','\x1a','D','S','\0','\0','\0'
};

#pragma pack(push, 1)
struct PdbInfoStreamHeader {
    DWORD Version;
    DWORD Signature;
    DWORD Age;
    GUID Guid;
};
#pragma pack(pop)

// Reads the PDB Info Stream (MSF stream #1) of the PDB at `pdbPath` and
// fills `identifier` with the same GUID+age string ReadCodeViewInfo()
// builds from a PE's CodeView entry, so the two can be string-compared to
// confirm a candidate PDB actually matches a given binary. Parses just
// enough of the MSF container (superblock, stream directory, stream #1's
// first block) to get there; returns false on anything unexpected --
// including a directory block list spanning more than one block, which
// essentially never happens for a real-world PDB -- so callers always fail
// safe into re-downloading rather than risk a wrong match.
bool ReadPdbIdentifier(const std::wstring& pdbPath, std::wstring& identifier) {
    std::ifstream file(pdbPath, std::ios::binary);
    if (!file) return false;

    char magic[sizeof(kMsfMagic)];
    file.read(magic, sizeof(magic));
    if (!file || std::memcmp(magic, kMsfMagic, sizeof(kMsfMagic)) != 0) return false;

    DWORD blockSize = 0, freeBlockMapBlock = 0, numBlocks = 0, numDirectoryBytes = 0, unknown = 0, blockMapAddr = 0;
    file.read(reinterpret_cast<char*>(&blockSize), sizeof(blockSize));
    file.read(reinterpret_cast<char*>(&freeBlockMapBlock), sizeof(freeBlockMapBlock));
    file.read(reinterpret_cast<char*>(&numBlocks), sizeof(numBlocks));
    file.read(reinterpret_cast<char*>(&numDirectoryBytes), sizeof(numDirectoryBytes));
    file.read(reinterpret_cast<char*>(&unknown), sizeof(unknown));
    file.read(reinterpret_cast<char*>(&blockMapAddr), sizeof(blockMapAddr));
    if (!file || blockSize == 0) return false;

    DWORD numDirBlocks = (numDirectoryBytes + blockSize - 1) / blockSize;
    if (numDirBlocks == 0 || static_cast<unsigned long long>(numDirBlocks) * 4 > blockSize) {
        // The list of directory blocks itself doesn't fit in one block --
        // an extremely large PDB we don't bother handling here.
        return false;
    }

    file.seekg(static_cast<std::streamoff>(blockMapAddr) * blockSize, std::ios::beg);
    std::vector<DWORD> dirBlocks(numDirBlocks);
    file.read(reinterpret_cast<char*>(dirBlocks.data()), static_cast<std::streamsize>(numDirBlocks) * 4);
    if (!file) return false;

    std::vector<char> dirBytes(numDirectoryBytes);
    size_t bytesRead = 0;
    for (DWORD blockIndex : dirBlocks) {
        size_t toRead = (std::min)(static_cast<size_t>(blockSize), dirBytes.size() - bytesRead);
        file.seekg(static_cast<std::streamoff>(blockIndex) * blockSize, std::ios::beg);
        file.read(dirBytes.data() + bytesRead, static_cast<std::streamsize>(toRead));
        if (!file) return false;
        bytesRead += toRead;
    }

    if (dirBytes.size() < 4) return false;
    DWORD numStreams = 0;
    std::memcpy(&numStreams, dirBytes.data(), 4);
    if (numStreams < 2) return false; // need at least stream 0 (Old Directory) and 1 (PDB Info)

    size_t offset = 4;
    if (offset + static_cast<size_t>(numStreams) * 4 > dirBytes.size()) return false;
    std::vector<DWORD> streamSizes(numStreams);
    std::memcpy(streamSizes.data(), dirBytes.data() + offset, static_cast<size_t>(numStreams) * 4);
    offset += static_cast<size_t>(numStreams) * 4;

    // Stream block lists follow, one variable-length array per stream, in
    // stream order -- so streams before #1 must still be walked (skipped)
    // to reach its list.
    DWORD pdbInfoStreamFirstBlock = 0;
    bool foundStream1 = false;
    for (DWORD i = 0; i < numStreams; ++i) {
        DWORD size = streamSizes[i];
        DWORD count = (size == 0xFFFFFFFFu || size == 0) ? 0 : (size + blockSize - 1) / blockSize;
        if (offset + static_cast<size_t>(count) * 4 > dirBytes.size()) return false;

        if (i == 1 && count > 0) {
            std::memcpy(&pdbInfoStreamFirstBlock, dirBytes.data() + offset, 4);
            foundStream1 = true;
        }
        offset += static_cast<size_t>(count) * 4;
    }
    if (!foundStream1) return false;

    PdbInfoStreamHeader header{};
    file.seekg(static_cast<std::streamoff>(pdbInfoStreamFirstBlock) * blockSize, std::ios::beg);
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file) return false;

    wchar_t idBuffer[64];
    swprintf_s(idBuffer, L"%08X%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X%X",
               header.Guid.Data1, header.Guid.Data2, header.Guid.Data3,
               header.Guid.Data4[0], header.Guid.Data4[1], header.Guid.Data4[2],
               header.Guid.Data4[3], header.Guid.Data4[4], header.Guid.Data4[5],
               header.Guid.Data4[6], header.Guid.Data4[7], header.Age);
    identifier = idBuffer;
    return true;
}

// True if `candidateIdentifier` (from a local PDB's own MSF header) is a
// valid match for `expectedIdentifier` (built from a PE's CodeView entry).
// Both are "32 hex GUID digits + hex age" strings (see ReadCodeViewInfo),
// but only the GUID -- always exactly 32 characters -- is compared. The
// GUID alone already uniquely identifies the exact build the PDB was
// produced for; Age is just a rewrite counter for the PDB's own MSF
// container (bumped by incremental linking, symbol-server repackaging,
// ...), not a timestamp or freshness indicator, and it commonly differs
// from the Age embedded in the binary without the underlying debug content
// having changed at all -- e.g. explorer.pdb from the public Microsoft
// symbol server has Age=2 in its own header against Age=1 in explorer.exe's
// CodeView record, yet is exactly the right (and only) PDB for that binary.
bool IdentifierMatches(const std::wstring& candidateIdentifier, const std::wstring& expectedIdentifier) {
    if (candidateIdentifier.size() < 32 || expectedIdentifier.size() < 32) return false;
    return _wcsnicmp(candidateIdentifier.c_str(), expectedIdentifier.c_str(), 32) == 0;
}

// Strips trailing slashes so servers can be joined with a single "/".
std::wstring TrimTrailingSlashes(const std::wstring& value) {
    std::wstring trimmed = value;
    while (!trimmed.empty() && (trimmed.back() == L'\\' || trimmed.back() == L'/')) {
        trimmed.pop_back();
    }
    return trimmed;
}

// Builds the ordered, de-duplicated list of symbol servers to try: the
// -srv= override (if any), then every srv*/symsrv* entry found in
// _NT_SYMBOL_PATH, then the default public Microsoft symbol server. See
// https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/symbol-path
// for the full _NT_SYMBOL_PATH syntax; only the download-URL field of each
// srv*/symsrv* segment is used here -- local cache-only segments
// ("cache*...", or a bare local directory) are search locations, not
// download sources, and are intentionally not handled.
std::vector<std::wstring> CollectCandidateServers(const std::wstring& symbolServerOverride) {
    std::vector<std::wstring> servers;

    if (!symbolServerOverride.empty()) {
        servers.push_back(symbolServerOverride);
    }

    std::vector<wchar_t> envBuffer(4096);
    DWORD envLen = GetEnvironmentVariableW(L"_NT_SYMBOL_PATH", envBuffer.data(), static_cast<DWORD>(envBuffer.size()));
    if (envLen > 0 && envLen < envBuffer.size()) {
        std::wstring symPath(envBuffer.data(), envLen);
        std::wstringstream stream(symPath);
        std::wstring segment;
        while (std::getline(stream, segment, L';')) {
            if (segment.empty()) continue;

            std::wstring lowerSegment = segment;
            for (wchar_t& c : lowerSegment) c = static_cast<wchar_t>(towlower(c));

            bool isSrv = lowerSegment.compare(0, 4, L"srv*") == 0;
            bool isSymSrv = lowerSegment.compare(0, 7, L"symsrv*") == 0;
            if (!isSrv && !isSymSrv) continue;

            size_t lastStar = segment.find_last_of(L'*');
            if (lastStar == std::wstring::npos) continue;
            std::wstring url = segment.substr(lastStar + 1);
            if (!url.empty()) servers.push_back(url);
        }
    }

    servers.push_back(L"https://msdl.microsoft.com/download/symbols");

    std::vector<std::wstring> unique;
    for (const auto& server : servers) {
        std::wstring trimmed = TrimTrailingSlashes(server);
        if (!trimmed.empty() && std::find(unique.begin(), unique.end(), trimmed) == unique.end()) {
            unique.push_back(trimmed);
        }
    }
    return unique;
}

// Downloads `url` via WinHTTP into `localPath`. Returns false (with
// `errorMessage` filled) on any parse/connect/HTTP-status/IO failure, and
// removes whatever partial file it may have started writing.
bool DownloadUrlToFile(const std::wstring& url, const std::wstring& localPath, std::wstring& errorMessage) {
    URL_COMPONENTS urlComponents{};
    urlComponents.dwStructSize = sizeof(urlComponents);
    wchar_t hostName[256]{};
    std::vector<wchar_t> urlPath(2048);
    urlComponents.lpszHostName = hostName;
    urlComponents.dwHostNameLength = static_cast<DWORD>(std::size(hostName));
    urlComponents.lpszUrlPath = urlPath.data();
    urlComponents.dwUrlPathLength = static_cast<DWORD>(urlPath.size());

    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &urlComponents)) {
        errorMessage = L"Failed to parse symbol server URL: " + url;
        return false;
    }

    bool useHttps = (urlComponents.nScheme == INTERNET_SCHEME_HTTPS);

    HINTERNET hSession = WinHttpOpen(L"ObfSymbolsEx/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        errorMessage = L"WinHttpOpen failed";
        return false;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, urlComponents.lpszHostName, urlComponents.nPort, 0);
    if (!hConnect) {
        errorMessage = L"WinHttpConnect failed for " + std::wstring(urlComponents.lpszHostName);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD requestFlags = useHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlComponents.lpszUrlPath, NULL,
                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, requestFlags);
    if (!hRequest) {
        errorMessage = L"WinHttpOpenRequest failed";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    BOOL sent = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    BOOL received = sent && WinHttpReceiveResponse(hRequest, NULL);

    bool ok = false;
    if (received) {
        DWORD statusCode = 0;
        DWORD statusCodeSize = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_FLAG_NUMBER | WINHTTP_QUERY_STATUS_CODE,
                             WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);

        if (statusCode == 200) {
            std::ofstream outFile(localPath, std::ios::binary);
            if (!outFile) {
                errorMessage = L"Failed to create local file: " + localPath;
            } else {
                std::vector<char> buffer(65536);
                DWORD bytesAvailable = 0;
                bool readError = false;
                while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
                    DWORD toRead = (std::min)(bytesAvailable, static_cast<DWORD>(buffer.size()));
                    DWORD bytesRead = 0;
                    if (!WinHttpReadData(hRequest, buffer.data(), toRead, &bytesRead) || bytesRead == 0) {
                        readError = true;
                        break;
                    }
                    outFile.write(buffer.data(), bytesRead);
                }
                outFile.close();
                ok = !readError;
                if (!ok) errorMessage = L"Download interrupted for " + url;
            }
        } else {
            errorMessage = L"HTTP " + std::to_wstring(statusCode) + L" for " + url;
        }
    } else {
        errorMessage = L"Request failed for " + url;
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (!ok) {
        DeleteFileW(localPath.c_str());
    }
    return ok;
}

} // namespace

bool PdbSymbolDownloader::IsPeFile(const std::wstring& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;

    IMAGE_DOS_HEADER dosHeader{};
    file.read(reinterpret_cast<char*>(&dosHeader), sizeof(dosHeader));
    if (!file || dosHeader.e_magic != IMAGE_DOS_SIGNATURE) return false;

    file.seekg(dosHeader.e_lfanew, std::ios::beg);
    DWORD peSignature = 0;
    file.read(reinterpret_cast<char*>(&peSignature), sizeof(peSignature));
    return static_cast<bool>(file) && peSignature == IMAGE_NT_SIGNATURE;
}

bool PdbSymbolDownloader::TryDownloadPdb(const std::wstring& peFilePath,
                                          const std::wstring& symbolServerOverride,
                                          std::wstring& outPdbPath,
                                          std::wstring& errorMessage) {
    std::wstring pdbFileName;
    std::wstring identifier;
    if (!ReadCodeViewInfo(peFilePath, pdbFileName, identifier)) {
        errorMessage = L"No CodeView (RSDS) debug directory found in " + peFilePath;
        return false;
    }

    wchar_t exePathBuffer[MAX_PATH]{};
    GetModuleFileNameW(NULL, exePathBuffer, static_cast<DWORD>(std::size(exePathBuffer)));
    std::wstring exeDir(exePathBuffer);
    size_t lastSlash = exeDir.find_last_of(L"\\/");
    exeDir = (lastSlash == std::wstring::npos) ? L"." : exeDir.substr(0, lastSlash);

    size_t inputLastSlash = peFilePath.find_last_of(L"\\/");
    std::wstring inputDir = (inputLastSlash == std::wstring::npos) ? L"." : peFilePath.substr(0, inputLastSlash);

    std::wstring downloadTargetPath = exeDir + L"\\" + pdbFileName;

    // Reuse a PDB that's already sitting next to ObfSymbolsEx.exe or next
    // to the input file, but only after confirming its own GUID actually
    // matches this PE -- a same-named PDB left over from a different build
    // must never be silently substituted.
    std::vector<std::wstring> candidateDirs = { exeDir };
    if (_wcsicmp(inputDir.c_str(), exeDir.c_str()) != 0) {
        candidateDirs.push_back(inputDir);
    }
    for (const std::wstring& dir : candidateDirs) {
        std::wstring candidatePath = dir + L"\\" + pdbFileName;
        DWORD attrs = GetFileAttributesW(candidatePath.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) continue;

        std::wstring candidateIdentifier;
        if (ReadPdbIdentifier(candidatePath, candidateIdentifier) && IdentifierMatches(candidateIdentifier, identifier)) {
            std::wcout << L"  Found matching PDB already present: " << candidatePath << std::endl;
            outPdbPath = candidatePath;
            return true;
        }
        std::wcout << L"  " << candidatePath << L" exists but doesn't match this binary; ignoring it." << std::endl;
    }

    std::vector<std::wstring> servers = CollectCandidateServers(symbolServerOverride);
    std::wstring lastError;
    for (const auto& server : servers) {
        std::wstring url = server + L"/" + pdbFileName + L"/" + identifier + L"/" + pdbFileName;
        std::wcout << L"  Trying " << url << L" ..." << std::endl;
        if (DownloadUrlToFile(url, downloadTargetPath, lastError)) {
            std::wcout << L"  Downloaded to " << downloadTargetPath << std::endl;
            outPdbPath = downloadTargetPath;
            return true;
        }
        std::wcout << L"  Failed (" << lastError << L")" << std::endl;
    }

    errorMessage = L"No symbol server had " + pdbFileName + L" (" + identifier + L"): " + lastError;
    return false;
}
