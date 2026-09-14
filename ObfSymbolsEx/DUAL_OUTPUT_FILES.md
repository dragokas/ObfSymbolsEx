# Dual Output Files Feature

## Overview

ObfSymbolsEx now automatically generates **two output files**:
1. **Mapping File** - Complete mapping with both obfuscated and real names
2. **Obfuscated File** - Clean obfuscated symbols without real names

This separation allows you to:
- Keep the mapping file **private** for internal use
- Distribute the obfuscated file for **production deployment**
- Maintain security while enabling symbol lookup

## Generated Files

When you run:
```bash
ObfSymbolsEx.exe myapp.pdb symbols.sym
```

Two files are created:
```
symbols.sym               ← Mapping file (keep private)
symbols_obfuscated.sym    ← Obfuscated file (can distribute)
```

ObfSymbolsEx additionally writes four VTable-reconstruction files
(`symbols_vtable.sym`, `symbols_vtable_obfuscated.sym`,
`symbols_vtable_classes.sym`, `symbols_vtable_inheritance.sym`) — the first
pair follows the same mapping/obfuscated split described here, while the
latter two are real-names-only files with no obfuscated counterpart.
See [VTABLE_RECONSTRUCTION.md](VTABLE_RECONSTRUCTION.md) for their format —
this document covers the function-symbol pair only.

## File Formats

### File 1: Mapping File (symbols.sym)

**Purpose:** Complete mapping for internal reference and debugging

**Format:**
```
VISIBILITY ADDRESS SIZE=N OBFUSCATED_NAME REAL_NAME(SIGNATURE) -> RETURN_TYPE
```

**Example:**
```
PUBLIC 0x11D00 SIZE=73 obf_A630929A MathOperations::MathOperations(double) -> void
PUBLIC 0x12EC0 SIZE=91 obf_B80DFA1D MathOperations::Add(double, double) -> double
PUBLIC 0x12F40 SIZE=85 obf_15846C9C MathOperations::Calculate(double) -> double
PRIVATE 0x18810 SIZE=112 obf_7086C891 OverloadedFunction(int) -> void
```

**Contains:**
- ✅ Obfuscated names
- ✅ Real function names
- ✅ Full signatures
- ✅ Return types
- ✅ All metadata

**Usage:**
- Internal debugging
- Symbol lookup/mapping
- Documentation
- **Keep this file secure!**

### File 2: Obfuscated File (symbols_obfuscated.sym)

**Purpose:** Clean obfuscated symbols for production use

**Format:**
```
VISIBILITY ADDRESS SIZE=N OBFUSCATED_NAME
```

**Example:**
```
PUBLIC 0x11D00 SIZE=73 obf_A630929A
PUBLIC 0x12EC0 SIZE=91 obf_B80DFA1D
PUBLIC 0x12F40 SIZE=85 obf_15846C9C
PRIVATE 0x18810 SIZE=112 obf_7086C891
```

**Contains:**
- ✅ Obfuscated names
- ✅ Metadata (address, size, visibility)
- ❌ No real function names
- ❌ No signatures
- ❌ No return types (same reasoning as names/signatures — see `OUTPUT_FORMAT.md`)

**Usage:**
- Production deployment
- Crash dump analysis (with obfuscated names)
- Performance profiling
- **Safe to distribute** - completely obfuscated

## Comparison

### Side-by-Side Example

**Mapping File (symbols.sym):**
```
PUBLIC 0x11D00 SIZE=73 obf_A630929A MathOperations::MathOperations(double)
PUBLIC 0x11D60 SIZE=62 obf_667FDFBA MathOperations::MathOperations()
PUBLIC 0x12EC0 SIZE=91 obf_B80DFA1D MathOperations::Add(double, double)
PUBLIC 0x132D0 SIZE=149 obf_8C3BC6B3 MathOperations::Divide(double, double)
```

**Obfuscated File (symbols_obfuscated.sym):**
```
PUBLIC 0x11D00 SIZE=73 obf_A630929A
PUBLIC 0x11D60 SIZE=62 obf_667FDFBA
PUBLIC 0x12EC0 SIZE=91 obf_B80DFA1D
PUBLIC 0x132D0 SIZE=149 obf_8C3BC6B3
```

**Key Differences:**
- Mapping: `obf_A630929A MathOperations::MathOperations(double)`
- Obfuscated: `obf_A630929A`

Both the real name and signature are **completely removed** for maximum obfuscation.

## File Size Comparison

From validation tests:

