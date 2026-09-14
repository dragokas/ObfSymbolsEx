# ObfSymbolsEx

A tool for extracting function symbols from PDB (Program Database) files without requiring COM registration.

## Quick Start

### Building ObfSymbolsEx only
```
.\ObfSymbolsEx\build.bat
```

### Using ObfSymbolsEx

```powershell
.\ObfSymbolsEx\x64\Release\ObfSymbolsEx.exe input.pdb output.sym
```

The input can also be the built `.exe`/`.dll` itself instead of its `.pdb`
— ObfSymbolsEx decides which by extension, and for a binary path lets DIA
locate the matching PDB itself.

# ObfSymbolsEx vs ObfSymbols

> **ObfSymbolsEx** is an extended version of the original [ObfSymbols](https://github.com/chrisnas/VibeCoding/tree/main/ObfSymbols) utility by Christophe Nasarre ([@chrisnas](https://github.com/chrisnas)) — see the original article, [*"Vibe coding a PDB dumper, or how I became a product manager"*](https://chnasarre.medium.com/vibe-coding-a-pdb-dumper-or-how-i-became-a-product-manager-f957106a3e9f).

**Added in this fork:**
- **VTable methods dump** — full virtual-method/vtable reconstruction (`_vtable.sym`, `_vtable_classes.sym`, `_vtable_inheritance.sym`), including override slot resolution and multiple-inheritance shape recovery
- **Return types**
- **Calling convention** detection
- **Complex/basic type detection** — pointers, references, arrays, function pointers, pointer-to-member-function types, `char16_t`/`char32_t`, and variadic `...`
- **Source file + line number** for each symbol, resolved from the PDB's own debug line info
- **Identical Code Folding (ICF) fixes** — for both vtable metadata and source-file attribution, where the linker folds byte-identical functions together
- **Destructor / pure-virtual override resolution fixes**
- **Direct `.exe`/`.dll` input** — DIA locates the matching PDB automatically
- **Column-aligned output** for readability

## Overview

This solution contains three projects:

1. **ObfSymbolsEx** - The main PDB symbol extraction tool
2. **TestApp** - A comprehensive test application (EXE) for validation
3. **TestDLL** - A test DLL with exported symbols for validation

## Features

### 🚀 Main Highlights

- ✅ **No Registration Required** - Dynamically loads DIA SDK without COM registration
- ✅ **Portable** - Works without Visual Studio on target machines
- ✅ **Comprehensive Output** - Lists function symbols with PUBLIC/PRIVATE, address, size, and name
- ✅ **Easy Distribution** - Just two files: `ObfSymbolsEx.exe` + `msdia140.dll`
- ✅ **Full Validation** - Includes extensive test application with 460+ test symbols

### Output Files

Given `ObfSymbolsEx.exe server.pdb server.sym`, six files are written next to `server.sym`:

| File | Purpose |
|---|---|
| `server.sym` | Mapping file — real names, signatures, return types, source file:line |
| `server_obfuscated.sym` | Same symbols, real name/signature/source file stripped — safe to redistribute |
| `server_vtable.sym` | Raw per-vtable slot dump (mapping names), including unresolved/unknown slots |
| `server_vtable_obfuscated.sym` | Same vtable dump, obfuscated names only |
| `server_vtable_classes.sym` | Per-class resolved virtual-method table (only known slots, one block per class) |
| `server_vtable_inheritance.sym` | Per-class base-class tree, no vtable data |

### Output Format

**server.sym**

```
PUBLIC 0x1200 SIZE=88 obf_1561179B __thiscall Calculator::Add(double, double) -> double IS_VIRTUAL=0 VTABLE_OFFSET=-1 VTABLE_INDEX=-1 VTABLE_CLASS= VTABLE_INTRO=0 VTABLE_SHAPE=0 VTABLE_SLOTS=0 THIS_ADJUST=0 KIND=FUNCTION THUNK=0 THUNK_ORDINAL=0 THUNK_TARGET_RVA=0x0 SOURCE_FILE=Calculator.cpp:42
```

Each line contains:
- **PUBLIC/PRIVATE** - Function visibility
- **Address** - Relative Virtual Address (RVA) in hexadecimal
- **Size** - Function size in bytes (decimal)
- **Obfuscated name** - Non-reversible `obf_XXXXXXXX` hash (see `ObfSymbolsEx/OBFUSCATION_ALGORITHM.md`)
- **Calling convention** - Bare value (`__thiscall`, `__cdecl`, ...), no `KEY=` prefix, right before the name (right before the obfuscated name instead, in the obfuscated file)
- **Name** - Function name (with C++ name mangling)
- **Signature** - Function parameters (without return type)
- **Return type** - Preceded by `-> ` (mapping file only)
- **Trailing fields** - `IS_VIRTUAL` and VTable/thunk metadata (see `ObfSymbolsEx/OUTPUT_FORMAT.md`); non-virtual, non-thunk symbols carry non-applicable values (`-1`, `0`)
- **Source file** - `SOURCE_FILE=File.cpp:Line`, the last field, mapping file only (omitted from the obfuscated file, like the name/signature/return type)

**Signature Notes:**
- Basic types (`int`, `double`, `float`, `bool`, `char`, etc.) are displayed correctly
- Complex types (classes, STL containers, references) may show as `?`
- Empty parameter lists show as `()`
- Helps differentiate function overloads

This is a simplified single-line view; the full column and field reference
lives in `ObfSymbolsEx/OUTPUT_FORMAT.md`. ObfSymbolsEx also writes a matching
obfuscated-only file plus three VTable-reconstruction files (five files per
run) — see `ObfSymbolsEx/DUAL_OUTPUT_FILES.md` and
`ObfSymbolsEx/VTABLE_RECONSTRUCTION.md`.

**server_vtable.sym**

```
CLASS=CBaseEntity CLASS_ID=456 SHAPE=789 SLOTS=87 VTABLE_SOURCE=DIA_VTABLE ID=123 RVA=0x107DFD28
SLOT=26 OFFSET=0x68 RVA=0x1034D3E0 SIZE=123 OBFUSCATED=obf_1561179B MATCH=CLASS_SHAPE KIND=FUNCTION THUNK=0 THUNK_ORDINAL=0 THUNK_TARGET_RVA=0x0 THIS_ADJUST=0 __thiscall METHOD=SetModel(char const*) METHOD_RETURN_TYPE=void METHOD_CLASS=CBaseEntity SOURCE_FILE=baseentity.cpp:1902
SLOT=41 OFFSET=0xA8 RVA=-1 SIZE=-1 OBFUSCATED=- MATCH=NONE METHOD=UNKNOWN
```

One `CLASS=...` header per vtable, then one `SLOT=` line per entry. Same
per-method fields as `server.sym` above (`OBFUSCATED`, calling convention,
`METHOD` name+signature, `METHOD_RETURN_TYPE`, `SOURCE_FILE`, ...), plus
`OFFSET` (byte offset into the vtable) and `MATCH` (how the slot was
resolved — `CLASS_SHAPE` or `NONE`); an unresolved slot prints
`RVA=-1`/`METHOD=UNKNOWN` instead of being omitted.

**server_vtable_classes.sym**

```
CLASS CBaseEntity CLASS_ID=456 VTABLE_SLOTS=87
  [0] RVA=0x1034A100 __thiscall CBaseEntity::~CBaseEntity() -> void SHAPE=789 INTRO=1 THIS_ADJUST=0 KIND=DESTRUCTOR SOURCE_FILE=baseentity.cpp:1780
  [26] RVA=0x1034D3E0 __thiscall CBaseEntity::SetModel(char const*) -> void SHAPE=789 INTRO=0 THIS_ADJUST=0 KIND=FUNCTION SOURCE_FILE=baseentity.cpp:1902
```

Same idea, but only the slots DIA could actually resolve for that class (no
`UNKNOWN` rows), indexed by `[slot]` instead of address. `INTRO=1` marks the
class that first declares that slot; `SHAPE`/`THIS_ADJUST` mirror the
mapping file's `VTABLE_SHAPE`/`THIS_ADJUST`.

**server_vtable_inheritance.sym**

```
CLASS CAI_SpeechFilter CLASS_ID=3213
  +-- CBaseEntity
  |   `-- IServerEntity
  |       `-- IServerUnknown
  |           `-- IHandleEntity
  `-- IEntityListener
```

No vtable/method data — just each class's base-class chain as an ASCII
tree, handy for spotting which ancestor introduces a slot seen in the two
files above.

### Building everything

**Option 1: Build entire solution**
```powershell
.\build-all.ps1
```

**Option 2: Build individual projects**
```powershell
cd ObfSymbolsEx
.\build.ps1
cd ..\TestApp
.\build.ps1
```

### Testing & Validation

The solution includes comprehensive validation that tests both EXE and DLL symbol extraction.

See details in: [UNIT_TESTS.md](UNIT_TESTS.md)

## Project Structure

```
ObfSymbols/
├── ObfSymbolsEx.sln        # Classic Visual Studio solution
├── ObfSymbolsEx.slnx       # Modern XML-based solution
├── build-all.ps1           # Build entire solution
├── clean-all.ps1           # Clean all build artifacts
├── validate.ps1            # Build-and-eyeball smoke test
├── unit-tests.ps1          # Pass/fail regression suite (see UNIT_TESTS.md)
├── README.md               # This file
│
├── ObfSymbolsEx/           # Main application
│   ├── ObfSymbolsEx.cpp    # Source code with dynamic DLL loading
│   ├── ObfSymbolsEx.vcxproj  # Project file
│   ├── build.ps1           # Project build script (auto-copies DLL)
│   ├── clean.ps1           # Project clean script
│   ├── copy-dia-dll.ps1    # Manual DLL copy utility
│   ├── README.md           # Detailed project documentation
│   └── IMPLEMENTATION_NOTES.md  # Technical implementation details
│
├── TestApp/                # Test application (EXE)
│   ├── TestApp.cpp         # Comprehensive test symbols (460+ symbols)
│   ├── TestApp.vcxproj     # Project file
│   ├── build.ps1           # Project build script
│   └── README.md           # Test documentation
│
└── TestDLL/                # Test DLL
    ├── TestDLL.h           # Exported symbols header
    ├── TestDLL.cpp         # DLL implementation (227+ symbols)
    ├── TestDLL.vcxproj     # Project file
    ├── build.ps1           # Project build script
    └── README.md           # DLL test documentation
```

## Requirements

### For Building
- Windows 10 or newer
- Visual Studio 2022 or newer
- Windows SDK with DIA SDK support
- C++20 compiler support

### For Running
- Windows 10 or newer
- `msdia140.dll` (automatically copied during build)

**No Visual Studio or registration required on target machines!**

## Build Scripts

### Solution Level

- **`build-all.ps1`** / **`build-all.bat`** - Build both projects
- **`clean-all.ps1`** / **`clean-all.bat`** - Clean all build artifacts

Usage:
```powershell
# Default: Release x64
.\build-all.ps1

# Specify configuration
.\build-all.ps1 -Configuration Debug -Platform x64
.\build-all.ps1 -Configuration Release -Platform x86
```

### Project Level

Each project has its own build scripts:
- `build.ps1` / `build.bat` - Build the project
- `clean.ps1` / `clean.bat` - Clean build artifacts

## Distribution

To distribute ObfSymbolsEx:

1. Build the project (DLL is automatically copied):
   ```powershell
   cd ObfSymbolsEx
   .\build.ps1
   ```

2. Distribute these two files together:
   - `x64\Release\ObfSymbolsEx.exe`
   - `x64\Release\msdia140.dll`

That's it! No installation, no registration, no dependencies.

## Usage Examples

### Extract Symbols
```powershell
.\ObfSymbolsEx\x64\Release\ObfSymbolsEx.exe myapp.pdb symbols.sym
```

### Filter Results
```powershell
# Show only PUBLIC symbols
Get-Content symbols.sym | Where-Object { $_ -match "^PUBLIC" }

# Find specific function
Get-Content symbols.sym | Where-Object { $_ -match "MyFunction" }

# Count symbols by type
(Get-Content symbols.sym | Where-Object { $_ -match "^PUBLIC" }).Count
(Get-Content symbols.sym | Where-Object { $_ -match "^PRIVATE" }).Count
```

### Batch Processing
```powershell
# Process multiple PDB files
Get-ChildItem -Filter "*.pdb" | ForEach-Object {
    $outFile = $_.BaseName + "_symbols.sym"
    .\ObfSymbolsEx\x64\Release\ObfSymbolsEx.exe $_.FullName $outFile
}
```

## Troubleshooting

### "Failed to locate msdia140.dll"

The build script should automatically copy this DLL. If not:

```powershell
cd ObfSymbolsEx
.\copy-dia-dll.ps1
```

Or manually copy from Visual Studio:
```
Source: C:\Program Files\Microsoft Visual Studio\2022\<Edition>\DIA SDK\bin\amd64\msdia140.dll
Destination: Same folder as ObfSymbolsEx.exe
```

### Build Errors

1. Ensure Visual Studio 2022 is installed with C++ support
2. Verify the DIA SDK is included in your Visual Studio installation
3. Try cleaning and rebuilding:
   ```powershell
   .\clean-all.ps1
   .\build-all.ps1
   ```

### Symbol Extraction Issues

- Verify the PDB file is valid and not corrupted
- Ensure the PDB was generated by Visual C++ compiler
- Check file permissions on both input and output files

## Technical Details

- **Language**: C++20
- **Platform**: Windows x64/x86
- **Toolset**: Visual Studio 2022 (v143)
- **DIA SDK**: msdia140.dll (included with Visual Studio)
- **COM**: Dynamic loading without registration

For implementation details, see `ObfSymbolsEx/IMPLEMENTATION_NOTES.md`.

## Contributing

This project demonstrates best practices for:
- Dynamic DLL loading without registration
- PDB parsing using DIA SDK
- Portable C++ application design
- Comprehensive testing strategies

## License

This project is for internal use.

## Authors

ObfSymbolsEx is created by Dragokas & AI.
It is based on tool created as part of Datadog R&D Week 2025 (original [ObfSymbols](https://github.com/chrisnas/VibeCoding/tree/main/ObfSymbols) by Christophe Nasarre).
