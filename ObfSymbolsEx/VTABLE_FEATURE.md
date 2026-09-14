# VTable index extraction

ObfSymbolsEx now extracts virtual-function metadata from DIA/PDB and writes it to both output files.

## Fields

- `IS_VIRTUAL=1/0` — whether DIA reports this symbol as virtual (`get_virtual`), read directly rather than inferred. Added because `VTABLE_SHAPE` being non-zero is *not* a reliable stand-in for this: DIA very often does not expose a shape ID on a method directly, and the enclosing class's own shape (the fallback) is not always available either — see "Known anomaly" below. A method can be genuinely virtual with `VTABLE_SHAPE=0`.
- `VTABLE_OFFSET=0xNN` — byte offset reported by DIA (`get_virtualBaseOffset`).
- `VTABLE_INDEX=N` — `VTABLE_OFFSET / target pointer size`.
- `VTABLE_CLASS=ClassName` — enclosing class reported by DIA.
- `VTABLE_INTRO=1/0` — whether DIA marks the method as a virtual introduction (`get_intro`).
- `VTABLE_SHAPE=N` — DIA virtual-table-shape symbol ID.
- `VTABLE_SLOTS=N` — number of entries in that `VTableShape`.
- `THIS_ADJUST=N` — DIA `this` adjustment for the method.

These eight fields, plus four more described below, are written as a fixed
trailing `KEY=VALUE` block on **every** line of `<output>.sym` /
`<output>_obfuscated.sym` — not only for virtual functions. A non-virtual
symbol simply carries the non-applicable values (`IS_VIRTUAL=0`,
`VTABLE_OFFSET=-1`, `VTABLE_SHAPE=0`, etc.). The four additional trailing
fields are:

- `KIND=FUNCTION|THUNK|DESTRUCTOR|DELETING_DESTRUCTOR|VECTOR_DELETING_DESTRUCTOR`
  — a presentation-aid classification, independent of DIA's
  `isVirtual`/`isThunk` flags. `ClassifyFunctionKind()` recognizes this from
  the *friendly* name DIA's `get_name()` actually returns for a
  `SymTagFunction`/`SymTagThunk` — e.g. `"ClassName::~ClassName"` or
  `` "ClassName::`scalar deleting destructor'" `` — never the raw decorated
  form (`"??1ClassName@@..."`, `"??_GClassName@@..."`), which `get_name()`
  does not produce for these symbol kinds. An earlier version of this
  classifier checked for the raw decorated prefixes, which never matched
  anything DIA actually returns, so `KIND` was always `FUNCTION` or `THUNK`
  in practice; this is fixed.
- `THUNK=1/0` — `1` when this record is a DIA `SymTagThunk` rather than a
  `SymTagFunction`.
- `THUNK_ORDINAL=N` — DIA thunk ordinal, `0` when not a thunk.
- `THUNK_TARGET_RVA=0xNN` — the RVA the thunk jumps to, `0x0` when not a
  thunk.

`SOURCE_FILE=File.cpp:Line` is a **fifth** additional trailing field, and the
line's last field, but it does not follow the "written on every line of both
files" rule the twelve fields above do — see its own section below.

## Source file and line (mapping file only)

`SOURCE_FILE` is read via `IDiaSession::findLinesByRVA(rva, length, ...)`
against the symbol's own RVA/length — the first line record found is
generally the function's opening line (often the opening brace, just after
the prologue), not necessarily the exact declaration line, but close enough
to locate the function in source. Unlike every `Extract*()` helper above,
this lookup needs the `IDiaSession` itself, not just the symbol, since line
information is a session-level query, not a property of the symbol. Only the
bare filename is kept (DIA reports full build-machine paths, e.g.
`d:\build\...\ai_behavior_lead.cpp` becomes `ai_behavior_lead.cpp`). `?` when
DIA has no line data for that RVA range — a zero-length symbol, a module
compiled without line info, most thunks, and so on.

Unlike `CALLING_CONVENTION` (ABI metadata, harmless in both files),
`SOURCE_FILE` is treated like `REAL_NAME`/`SIGNATURE`/`RETURN_TYPE`: a
source path can be as revealing as the function's own name, so it is
**omitted entirely from the obfuscated file** (`<output>_obfuscated.sym`,
`_vtable_obfuscated.sym`). It appears only in `<output>.sym`, `_vtable.sym`,
and `_vtable_classes.sym`, always as each line's last field.

