# TestApp - Symbol Generation Test Application

A test application designed to generate a PDB file with various types of symbols for validating the ObfSymbolsEx tool.

## Purpose

This application contains a comprehensive set of C++ constructs to test ObfSymbolsEx' ability to extract different types of function symbols:

### Included Test Cases

1. **Simple Functions**
   - Functions with no parameters
   - Functions with return values
   - Functions with multiple parameters

2. **Function Overloads**
   - Same function name with different parameter types
   - Same function name with different parameter counts

3. **Template Functions**
   - Generic functions with template parameters
   - Multiple template instantiations

4. **Static Functions**
   - Functions with internal linkage (should appear as PRIVATE)

5. **Inline Functions**
   - Small functions that may be inlined

6. **Structs with Methods**
   - Constructors (default and parameterized)
   - Member functions
   - Operator overloads

7. **Classes with Various Member Types**
   - Public and private methods
   - Constructor overloads
   - Destructors
   - Const methods
   - Static methods
   - Method overloads

8. **Inheritance and Virtual Methods**
   - Base class with virtual methods
   - Derived classes overriding virtual methods
   - Pure virtual functions

9. **Template Classes**
   - Generic classes with multiple type instantiations

10. **Namespaces**
    - Functions within namespaces
    - Nested namespaces

11. **Complex Parameter Types**
    - References, pointers, const parameters
    - STL containers as parameters

12. **Special Functions**
    - Functions with default parameters
    - Variadic functions

## Building

### Using the Build Script (Recommended)

**PowerShell:**
```powershell
.\build.ps1
```

**Command Prompt:**
```cmd
build.bat
```

Options:
```powershell
.\build.ps1 -Configuration Debug -Platform x64
.\build.ps1 -Configuration Release -Platform x86
```

### Manual Build

From the TestApp directory:
```powershell
msbuild TestApp.vcxproj /p:Configuration=Debug /p:Platform=x64
```

## Output Files

After building, you'll find:
- `x64\Debug\TestApp.exe` - The executable
- `x64\Debug\TestApp.pdb` - The PDB file containing symbol information

## Using for Validation

### Option 1: Run the Validation Script (Recommended)

From the parent directory:
```powershell
.\validate.ps1
```

This will:
1. Build ObfSymbolsEx
2. Build TestApp
3. Run ObfSymbolsEx on TestApp.pdb
4. Display analysis of the results

### Option 2: Manual Validation

1. Build TestApp:
   ```powershell
   cd TestApp
   .\build.ps1
   ```

2. Run ObfSymbolsEx on the generated PDB:
   ```powershell
   cd ..\ObfSymbolsEx
   .\x64\Release\ObfSymbolsEx.exe ..\TestApp\x64\Debug\TestApp.pdb TestApp_symbols.sym
   ```

3. Review the output:
   ```powershell
   Get-Content TestApp_symbols.sym
   ```

## Expected Symbols

The PDB should contain symbols for:
- All non-inlined functions
- Class constructors and destructors
- Class member functions
- Overloaded functions (with name mangling)
- Template instantiations
- Virtual method tables (vtables)
- Namespace functions

## Verification Points

When validating ObfSymbolsEx output, verify:

1. **Visibility Detection**
   - Static functions should be marked PRIVATE
   - Non-static functions should be marked PUBLIC

2. **Overload Handling**
   - Different overloads of the same function should appear separately
   - Name mangling should be visible in the symbol names

3. **Class Methods**
   - Constructors, destructors, and member functions should be present
   - Virtual methods should appear for both base and derived classes

4. **Templates**
   - Template instantiations should appear with their specific types

5. **Address and Size**
   - All symbols should have valid RVA (Relative Virtual Address)
   - Function sizes should be non-zero for most functions

## Sample Output

Expected output format from ObfSymbolsEx:
```
PUBLIC 0x1000 45 main
PUBLIC 0x1050 32 SimpleFunction
PUBLIC 0x1070 64 ?OverloadedFunction@@YAXH@Z
PUBLIC 0x10B0 64 ?OverloadedFunction@@YAXN@Z
PRIVATE 0x10F0 48 StaticFunction
PUBLIC 0x1120 128 ?Add@Calculator@@QEAANN@Z
...
```

## Notes

- The application is functional and can be run to verify it works correctly
- All test functions are called from main() to ensure they're included in the final binary
- Debug builds typically preserve more symbol information than Release builds
- Some compiler optimizations may inline or remove certain functions

