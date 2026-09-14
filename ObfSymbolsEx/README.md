# ObfSymbolsEx - PDB Function Symbol Extractor

A console application that parses PDB (Program Database) files and extracts function symbols to a text file.

> Extended version of [ObfSymbols](https://github.com/chrisnas/VibeCoding/tree/main/ObfSymbols) by Christophe Nasarre ([@chrisnas](https://github.com/chrisnas)) — see [OUTPUT_FORMAT.md](OUTPUT_FORMAT.md) for what this fork adds.

## Overview

ObfSymbolsEx reads a `.pdb` (or a `.exe`/`.dll`, letting DIA locate the
matching PDB itself) and outputs function symbols to a `.sym` file. See
[OUTPUT_FORMAT.md](OUTPUT_FORMAT.md) for the full, current column-by-column
format reference — the columns include visibility, address, size,
obfuscated name, calling convention, real name + signature, return type,
`IS_VIRTUAL`/`VTABLE_*` metadata, and source file + line.

## Requirements

- Windows operating system
- Visual Studio 2022 or later (for building)
- Windows SDK with Debug Interface Access (DIA) SDK

**Note:** No DLL registration is required! ObfSymbolsEx dynamically loads `msdia140.dll` without requiring COM registration.

## Building the Project

### Option 1: Using the Build Script (Recommended)

Simply run the build script from the project directory:

**PowerShell:**
```powershell
.\build.ps1
```

**Command Prompt:**
```cmd
build.bat
```

You can optionally specify the configuration and platform:
```powershell
.\build.ps1 -Configuration Debug -Platform x64
.\build.ps1 -Configuration Release -Platform x86
```

Or with the batch file:
```cmd
build.bat Debug x64
build.bat Release x86
```

### Option 2: Using Visual Studio

1. Open the solution in Visual Studio 2022
2. Select your desired configuration (Debug/Release) and platform (x86/x64)
3. Build the solution (Ctrl+Shift+B)

### Option 3: Using MSBuild Directly

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" ObfSymbolsEx.vcxproj /p:Configuration=Release /p:Platform=x64
```

## Cleaning Build Artifacts

To clean all build artifacts and intermediate files:

**PowerShell:**
```powershell
.\clean.ps1
```

**Command Prompt:**
```cmd
clean.bat
```

## Usage

```
ObfSymbolsEx.exe <input.pdb|input.exe|input.dll> <output.sym>
```

### Parameters

- `<input.pdb|input.exe|input.dll>`: Path to the input PDB file, or to the
  built `.exe`/`.dll` itself — DIA locates and loads the matching PDB (same
  directory as the binary, embedded GUID/age against a symbol server or
  local cache, ...), decided purely from the extension
- `<output.sym>`: Path to the output symbol file

### Example

```
ObfSymbolsEx.exe myapp.pdb symbols.sym
ObfSymbolsEx.exe myapp.exe symbols.sym
```

This will read `myapp.pdb` and create `symbols.sym` containing all function symbols.

## Output Format Example

```
PUBLIC 0x1000 SIZE=45  obf_A7B3C92E __cdecl    main() -> int IS_VIRTUAL=0 VTABLE_OFFSET=-1 VTABLE_INDEX=-1 VTABLE_CLASS= VTABLE_INTRO=0 VTABLE_SHAPE=0 VTABLE_SLOTS=0 THIS_ADJUST=0 KIND=FUNCTION THUNK=0 THUNK_ORDINAL=0 THUNK_TARGET_RVA=0x0 SOURCE_FILE=main.cpp:12
PRIVATE 0x1050 SIZE=128 obf_5C2A376B __thiscall HelperFunction(int, double) -> void IS_VIRTUAL=0 VTABLE_OFFSET=-1 VTABLE_INDEX=-1 VTABLE_CLASS= VTABLE_INTRO=0 VTABLE_SHAPE=0 VTABLE_SLOTS=0 THIS_ADJUST=0 KIND=FUNCTION THUNK=0 THUNK_ORDINAL=0 THUNK_TARGET_RVA=0x0 SOURCE_FILE=helper.cpp:8
```

This is a shortened illustrative example — see
[OUTPUT_FORMAT.md](OUTPUT_FORMAT.md) for the complete, versioned format
reference (column alignment, every trailing field, and the obfuscated-file
variant).

**Note:** A type genuinely outside everything `GetTypeName()` recognizes
(basic types, named types, pointers/references, arrays, function pointers,
pointer-to-member-function types, `char16_t`/`char32_t`, `...`) still shows
as `?` — see OUTPUT_FORMAT.md's "Type Resolution" section.

## Features

- Extracts all function symbols from PDB files, including their full signature and return type
- Detects calling convention (`__cdecl`, `__thiscall`, `__stdcall`, `__fastcall`, ...)
- Resolves virtual-function metadata and reconstructs full vtables, including overriding methods with no DIA-recorded slot offset
- Resolves each symbol's source file + line number from the PDB's own debug line info
- Identifies PUBLIC vs PRIVATE functions based on export status and access level
- Outputs symbols sorted by address, with column-aligned formatting for readability
- Displays progress information during processing

## Technical Details

The application uses the Microsoft Debug Interface Access (DIA) SDK to read PDB files. The DIA SDK is the official Microsoft API for accessing debug information stored in PDB files.

### Function Visibility Determination

A function is considered **PUBLIC** if:
- It is exported from the module, OR
- It has public access level (CV_public)

Otherwise, the function is marked as **PRIVATE**.

## Distribution

ObfSymbolsEx can be distributed as a standalone application. The build script automatically copies `msdia140.dll` to the executable directory.

### For Standalone Distribution

1. Build the project (the DLL is copied automatically)
2. Distribute both files together:
   - `ObfSymbolsEx.exe`
   - `msdia140.dll`

### Manual DLL Copy

If you need to manually copy the DLL:
```powershell
.\copy-dia-dll.ps1
```

## How It Works

ObfSymbolsEx dynamically loads the DIA SDK DLL (`msdia140.dll`) at runtime without requiring COM registration:

1. **First**, it checks for `msdia140.dll` in the same directory as the executable
2. **Then**, it searches common Visual Studio 2022 installation paths
3. **Finally**, it searches `%PATH%` via `SearchPathW`
4. The DLL is loaded directly using `LoadLibrary` and `DllGetClassObject`

Note: an earlier prototype embedded `msdia140.dll` inside the executable as a
resource and extracted it on first run. That approach was intentionally
removed — see [EMBEDDED_DLL_FEATURE.md](EMBEDDED_DLL_FEATURE.md).

This approach provides:
- ✅ No registration required
- ✅ Portable deployment
- ✅ Works without Visual Studio on target machines
- ✅ Multiple versions can coexist

## Troubleshooting

### "Failed to locate msdia140.dll"

**Solution 1 (Recommended):** Copy the DLL to the executable directory
```powershell
.\copy-dia-dll.ps1
```

**Solution 2:** Place `msdia140.dll` from your Visual Studio installation next to `ObfSymbolsEx.exe`:
- Source: `C:\Program Files\Microsoft Visual Studio\2022\<Edition>\DIA SDK\bin\amd64\msdia140.dll`
- Destination: Same folder as `ObfSymbolsEx.exe`

### "Failed to load PDB file"

Ensure:
- The PDB file path is correct
- The PDB file is not corrupted
- You have read permissions for the file
- The PDB format is supported (generated by Visual C++ compiler)

## Testing and Validation

A comprehensive test application (`TestApp`) is included to validate ObfSymbolsEx functionality.

### Quick Validation

Run the validation script from the root directory:

**PowerShell:**
```powershell
.\validate.ps1
```

**Command Prompt:**
```cmd
validate.bat
```

This will:
1. Build ObfSymbolsEx (Release)
2. Build TestApp (Debug) 
3. Run ObfSymbolsEx on TestApp.pdb
4. Generate `TestApp_symbols.sym`
5. Display analysis of extracted symbols

### TestApp Contents

The test application includes:
- Simple functions and function overloads
- Template functions and classes
- Static and inline functions
- Structs and classes with methods
- Constructors, destructors, and operators
- Virtual methods and inheritance
- Namespace functions
- Complex parameter types

See `TestApp/README.md` for detailed information.

### Manual Testing

1. Build TestApp:
   ```powershell
   cd TestApp
   .\build.ps1
   cd ..
   ```

2. Run ObfSymbolsEx on TestApp's PDB:
   ```powershell
   .\ObfSymbolsEx\x64\Release\ObfSymbolsEx.exe TestApp\x64\Debug\TestApp.pdb output.sym
   ```

3. Review the output:
   ```powershell
   Get-Content output.sym
   ```

## Project Structure

```
ObfSymbols/
├── ObfSymbolsEx/          # Main PDB parser application
│   ├── ObfSymbolsEx.cpp   # Source code
│   ├── build.ps1          # Build script
│   ├── clean.ps1          # Clean script
│   └── README.md          # Documentation
├── TestApp/               # Test application for validation
│   ├── TestApp.cpp        # Test code with various symbols
│   ├── build.ps1          # Build script
│   └── README.md          # Test documentation
├── validate.ps1           # Validation script
└── README.md              # This file
```

## License

This project is for internal use.

## VTable metadata

For virtual functions ObfSymbolsEx emits:

- `VTABLE_OFFSET` — DIA byte offset in the virtual table, or `-1` when unavailable.
- `VTABLE_INDEX` — slot index calculated from the PDB target pointer size.
- `VTABLE_CLASS` — enclosing class reported by DIA.
- `VTABLE_INTRO` — whether DIA marks the function as introducing a virtual slot.
- `VTABLE_SHAPE` — DIA virtual-table shape ID.
- `VTABLE_SLOTS` — number of entries in the corresponding VTableShape when available.
- `THIS_ADJUST` — DIA `this` adjustment.

The extractor also performs a second UDT/method pass and merges its metadata by RVA. This improves coverage for C++ class methods and preserves the original one-record-per-function output format.

A third pass resolves `VTABLE_OFFSET`/`VTABLE_INDEX` for overriding
(non-introducing) virtual methods, including destructors and pure-virtual
interface declarations with no compiled body anywhere in the PDB — DIA's own
data has no slot offset for these at all. See `VTABLE_FEATURE.md` for why and
how.

## Reconstructed VTable output

ObfSymbolsEx also generates four additional files, for a total of six output
files per run:

- `<output>_vtable.sym` / `<output>_vtable_obfuscated.sym` — one `VTABLE`
  record per concrete or reconstructed vtable, listing every slot described by
  its `VTableShape` in ascending slot order. Missing associations are
  reported as `UNKNOWN` instead of being inferred from source order or SDK
  data.
- `<output>_vtable_classes.sym` — the same virtual-method metadata regrouped
  by class and sorted by vtable index, for a quick per-class view. This file
  has no obfuscated counterpart; it always includes real names.
- `<output>_vtable_inheritance.sym` — for every class in `_vtable_classes.sym`,
  its full recursive base-class chain (as DIA reports it, down to the root
  class(es)) drawn as an ASCII tree. No obfuscated counterpart.

See `VTABLE_RECONSTRUCTION.md` for the full format, field reference, and
sort-order details for all three files.