## Calling convention (not part of the trailing block)

`CALLING_CONVENTION` is not a `KEY=VALUE` trailing field — it is a bare
positional column (`__thiscall`, `__cdecl`, `__stdcall`, `__fastcall`, ...,
or `?` if DIA can't provide it), read from `IDiaSymbol::get_callingConvention()`
on the same function-type symbol `RETURN_TYPE`/`SIGNATURE` already reach via
`get_type()` (a `CV_call_e` value from `cvconst.h`; an unrecognized code
prints as `CV_CALL_0xNN` rather than `?`, since the enum is small and fully
known). It sits immediately before whichever name identifies the function on
that line — the real name in the mapping file, the obfuscated name in the
obfuscated file (there is no real name there for it to precede). Unlike
`RETURN_TYPE`, it carries no naming information, so it appears in **both**
output files. See "Column Descriptions" in
[OUTPUT_FORMAT.md](OUTPUT_FORMAT.md) for the exact position.

See [OUTPUT_FORMAT.md](OUTPUT_FORMAT.md) for the full column-by-column
reference, and [VTABLE_RECONSTRUCTION.md](VTABLE_RECONSTRUCTION.md) for how
these fields feed into the `_vtable.sym`, `_vtable_classes.sym`, and
`_vtable_inheritance.sym` reports.

The pointer size is taken from the PDB target machine, so a 64-bit ObfSymbolsEx build can correctly process an x86 PDB.

## Multiple inheritance

`VTABLE_INDEX` is the slot within the virtual table described by the method's DIA metadata. `VTABLE_CLASS`, `VTABLE_SHAPE`, and `THIS_ADJUST` are emitted as additional disambiguation information. A single most-derived C++ class can have multiple vftables; therefore the numeric slot alone must not be treated as a globally unique identifier.

The UDT pass additionally enumerates methods as children of their class and merges the virtual metadata by RVA. This improves coverage when the global `SymTagFunction` enumeration does not expose all method/type relationships directly.

## Overriding virtual methods (VTABLE_INDEX inheritance)

CodeView only stores a vtable slot offset — the `LF_ONEMETHOD` `vbaseoff`
field — on the method that **introduces** a virtual function
(`VTABLE_INTRO=1`). A method that merely **overrides** an inherited virtual
(`VTABLE_INTRO=0`) has no such field in the PDB at all, so
`IDiaSymbol::get_virtualBaseOffset()` reports `0` for it — not a genuine slot
0, just the absence of data read back as zero. Left as DIA reports it, every
override in a PDB would show `VTABLE_OFFSET=0x0 VTABLE_INDEX=0`, regardless
of its actual slot.

ObfSymbolsEx corrects this with a third extraction pass, after the two
described above: for every virtual method with `VTABLE_INTRO=0`, it walks the
method's own class's base-class chain — exactly as DIA reports it via
`SymTagBaseClass` — looking for the nearest ancestor that introduces a
virtual method with the same unqualified name and parameter signature. That
ancestor's `VTABLE_INDEX` is the slot being overridden, so it is copied onto
the override (and `VTABLE_OFFSET` is recomputed as
`VTABLE_INDEX * target_pointer_size`). `VTABLE_CLASS`, `VTABLE_SHAPE`, and
`VTABLE_SLOTS` are left untouched — they already correctly describe the
override's *own* class and vtable; only the slot number was ever wrong.

This lookup is derived entirely from the PDB's own DIA-reported class
hierarchy and virtual-method metadata — never from source declarations,
function ordering, an external SDK, or (for this project specifically) the
reference game source under `ASWRD_Game_Source_Code/`, which exists only to
help a human verify the result. When no matching introducing ancestor can be
found in the PDB, ObfSymbolsEx reports `VTABLE_OFFSET=-1 VTABLE_INDEX=-1`
instead of the misleading `0` DIA would otherwise imply.

### Destructors match by kind, not by name

A destructor's (and the compiler-generated "deleting destructor" wrapper's)
own name always repeats its *own* class — `"~CASW_Marine"` vs
`"~CASW_Inhabitable_NPC"`, `` "CASW_Marine::`scalar deleting destructor'" ``
vs `` "CASW_Inhabitable_NPC::`scalar deleting destructor'" `` — so a naive
"strip the class qualifier" comparison never matches across a hierarchy the
way it does for an ordinarily-named method. `UnqualifiedMethodKey()`
special-cases all three destructor-like kinds (plain destructor, scalar
deleting destructor, vector deleting destructor — see `ClassifySpecialMethod()`
in `PdbSymbolExtractor.cpp`) to a fixed key per kind, so a derived class's
destructor override correctly resolves to whichever ancestor first declares
that destructor kind as virtual.

