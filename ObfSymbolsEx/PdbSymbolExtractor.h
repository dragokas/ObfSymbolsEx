// PdbSymbolExtractor.h - Header for PDB symbol extraction class
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <windows.h>

// Forward declarations to avoid including heavy COM headers in header file
struct IDiaDataSource;
struct IDiaSession;

// Structure to hold function symbol information
struct FunctionSymbol {
    std::wstring name;
    std::wstring obfuscatedName;  // Obfuscated name (e.g., sub_12345)
    std::wstring signature;       // Function signature (parameters only, no return type)
    std::wstring returnType;      // Function return type name, "?" if unresolved/unavailable
    std::wstring callingConvention; // e.g. "__thiscall", "__cdecl"; "?" if unavailable
    std::wstring sourceLocation;  // "File.cpp:123" from the first line record at this RVA, "?" if unavailable
    DWORD rva;                    // Relative Virtual Address
    ULONGLONG length;
    bool isPublic;
    bool isVirtual;               // True if DIA reports this as a virtual function
    bool isThunk;                 // True when this symbol is a DIA SymTagThunk
    DWORD thunkOrdinal;           // DIA THUNK_ORDINAL value, 0 if unavailable
    DWORD thunkTargetRva;         // Target RVA for a thunk, 0 if unavailable
    std::wstring symbolKind;      // FUNCTION / THUNK / DESTRUCTOR_* classification
    bool isIntroducingVirtual;     // True if this method introduces a virtual slot (DIA get_intro)
    LONG vtableOffset;             // Byte offset in the vtable, -1 if unavailable
    LONG vtableIndex;              // vtableOffset / target pointer size, -1 if unavailable
    DWORD vtableShapeId;           // DIA virtual table shape ID, 0 if unavailable
    DWORD vtableSlotCount;         // Number of entries in the corresponding VTableShape
    LONG thisAdjust;               // DIA this-adjustment, 0 if unavailable
    std::wstring className;        // Enclosing C++ class when DIA provides it
    DWORD classParentId;           // DIA symbol ID of the enclosing class, 0 if unavailable
};

// One concrete VTable symbol emitted by DIA. A class can have multiple VTables
// (for example with multiple inheritance), so VTableShape alone is not enough
// to identify a concrete table.
struct VTableSymbol {
    DWORD symbolId;                 // DIA SymIndexId of the VTable symbol
    DWORD rva;                      // RVA of the VTable pointer/data when DIA provides it
    DWORD classParentId;            // DIA class-parent symbol ID
    DWORD shapeId;                  // DIA VTableShape symbol ID
    DWORD slotCount;                // Number of entries in the VTableShape
    DWORD pointerSize;              // Target pointer size from the PDB (4 or 8)
    std::wstring className;         // Owning class name
};

// One class (SymTagUDT) node in the inheritance graph, as reported directly
// by DIA -- never from source code. Keyed externally by DIA symIndexId.
struct ClassHierarchyInfo {
    std::wstring className;         // Class name reported by DIA
    std::vector<DWORD> baseClassIds; // Direct bases (SymTagBaseClass), DIA declaration order
};

// Class responsible for extracting symbols from PDB files
// Note: DIA COM object is initialized in constructor
class PdbSymbolExtractor {
public:
    PdbSymbolExtractor();
    ~PdbSymbolExtractor();

    // Check if the extractor was successfully initialized
    bool IsInitialized() const { return _initialized; }

    // Main extraction method
    // Returns true on success, false on failure
    // `pdbPath` may be a .pdb path (loaded directly) or a .exe/.dll path
    // (DIA locates and loads the matching PDB itself -- same directory as
    // the binary, embedded GUID/age against a symbol server or local cache,
    // ...), decided purely from the extension.
    // `targetPointerSize` is the PDB's own target pointer size (4 or 8),
    // taken from its machine type -- not sizeof(void*) of this process --
    // so callers writing derived vtable data (e.g. reconstructed/synthetic
    // tables) never need to guess it themselves.
    bool ExtractSymbols(const std::wstring& pdbPath, std::vector<FunctionSymbol>& symbols,
                        std::vector<VTableSymbol>& vtables,
                        std::unordered_map<DWORD, ClassHierarchyInfo>& classHierarchy,
                        DWORD& targetPointerSize);

    // Get the last error message
    std::wstring GetLastError() const { return _lastError; }

private:
    bool _initialized;                      // True if DIA SDK was loaded successfully
    std::wstring _lastError;                // Last error message
    IDiaDataSource* _pDiaDataSource;        // DIA data source (initialized in constructor)

    // Helper methods
    std::wstring FindMsdiaDll();
    HRESULT NoRegCoCreate(const std::wstring& dllPath, REFCLSID rclsid, REFIID riid, void** ppv);
    HRESULT ExtractSymbolsFromPdb(const std::wstring& pdbPath,
                                   std::vector<FunctionSymbol>& symbols,
                                   std::vector<VTableSymbol>& vtables,
                                   std::unordered_map<DWORD, ClassHierarchyInfo>& classHierarchy,
                                   DWORD& targetPointerSize);
    
    // Symbol processing helpers
    std::wstring ExtractFunctionSignature(struct IDiaSymbol* pSymbol);
    std::wstring ExtractReturnType(struct IDiaSymbol* pSymbol);
    std::wstring ExtractCallingConvention(struct IDiaSymbol* pSymbol);
    std::wstring ExtractSourceLocation(struct IDiaSession* pSession, DWORD rva, DWORD length);
    std::wstring GetTypeName(struct IDiaSymbol* pType);
    std::wstring BuildFunctionPointerTypeName(struct IDiaSymbol* pPointerType, struct IDiaSymbol* pFunctionType);
    std::wstring GenerateObfuscatedName(const std::wstring& realName, DWORD rva);
};