| File | Symbols | With Real Names | Obfuscated Only | Reduction |
|------|---------|-----------------|-----------------|-----------|
| TestDLL | 217 | 14.2 KB | 7.0 KB | **50.7%** |
| TestApp | 435 | 41.8 KB | 14.0 KB | **66.5%** |

**Average size reduction:** ~60% when removing real names and signatures

## Use Cases

### 1. Internal Development

Use the **mapping file**:
```bash
# Debugging with full names
grep "MathOperations::Add" symbols.sym
# Result: PUBLIC 0x12EC0 SIZE=91 obf_B80DFA1D MathOperations::Add(double, double)
```

### 2. Production Deployment

Use the **obfuscated file**:
```bash
# Deploy only obfuscated symbols
cp symbols_obfuscated.sym production/symbols.sym

# Keep mapping file secure
cp symbols.sym secure_storage/
```

### 3. Crash Dump Analysis

**Step 1:** Crash occurs with obfuscated symbol
```
Crash at: obf_B80DFA1D+0x15
```

**Step 2:** Lookup in mapping file (internal only)
```bash
grep "obf_B80DFA1D" symbols.sym
# Result: PUBLIC 0x12EC0 SIZE=91 obf_B80DFA1D MathOperations::Add(double, double)
```

**Step 3:** Identify real function
```
Crash occurred in: MathOperations::Add at offset 0x15
```

### 4. Customer Support

**Scenario:** Customer sends crash report with obfuscated symbols

**Customer sees:**
```
Stack trace:
  obf_FA692B1A+0x20
  obf_1561179B+0x08
  obf_C4D5FA4A+0x45
```

**Support team uses mapping file:**
```bash
grep -E "obf_FA692B1A|obf_1561179B|obf_C4D5FA4A" symbols.sym

# Results:
PUBLIC 0x16950 SIZE=73 obf_FA692B1A Calculator::Calculator(double)
PUBLIC 0x180D0 SIZE=88 obf_1561179B Calculator::Add(double)
PUBLIC 0x18200 SIZE=233 obf_C4D5FA4A Calculator::Calculate(double, double, char)
```

**Decoded stack trace:**
```
Stack trace:
  Calculator::Calculator(double)+0x20
  Calculator::Add(double)+0x08
  Calculator::Calculate(double, double, char)+0x45
```

### 5. Security-Conscious Distribution

**Scenario:** Software with proprietary algorithms

**Internal:**
```
symbols.sym (private):
PUBLIC 0x15000 SIZE=245 obf_A1B2C3D4 ProprietaryAlgorithm::EncryptionCore(byte*, int)
PUBLIC 0x15100 SIZE=189 obf_E5F6A7B8 ProprietaryAlgorithm::KeyGeneration(int)
```

**Distributed:**
```
symbols_obfuscated.sym (public):
PUBLIC 0x15000 SIZE=245 obf_A1B2C3D4(?, int)
PUBLIC 0x15100 SIZE=189 obf_E5F6A7B8(int)
```

Customers can use symbols for debugging but cannot see algorithm names.

## Workflow Example

### Development Phase

```bash
# Generate symbols during build
ObfSymbolsEx.exe myapp.pdb symbols.sym

# Two files created:
# - symbols.sym (mapping)
# - symbols_obfuscated.sym (obfuscated)

# Use mapping file for development debugging
gdb myapp -symbols symbols.sym
```

### Release Phase

```bash
# Package only obfuscated file
tar -czf myapp-release.tar.gz myapp.exe symbols_obfuscated.sym

# Archive mapping file securely
mv symbols.sym releases/v1.2.3/symbols-internal.sym
```

### Support Phase

```bash
# Customer sends crash dump with obfuscated names
# Support team uses mapping file to decode

# Automated lookup tool:
./decode_symbols.sh crash_dump.txt symbols.sym > decoded_crash.txt
```

## Security Best Practices

### ✅ DO

1. **Keep mapping file secure**
   - Store in restricted access locations
   - Use encryption for archived copies
   - Never include in customer deliveries

2. **Distribute obfuscated file**
   - Include with production builds
   - Safe for customer deployment
   - Enables debugging without exposing internals

3. **Version control**
   - Archive mapping files per release
   - Tag with build number/version
   - Maintain for support purposes

4. **Access control**
   - Limit mapping file access to dev/support teams
   - Use different access levels for different files
   - Audit access to mapping files

### ❌ DON'T

1. **Never publish mapping file**
   - Don't commit to public repos
   - Don't include in installers
   - Don't send to customers

2. **Don't mix them up**
   - Use clear naming conventions
   - Separate directories for each type
   - Automated checks in build process

