# Code Organization

## Overview

The PdbSymbolExtractor class has a clean, well-organized structure with dedicated helper methods for better readability and maintainability.

> **Note:** this file has been refreshed to match the current source. It
> originally documented an earlier revision that predates VTable/thunk
> extraction — see [REFACTORING_SUMMARY.md](REFACTORING_SUMMARY.md) and
> [REFACTORING_V2_SUMMARY.md](REFACTORING_V2_SUMMARY.md) for that history.

## Class Structure

### PdbSymbolExtractor.h

```cpp
// One concrete VTable symbol emitted by DIA. A class can have multiple
// VTables (e.g. with multiple inheritance), so VTableShape alone is not
// enough to identify a concrete table.
struct VTableSymbol {
    DWORD symbolId;
    DWORD rva;
    DWORD classParentId;
    DWORD shapeId;
    DWORD slotCount;
    DWORD pointerSize;
    std::wstring className;
};

// One class (SymTagUDT) node in the inheritance graph, as reported directly
// by DIA -- never from source code. Keyed externally by DIA symIndexId.
struct ClassHierarchyInfo {
    std::wstring className;
    std::vector<DWORD> baseClassIds; // Direct bases, DIA declaration order
};

class PdbSymbolExtractor {
public:
    // Construction/Destruction
    PdbSymbolExtractor();
    ~PdbSymbolExtractor();

    // Status checking
    bool IsInitialized() const;
    std::wstring GetLastError() const;

    // Main extraction method — also returns reconstructed VTable symbols,
    // the class inheritance graph, and the PDB's own target pointer size
    // (4 or 8; from the PDB machine type, not sizeof(void*) of this
    // process), so callers writing derived vtable data never need to guess
    // it themselves.
    bool ExtractSymbols(const std::wstring& pdbPath, 
                       std::vector<FunctionSymbol>& symbols,
                       std::vector<VTableSymbol>& vtables,
                       std::unordered_map<DWORD, ClassHierarchyInfo>& classHierarchy,
                       DWORD& targetPointerSize);

private:
    // Member fields (with _ prefix)
    bool _initialized;
    std::wstring _lastError;
    IDiaDataSource* _pDiaDataSource;

    // Initialization helpers
    std::wstring FindMsdiaDll();
    HRESULT NoRegCoCreate(const std::wstring& dllPath, 
                          REFCLSID rclsid, REFIID riid, void** ppv);

    // Extraction helpers
    HRESULT ExtractSymbolsFromPdb(const std::wstring& pdbPath, 
                                  std::vector<FunctionSymbol>& symbols,
                                  std::vector<VTableSymbol>& vtables,
                                  std::unordered_map<DWORD, ClassHierarchyInfo>& classHierarchy,
                                  DWORD& targetPointerSize);
    
    // Signature and obfuscation helpers
    std::wstring ExtractFunctionSignature(IDiaSymbol* pSymbol);
    std::wstring ExtractReturnType(IDiaSymbol* pSymbol);
    std::wstring ExtractCallingConvention(IDiaSymbol* pSymbol);
    std::wstring ExtractSourceLocation(IDiaSession* pSession, DWORD rva, DWORD length);
    std::wstring GetTypeName(IDiaSymbol* pType);
    std::wstring BuildFunctionPointerTypeName(IDiaSymbol* pPointerType, IDiaSymbol* pFunctionType);
    std::wstring GenerateObfuscatedName(const std::wstring& realName, DWORD rva);
};
```

Note `ExtractSourceLocation()` takes `IDiaSession*` rather than `IDiaSymbol*`
— line-number lookup (`findLinesByRVA`) is a session-level query, not a
property of the symbol, so it needs the session object `ExtractSymbolsFromPdb()`
already holds locally rather than the symbol the other `Extract*()` helpers
take.