### Pure-virtual introducing declarations with no compiled body

The method that introduces a virtual function does not need a compiled,
out-of-line body anywhere in the PDB for its `vbaseoff` to exist — a common
case is a pure-virtual declaration on an abstract interface base class (e.g.
an `IFoo` interface with no data and no concrete instances) that is never
given a definition. Such a declaration never becomes a `SymTagFunction` with
an RVA, so it can never appear as a regular extracted symbol, and would
otherwise be invisible to override resolution.

ObfSymbolsEx captures this directly from DIA's TYPE-level method metadata: the
UDT method-enumeration pass (see [CODE_ORGANIZATION.md](CODE_ORGANIZATION.md))
records `get_intro()`/`get_virtualBaseOffset()` for *every* method a class
declares, independent of whether that method has an RVA, into a side table
keyed by class + unqualified name/signature. The override-resolution pass
above checks this side table in addition to compiled introducing methods, so
an override of a purely-declared interface method still resolves correctly.

### Matching caveat

Matching is by unqualified name + parameter signature within the DIA-reported
base-class graph, which is exactly what the compiler itself uses to resolve
an override — but in a diamond-inheritance PDB where two unrelated ancestors
happen to introduce a same-named, same-signature virtual, the nearer one
found by this search (declaration-order, depth-first) is used; this is a
known, accepted limitation rather than an attempt to fully replicate C++
overload/override resolution.

### Identical-code-folded RVAs corrupted unrelated methods' metadata

The UDT method-enrichment pass (see [CODE_ORGANIZATION.md](CODE_ORGANIZATION.md))
matches a UDT's own method children to the corresponding entry in the main
`symbols` list *by RVA*, then merges DIA's virtual/vtable metadata for that
method onto the matched entry. This worked as long as one RVA identified one
method — but the MSVC linker performs identical/COMDAT code folding by
default in a release build: any two functions whose compiled bodies are
byte-identical (extremely common for trivial one-line getters, empty
overrides, `{ return -1; }`-style stubs, and thin forwarding wrappers such as
`int DisplayClip1() { return Clip1(); }`) are folded into a single physical
function at a single RVA. In a codebase the size of this project's Source
Engine PDB, one RVA can end up shared by dozens of otherwise-unrelated
methods from completely different classes.

Matching by RVA alone meant that when a UDT method-child lookup landed on an
RVA shared by several distinct symbols, its virtual metadata (`isVirtual`,
`VTABLE_OFFSET`/`INDEX`, `VTABLE_SHAPE`, `classParentId`, ...) was merged into
*whichever* `symbols` entry happened to have claimed that RVA first — not
necessarily the entry the current UDT method-child actually was. Two
observable symptoms resulted:

- A genuinely non-virtual method could appear to be virtual, with an
  unrelated method's real `VTABLE_SHAPE`/`VTABLE_INDEX` attached to it. For
  example `CASW_Weapon_Sentry::GetSentryAmmo()` (a plain, non-virtual getter
  in the game source) used to show `VTABLE_SHAPE=8934` because it shares an
  RVA with the genuinely-virtual `CASW_Weapon_Sentry::DisplayClip1()` (whose
  body, `{ return GetSentryAmmo(); }`, compiled identically).
- A genuinely virtual method could lose its own correct data to an unrelated
  method processed later at the same RVA — for example
  `CBaseCombatWeapon::Clip1()`/`Clip2()` (plain, non-virtual getters in the
  game source) used to show `VTABLE_INTRO=1` with a real-looking index,
  borrowed from `CASW_Weapon::DisplayClip1()`/`DisplayClip2()`, which
  genuinely do introduce those slots and happen to fold to the same RVA.

