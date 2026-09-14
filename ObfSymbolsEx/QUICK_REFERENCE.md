# ObfSymbolsEx Quick Reference

## Command

```bash
ObfSymbolsEx.exe <input.pdb|input.exe|input.dll> <output.sym>
```

A `.exe`/`.dll` input is loaded via DIA's `loadDataForExe`, which locates
the matching PDB itself (same directory as the binary, symbol server, or
local symbol cache) — decided purely from the extension, so a `.pdb` input
still goes through `loadDataFromPdb` exactly as before.

## Output Files

Every run writes six files:

| File | Purpose | Contents | Distribution |
|------|---------|----------|--------------|
| `output.sym` | **Mapping** | Obfuscated + Real Names | ❌ Keep Private |
| `output_obfuscated.sym` | **Production** | Obfuscated Only | ✅ Safe to Share |
| `output_vtable.sym` | VTable slots, one block per table | Obfuscated + Real Names | ❌ Keep Private |
| `output_vtable_obfuscated.sym` | VTable slots, one block per table | Obfuscated Only | ✅ Safe to Share |
| `output_vtable_classes.sym` | Virtual methods, one block per class | Real names only (no obfuscated variant) | ❌ Keep Private |
| `output_vtable_inheritance.sym` | Base-class tree, one block per class | Real names only (no obfuscated variant) | ❌ Keep Private |

See [VTABLE_RECONSTRUCTION.md](VTABLE_RECONSTRUCTION.md) for the vtable file formats and sort order.

## File Formats

### Mapping File (Full)
```
PUBLIC 0x11D00 SIZE=73 obf_A630929A __thiscall MathOperations::MathOperations(double)
       │      │   │              │        └─ Real name with signature
       │      │   │              └─ Calling convention (bare value, no KEY=)
       │      │   └─ Obfuscated name (hash-based)
       │      └─ Size in bytes
       └─ Relative Virtual Address
```

### Obfuscated File (Clean)
```
PUBLIC 0x11D00 SIZE=73 __thiscall obf_A630929A
       │      │      │        └─ Obfuscated name only
       │      │      └─ Calling convention (bare value, no KEY=)
       │      └─ Size in bytes
       └─ Relative Virtual Address
```

In the real file, `VISIBILITY`, `SIZE=N`, and `CALLING_CONVENTION` each get
extra trailing spaces so short values (`PUBLIC`, `SIZE=8`, `__cdecl`) line
up in a column with long ones (`PRIVATE`, `SIZE=29382`, `__thiscall`) —
these single-line examples don't show it since there's nothing to align
against. See `OUTPUT_FORMAT.md`'s Format History (Version 9.0) for the
real, aligned example.

## Obfuscation Algorithm

**Method:** FNV-1a Hash + RVA Mixing

```
obf_XXXXXXXX = FNV1a_Hash(FunctionName) ⊕ RVA
```

**Properties:**
- ✅ Non-reversible
- ✅ Deterministic
- ✅ Unique (no collisions)
- ✅ Fast (< 1ms for 10K functions)

## Common Use Cases

### Development
```bash
# Use mapping file for debugging
grep "MyFunction" output.sym
```

### Production
```bash
# Deploy obfuscated file only
cp output_obfuscated.sym release/symbols.sym
```

### Symbol Lookup
```bash
# Find real name from obfuscated
grep "obf_A630929A" output.sym
# Result: obf_A630929A MathOperations::MathOperations(double)
```

### Crash Analysis
```bash
# Customer reports: Crash at obf_A630929A+0x15
# Lookup in mapping file:
grep "obf_A630929A" output.sym
# Identify: MathOperations::MathOperations(double) at offset 0x15
```

## File Size

**Typical Reduction:** 50-66% smaller obfuscated files

| Symbols | Mapping File | Obfuscated File | Reduction |
|---------|--------------|-----------------|-----------|
| 217 | 14.2 KB | 7.0 KB | 50.7% |
| 435 | 41.8 KB | 14.0 KB | 66.5% |

## Security Guidelines

### ✅ DO
- Keep mapping file in secure location
- Distribute obfuscated file with releases
- Archive mapping files per version
- Use obfuscated file for customer support

### ❌ DON'T
- Never commit mapping file to public repos
- Never send mapping file to customers
- Never lose mapping files (archive them!)