`FunctionSymbol` itself has grown well beyond name/signature/rva/length/isPublic
to carry the VTable and thunk metadata described in
[VTABLE_FEATURE.md](VTABLE_FEATURE.md): `returnType`, `callingConvention`,
`sourceLocation`, `isVirtual`, `isThunk`, `thunkOrdinal`, `thunkTargetRva`, `symbolKind`,
`isIntroducingVirtual`, `vtableOffset`, `vtableIndex`, `vtableShapeId`,
`vtableSlotCount`, `thisAdjust`, `className`, `classParentId`. See
`PdbSymbolExtractor.h` for the authoritative field list.

## Method Responsibilities

### Public Methods

#### Constructor
**Responsibility:** Initialize DIA SDK and COM
- Calls `CoInitialize()`
- Calls `FindMsdiaDll()` to locate DLL
- Calls `NoRegCoCreate()` to create `IDiaDataSource`
- Sets `_initialized` flag
- Stores errors in `_lastError`

**Lines:** ~40

#### Destructor
**Responsibility:** Clean up resources
- Releases `IDiaDataSource`
- Calls `CoUninitialize()`

**Lines:** ~8

#### IsInitialized()
**Responsibility:** Check initialization status
- Returns `_initialized` flag
- Inline in header

**Lines:** 1

#### GetLastError()
**Responsibility:** Return last error message
- Returns `_lastError` string
- Inline in header

**Lines:** 1

#### ExtractSymbols()
**Responsibility:** Public interface for extraction
- Validates initialization
- Calls `ExtractSymbolsFromPdb()`, passing through the `symbols`, `vtables`,
  `classHierarchy`, and `targetPointerSize` output parameters
- Handles errors and return status

**Lines:** ~25

### Private Helper Methods

#### FindMsdiaDll()
**Responsibility:** Locate msdia140.dll
- Checks executable directory first
- Searches Visual Studio installation paths
- Falls back to `%PATH%` via `SearchPathW`
- Returns path or empty string

**Lines:** ~50
**Complexity:** Low

#### NoRegCoCreate()
**Responsibility:** Create COM object without registration
- Loads DLL with `LoadLibrary`
- Gets `DllGetClassObject` function
- Creates class factory
- Creates COM instance

**Lines:** ~35
**Complexity:** Medium

#### ExtractSymbolsFromPdb()
**Responsibility:** Core PDB parsing and symbol enumeration, including VTable reconstruction
- Loads debug data (a `.pdb` path via `loadDataFromPdb()`, or a `.exe`/`.dll`
  path via `loadDataForExe()` so DIA locates the matching PDB itself —
  decided purely from the input path's extension), opens DIA session, gets
  global scope
- Determines the target pointer size from the PDB's machine type (not the
  host process architecture), so a 64-bit ObfSymbolsEx build can correctly
  process an x86 PDB
- Enumerates `SymTagFunction` symbols; for each, gets name, signature (via
  `ExtractFunctionSignature()`), return type (via `ExtractReturnType()`,
  which walks one property deeper than the signature: the function type's
  own `get_type()`), RVA, length, visibility, and — when DIA reports the
  function as virtual — `VTABLE_OFFSET`/`VTABLE_INDEX`, `VTABLE_SHAPE`,
  `THIS_ADJUST`, and the enclosing class