**Fix:** the merge now keys on **(RVA, fully qualified name)** instead of RVA
alone (`symbolByRvaAndName` in `PdbSymbolExtractor.cpp`). When more than one
distinct symbol shares an RVA, the qualified name disambiguates which one a
given UDT method-child actually is.

Getting the qualified name right required understanding an *unrelated*,
separately-confirmed DIA naming quirk: `IDiaSymbol::get_name()` on a UDT's
own method child returns the **unqualified** name (e.g. `"Suicide"`), while
the same logical method's *global* `SymTagFunction` entry — what
`symbolByRvaAndName` is built from — is reported **class-qualified** (e.g.
`"CASW_Marine::Suicide"`). This was confirmed, not assumed: instrumenting the
lookup and diff-checking global vs. UDT-child names for every mismatch across
the whole PDB (35124 instances) showed the pattern held with zero
counterexceptions — always exactly `("Class::method", "method")`, never a
different kind of formatting difference (no casing, whitespace, calling
convention, or template-argument differences involved). So the UDT-child
name is reconstructed to the same qualified form using the class's own name
(already recorded into `classHierarchy` earlier in that class's own UDT
iteration) before it is used as the lookup key — `ClassName + "::" +
methodName` — which is then looked up exactly, whether or not the RVA is
shared by other symbols. A single-owner-RVA fallback (using the one existing
entry outright when the reconstructed name still doesn't match) remains only
as a last-resort safety net for the case where DIA could not provide a class
name at all; it is not the primary mechanism.

This fix eliminated all 47 previously-observed cases of a method showing a
non-zero `VTABLE_SHAPE` while remaining unresolved, and corrected the
`VTABLE_INDEX`/`IS_VIRTUAL` attribution on every checked instance of the two
symptoms above (verified against `ASWRD_Game_Source_Code/` for representative
cases: `GetSentryAmmo`, `Clip1`/`Clip2`, `ScriptExtinguish`,
`CBaseAchievement::GetPointValue`, `IParticleSystemQuery::MovePointInsideControllingObject`,
`ConVar::SetValue(int)`, and others). After this fix plus the destructor and
pure-virtual-declaration fixes above, this PDB has **zero** virtual methods
(`IS_VIRTUAL=1`) left with an unresolved `VTABLE_INDEX=-1`.

### The same ICF phenomenon also corrupts SOURCE_FILE

The RVA collisions described above don't only affect `VTABLE_*` merging —
they affect `SOURCE_FILE` resolution too, in a way that's easy to miss
because it fails silently rather than loudly. `IDiaSession::findLinesByRVA`
answers "what line info is at this address", and when several
byte-identical functions have been folded together, that address's one
surviving line record is only actually correct for whichever *one* of them
the linker kept debug info for — wrong, not merely missing, for the rest.
`CBaseSpriteProjectile::Precache()` and dozens of other unrelated trivial
overrides across unrelated classes all shared one RVA and all reported the
same borrowed `dt_send.cpp:290` before this was fixed.

The fix reuses `symbolByRvaAndName` (already built for the reason above) as
a trust check: an RVA's resolved `SOURCE_FILE` is kept only when every
symbol sharing that RVA reduces to the same unqualified name — which is
true and correct for a class-template method folded across many
instantiations (`CUtlMemory<T,int>::Grow(int)`, genuinely one definition
site in a header), and false, so cleared to `?`, for unrelated methods that
merely happen to compile to identical bytes. See
[OUTPUT_FORMAT.md](OUTPUT_FORMAT.md#source_file-trust-and-identical-code-folding)
for the full detail and verified numbers.

### Secondary multiple-inheritance bases: recovering the right VTABLE_SHAPE

A class with two or more *pure-interface* secondary bases (e.g. a class
implementing both `IBotController` and `IPlayerInfo`) used to show a
resolved `VTABLE_INDEX` larger than its own reported `VTABLE_SLOTS` for
methods belonging to the secondary interface DIA doesn't surface at the
class level. This was root-caused by instrumentation rather than guessed:

- `IDiaSymbol::get_virtualTableShapeId()` on the individual method returns
  `S_FALSE` (not an error, but no value) for every single method of a real
  multi-secondary-interface class checked (`CPlayerInfo`, implementing both
  `IBotController` and `IPlayerInfo`) — including its one genuinely
  introducing method. The per-method query is not a usable source of the
  correct shape here at all.
- `IDiaSymbol::get_virtualTableShapeId()` on the *class itself*
  (`CPlayerInfo`'s own UDT, i.e. `udtShapeId`) consistently reports only
  **one** shape — confirmed identical (same shape ID) across the class's
  duplicate UDT symbol entries in the same PDB, so it isn't a
  duplicate-record artifact either. That one shape happens to be
  `IBotController`'s (12 slots), leaving `IPlayerInfo`'s 24-method interface
  with no shape information at the `CPlayerInfo`/method level at all.
- `IPlayerInfo`'s **own** UDT, visited independently as its own
  `SymTagUDT` (it is a real, separate type record, being an abstract
  interface base class), reports its own correct, complete shape (24 slots)
  reliably via the same class-level query. A pure interface base class that
  itself only has one vtable does not suffer the ambiguity a class combining
  several of them does.

**Fix:** `introducingSlotsByClass` (used for override resolution above; see
"Pure-virtual introducing declarations with no compiled body") now also
records the introducing class's own shape ID and slot count alongside the
slot index, captured at the point that introducing class's own UDT is
visited. When an override resolves through this table (or through a compiled
introducing `FunctionSymbol`, which already carries its own correct shape)
and the override's own class-level shape is either missing or provably wrong
for the resolved index (`VTABLE_INDEX >= VTABLE_SLOTS` on the class-level
shape, or no shape at all), `VTABLE_SHAPE`/`VTABLE_SLOTS` are replaced with
the introducing interface's own values. This is safe specifically because a
class that only *implements* an interface without adding further virtuals of
its own to that particular vtable has a secondary vtable identical in shape
to the interface's own — it is not a guess, and the correction only fires
when the existing shape is already known to be impossible for that slot,
so a single-inheritance override's own (correct, and typically larger)
class-level shape — such as `CASW_Marine::Suicide()`'s own 702-slot shape,
still describing its own class correctly — is left untouched.

Verified against `ASWRD_Game_Source_Code/`: `CPlayerInfo : public
IBotController, public IPlayerInfo` (`server/playerinfomanager.h`) confirms
the two-interface hierarchy. After this fix, `CPlayerInfo`'s `IBotController`
methods (index 0-11) keep `VTABLE_SHAPE=4739 VTABLE_SLOTS=12`, and its
`IPlayerInfo` methods (index 12-23, e.g. `IsDead`, `GetHealth`,
`GetLastUserCommand`) correctly show `VTABLE_SHAPE=8471 VTABLE_SLOTS=24` —
`IPlayerInfo`'s own shape. No case of `VTABLE_INDEX >= VTABLE_SLOTS` remains
anywhere in this PDB.

## Important limitation

This implementation reports what DIA exposes, and derives what DIA does not
expose (overriding methods, see above) only from this PDB's own DIA data.
When a virtual method's slot cannot be determined either way,
`VTABLE_INDEX=-1` is retained rather than guessed from source declarations,
function ordering, or an external SDK.


### VTable thunks

ObfSymbolsEx also enumerates DIA `SymTagThunk` symbols. Virtual thunks carry their own `VTABLE_OFFSET`, `VTABLE_INDEX`, `VTABLE_SHAPE`, `THIS_ADJUST`, `THUNK_ORDINAL`, and `THUNK_TARGET_RVA`. During VTable reconstruction, a matching thunk is preferred over a normal function symbol because the thunk can be the concrete callable entry stored in the slot. Unknown information is not guessed.

Thunk signatures are extracted the same way as for regular functions
(`ExtractFunctionSignature()`), so a thunk's real name prints with its
parameter list just like any other symbol, and can be matched by
name+signature during the override-resolution pass described above. An
overriding virtual method emitted as an adjustor thunk (common for a
secondary-base override under multiple inheritance) benefits from that same
`VTABLE_INDEX` resolution.
