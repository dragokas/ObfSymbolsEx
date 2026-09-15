# Unit Tests

What `unit-tests.ps1` checks, and how to run it.

## Running

```powershell
.\unit-tests.ps1
```

This builds `ObfSymbolsEx` (Release x64), `TestApp`/`TestDLL` (Debug x64,
plus a Release x64 build of `TestDLL` specifically for the ICF check),
extracts symbols into `UnitTestResults\`, and runs ~43 assertions — each one
checks an exact field value in the real `.sym` output (e.g. "does
`Rectangle::Area` resolve to the same `VTABLE_INDEX` as `Shape::Area`?"),
and the script exits non-zero if any of them fail. Run this whenever you
change extraction logic, to confirm nothing broke.

Pass `-SkipBuild` to re-run against already-built artifacts (fast iteration
while only editing the test script itself).

If you just want a quick eyeball after a build rather than a real
pass/fail check, `.\validate.ps1` (repo root) does that instead — it prints
a sample of the extracted symbols but doesn't assert anything.

## Why Debug *and* Release

Small test functions get inlined away entirely under Release optimization
(confirmed empirically — even the pre-existing `SimpleFunction` disappears
from the PDB in Release), so every type/signature/vtable/override assertion
runs against a **Debug** build, matching `validate.ps1`'s existing default.
The Identical Code Folding check is the one exception: ICF is a linker
optimization (`/OPT:ICF`) that Debug builds don't perform, so that specific
check runs against a dedicated **Release** build of `TestDLL`.

## What each section covers

- **Return types** — `FunctionWithReturn`, `MathOperations::Add`, and a
  complex STL return type (`Widget::Serialize`) resolve correctly instead
  of falling back to `?`.
- **Calling convention** — `extern "C"` DLL exports report `__cdecl`.
- **Complex/basic type detection** — `char16_t`/`char32_t` parameters, a
  fixed-size array passed by reference (`float[3] &`), an ordinary function
  pointer (`void (__cdecl *)(int)`), a pointer-to-member-function
  (`double (__cdecl Calculator::*)(double)`), and a variadic `...`
  parameter — all added to `TestApp.cpp` specifically because none of them
  had prior coverage.
- **Source file + line number** — `SOURCE_FILE=File.cpp:Line` resolves for
  both an EXE and a DLL symbol.
- **Destructor / pure-virtual override resolution** — `Rectangle`/`Circle`
  gained their own overriding destructors (`TestApp.cpp`) specifically to
  test this; asserts their `VTABLE_INDEX`/`VTABLE_SHAPE` match the
  introducing `Shape::~Shape`, and that the pre-existing `Area()` override
  pair resolves the same way.
- **VTable methods dump / multiple inheritance** — `Renderable`,
  `Serializable`, and `Widget : public Renderable, public Serializable`
  (new in `TestApp.cpp`) give the first coverage of a class with two
  independent vtables; asserts both bases get their own vtable-classes
  header and both appear in Widget's inheritance dump.
- **Identical Code Folding (ICF) fix** — `ICFTestA::Zero`/`ICFTestB::Zero`
  (new in `TestDLL.h`/`.cpp`) are byte-identical trivial methods the linker
  reliably folds onto one RVA in Release; asserts both remain correctly and
  distinctly attributed (name, obfuscated hash, owning class) instead of
  being merged or corrupted by the shared address.
- **Direct `.exe`/`.dll` input** — running `ObfSymbolsEx.exe` against
  `TestApp.exe`/`TestDLL.dll` directly produces byte-identical output to
  running it against the matching `.pdb`.
- **Column-aligned output** — every mapping-file line's address starts at
  the same character column regardless of `PUBLIC`/`PRIVATE` width.
- **Obfuscated name / dual-output-file consistency** — every symbol gets an
  `obf_XXXXXXXX` name, the obfuscated file has exactly one line per mapping
  line, and it never leaks the real name, return type, or source file.
- **Report filtering (`-fc`/`-fm`)** — `-fc+`/`-fc-` match class names by
  case-insensitive substring (not exact), drop free functions from a `-fc+`
  result (nothing to match), and keep/drop a whole group (header + members)
  in `server_vtable_classes.sym`; `-fm+`/`-fm-` match a whole line (so
  `PUBLIC`/`PRIVATE` are fair game); `+` and `-` combine like the README's
  own `-fc+CBaseEntity -fm+model -fm-Index` example; WORD may be quoted; an
  unrecognized switch fails with a non-zero exit code.

## Fixtures added for this suite

`TestApp.cpp`: overriding destructors on `Rectangle`/`Circle`; the
`Renderable`/`Serializable`/`Widget` multiple-inheritance hierarchy;
`WideCharParameters`, `SumFixedArray`, `InvokeCallback`/`PrintInt`, and
`InvokeCalculatorMethod` (plus calls to all of them, and to the
previously-unused `VariadicFunction`, from `main()` so none get eliminated).

`TestDLL.h`/`.cpp`: the `ICFTestA`/`ICFTestB` ICF-folding fixture.
