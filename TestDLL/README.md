# TestDLL - DLL Symbol Generation Test

A test DLL designed to generate comprehensive PDB symbols for validating the ObfSymbolsEx tool with DLL files.

## Purpose

This DLL complements TestApp (EXE) to ensure ObfSymbolsEx correctly extracts symbols from both executable and library files.

## Exported Symbols

### C++ Classes

**MathOperations** - Comprehensive math operations class
- Constructors (default and parameterized)
- Destructor
- Public methods: Add, Subtract, Multiply, Divide, Power, Sqrt, Factorial
- Private helper: ValidateInput
- Overloaded Calculate methods
- Const method: GetLastResult
- Static methods: Pi(), E()

**Container<T>** - Template class with explicit instantiations
- Container<int>
- Container<double>
- Methods: Add, Get, GetSize, Clear

### Structs

**Point3D** - 3D point with operations
- Constructors
- Methods: Length, Normalize
- Operator overloads: +, -, *

### C Functions (extern "C")

- IntegerAdd
- IntegerMultiply
- DoubleAdd
- DoubleMultiply

### C++ Functions

- PrintMessage
- StringLength

### Namespace Functions

**Geometry namespace:**
- CircleArea
- CirclePerimeter
- RectangleArea
- TriangleArea

**Geometry::Advanced namespace:**
- SphereVolume
- CylinderVolume

## Symbol Export

All public symbols are exported using `__declspec(dllexport)` which makes them:
- **PUBLIC** symbols in the PDB
- Visible to ObfSymbolsEx extraction
- Available for import by other modules

## Building

```powershell
# Default: Debug x64
.\build.ps1

# Specify configuration
.\build.ps1 -Configuration Release -Platform x64
```

## Output Files

After building:
- `x64\Debug\TestDLL.dll` - The DLL
- `x64\Debug\TestDLL.pdb` - Symbol file
- `x64\Debug\TestDLL.lib` - Import library

## Testing with ObfSymbolsEx

```powershell
# Build the DLL
.\build.ps1

# Extract symbols
..\ObfSymbolsEx\x64\Release\ObfSymbolsEx.exe .\x64\Debug\TestDLL.pdb TestDLL_symbols.sym

# View results
Get-Content TestDLL_symbols.sym
```

## Expected Symbol Types

The PDB should contain:

1. **Exported Class Methods**
   - MathOperations::Add, Subtract, etc.
   - Container<int>::Add, Get, etc.
   - Container<double>::Add, Get, etc.

2. **Exported C Functions**
   - IntegerAdd, IntegerMultiply (no name mangling)
   - DoubleAdd, DoubleMultiply (no name mangling)

3. **Exported C++ Functions**
   - PrintMessage (with C++ name mangling)
   - StringLength (with C++ name mangling)

4. **Namespace Functions**
   - Geometry::CircleArea (with namespace in name)
   - Geometry::Advanced::SphereVolume (nested namespace)

5. **Template Instantiations**
   - Container<int> methods
   - Container<double> methods

## Validation Points

When testing with ObfSymbolsEx:

✅ **DLL-specific symbols** appear (exported functions)
✅ **C functions** show without name mangling
✅ **C++ functions** show with name mangling
✅ **Template instantiations** appear as concrete types
✅ **Namespace functions** include namespace in symbol name
✅ **Class methods** are properly identified
✅ **Private methods** may appear as PRIVATE or not at all

## Integration with TestApp

TestApp can optionally link against TestDLL to demonstrate:
- Symbol extraction from multiple modules
- DLL dependency in PDB analysis
- Import library usage

## Notes

- DLL exports are typically PUBLIC symbols
- Some compiler optimizations may affect symbol visibility
- Debug builds preserve more symbol information than Release builds
- The PDB contains both public and private symbols, but exports are always public