3. **Don't lose mapping files**
   - Archive every release
   - Backup to secure locations
   - Document retention policy

## Integration Examples

### Build Script (PowerShell)

```powershell
# Build and generate symbols
msbuild /p:Configuration=Release MyApp.vcxproj

# Generate symbol files
.\ObfSymbolsEx.exe x64\Release\MyApp.pdb MyApp_v1.0.sym

# Separate files for different purposes
Move-Item MyApp_v1.0.sym archive/internal/
Move-Item MyApp_v1.0_obfuscated.sym release/symbols/

# Package for distribution (obfuscated only)
Compress-Archive -Path release/* -DestinationPath MyApp_v1.0.zip
```

### Build Script (Bash)

```bash
#!/bin/bash
# Build and generate symbols
make release

# Generate symbol files
./ObfSymbolsEx build/release/myapp.pdb myapp_v1.0.sym

# Organize files
mv myapp_v1.0.sym archive/internal/
mv myapp_v1.0_obfuscated.sym release/symbols/

# Create release package (obfuscated only)
tar -czf myapp_v1.0.tar.gz -C release .
```

### Symbol Lookup Tool (Python)

```python
#!/usr/bin/env python3
import sys
import re

def lookup_symbol(obf_name, mapping_file):
    """Look up real name from obfuscated name"""
    pattern = rf'^(PUBLIC|PRIVATE)\s+0x[0-9A-F]+\s+\d+\s+{obf_name}\s+(.+)$'
    
    with open(mapping_file, 'r') as f:
        for line in f:
            match = re.match(pattern, line.strip())
            if match:
                return match.group(2)  # Real name with signature
    return None

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: lookup_symbol.py <obf_name> <mapping_file>")
        sys.exit(1)
    
    obf_name = sys.argv[1]
    mapping_file = sys.argv[2]
    
    real_name = lookup_symbol(obf_name, mapping_file)
    if real_name:
        print(f"{obf_name} -> {real_name}")
    else:
        print(f"Symbol not found: {obf_name}")
```

**Usage:**
```bash
$ python lookup_symbol.py obf_A630929A symbols.sym
obf_A630929A -> MathOperations::MathOperations(double)
```

### Crash Dump Decoder (PowerShell)

```powershell
param(
    [string]$CrashDump,
    [string]$MappingFile
)

# Read mapping into hashtable
$symbolMap = @{}
Get-Content $MappingFile | ForEach-Object {
    if ($_ -match '^(PUBLIC|PRIVATE)\s+0x[0-9A-F]+\s+\d+\s+(obf_[0-9A-F]{8})\s+(.+)$') {
        $symbolMap[$Matches[2]] = $Matches[3]
    }
}

# Decode crash dump
Get-Content $CrashDump | ForEach-Object {
    $line = $_
    foreach ($obfName in $symbolMap.Keys) {
        if ($line -match $obfName) {
            $realName = $symbolMap[$obfName]
            $line = $line -replace $obfName, "$obfName ($realName)"
        }
    }
    Write-Output $line
}
```

**Usage:**
```powershell
.\decode_crash.ps1 -CrashDump crash.txt -MappingFile symbols.sym > decoded_crash.txt
```

## Statistics

From validation tests (652 total symbols):

### File Sizes

| Metric | TestDLL (217 symbols) | TestApp (435 symbols) |
|--------|----------------------|----------------------|
| Mapping file | 14.2 KB | 41.8 KB |
| Obfuscated file | 8.7 KB | 17.1 KB |
| **Reduction** | **38.7%** | **59.1%** |

### Size Per Symbol

| File Type | Avg Bytes/Symbol |
|-----------|------------------|
| Mapping file | ~75 bytes |
| Obfuscated file | ~42 bytes |
| **Difference** | ~33 bytes/symbol |

### Distribution

- **PUBLIC symbols:** ~28% (safe to expose)
- **PRIVATE symbols:** ~72% (internal implementation)

## Conclusion

The dual output file feature provides:

✅ **Security** - Keep real names private  
✅ **Flexibility** - Use appropriate file for each scenario  
✅ **Efficiency** - 40-60% smaller obfuscated files  
✅ **Traceability** - Full mapping available when needed  
✅ **Automation** - Both files generated automatically  

This enables a complete symbol management workflow from development through production deployment and customer support.

---

**Related Documentation:**
- `OBFUSCATION_ALGORITHM.md` - Hash algorithm details
- `OUTPUT_FORMAT.md` - File format specification
- `OBFUSCATION_UPGRADE.md` - Migration guide
- `VTABLE_RECONSTRUCTION.md` - The three additional VTable-reconstruction files