## Build Integration

### PowerShell
```powershell
# Generate symbols
.\ObfSymbolsEx.exe build\MyApp.pdb symbols\MyApp.sym

# Organize
Move-Item symbols\MyApp.sym symbols\internal\
Move-Item symbols\MyApp_obfuscated.sym release\
```

### Bash
```bash
# Generate symbols
./ObfSymbolsEx build/myapp.pdb symbols/myapp.sym

# Organize
mv symbols/myapp.sym symbols/internal/
mv symbols/myapp_obfuscated.sym release/
```

## Lookup Tools

### PowerShell
```powershell
# Lookup obfuscated name
Get-Content symbols.sym | Where-Object { $_ -match "obf_A630929A" }
```

### Bash
```bash
# Lookup obfuscated name
grep "obf_A630929A" symbols.sym
```

### Python
```python
import re

def lookup(obf_name, mapping_file):
    with open(mapping_file) as f:
        for line in f:
            if obf_name in line:
                match = re.search(r'obf_[0-9A-F]{8}\s+(.+)$', line)
                if match:
                    return match.group(1)
    return None

# Usage
real_name = lookup('obf_A630929A', 'symbols.sym')
print(f"obf_A630929A -> {real_name}")
```

## Parsing Regex

This has drifted out of sync with the real format twice already as fields
were added (`SIZE=`, `IS_VIRTUAL`, `RETURN_TYPE`, ...), so **the verified,
line-by-line-tested regex lives in `OUTPUT_FORMAT.md`'s "Parsing the
Format" section** — copy it from there rather than from here. In short,
after the visibility/address/size/obf-name columns, every mapping-file line
also carries `-> RETURN_TYPE` and a fixed trailing `IS_VIRTUAL=...` block;
the obfuscated file only carries the trailing block, no name, signature, or
return type.

## Examples

### Complete Workflow

```bash
# 1. Build application
msbuild /p:Configuration=Release MyApp.sln

# 2. Generate symbol files
ObfSymbolsEx.exe build/Release/MyApp.pdb symbols/MyApp_v1.0.sym

# Output:
#   symbols/MyApp_v1.0.sym              (mapping)
#   symbols/MyApp_v1.0_obfuscated.sym   (obfuscated)

# 3. Secure mapping file
mv symbols/MyApp_v1.0.sym archive/internal/v1.0/

# 4. Package for release (obfuscated only)
cp symbols/MyApp_v1.0_obfuscated.sym release/MyApp.sym
zip release/MyApp_v1.0.zip release/*

# 5. Customer reports crash
# Crash dump shows: obf_FA692B1A+0x20

# 6. Lookup in archived mapping
grep "obf_FA692B1A" archive/internal/v1.0/MyApp_v1.0.sym
# Result: PUBLIC 0x16950 SIZE=73 obf_FA692B1A Calculator::Calculator(double)

# 7. Identify issue
# Bug in Calculator::Calculator(double) at offset 0x20
```

## Statistics (from validation)

**Total Symbols Tested:** 652
- TestDLL: 217 symbols (59 PUBLIC, 158 PRIVATE)
- TestApp: 435 symbols (128 PUBLIC, 307 PRIVATE)

**Hash Collisions:** 0
**Success Rate:** 100%

## Documentation

- `README.md` - Getting started
- `OBFUSCATION_ALGORITHM.md` - Hash algorithm details
- `OBFUSCATION_UPGRADE.md` - Migration from old format
- `OUTPUT_FORMAT.md` - File format specification
- `DUAL_OUTPUT_FILES.md` - Detailed dual-file guide
- `VTABLE_FEATURE.md` - Per-symbol VTable/thunk field reference
- `VTABLE_RECONSTRUCTION.md` - `_vtable.sym` / `_vtable_classes.sym` / `_vtable_inheritance.sym` format and sort order
- `CODE_ORGANIZATION.md` - Code structure
- `EMBEDDED_DLL_FEATURE.md` - Current `msdia140.dll` location strategy (embedded-resource approach was removed)

## Version

**Current Version:** 4.0
- Hash-based obfuscation (FNV-1a)
- Dual output files
- Function signatures
- Prefix: `obf_` (changed from `sub_`)

---

**© 2025 - ObfSymbolsEx**

