# Validation Summary - DLL and EXE Symbol Testing

This document summarizes the comprehensive validation system for ObfSymbolsEx that tests both DLL and EXE symbol extraction.

## Test Projects

### 1. TestDLL (DLL Project)

**Purpose:** Validate symbol extraction from DLL files with exported functions

**Symbol Count:** 217 function symbols
- PUBLIC: 59 symbols
- PRIVATE: 158 symbols

**Test Coverage:**

#### Exported C++ Classes
- **MathOperations** - Full math library class
  - Constructors (default and parameterized)
  - Destructor
  - Public methods: Add, Subtract, Multiply, Divide, Power, Sqrt, Factorial
  - Private helper: ValidateInput
  - Overloaded Calculate methods
  - Const method: GetLastResult
  - Static methods: Pi(), E()

- **Container<T>** - Template class with explicit instantiations
  - Container<int> with methods: Add, Get, GetSize, Clear
  - Container<double> with methods: Add, Get, GetSize, Clear

#### Exported Structs
- **Point3D** - 3D point operations
  - Constructors
  - Methods: Length, Normalize
  - Operator overloads: +, -, *

#### C Functions (extern "C")
- IntegerAdd, IntegerMultiply
- DoubleAdd, DoubleMultiply

#### C++ Functions
- PrintMessage, StringLength (with name mangling)

#### Namespace Functions
- **Geometry::** CircleArea, CirclePerimeter, RectangleArea, TriangleArea
- **Geometry::Advanced::** SphereVolume, CylinderVolume

### 2. TestApp (EXE Project)

**Purpose:** Validate symbol extraction from executable files

**Symbol Count:** 435 function symbols
- PUBLIC: 128 symbols
- PRIVATE: 307 symbols

**Test Coverage:**

- Simple functions with various signatures
- Function overloads (4+ versions)
- Static functions (correctly marked PRIVATE)
- Template functions (int and double instantiations)
- Classes with constructors, destructors, methods
- Virtual methods and inheritance (Shape, Rectangle, Circle)
- Template classes with instantiations
- Namespaces (including nested: MathUtils::Advanced)
- Complex parameter types (STL containers, references, pointers)
- Operators

## Validation Script

**Location:** `validate.ps1` / `validate.bat`

**What It Does:**

1. **Builds ObfSymbolsEx** (Release x64)
   - Ensures the tool is up-to-date
   - Copies msdia140.dll for standalone use

2. **Builds TestDLL** (Debug x64)
   - Generates TestDLL.dll and TestDLL.pdb
   - Debug build preserves maximum symbol information

3. **Builds TestApp** (Debug x64)
   - Generates TestApp.exe and TestApp.pdb
   - Debug build preserves maximum symbol information

4. **Extracts DLL Symbols**
   - Runs ObfSymbolsEx on TestDLL.pdb
   - Generates TestDLL_symbols.sym
   - Verifies expected DLL-specific symbols

5. **Extracts EXE Symbols**
   - Runs ObfSymbolsEx on TestApp.pdb
   - Generates TestApp_symbols.sym
   - Verifies expected EXE-specific symbols

6. **Reports Results**
   - Symbol counts (total, PUBLIC, PRIVATE)
   - Sample symbols from each file
   - Verification of expected symbols
   - Summary comparison

## Running Validation

### Quick Validation
```powershell
.\validate.ps1
```

### Custom Configuration
```powershell
.\validate.ps1 -Configuration Release -Platform x64
```

## Expected Results

### DLL Symbols (TestDLL.pdb)
```
Output file: TestDLL_symbols.sym
Total symbols: 217
  - PUBLIC symbols: 59
  - PRIVATE symbols: 158
```

**Key Verifications:**
- ✅ MathOperations class methods
- ✅ Container template instantiations
- ✅ Point3D struct methods
- ✅ C function exports (IntegerAdd, DoubleAdd)
- ✅ Namespace functions (CircleArea, SphereVolume)

### EXE Symbols (TestApp.pdb)
```
Output file: TestApp_symbols.sym
Total symbols: 435
  - PUBLIC symbols: 128
  - PRIVATE symbols: 307
```

**Key Verifications:**
- ✅ SimpleFunction
- ✅ OverloadedFunction (4 versions)
- ✅ Calculator class methods
- ✅ Rectangle class methods
- ✅ Circle class methods
- ✅ main function

## Symbol Classification