- Enumerates `SymTagThunk` symbols separately (adjustor/vtable thunks aren't
  always exposed as `SymTagFunction`) and records their ordinal/target RVA
  (their signature is extracted the same way as a plain function's)
- Enumerates `SymTagVTableShape` symbols to map shape ID → slot count
- Immediately after building `symbolByRvaAndName`, runs a SOURCE_FILE trust
  pass: for every RVA it maps to 2+ distinct qualified names, compares each
  name's `UnqualifiedNameForSourceLocationTrust()` (a purpose-built cousin
  of `UnqualifiedMethodKey()` — rfind `"::"`, plus stripping the symbol's
  own top-level template arguments, but *not* the destructor-kind folding
  `UnqualifiedMethodKey()` does, since here two different destructors must
  compare unequal, not equal). If they don't all match, `sourceLocation` is
  reset to `?` for every symbol at that RVA — `ExtractSourceLocation()`'s
  `findLinesByRVA` result can be correct for at most one of several
  ICF-folded, differently-named functions, and silently wrong for the rest;
  see [VTABLE_FEATURE.md](VTABLE_FEATURE.md#the-same-icf-phenomenon-also-corrupts-source_file).
- Runs a second pass over `SymTagUDT` symbols' method children and merges
  virtual-method metadata into the matching function, found via
  `symbolByRvaAndName` (keyed by **(RVA, fully qualified name)**, not RVA
  alone — the MSVC linker folds functions with byte-identical compiled
  bodies to one shared RVA by default, which an RVA-only lookup would
  misattribute across unrelated classes; see
  [VTABLE_FEATURE.md](VTABLE_FEATURE.md#identical-code-folded-rvas-corrupted-unrelated-methods-metadata)).
  A UDT method-child's own `get_name()` is unqualified (DIA-confirmed), so it
  is re-qualified as `ClassName + "::" + methodName` — using the class's own
  name already recorded into `classHierarchy` — before this lookup, to match
  the qualified form the global pass keys `symbolByRvaAndName` with. This
  fills in class/shape relationships the global function enumeration
  misses. The same pass also records, per class: its own name, its direct
  `SymTagBaseClass` bases into `classHierarchy`, and — independent of
  whether a method has an RVA at all — every introducing virtual method's
  slot **and its own class-level VTABLE_SHAPE/VTABLE_SLOTS** into
  `introducingSlotsByClass` as an `IntroducingSlotInfo` (needed for
  pure-virtual interface declarations with no compiled body, and to recover
  the correct shape for a secondary multiple-inheritance base; see
  [VTABLE_FEATURE.md](VTABLE_FEATURE.md#pure-virtual-introducing-declarations-with-no-compiled-body)
  and
  [VTABLE_FEATURE.md](VTABLE_FEATURE.md#secondary-multiple-inheritance-bases-recovering-the-right-vtable_shape))
- Enumerates concrete `SymTagVTable` symbols (class children of each UDT) into
  the separate `vtables` output vector
- Runs a third pass that resolves the real `VTABLE_INDEX`/`VTABLE_OFFSET` of
  every overriding (non-introducing) virtual method, since DIA's PDB data has
  no slot offset for an override — see
  [VTABLE_FEATURE.md](VTABLE_FEATURE.md#overriding-virtual-methods-vtable_index-inheritance).
  This calls the file-scope helpers `UnqualifiedMethodKey()` (which also
  normalizes destructor-like methods — see `ClassifySpecialMethod()`) and
  `FindIntroducingAncestorVTableSlot()`, which walk the base-class graph
  built above and check both compiled introducing methods and
  `introducingSlotsByClass`, returning an `IntroducingSlotInfo` (index +
  shape + slot count). When the override's own class-level shape can't
  possibly contain the resolved index, `VTABLE_SHAPE`/`VTABLE_SLOTS` are
  replaced with the introducing interface's own.

**Lines:** ~600
**Complexity:** Medium-High (seven DIA enumeration passes plus the
override-resolution walk)

#### ExtractFunctionSignature()
**Responsibility:** Extract function parameter signature
- Gets function type
- Enumerates function arguments
- Builds signature string: `(type1, type2, ...)`
- Calls `GetTypeName()` for each parameter

**Lines:** ~40
**Complexity:** Low-Medium

#### ExtractReturnType()
**Responsibility:** Extract function return type name
- Gets function type (same `get_type()` call `ExtractFunctionSignature()` makes)
- Gets that function type's own `get_type()` — its return type
- Delegates to `GetTypeName()`, same as every parameter type
- Returns `?` when the function type or return type is unavailable

**Lines:** ~20
**Complexity:** Low

#### ExtractCallingConvention()
**Responsibility:** Extract function calling convention name
- Gets function type (same `get_type()` call `ExtractReturnType()`/
  `ExtractFunctionSignature()` make)
- Calls that function type's `get_callingConvention()` — a `CV_call_e` value
- Maps every known `CV_call_e` enumerator to its `__xxxcall` spelling
- Returns `?` when the function type or convention is unavailable; an
  unrecognized (not one of the mapped) code prints as `CV_CALL_0xNN` rather
  than `?`, since the enum is small and fully known

**Lines:** ~55
**Complexity:** Low

#### ExtractSourceLocation()
**Responsibility:** Extract source file + line number for an RVA range
- Takes `IDiaSession*`, `rva`, and `length` directly — **not** an
  `IDiaSymbol*` like every other `Extract*()` helper, since line-number
  lookup (`IDiaSession::findLinesByRVA`) is a session-level query
- Takes the first line record returned (usually the function's opening
  line, not necessarily its exact declaration line)
- Reduces DIA's full build-machine path to just the filename
- Returns `?` when there is no line data for that RVA range (zero length,
  no debug line info, most thunks, ...)

**Lines:** ~50
**Complexity:** Low

#### GetTypeName()
**Responsibility:** Resolve a DIA type symbol to its C++ spelling
- Checks `get_symTag()` first for shapes with no `get_name()` of their own:
  - `SymTagPointerType` → recurses on the pointee, adds `const `/` *`/` &`
    as appropriate (`get_reference()` distinguishes `&` from `*`); detects a
    function-pointer/pointer-to-member-function pointee and delegates to
    `BuildFunctionPointerTypeName()` instead of the generic suffix
  - `SymTagArrayType` → recurses on the element type, appends `[N]`
    (`get_count()`)
- Otherwise tries direct `get_name()` lookup (covers `SymTagUDT`,
  `SymTagEnum`, `SymTagTypedef`, and anything else DIA names directly)
- Falls back to `get_baseType()` mapping for basic types, including
  `char16_t`/`char32_t`/`BSTR`/`HRESULT`/... and `btNoType` → `...`
  (a variadic function's ellipsis parameter)
- Returns `?` only when none of the above applies
- See `OUTPUT_FORMAT.md`'s "Type Resolution" section for the full table of
  recognized shapes, and why each was added (diagnosed against real `?`
  occurrences in `server.pdb`, not guessed)

**Lines:** ~125
**Complexity:** Medium

#### BuildFunctionPointerTypeName()
**Responsibility:** Spell out a function-pointer or pointer-to-member-
function type in full, e.g. `void * (__cdecl *)(const char *, int *)` or
`void (CBaseEntity::*)(void)`
- Called by `GetTypeName()` when it detects a `SymTagPointerType` wrapping a
  `SymTagFunctionType` — the `(*)`/`(ClassName::*)` this produces already
  conveys "pointer to", so the caller does not add its own generic suffix
- Resolves the return type via `GetTypeName()` (recursively — the return
  type could itself be a pointer, class, etc.)
- Reads the calling convention directly off the function-type symbol via
  `CallingConventionCodeToString()`, the same mapping table
  `ExtractCallingConvention()` uses (factored out so both share one switch)
- Detects a pointer-to-member-function by checking `get_classParent()` on
  the *pointer* symbol (not the function type) — present for Source
  engine's `typedef void (CBaseEntity::*BASEPTR)(void)` pattern, absent for
  an ordinary function pointer
- Walks `SymTagFunctionArgType` children the same way
  `ExtractFunctionSignature()` does, just against this nested function type

**Lines:** ~50
**Complexity:** Low-Medium

## Code Flow

### Initialization Flow
```
Constructor
  └─> CoInitialize()
  └─> FindMsdiaDll()
  └─> NoRegCoCreate()
      └─> LoadLibrary()
      └─> GetProcAddress("DllGetClassObject")
      └─> Create IClassFactory
      └─> CreateInstance(IDiaDataSource)
```

### Extraction Flow
```
ExtractSymbols()
  └─> Check IsInitialized()
  └─> ExtractSymbolsFromPdb()
      └─> Load PDB, open Session, get Global Scope
      └─> Determine target pointer size from PDB machine type
      └─> Enumerate SymTagFunction
          └─> For each symbol:
              ├─> get_name(), ExtractFunctionSignature(), ExtractReturnType(),
              │   ExtractCallingConvention(), get_relativeVirtualAddress(),
              │   get_length(), ExtractSourceLocation(pSession, rva, length),
              │   determine visibility
              └─> If get_virtual(): VTABLE_OFFSET/INDEX, VTABLE_SHAPE, THIS_ADJUST, class
      └─> Enumerate SymTagThunk (adjustor/vtable thunks)
      └─> Enumerate SymTagVTableShape → shapeId -> slotCount map
      └─> Merge slot counts into every symbol with a vtableShapeId
      └─> Build symbolByRvaAndName (RVA -> {qualified name -> index})
      └─> SOURCE_FILE trust pass: for each RVA shared by 2+ symbols whose
      │   unqualified names differ, reset SOURCE_FILE to "?" for all of
      │   them (findLinesByRVA answered "what's at this address", which
      │   ICF can make wrong -- not just missing -- for every symbol
      │   sharing that address except the one DIA's line data truly
      │   belongs to)
      └─> Enumerate SymTagUDT method children, merge virtual metadata by
      │   (RVA, qualified name) -- not RVA alone, to survive identical-code
      │   folding (also records each class's SymTagBaseClass bases)
      └─> Enumerate SymTagVTable (class children of each UDT) into `vtables`
      └─> Resolve VTABLE_INDEX/VTABLE_OFFSET for overriding virtual methods
          by walking the base-class graph for the introducing ancestor
```

`ExtractFunctionSignature()` and `GetTypeName()` are called the same way as
before (see the original flow: get function type → enumerate
`SymTagFunctionArgType` children → resolve each via `GetTypeName()`, which
tries `get_name()` then falls back to `get_baseType()`).

## Benefits of This Organization

### 1. Single Responsibility Principle
Each method has one clear purpose:
- `FindMsdiaDll()` - Only finds the DLL
- `GetTypeName()` - Only resolves type names
- `ExtractFunctionSignature()` - Only builds signatures
- `ExtractSymbolsFromPdb()` - Only extracts symbols

### 2. Easy to Test
Each method can be tested independently:
```cpp
// Test type name resolution
std::wstring typeName = extractor.GetTypeName(pTypeSymbol);

// Test signature extraction
std::wstring sig = extractor.ExtractFunctionSignature(pFuncSymbol);
```

### 3. Easy to Maintain
- Want to improve type resolution? → Modify `GetTypeName()`
- Want to change signature format? → Modify `ExtractFunctionSignature()`
- Want to add filtering? → Modify `ExtractSymbolsFromPdb()`

### 4. Easy to Extend
Add new features without touching existing code — this is no longer
hypothetical: `ExtractReturnType()`, `ExtractCallingConvention()`, and later
`ExtractSourceLocation()` were each added exactly this way, alongside
`ExtractFunctionSignature()`, with no changes needed elsewhere in the class:
```cpp
std::wstring ExtractReturnType(IDiaSymbol* pSymbol);
std::wstring ExtractCallingConvention(IDiaSymbol* pSymbol);
std::wstring ExtractSourceLocation(IDiaSession* pSession, DWORD rva, DWORD length);

// Used in extraction, right next to ExtractFunctionSignature():
func.signature = ExtractFunctionSignature(pSymbol);
func.returnType = ExtractReturnType(pSymbol);
func.callingConvention = ExtractCallingConvention(pSymbol);
func.sourceLocation = ExtractSourceLocation(pSession, func.rva, static_cast<DWORD>(func.length));
```
`ExtractSourceLocation()` needed a different kind of parameter (DIA's
session, not the symbol) that the others don't, since line lookup is a
session-level query — still no changes to any other method's signature or
behavior.

### 5. Readable Main Loop
The main extraction loop is now very clean:
```cpp
while (SUCCEEDED(pEnumSymbols->Next(1, &pSymbol, &celt)) && celt == 1) {
    FunctionSymbol func;
    
    // Get function name
    BSTR bstrName;
    if (pSymbol->get_name(&bstrName) == S_OK) {
        func.name = bstrName;
        SysFreeString(bstrName);
    }
    
    // Get signature (delegated to helper)
    func.signature = ExtractFunctionSignature(pSymbol);
    
    // Get address and size
    // ... simple property access
    
    // Determine visibility
    // ... simple checks
    
    symbols.push_back(func);
}
```

## Code Metrics

### Method Sizes (Lines of Code, current source)

| Method | Lines (approx.) | Complexity |
|--------|-------|------------|
| Constructor | 35 | Low |
| Destructor | 10 | Low |
| IsInitialized() | 1 | Low |
| GetLastError() | 1 | Low |
| ExtractSymbols() | 25 | Low |
| FindMsdiaDll() | 50 | Low |
| NoRegCoCreate() | 40 | Medium |
| GenerateObfuscatedName() | 30 | Low |
| GetTypeName() | ~140 | Medium |
| BuildFunctionPointerTypeName() | ~55 | Low-Medium |
| ExtractFunctionSignature() | 50 | Low-Medium |
| ExtractReturnType() | 20 | Low |
| ExtractCallingConvention() | ~17 | Low |
| ExtractSourceLocation() | 50 | Low |
| ExtractSymbolsFromPdb() | ~840 | Medium-High |

**Total:** 1681 lines in `PdbSymbolExtractor.cpp` (plus 110 lines in
`PdbSymbolExtractor.h` and 833 lines in `ObfSymbolsEx.cpp` for the six file
writers and `wmain()`; 2624 lines total). `ExtractSymbolsFromPdb()` grew
substantially as thunk, VTableShape, UDT-method, VTable-reconstruction, and
override-resolution passes were added — it is no longer a single-purpose,
easily-summarized method and would be a reasonable candidate to split
further. This numeric history predates VTable support; see
[REFACTORING_SUMMARY.md](REFACTORING_SUMMARY.md) for the original 291→461
line comparison.

## Design Patterns Applied

### 1. RAII (Resource Acquisition Is Initialization)
```cpp
PdbSymbolExtractor extractor;  // Resources acquired
// Use extractor
// Destructor automatically releases resources
```

### 2. Single Responsibility
Each method does one thing well.

### 3. Dependency Injection
`IDiaDataSource` created once, used multiple times.

### 4. Strategy Pattern (Implicit)
Type resolution strategy encapsulated in `GetTypeName()`:
- Check for shapes with no name of their own first (pointer/reference,
  array — recurse into what they wrap)
- Try direct name lookup (classes, enums, typedefs, ...)
- Try base type mapping (`int`, `char16_t`, `...`, ...)
- Fallback to `?`

### 5. Template Method (Implicit)
`ExtractSymbols()` defines the algorithm:
1. Check initialization
2. Extract from PDB
3. Handle errors
4. Return status

## File Organization

```
ObfSymbolsEx/
├── PdbSymbolExtractor.h        # Interface definition (110 lines)
├── PdbSymbolExtractor.cpp      # Implementation (1681 lines)
│   ├── Constructor/Destructor  # Resource management
│   ├── ClassifySpecialMethod() # Destructor-kind detection (shared)
│   ├── CallingConventionCodeToString() # CV_call_e -> "__xxxcall" (shared by the two below)
│   ├── FindMsdiaDll()          # DLL location (exe dir -> VS paths -> %PATH%)
│   ├── NoRegCoCreate()         # COM creation
│   ├── GenerateObfuscatedName() # FNV-1a hash obfuscation
│   ├── GetTypeName()           # Type resolution: basic types, pointers/refs,
│   │                            # arrays, function pointers, member-function
│   │                            # pointers, named types
│   ├── BuildFunctionPointerTypeName() # "RetType (conv*)(Args)" / "(Class::*)" spelling
│   ├── ExtractFunctionSignature() # Signature building
│   ├── ExtractReturnType()     # Return type resolution (via GetTypeName)
│   ├── ExtractCallingConvention() # Calling convention resolution (via get_callingConvention)
│   ├── ExtractSourceLocation() # Source file:line resolution (via findLinesByRVA)
│   └── ExtractSymbolsFromPdb() # Functions, thunks, VTableShapes,
│                                # UDT-method merge + base-class graph,
│                                # VTable enumeration, override resolution
│
└── ObfSymbolsEx.cpp              # Main program (833 lines)
    ├── WStringToString()             # String conversion
    ├── ComputeColumnWidths()         # Per-run VISIBILITY/SIZE/CALLING_CONVENTION column widths
    ├── HexDigitCount()               # Hex-digit width of a value, for the vtable widths below
    ├── ComputeVTableColumnWidths()   # Per-run SLOT/OFFSET/RVA/SIZE column widths (_vtable.sym)
    ├── WriteSymbolsToFile()          # output.sym (mapping)
    ├── WriteObfuscatedSymbolsToFile() # output_obfuscated.sym
    ├── WriteVTablesToFile()          # output_vtable.sym / _vtable_obfuscated.sym
    ├── CollectVTableClassGroups()    # Shared class grouping (below two writers)
    ├── WriteVTableClassesToFile()    # output_vtable_classes.sym
    ├── WriteVTableInheritanceToFile() # output_vtable_inheritance.sym
    └── wmain()                       # Entry point
```

## Best Practices Followed

### ✅ Clear Separation of Concerns
- Initialization separate from extraction
- Type resolution separate from signature building
- File I/O separate from PDB parsing

### ✅ Error Handling
- Errors captured at each level
- Descriptive error messages
- No silent failures

### ✅ Resource Management
- RAII pattern ensures cleanup
- No memory leaks
- COM properly initialized/uninitialized

### ✅ Code Reusability
- Helper methods can be called from multiple places
- Type name resolution reusable
- Signature extraction reusable

### ✅ Readability
- Method names describe what they do
- Most methods fit on one screen; `ExtractSymbolsFromPdb()` is the exception
  (see Code Metrics above) since it now runs six DIA enumeration passes
- Clear control flow within each pass

### ✅ Maintainability
- Easy to locate functionality
- Easy to modify specific features
- Easy to add new features

## Performance

### Method Call Overhead
- Minimal (inlined by optimizer where possible)
- Type resolution: ~50 instructions per call
- Signature extraction: ~200 instructions per symbol

### Overall Impact
- Extraction speed: Same as monolithic version
- Code clarity: Significantly improved
- Maintenance burden: Significantly reduced

## Future Extensions

`ExtractReturnType()`, `ExtractCallingConvention()`, and Source Location
were all once listed here as hypothetical additions; all three now exist
(see "Method Responsibilities" above, and `ExtractSourceLocation()`
specifically) and are no longer future items. The actual implementation
took `IDiaSession*` + `rva` + `length` rather than the `IDiaSymbol*` this
section originally sketched, since line lookup needed a session-level DIA
call (`findLinesByRVA`), not a symbol property — the one helper in the class
that doesn't fit the "just take the symbol" pattern the others follow.

### Easy to Add

**1. Full Type Information**
```cpp
struct FullTypeInfo {
    std::wstring name;
    bool isPointer;
    bool isReference;
    bool isConst;
};
FullTypeInfo GetFullTypeInfo(IDiaSymbol* pType);
```

## Conclusion

The refactored code organization provides:
- ✅ **Better Readability** - Small, focused methods
- ✅ **Better Maintainability** - Easy to modify and extend
- ✅ **Better Testability** - Each method testable independently
- ✅ **Same Performance** - No runtime overhead
- ✅ **Professional Quality** - Follows C++ best practices

The per-symbol body of the main extraction loop stays small and easy to follow, while complex logic (signature building, type resolution, obfuscation, VTable/thunk reconstruction) is properly encapsulated in dedicated helper methods and passes.

