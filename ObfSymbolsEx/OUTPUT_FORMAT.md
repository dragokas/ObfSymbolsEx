# ObfSymbolsEx Output Format

## Current Format

Each line in the output `.sym` file represents one function or thunk symbol with the following format:

```
VISIBILITY ADDRESS SIZE=N OBFUSCATED_NAME CALLING_CONVENTION REAL_NAME(SIGNATURE) -> RETURN_TYPE <trailing KEY=VALUE fields>
```

In the obfuscated file, `CALLING_CONVENTION` moves one slot earlier — right
before `OBFUSCATED_NAME` instead of after it — since there is no real name
for it to sit in front of there:

```
VISIBILITY ADDRESS SIZE=N CALLING_CONVENTION OBFUSCATED_NAME <trailing KEY=VALUE fields>
```

### Column Descriptions

| Column | Description | Example |
|--------|-------------|---------|
| **VISIBILITY** | `PUBLIC` or `PRIVATE` - indicates symbol visibility | `PUBLIC` |
| **ADDRESS** | Hexadecimal RVA (Relative Virtual Address) with `0x` prefix | `0x00011D00` |
| **SIZE** | Decimal size of the function in bytes | `SIZE=73` |
| **OBFUSCATED_NAME** | FNV-1a hash of name + RVA (format: `obf_XXXXXXXX`) — see `OBFUSCATION_ALGORITHM.md` | `obf_A630929A` |
| **CALLING_CONVENTION** | Calling convention reported by DIA on the function's type (`get_callingConvention`, a `CV_call_e` value) — e.g. `__thiscall`, `__cdecl`, `__stdcall`, `__fastcall`. A bare value with no `KEY=` prefix, sitting immediately before whichever name identifies the function on this line — `REAL_NAME` in the mapping file, `OBFUSCATED_NAME` in the obfuscated file (see the two format lines above). It is ABI metadata, not name-revealing, so it appears in **both** files. `?` if DIA can't provide it; an unrecognized `CV_call_e` code (not one of the known enumerators) prints as `CV_CALL_0xNN` instead of `?`, since the enum is small and fully known | `__thiscall` |
| **REAL_NAME** | Original function name from PDB (mapping file only) | `MathOperations::MathOperations` |
| **SIGNATURE** | Function parameter types in parentheses (mapping file only) | `(double)` |
| **RETURN_TYPE** | Function return type, preceded by `-> ` (mapping file only) | `-> void` |

`RETURN_TYPE` is resolved via the same `GetTypeName()` machinery as each
parameter in `SIGNATURE` — `pFunctionType->get_type()` gives the function
type's own return-type symbol, one property deeper than the
`SymTagFunctionArgType` children `SIGNATURE` already walks. `GetTypeName()`
resolves, by construction:

- Basic types (`int`, `bool`, `void`, `float`, `double`, `char16_t`,
  `char32_t`, COM types like `BSTR`/`HRESULT`, ...)
- Named types with their own DIA symbol name (classes, structs, enums,
  typedefs, templates — e.g. `QAngle`, `CUtlVector<int>`)
- Pointers and references to any of the above, including
  cv-qualification on the pointee (`const QAngle &`, `CBaseEntity *`,
  `const char *`)
- Fixed-size arrays (`float[3]`)
- Function pointers and pointer-to-member-function types, spelled out in
  full including their own calling convention and parameter list (e.g.
  `void * (__cdecl *)(const char *, int *)`, or
  `void (CBaseEntity::*)(void)` for a pointer-to-member-function)
- The `...` of a variadic function's parameter list

A type still resolves to `?` only when DIA gives no usable information at
all for it — see "Type Resolution (`GetTypeName()`)" below for the full list
of DIA shapes `GetTypeName()` understands and why each one previously fell
through to `?` before being added. Like `REAL_NAME` and
`SIGNATURE`, `RETURN_TYPE` is **omitted entirely** from the obfuscated file —
a specific return type (e.g. a pointer to a particular game-object class)
can hint at a function's purpose almost as much as its name can, so it gets
the same treatment. `CALLING_CONVENTION` is the one exception to that rule:
it carries no naming information, so unlike `REAL_NAME`/`SIGNATURE`/
`RETURN_TYPE` it is **not** omitted from the obfuscated file.

### Trailing Fields

Every line — not just virtual functions — ends with this fixed block of
`KEY=VALUE` fields, in this order:

| Field | Description | Non-applicable value |
|-------|-------------|-----------------------|
| **IS_VIRTUAL** | `1` if DIA reports this symbol as a virtual function (`get_virtual`), else `0`. Added so a virtual method can be identified directly rather than inferred from `VTABLE_SHAPE` being non-zero, which is not reliable — DIA frequently does not expose a shape ID for a method directly (see `VTABLE_FEATURE.md`) even when it is genuinely virtual. | `0` |
| **VTABLE_OFFSET** | Byte offset in the virtual function table reported by DIA (`get_virtualBaseOffset`) | `-1` |
| **VTABLE_INDEX** | Vtable slot: `VTABLE_OFFSET / target pointer size` | `-1` |
| **VTABLE_CLASS** | Enclosing class reported by DIA (`get_classParent`). **Can itself contain a space** for a template class with a pointer/reference argument (e.g. `CDataManager<CBoneCache *,...>` — `GetTypeName()` puts a space before `*`); a naive whitespace-split parser must not assume this or any other field value is a single token — anchor on the known field names instead (see the Python example below) | empty |
| **VTABLE_INTRO** | `1` if DIA marks this as introducing a virtual slot (`get_intro`), else `0` | `0` |
| **VTABLE_SHAPE** | DIA VTableShape symbol ID. An internal DIA `SymIndexId`, not a stable identifier — see the note in `VTABLE_RECONSTRUCTION.md`'s "Header fields" section | `0` |
| **VTABLE_SLOTS** | Number of entries in that VTableShape | `0` |
| **THIS_ADJUST** | DIA `this`-adjustment for the method | `0` |
| **KIND** | `FUNCTION`, `THUNK`, `DESTRUCTOR`, `DELETING_DESTRUCTOR`, or `VECTOR_DELETING_DESTRUCTOR` (classified from DIA's friendly name, presentation only — see `VTABLE_FEATURE.md`) | `FUNCTION` |
| **THUNK** | `1` if this record is a `SymTagThunk` (adjustor/vtable thunk), else `0` | `0` |
| **THUNK_ORDINAL** | DIA thunk ordinal | `0` |
| **THUNK_TARGET_RVA** | RVA the thunk jumps to | `0x0` |
| **SOURCE_FILE** | `File.cpp:123` — the source file and line number of the first line record DIA has for this symbol's RVA range (`IDiaSession::findLinesByRVA`), or `?` if DIA has no line information there, **or** if it does but that information cannot be trusted for this specific symbol — see "SOURCE_FILE trust and identical code folding" below. **Mapping file only** — unlike every other trailing field above, it is omitted entirely from the obfuscated file, for the same reason `REAL_NAME`/`SIGNATURE`/`RETURN_TYPE` are: a source path (e.g. `ai_behavior_lead.cpp`) can reveal as much about a function's purpose as its name can. Only the filename is kept, not the full build-machine path DIA reports (e.g. `d:\build\...\ai_behavior_lead.cpp` becomes just `ai_behavior_lead.cpp`) | `?` |

See `VTABLE_FEATURE.md` for how these fields are derived and their multiple-inheritance caveats.

### Complete Example

```
PUBLIC 0x11D00 SIZE=73 obf_A630929A __thiscall MathOperations::MathOperations(double) -> void IS_VIRTUAL=0 VTABLE_OFFSET=-1 VTABLE_INDEX=-1 VTABLE_CLASS= VTABLE_INTRO=0 VTABLE_SHAPE=0 VTABLE_SLOTS=0 THIS_ADJUST=0 KIND=FUNCTION THUNK=0 THUNK_ORDINAL=0 THUNK_TARGET_RVA=0x0 SOURCE_FILE=MathOperations.cpp:12
```

Segment by segment:

| Segment | Meaning |
|---------|---------|
| `PUBLIC` | Visibility |
| `0x11D00` | Address (RVA) |
| `SIZE=73` | Size (bytes) |
| `obf_A630929A` | Obfuscated Name |
| `__thiscall` | Calling Convention (bare value, no `KEY=` prefix) |
| `MathOperations::MathOperations` | Real Name |
| `(double)` | Signature |
| `-> void` | Return Type |
| everything from `IS_VIRTUAL=` onward | Trailing field block (below) |

Every line — virtual or not — carries the full trailing field block, **with
one exception**: `SOURCE_FILE` (the last field) is mapping-file only and
does not appear at all in the obfuscated file — every other trailing field
does. See "Trailing Fields" above for the complete, current list;
non-applicable values are `-1` (offsets/indices), `0` (counts/flags), or an
empty string (`VTABLE_CLASS=`) rather than being omitted. `RETURN_TYPE` and
`SOURCE_FILE` themselves fall back to `?` (never omitted, in the files where
they appear) when DIA's data isn't resolvable, same as an unresolved
parameter type in `SIGNATURE`. See `VTABLE_FEATURE.md` for how the trailing
fields are derived and their multiple-inheritance caveats.

## Type Resolution (`GetTypeName()`)

`RETURN_TYPE` and each parameter type in `SIGNATURE` are both resolved by
the same `GetTypeName()` function, walking whatever DIA symbol shape
represents that type. Every shape it recognizes:

| DIA shape | Example | Resolved as |
|-----------|---------|-------------|
| `SymTagBaseType` | `int`, `float`, `char16_t`, `BSTR` | The mapped keyword — see the source for the full `BasicType` table |
| `SymTagUDT` / `SymTagEnum` / `SymTagTypedef` | `QAngle`, `CUtlVector<int>` | DIA's own `get_name()` on the symbol, unmodified |
| `SymTagPointerType` (`get_reference()==FALSE`) | `CBaseEntity *`, `const char *` | Pointee's own resolution, plus `const ` if the pointee is const-qualified, plus a trailing ` *` |
| `SymTagPointerType` (`get_reference()==TRUE`) | `const QAngle &` | Same as above, with a trailing ` &` instead of ` *` |
| `SymTagArrayType` | `float[3]` | Element type's own resolution plus `[N]` (`get_count()`), or `[]` if DIA doesn't give a count |
| `SymTagPointerType` → `SymTagFunctionType` | `void * (__cdecl *)(const char *, int *)` | Full C-style function-pointer spelling: return type, calling convention, `(*)`, and parameter list — see `BuildFunctionPointerTypeName()` |
| Same, with `get_classParent()` set on the pointer | `void (CBaseEntity::*)(void)` | Same as above but `(ClassName::*)` instead of `(*)` — a pointer-to-member-function, e.g. Source engine's `typedef void (CBaseEntity::*BASEPTR)(void)` pattern |
| `SymTagBaseType` with `baseType==btNoType` | `...` | The variadic ellipsis parameter of a function like `CUtlBuffer::Scanf(const char*, ...)` — DIA gives it a real parameter slot with this base type rather than omitting it |

### Why this exists

The single most common cause of a `?` return type before this was a
reference-to-class return, the extremely common C++ pattern
`const Foo& SomeGetter()`. DIA represents `T&`/`T*` with no `get_name()` of
their own — a bare `SymTagPointerType` wrapping the real type, distinguished
from a plain pointer only by `get_reference()` — so without unwrapping it,
`GetTypeName()`'s original two-step "try `get_name()`, then try
`get_baseType()`" logic found neither and fell through to `?` for every
such return type and every reference/pointer-typed parameter.

This was diagnosed, not guessed: `CASW_Marine::ASWEyeAngles()` (declared
`virtual const QAngle& ASWEyeAngles( void )`, `QAngle` from `vector.h`) was
confirmed present in `server.pdb` and instrumented directly — dumping DIA's
`symTag`/`get_name()`/`get_baseType()`/`get_constType()`/`get_reference()`
chain for its return type showed exactly `SymTagPointerType(reference=1)`
wrapping `SymTagUDT(name="QAngle", const=1)`. The same instrumentation
approach, pointed at other real `?` occurrences in the same PDB, is what
found the remaining shapes above (function pointers, pointer-to-member
functions, the `...` ellipsis, and `char16_t`/`char32_t`) one at a time,
each backed by a concrete example function, rather than added speculatively.

**Verified impact**, extracting `server.pdb` (39501 symbols) before and
after: unresolved return types dropped from 5695 to 0, and unresolved
signature parameters dropped from 12080 to 0. A line-by-line diff against
the pre-fix output, keyed by each symbol's stable `obf_XXXXXXXX` identity,
confirmed **zero** previously-resolved value changed to something else —
every difference was a `?` becoming a real type, nothing else.

### Residual `?` cases

`GetTypeName()` still falls back to `?` for a DIA type shape genuinely
outside the table above — for example `SymTagCustomType` or a bitfield
specification. None occurred in the verification PDB; the fallback exists
for correctness (never guess a type name), not because such cases are
expected to be common. If one turns up, the same instrumentation-first
approach used above — confirm the exact DIA shape before writing a
resolution for it — is how to add support for it.

## SOURCE_FILE Trust and Identical Code Folding

`SOURCE_FILE` resolves via `IDiaSession::findLinesByRVA(rva, length)` —
"what line info exists at this RVA" — which is a fundamentally different
question from "what line info belongs to this symbol". Those two questions
have the same answer for the overwhelming majority of functions, but not
always, because of identical code folding (ICF): when the linker collapses
several functions with byte-identical compiled bodies into one physical
RVA, DIA can still only report one line record for that address. That
record is only actually correct for whichever *one* of the folded functions
the linker happened to retain debug info for — silently wrong, not missing,
for every other symbol name sharing that RVA.

This was confirmed directly against `server.pdb`: `CBaseSpriteProjectile::Precache()`
and `CBaseSpriteProjectile::HandleThink()` (and, it turns out, dozens of
other completely unrelated trivial one-line overrides across unrelated
classes — `CBaseEntity::EndBlocked()`, `CASW_Queen::AlertSound()`,
`CBaseGameStats::Event_Credits()`, ...) all share one 3-byte RVA and all
resolved to the identical `dt_send.cpp:290` before this fix — a location
that is, at best, correct for one of them and wrong for the rest, since
none of these methods are declared anywhere near `dt_send.cpp`.

### The fix: a trust pass keyed on `symbolByRvaAndName`

ObfSymbolsEx already builds a `(RVA → {qualified names})` map during
extraction to survive this exact ICF phenomenon for `VTABLE_*` field
merging (see `VTABLE_FEATURE.md`'s "Identical-code-folded RVAs" section).
After all symbols are extracted, a second pass reuses that same map purely
to judge `SOURCE_FILE` trust: for every RVA shared by two or more symbols,
if their **unqualified names** (the part after the last `::`, with the
symbol's own top-level template arguments stripped — so `Precache` vs
`HandleThink` compare unequal, but `Grow` vs `Grow` from two different
`CUtlMemory<T,int>` instantiations compare equal) are not all identical,
`SOURCE_FILE` is reset to `?` for **every** symbol sharing that RVA — the
resolved value cannot be verified as belonging to any specific one of them,
so it is withdrawn rather than left looking authoritative for all of them.

This deliberately does **not** touch the extremely common case of a
class-template method folded across many instantiations (every
`CUtlMemory<T,int>::Grow(int)`, every `CUtlVector<T,...>::InsertBefore(...)`,
...): those genuinely share one definition site (the template body in a
header), so the one file/line DIA reports is equally correct for all of
them and is left alone. Verified: `CUtlMemory<CFlexAnimationTrack *,int>::Grow(int)`
and every other `Grow` instantiation sharing its RVA still correctly report
`utlmemory.h:702` after this pass, while `CBaseSpriteProjectile::Precache()`
now correctly reports `?` instead of the borrowed `dt_send.cpp:290`.

**Residual limitation:** a small number of RVAs have no line record at all
even after a symbol is confirmed unique at that address (`CBaseSpriteProjectile::Think()`,
RVA `0x1F8B40`'s sibling case, is one) — `findLinesByRVA` succeeds but
returns zero records. This means the surviving ICF instance's own debug
line info was never retained (most likely folded against code from a
compiland, or a statically-linked library, that itself carries no line
table), and there is no further DIA query that can recover it. This
remains an honest `?`, same as before.

**Verified impact**, extracting `server.pdb` again after this pass: 3177
previously-resolved `SOURCE_FILE` values were reset to `?`. A line-by-line
diff against the pre-fix output, keyed by each symbol's `obf_XXXXXXXX`
identity and ignoring only the `SOURCE_FILE=` value itself, confirmed
**zero** unexpected differences — this pass touches nothing else.

## Obfuscated Name Generation

### Algorithm: FNV-1a Hash with RVA Mixing

The obfuscated name is generated using a cryptographic-quality hash that combines the function name with its RVA:

```cpp
// FNV-1a hash algorithm with RVA mixing
std::wstring GenerateObfuscatedName(const std::wstring& realName, DWORD rva) {
    const uint32_t FNV_PRIME = 0x01000193;
    const uint32_t FNV_OFFSET_BASIS = 0x811C9DC5;
    
    uint32_t hash = FNV_OFFSET_BASIS;
    
    // Hash the function name
    for (wchar_t c : realName) {
        hash ^= (c & 0xFF);
        hash *= FNV_PRIME;
        hash ^= ((c >> 8) & 0xFF);
        hash *= FNV_PRIME;
    }
    
    // Mix in RVA for uniqueness
    hash ^= rva;
    hash *= FNV_PRIME;
    hash ^= (rva >> 16);
    hash *= FNV_PRIME;
    
    // Format: obf_XXXXXXXX
    wchar_t obfName[32];
    swprintf_s(obfName, 32, L"obf_%08X", hash);
    return obfName;
}
```

### Examples

| Real Name | RVA | Obfuscated Name |
|-----------|-----|-----------------|
| `MathOperations::MathOperations(double)` | `0x11D00` | `obf_A630929A` |
| `Calculator::Add(double)` | `0x180D0` | `obf_1561179B` |
| `OverloadedFunction(int)` | `0x18810` | `obf_7086C891` |
| `OverloadedFunction(double)` | `0x18970` | `obf_ADBD1271` |

### Properties

✅ **Non-Reversible** - Cannot extract function name or address from hash  
✅ **Deterministic** - Same function name + RVA always produces same hash  
✅ **Unique** - RVA mixing ensures no collisions even for identically named functions  
✅ **Fast** - FNV-1a is optimized for speed  
✅ **Industry Proven** - FNV-1a widely used in production systems  

## Example Output

### Simple Functions

```
PRIVATE 0x13EA0 SIZE=64 obf_9E7C8A1F DoubleAdd(double, double)
PRIVATE 0x13F40 SIZE=60 obf_C3E5B892 IntegerAdd(int, int)
PUBLIC 0x13EF0 SIZE=57 obf_7A2D4B58 CircleArea(double)
PUBLIC 0x14240 SIZE=73 obf_E9F16C3A SphereVolume(double)
```

### Overloaded Functions

Notice how each overload gets a unique hash despite having the same base name:

```
PRIVATE 0x18790 SIZE=102 obf_8811AE11 OverloadedFunction(?)
PRIVATE 0x18810 SIZE=112 obf_7086C891 OverloadedFunction(int)
PRIVATE 0x188A0 SIZE=157 obf_923AF0C1 OverloadedFunction(int, int)
PRIVATE 0x18970 SIZE=116 obf_ADBD1271 OverloadedFunction(double)
```

**Key Feature:** The hash combines function name + RVA, so identical names at different addresses produce different hashes.

### Class Methods

```
PUBLIC 0x11D00 SIZE=73 obf_A630929A MathOperations::MathOperations(double)
PUBLIC 0x11D60 SIZE=62 obf_667FDFBA MathOperations::MathOperations()
PUBLIC 0x123B0 SIZE=41 obf_5B6754E8 MathOperations::~MathOperations()
PUBLIC 0x12EC0 SIZE=91 obf_B80DFA1D MathOperations::Add(double, double)
PUBLIC 0x12F40 SIZE=85 obf_15846C9C MathOperations::Calculate(double)
PUBLIC 0x132D0 SIZE=149 obf_8C3BC6B3 MathOperations::Divide(double, double)
```

### Template Instantiations

```
PRIVATE 0x12C70 SIZE=60 obf_EE35F0A1 TemplateFunction<int>(int, int)
PRIVATE 0x12CC0 SIZE=64 obf_ADD08ABB TemplateFunction<double>(double, double)
PUBLIC 0x11C00 SIZE=92 obf_0405287A Container<int>::Container<int>()
PUBLIC 0x11C80 SIZE=92 obf_A9529B0A Container<double>::Container<double>()
```

### Constructors and Destructors

```
PUBLIC 0x16950 SIZE=73 obf_FA692B1A Calculator::Calculator(double)
PUBLIC 0x17830 SIZE=41 obf_AC9ABA68 Calculator::~Calculator()
PUBLIC 0x11DB0 SIZE=125 obf_B107EA62 Point3D::Point3D(double, double, double)
PUBLIC 0x11E50 SIZE=92 obf_68AAC802 Point3D::Point3D()
```

### Functions with Complex Signatures

```
PUBLIC 0x18200 SIZE=233 obf_C4D5FA4A Calculator::Calculate(double, double, char)
PUBLIC 0x18BA0 SIZE=66 obf_3F8A21C0 Calculator::Reset(double)
PUBLIC 0x18C00 SIZE=55 obf_9A1B7E44 Calculator::Reset()
```

## Use Cases

### 1. Symbol Mapping Table

The format provides a mapping between obfuscated and real names:

```
obf_A630929A -> MathOperations::MathOperations(double)
obf_7086C891 -> OverloadedFunction(int)
obf_ADBD1271 -> OverloadedFunction(double)
```

**Security Note:** The hash-based obfuscation makes reverse lookup computationally expensive without the mapping file.

### 2. Deobfuscation

When analyzing binaries or crash dumps with obfuscated names, this file allows you to:
- Look up the real function name from an obfuscated name
- Find the original signature from the address
- Understand the full context of the function

### 3. Symbol Substitution

Can be used to replace real names with obfuscated names in:
- Debug symbols
- Error messages
- Stack traces
- Profiler output

### 4. Binary Analysis

Provides context for reverse engineering:
- Function sizes help understand complexity
- Signatures help understand calling conventions
- Visibility helps understand export tables

## Parsing the Format

### Regular Expression

The current output uses the `obf_` prefix, and every line carries the
trailing field block, so a robust pattern only anchors the fixed-position
columns and leaves the trailing fields unparsed (or parsed separately as
`KEY=VALUE` tokens):

```regex
^(PUBLIC|PRIVATE)\s+(0x[0-9A-F]+)\s+SIZE=(\d+)\s+(obf_[0-9A-F]{8})\s+(\S+)\s+(.+?)(\(.*\))\s+->\s+(.*?)\s+IS_VIRTUAL=
```

### Capture Groups

1. Visibility: `(PUBLIC|PRIVATE)`
2. Address: `(0x[0-9A-F]+)`
3. Size: `(\d+)` (preceded by the literal `SIZE=`)
4. Obfuscated Name: `(obf_[0-9A-F]{8})`
5. Calling Convention: `(\S+)` — a single token (`__thiscall`, `__cdecl`,
   `?`, ...), never containing spaces, so it doesn't need the same
   non-greedy `.+?` treatment as the fields around it
6. Real Name: `(.+?)`
7. Signature: `(\(.*\))`
8. Return Type: `(.*?)` — everything between `-> ` and the trailing block
   (non-greedy, since a return type can itself contain spaces, e.g.
   `unsigned int` or a pointer type like `CBaseEntity *`)
9. Everything from `IS_VIRTUAL=` onward: trailing `KEY=VALUE` fields (see
   "Trailing Fields" above)

### Example Code (Python)

```python
import re

pattern = r'^(PUBLIC|PRIVATE)\s+(0x[0-9A-F]+)\s+SIZE=(\d+)\s+(obf_[0-9A-F]{8})\s+(\S+)\s+(.+?)(\(.*\))\s+->\s+(.*?)\s+(IS_VIRTUAL=.*)$'

# VTABLE_CLASS's own value can itself contain a space (a template class with
# a pointer argument, e.g. "CDataManager<CBoneCache *,...>" -- GetTypeName()
# puts a space before "*"), so splitting the trailing block on whitespace
# alone is not safe. Anchor on the fixed, known field names instead.
# CALLING_CONVENTION is NOT one of these -- it's a bare positional column
# (group 5 above), already captured by the main pattern, not a KEY=VALUE
# field in the trailing block.
FIELD_NAMES = ['IS_VIRTUAL', 'VTABLE_OFFSET', 'VTABLE_INDEX', 'VTABLE_CLASS',
               'VTABLE_INTRO', 'VTABLE_SHAPE', 'VTABLE_SLOTS', 'THIS_ADJUST',
               'KIND', 'THUNK', 'THUNK_ORDINAL', 'THUNK_TARGET_RVA', 'SOURCE_FILE']
field_pattern = re.compile(
    r'(' + '|'.join(FIELD_NAMES) + r')=(.*?)(?=\s+(?:' + '|'.join(FIELD_NAMES) + r')=|$)')

with open('symbols.sym', 'r') as f:
    for line in f:
        match = re.match(pattern, line.strip())
        if match:
            visibility, address, size, obf_name, calling_convention, real_name, signature, return_type, trailing = match.groups()
            fields = dict(field_pattern.findall(trailing))
            print(f"{obf_name} -> {return_type} {calling_convention} {real_name}{signature} "
                  f"(VTABLE_INDEX={fields.get('VTABLE_INDEX')}, SOURCE_FILE={fields.get('SOURCE_FILE')})")
```

### Example Code (PowerShell)

```powershell
Get-Content symbols.sym | ForEach-Object {
    if ($_ -match '^(PUBLIC|PRIVATE)\s+(0x[0-9A-F]+)\s+SIZE=(\d+)\s+(obf_[0-9A-F]{8})\s+(\S+)\s+(.+?)(\(.*\))\s+->\s+(.*?)\s+(IS_VIRTUAL=.*)$') {
        $obfName = $Matches[4]
        $callingConvention = $Matches[5]
        $realName = $Matches[6]
        $signature = $Matches[7]
        $returnType = $Matches[8]
        Write-Host "$obfName -> $returnType $callingConvention $realName$signature"
    }
}
```

### Example Code (C++)

```cpp
#include <regex>
#include <string>
#include <fstream>

struct SymbolEntry {
    std::string visibility;
    std::string address;
    int size;
    std::string obfuscatedName;
    std::string callingConvention;
    std::string realName;
    std::string signature;
    std::string returnType;
};

std::vector<SymbolEntry> ParseSymFile(const std::string& filename) {
    std::vector<SymbolEntry> symbols;
    std::ifstream file(filename);
    std::string line;
    
    std::regex pattern(R"(^(PUBLIC|PRIVATE)\s+(0x[0-9A-F]+)\s+SIZE=(\d+)\s+(obf_[0-9A-F]{8})\s+(\S+)\s+(.+?)(\(.*\))\s+->\s+(.*?)\s+IS_VIRTUAL=.*)");
    
    while (std::getline(file, line)) {
        std::smatch match;
        if (std::regex_match(line, match, pattern)) {
            SymbolEntry entry;
            entry.visibility = match[1];
            entry.address = match[2];
            entry.size = std::stoi(match[3]);
            entry.obfuscatedName = match[4];
            entry.callingConvention = match[5];
            entry.realName = match[6];
            entry.signature = match[7];
            entry.returnType = match[8];
            symbols.push_back(entry);
        }
    }
    
    return symbols;
}
```

## Statistics

### From Validation

**TestDLL.pdb (DLL)**
- Total symbols: 217
- PUBLIC symbols: 59 (27%)
- PRIVATE symbols: 158 (73%)

**TestApp.pdb (EXE)**
- Total symbols: 435
- PUBLIC symbols: 128 (29%)
- PRIVATE symbols: 307 (71%)

### Size Distribution

Function sizes range from small (single instruction) to large (complex logic):
- Small: 41-60 bytes (simple getters, empty destructors)
- Medium: 60-150 bytes (typical methods, simple algorithms)
- Large: 150-300 bytes (complex calculations, multiple branches)
- Very Large: 300+ bytes (template instantiations, complex logic)

## Format History

### Version 1.0 (Initial)
```
PUBLIC 0x1000 45 MyFunction
```
- Basic visibility, address, size, and name

### Version 2.0 (Added Signatures)
```
PUBLIC 0x1000 45 MyFunction(int, double)
```
- Added function parameter signatures for overload differentiation

### Version 3.0 (Added RVA-Based Obfuscation)
```
PUBLIC 0x1000 45 sub_00001000 MyFunction(int, double)
```
- Simple address-based obfuscation (reversible)
- Followed IDA Pro convention

### Version 4.0 (Hash-Based Obfuscation)
```
PUBLIC 0x1000 45 obf_A7B3C92E MyFunction(int, double)
```
- **FNV-1a hash** with RVA mixing
- **Non-reversible** obfuscation
- Changed prefix to `obf_` to distinguish from simple address encoding
- Maintains determinism and uniqueness

### Version 5.0 (Added Return Type, `IS_VIRTUAL`, VTable fields, and labeled `SIZE=`)
```
PUBLIC 0x1000 SIZE=45 obf_A7B3C92E MyFunction(int, double) -> void IS_VIRTUAL=0 VTABLE_OFFSET=-1 ...
```
- Added `RETURN_TYPE` (mapping file only, via the same `GetTypeName()` path
  `SIGNATURE` already used — see "Return Type" above) and `IS_VIRTUAL`, plus
  the full `VTABLE_*`/`THIS_ADJUST`/`KIND`/`THUNK*` trailing block described
  under "Trailing Fields"
- The previously-unlabeled third column is now `SIZE=N` instead of a bare
  number, matching the labeled style every other field already used
- The obfuscated file's format gained `SIZE=` too, but still never carries
  real name, signature, or return type
- `_vtable.sym`/`_vtable_obfuscated.sym` gained a `METHOD_RETURN_TYPE=`
  field (mapping file only, alongside `METHOD=`/`METHOD_CLASS=`), and
  `_vtable_classes.sym` gained an inline `-> ReturnType` after each method's
  signature — see `VTABLE_RECONSTRUCTION.md`

### Version 6.0 (Added `CALLING_CONVENTION`)
```
PUBLIC 0x1000 SIZE=45 obf_A7B3C92E MyFunction(int, double) -> void IS_VIRTUAL=0 CALLING_CONVENTION=__cdecl VTABLE_OFFSET=-1 ...
```
- Added `CALLING_CONVENTION` (via `IDiaSymbol::get_callingConvention()` on
  the same function-type symbol `RETURN_TYPE` and `SIGNATURE` already reach
  through `get_type()`) right after `IS_VIRTUAL` in the trailing block
- Unlike `RETURN_TYPE`, this field is ABI metadata rather than
  name-revealing, so it appears in **both** the mapping and obfuscated files
- `_vtable.sym`/`_vtable_obfuscated.sym` and `_vtable_classes.sym` gained the
  same `CALLING_CONVENTION=` field on each method/slot line — see
  `VTABLE_RECONSTRUCTION.md`
- Superseded by Version 7.0 below: `CALLING_CONVENTION` moved out of the
  trailing block entirely and lost its `KEY=` prefix.

### Version 7.0 (Repositioned `CALLING_CONVENTION`; dropped `_vtable_classes.sym`'s `OFFSET=`)
```
PUBLIC 0x1000 SIZE=45 obf_A7B3C92E __cdecl MyFunction(int, double) -> void IS_VIRTUAL=0 VTABLE_OFFSET=-1 ...
```
- `CALLING_CONVENTION` is no longer a trailing `KEY=VALUE` field. It moved to
  a bare positional column immediately before whichever name identifies the
  function on that line: right before `REAL_NAME` in the mapping file, or
  right before `OBFUSCATED_NAME` in the obfuscated file (which has no real
  name for it to precede) — see "Column Descriptions" above. Its own
  `KEY=` prefix (`CALLING_CONVENTION=`) is dropped; only the bare value
  (`__thiscall`, `__cdecl`, `?`, ...) is printed
- `_vtable.sym`/`_vtable_obfuscated.sym`'s SLOT line got the same treatment:
  the bare value now sits right before `METHOD=` in the real-names file, or
  right before `OBFUSCATED=` in the obfuscated file
- `_vtable_classes.sym`'s per-method line got the same repositioning (bare
  value right before the method name), **and** its `OFFSET=` field was
  removed entirely — `RVA=` was already present and `VTABLE_OFFSET` doesn't
  independently add information not already implied by `SHAPE=`/the
  method's own vtable index, so it was dropped rather than carried forward
- These are purely positional/cosmetic changes — no field's *value* changed,
  and `VTABLE_OFFSET` in the main `.sym`/`_obfuscated.sym` trailing block is
  unaffected; only `_vtable_classes.sym`'s per-method `OFFSET=` was removed.
  See `VTABLE_RECONSTRUCTION.md` for the updated per-file formats

### Version 8.0 (Added `SOURCE_FILE`)
```
PUBLIC 0x1000 SIZE=45 obf_A7B3C92E __cdecl MyFunction(int, double) -> void IS_VIRTUAL=0 VTABLE_OFFSET=-1 ... THUNK_TARGET_RVA=0x0 SOURCE_FILE=main.cpp:18
```
- Added `SOURCE_FILE`, the last trailing field, giving the source file and
  line number of the first line record DIA has for the symbol's RVA range
  (`IDiaSession::findLinesByRVA` — a session-level query, unlike every other
  `Extract*()` helper, which only needs the symbol itself). Only the
  filename is kept, not DIA's full build-machine path
- **Mapping file only.** Unlike `CALLING_CONVENTION` (ABI metadata, present
  in both files), a source path can be as name-revealing as `REAL_NAME`
  itself, so `SOURCE_FILE` follows `REAL_NAME`/`SIGNATURE`/`RETURN_TYPE`'s
  precedent and is omitted entirely from the obfuscated file — it is the one
  trailing field that isn't written on every line of both files
- `_vtable.sym`'s SLOT line and `_vtable_classes.sym`'s per-method line each
  gained the same field, as their own last field, mapping-file-only in both
  cases (there is no `_vtable_classes_obfuscated.sym`, and
  `_vtable_obfuscated.sym`'s slots don't get it) — see
  `VTABLE_RECONSTRUCTION.md`
- **Side effect worth knowing:** DIA's internal `SymIndexId` values (the
  numbers behind `VTABLE_SHAPE`, `VTABLE_CLASS`'s underlying class ID,
  `_vtable.sym`'s `ID=`/`CLASS_ID=`/`SHAPE=`) shifted for many symbols after
  this change, purely because the new `findLinesByRVA` calls touch the DIA
  session differently while it is loading/caching data. These IDs were
  never guaranteed stable across ObfSymbolsEx versions, machines, or even
  identical re-runs with different query patterns — they are session-local
  bookkeeping numbers, not content hashes. Nothing about the actual
  resolution logic changed: the same 12011 overriding virtual methods
  resolved to their inherited slot, the same 20 needed a
  `VTABLE_SHAPE`/`VTABLE_SLOTS` correction, and 0 remained unresolved,
  verified line-by-line against the pre-change output with these ID fields
  normalized out

### Version 9.0 (Column alignment for `VISIBILITY`, `SIZE`, `CALLING_CONVENTION`)
```
PRIVATE 0x3370 SIZE=2745  obf_C1B03811 __cdecl    BlendBones(...) -> void IS_VIRTUAL=0 ...
PUBLIC  0x4030 SIZE=100   obf_A148AD8E __thiscall CDataManager<...>::CreateResource(...) -> memhandle_t__ * IS_VIRTUAL=0 ...
```
- `VISIBILITY`, `SIZE=N`, and `CALLING_CONVENTION` are now left-padded with
  extra spaces to a per-run column width (`ADDRESS` and everything from the
  name onward remain unpadded) — see "Column alignment is partial, by
  design" above for why these three specifically and not the rest
- Purely cosmetic: this changes only the amount of whitespace between these
  fields, never any field's value. The documented parsing regexes already
  use `\s+` between fields, so they are unaffected — verified by re-running
  the documented pattern against the realigned output with 0 failures
  across all 39501 lines
- Applies to both `<output>.sym` and `<output>_obfuscated.sym`, with column
  widths computed independently per file (in practice the same widths in
  both, since both are computed from the same underlying symbol data) —
  `_vtable.sym`/`_vtable_obfuscated.sym`/`_vtable_classes.sym` got the same
  treatment for their own bounded fields one version later (see Version 10.0)

### Version 10.0 (VTABLE header reordered/renamed; column alignment extended to the vtable files) - Current
```
CLASS=CAISound CLASS_ID=3081 SHAPE=8886 SLOTS=197 VTABLE_SOURCE=METHOD_METADATA ID=- RVA=-
SLOT=11  OFFSET=0x2C  RVA=0x556B50   SIZE=6     OBFUSCATED=obf_B3D7BC55 MATCH=CLASS_SHAPE ...
```
- `_vtable.sym`'s/`_vtable_obfuscated.sym`'s table header reordered to put
  the identity fields a human is almost always looking for first —
  `CLASS`, `CLASS_ID`, `SHAPE`, `SLOTS` — and the source-tracking fields
  last — `VTABLE_SOURCE` (renamed from bare `SOURCE`, since it no longer
  sits right after a standalone `VTABLE` line-type marker at the very start
  of the line), `ID`, `RVA`. A header line is still identifiable as one by
  starting with `CLASS=` rather than `SLOT=`
- `_vtable.sym`'s/`_vtable_obfuscated.sym`'s `SLOT`, `OFFSET`, `RVA`, and
  `SIZE` are now left-padded the same way `VISIBILITY`/`SIZE`/
  `CALLING_CONVENTION` were in Version 9.0 — see
  [VTABLE_RECONSTRUCTION.md](VTABLE_RECONSTRUCTION.md)'s "Column alignment"
  section for the exact widths and reasoning
- `_vtable_classes.sym`'s bracketed vtable index (`[N]`) got the same
  treatment, aligned across the **whole file** rather than per class — see
  [VTABLE_RECONSTRUCTION.md](VTABLE_RECONSTRUCTION.md)
- Purely cosmetic aside from the header rename: no field's value changed, a
  line-by-line diff against the pre-change output (ignoring only whitespace
  runs, and the header's field order/`SOURCE`→`VTABLE_SOURCE` rename)
  confirmed 0 unexpected differences across all three files

## Benefits

✅ **Dual Identity** - Both obfuscated and real names in one file  
✅ **Complete Context** - Address, size, visibility, signature all present  
✅ **Easy Lookup** - Find real name from obfuscated name or vice versa  
✅ **Standardized** - Regular, `obf_`-prefixed identifiers  
✅ **Deterministic** - Reproducible from PDB file  
✅ **Machine-Parseable** - Regular, consistent format  
✅ **Human-Readable** - Clear columns, self-documenting  

## Limitations

- **Build Variation:** Obfuscated names include RVA component, which changes between builds
  - *Benefit:* Makes it harder to correlate functions across different builds
  - *Workaround:* For stable names, hash only the function name (remove RVA mixing)
- **Residual `?` types:** `GetTypeName()` resolves basic types, named types, pointers/references (with cv-qualification), arrays, function pointers, pointer-to-member-function types, and `...`, verified down to 0 unresolved return types and 0 unresolved parameter types across a 39501-symbol real-world PDB (see `VTABLE_FEATURE.md`). A type shape genuinely outside all of that (e.g. `SymTagCustomType`, a bitfield, or some other DIA construct not yet seen in practice) would still fall through to `?` — the fallback exists for exactly that case, not as a known common occurrence
- **Long Names:** Some STL functions have very long decorated names
- **Hash Collisions:** Theoretically possible (1 in 4 billion), but never observed in practice

## Future Enhancements

There are no open items in this list — everything once proposed here has
either shipped or been explicitly dropped; see the notes below.

**Return Type is done** (see "Return Type" under Column Descriptions above,
and Format History's Version 5.0) — it is no longer a future item.

**Calling Convention is done** (see the `CALLING_CONVENTION` row under
"Column Descriptions" above, and Format History's Version 6.0/7.0) — it is
no longer a future item.

**Module Name / Line Numbers is done**, as `SOURCE_FILE=File.cpp:Line` (see
the `SOURCE_FILE` row under "Trailing Fields" above, and Format History's
Version 8.0) — it is no longer a future item. "Module name" specifically
(the containing `.dll`/`.exe`, as opposed to source file) was not added:
every symbol in one `.sym` run already comes from the single PDB passed on
the command line, so a module-name column would be constant across the
entire file and add no information.

**Checksum was considered and dropped:** `obf_XXXXXXXX` is already an FNV-1a
hash of the real name + RVA (see "Obfuscated Name Generation" above), so it
already serves as a verification checksum — a separate `[CRC:...]` field
would just duplicate that.

**Column alignment is partial, by design.** `VISIBILITY`, `SIZE=N`, and
`CALLING_CONVENTION` — the only fields with a naturally small, bounded value
space (`PUBLIC`/`PRIVATE`; a byte count; one of a handful of known calling
conventions) — are left-padded to a column width computed from the widest
value that actually occurs in that run, so short values line up with long
ones:

```
PRIVATE 0x3370 SIZE=2745  obf_C1B03811 __cdecl    BlendBones(...) -> void ...
PUBLIC  0x4030 SIZE=100   obf_A148AD8E __thiscall CDataManager<...>::CreateResource(...) -> memhandle_t__ * ...
```

`ADDRESS` and everything from the name onward
(`REAL_NAME`/`SIGNATURE`/`RETURN_TYPE`/`VTABLE_CLASS`, ...) remain
unpadded: real-world PDBs (e.g. from a large game codebase) routinely have
template-heavy names hundreds of characters long, so fixed-width columns
there would either fail to align on those or pad every other line to match
them — that concern doesn't apply to `VISIBILITY`/`SIZE`/`CALLING_CONVENTION`
specifically, since their value space is bounded regardless of how long
names in the same file get. The format remains designed to be parsed with
`\s+`-tolerant regular expressions (see "Parsing the Format" above), not
read as fixed columns — the padding is a readability aid for a human
skimming the file, not a promise of exact column positions a parser should
depend on.

## Reconstructed VTable files

In addition to the normal `.sym` and `_obfuscated.sym` files, ObfSymbolsEx
writes four more files: `_vtable.sym`, `_vtable_obfuscated.sym`,
`_vtable_classes.sym`, and `_vtable_inheritance.sym` — six output files in
total per run.

See [VTABLE_RECONSTRUCTION.md](VTABLE_RECONSTRUCTION.md) for the complete
format of all four (including the `VTABLE_SOURCE=DIA_VTABLE|METHOD_METADATA`
field, every `MATCH=` value, and the documented sort order for
`_vtable.sym` and `_vtable_classes.sym`), and
[VTABLE_FEATURE.md](VTABLE_FEATURE.md) for how the underlying per-method
fields are derived, including thunks.