### PUBLIC Symbols
Symbols marked PUBLIC typically include:
- Exported class methods (`__declspec(dllexport)`)
- Non-static functions with external linkage
- Virtual methods
- Class constructors and destructors (for exported classes)

### PRIVATE Symbols
Symbols marked PRIVATE typically include:
- Static functions
- Internal helper functions
- Some template instantiations
- Anonymous namespace functions
- Compiler-generated internal functions

## Validation Output Format

The validation script displays:

```
========================================
DLL Symbol Extraction Results (TestDLL)
========================================
Output file: TestDLL_symbols.sym
Total symbols: 217
  - PUBLIC symbols: 59
  - PRIVATE symbols: 158

Sample DLL symbols (first 15):
-----------------------------
PUBLIC 0x11C00 92 Container<int>::Container<int>
PUBLIC 0x11C80 92 Container<double>::Container<double>
PUBLIC 0x11D00 73 MathOperations::MathOperations
...

DLL-Specific Symbols to Verify:
-------------------------------
  [OK] Found symbols containing 'MathOperations' (17 occurrences)
  [OK] Found symbols containing 'IntegerAdd' (1 occurrences)
  [OK] Found symbols containing 'Point3D' (9 occurrences)
...
```

## Files Generated

After running validation:

- **TestDLL_symbols.sym** - DLL function symbols (217 lines)
- **TestApp_symbols.sym** - EXE function symbols (435 lines)

Both files use the format:
```
PUBLIC/PRIVATE ADDRESS SIZE NAME
```

Example:
```
PUBLIC 0x11D00 73 MathOperations::MathOperations
PRIVATE 0x125B0 59 MathOperations::operator=
PUBLIC 0x12EC0 91 MathOperations::Add
```

## Integration with Build

The `build-all.ps1` script builds all three projects:

```powershell
.\build-all.ps1
```

Output:
```
ObfSymbolsEx.exe: x64\Release\ObfSymbolsEx.exe
msdia140.dll:   x64\Release\msdia140.dll
TestApp.exe:    x64\Release\TestApp.exe
TestDLL.dll:    x64\Release\TestDLL.dll
```

## Manual Testing

### Test DLL Separately
```powershell
cd TestDLL
.\build.ps1
cd ..
.\ObfSymbolsEx\x64\Release\ObfSymbolsEx.exe .\TestDLL\x64\Debug\TestDLL.pdb dll_output.sym
Get-Content dll_output.sym
```

### Test EXE Separately
```powershell
cd TestApp
.\build.ps1
cd ..
.\ObfSymbolsEx\x64\Release\ObfSymbolsEx.exe .\TestApp\x64\Debug\TestApp.pdb exe_output.sym
Get-Content exe_output.sym
```

## Success Criteria

Validation passes when:

1. ✅ All projects build without errors
2. ✅ ObfSymbolsEx successfully processes both PDB files
3. ✅ DLL symbols are extracted (200+ symbols)
4. ✅ EXE symbols are extracted (400+ symbols)
5. ✅ All expected symbol patterns are found
6. ✅ PUBLIC and PRIVATE classifications are reasonable
7. ✅ No crashes or errors during extraction

## Troubleshooting

### Build Warnings
The TestDLL project may show harmless warnings:
- `C4005`: Macro redefinition of TESTDLL_EXPORTS (expected)
- `C4910`: Template instantiation warnings (expected)

These warnings don't affect functionality.

### Symbol Count Variations
Symbol counts may vary slightly based on:
- Debug vs Release builds
- Compiler optimizations
- Template instantiation specifics
- Compiler version

The validation checks for symbol patterns, not exact counts.

## Continuous Validation

For development workflow:

```powershell
# After code changes to ObfSymbolsEx
cd ObfSymbolsEx
.\build.ps1
cd ..

# Run validation
.\validate.ps1

# Check both output files
Get-Content TestDLL_symbols.sym | Select-Object -First 20
Get-Content TestApp_symbols.sym | Select-Object -First 20
```

## Conclusion

The validation system ensures ObfSymbolsEx correctly:
- ✅ Loads msdia140.dll dynamically
- ✅ Parses both DLL and EXE PDB files
- ✅ Extracts function symbols with correct attributes
- ✅ Classifies symbols as PUBLIC or PRIVATE
- ✅ Preserves name mangling and signatures
- ✅ Handles templates, namespaces, and complex types
- ✅ Works standalone without Visual Studio on target machines

Total test coverage: **652 function symbols** across DLL and EXE files.

