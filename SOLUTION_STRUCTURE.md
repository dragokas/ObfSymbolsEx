# ObfSymbolsEx Solution Structure

This document describes the complete solution organization after all updates.

## Solution Files

### Visual Studio Solution Files

- **`ObfSymbolsEx.sln`** - Classic Visual Studio solution format (compatible with all VS versions)
- **`ObfSymbolsEx.slnx`** - Modern XML-based solution format (VS 2022+)

Both solution files reference:
1. `ObfSymbolsEx\ObfSymbolsEx.vcxproj` - Main PDB parser application
2. `TestApp\TestApp.vcxproj` - Comprehensive test application

### Solution-Level Build Scripts

#### `build-all.ps1` / `build-all.bat`
Builds the entire solution (both projects) with one command.

**Features:**
- Automatically finds MSBuild via vswhere
- Builds both projects in parallel when possible
- Copies `msdia140.dll` to output directory automatically
- Shows clear success/failure messages
- Provides quick test commands

**Usage:**
```powershell
.\build-all.ps1                              # Default: Release x64
.\build-all.ps1 -Configuration Debug         # Debug x64
.\build-all.ps1 -Platform x86               # Release x86
```

**Output Structure (Solution Builds):**
```
x64/
  Release/
    ObfSymbolsEx.exe
    msdia140.dll        # Automatically copied
    TestApp.exe
    TestApp.pdb
```

#### `clean-all.ps1` / `clean-all.bat`
Cleans all build artifacts from both projects and the solution.

**Removes:**
- All x64/x86 directories
- All Debug/Release directories
- .vs folder
- .user, .suo, .log files
- Generated .sym files

## Project Structure

### ObfSymbolsEx Project

```
ObfSymbolsEx/
├── ObfSymbolsEx.cpp              # Main source with dynamic DLL loading
├── ObfSymbolsEx.vcxproj          # Project file (v143 toolset, C++20)
├── ObfSymbolsEx.vcxproj.filters  # Visual Studio filters
├── ObfSymbolsEx.vcxproj.user     # User-specific settings
│
├── build.ps1 / build.bat       # Project build scripts
├── clean.ps1 / clean.bat       # Project clean scripts
├── copy-dia-dll.ps1            # Manual DLL copy utility
│
├── README.md                   # Project documentation
└── IMPLEMENTATION_NOTES.md     # Technical implementation details
```

**Key Features:**
- Dynamic DIA SDK loading (no registration)
- Searches for `msdia140.dll` in exe directory first
- Falls back to Visual Studio installation paths, then `%PATH%`
- Auto-copies DLL during build

### TestApp Project

```
TestApp/
├── TestApp.cpp                 # Comprehensive test code (435+ symbols)
├── TestApp.vcxproj             # Project file (v143 toolset, C++20)
├── TestApp.vcxproj.filters     # Visual Studio filters
│
├── build.ps1 / build.bat       # Project build scripts
└── README.md                   # Test documentation
```

**Test Coverage:**
- Simple functions (various signatures)
- Function overloads (4+ versions)
- Static functions (should be PRIVATE)
- Classes with methods
- Virtual methods and inheritance
- Template functions and classes
- Namespaces (including nested)
- Operators
- Complex parameter types

## Build Outputs

### Solution Build vs. Project Build

**Solution Build** (`.\build-all.ps1`):
```
x64/Release/
  ├── ObfSymbolsEx.exe
  ├── msdia140.dll
  ├── TestApp.exe
  └── TestApp.pdb
```

**Individual Project Build** (`cd ObfSymbolsEx; .\build.ps1`):
```
ObfSymbolsEx/x64/Release/
  ├── ObfSymbolsEx.exe
  └── msdia140.dll
```

Both approaches work correctly and automatically copy the DIA SDK DLL.

## Validation Scripts

### `validate.ps1` / `validate.bat`
Comprehensive end-to-end validation script.

**What it does:**
1. Builds ObfSymbolsEx (Release x64)
2. Builds TestApp (Debug x64 for better symbols)
3. Runs ObfSymbolsEx on TestApp.pdb
4. Generates TestApp_symbols.sym
5. Analyzes and displays results
6. Checks for expected symbols

**Usage:**
```powershell
.\validate.ps1
.\validate.ps1 -Configuration Release -Platform x64
```

## File Organization

### Root Level
```
ObfSymbols/
├── ObfSymbolsEx.sln        # Classic solution
├── ObfSymbolsEx.slnx       # Modern solution
├── README.md               # Main documentation
├── SOLUTION_STRUCTURE.md   # This file
│
├── build-all.ps1/bat       # Solution build scripts
├── clean-all.ps1/bat       # Solution clean scripts
├── validate.ps1/bat        # Build-and-eyeball smoke test
├── unit-tests.ps1          # Pass/fail regression suite
│
├── ObfSymbolsEx/           # Main project folder
└── TestApp/                # Test project folder
```

### Documentation Files

1. **`README.md`** (root) - Complete solution overview and usage guide
2. **`ObfSymbolsEx/README.md`** - Detailed ObfSymbolsEx project documentation
3. **`ObfSymbolsEx/IMPLEMENTATION_NOTES.md`** - Technical implementation details
4. **`TestApp/README.md`** - Test application documentation
5. **`UNIT_TESTS.md`** (root) - What `unit-tests.ps1` covers and why
5. **`SOLUTION_STRUCTURE.md`** (this file) - Solution organization

## Build Configurations

Both projects support:
- **Configurations:** Debug, Release
- **Platforms:** x86, x64
- **Toolset:** v143 (Visual Studio 2022)
- **Language Standard:** C++20

## Opening in Visual Studio

### Visual Studio 2022
```powershell
# Open solution
.\ObfSymbolsEx.sln
# or
start ObfSymbolsEx.sln
```

Both projects will appear in Solution Explorer with all configurations available.

### Command Line Build
```cmd
# Using MSBuild directly
msbuild ObfSymbolsEx.sln /p:Configuration=Release /p:Platform=x64

# Using devenv
devenv ObfSymbolsEx.sln /build "Release|x64"
```

## Distribution

### Standalone ObfSymbolsEx

Package these two files:
```
ObfSymbolsEx.exe
msdia140.dll
```

Both files are automatically placed in `x64\Release\` after building.

### Full Development Package

Include:
```
ObfSymbolsEx/
  ├── Source code
  ├── Project files
  └── Build scripts
TestApp/
  ├── Test source
  └── Build scripts
Solution files
Documentation
```

## Key Improvements

### What Was Updated for Solution Support

1. ✅ **Solution Files Created**
   - Added `ObfSymbolsEx.sln` (classic format)
   - Updated `ObfSymbolsEx.slnx` to include TestApp

2. ✅ **Solution-Level Scripts**
   - `build-all.ps1` - Builds entire solution
   - `clean-all.ps1` - Cleans all artifacts
   - Handles both solution and project output paths

3. ✅ **Project Integration**
   - Both projects use v143 toolset
   - Consistent C++20 standard
   - Compatible DIA SDK paths
   - Auto-copy msdia140.dll in both contexts

4. ✅ **Documentation Updates**
   - Root README.md for solution overview
   - Project-specific READMEs
   - Solution structure documentation
   - Implementation notes

5. ✅ **Validation Integration**
   - `validate.ps1` works with solution structure
   - End-to-end testing of both projects
   - Symbol extraction verification

## Workflow Examples

### Full Development Workflow
```powershell
# 1. Clone/download the solution
cd ObfSymbols

# 2. Build everything
.\build-all.ps1

# 3. Validate
.\validate.ps1

# 4. Use the tool
.\x64\Release\ObfSymbolsEx.exe .\x64\Release\TestApp.pdb output.sym

# 5. View results
Get-Content output.sym
```

### Individual Project Development
```powershell
# Work on ObfSymbolsEx only
cd ObfSymbolsEx
.\build.ps1
.\x64\Release\ObfSymbolsEx.exe test.pdb output.sym

# Work on TestApp only
cd TestApp
.\build.ps1
.\x64\Debug\TestApp.exe
```

### Clean and Rebuild
```powershell
# Clean everything
.\clean-all.ps1

# Rebuild
.\build-all.ps1
```

## Summary

The solution is now fully integrated with:
- ✅ Dual solution file formats (classic + modern)
- ✅ Solution-level build and clean scripts
- ✅ Individual project build scripts
- ✅ Automatic msdia140.dll deployment
- ✅ Comprehensive validation suite
- ✅ Complete documentation
- ✅ No COM registration required
- ✅ Portable and standalone capable

Both individual project builds and solution-wide builds work correctly, with automatic DLL copying and clear output organization.

