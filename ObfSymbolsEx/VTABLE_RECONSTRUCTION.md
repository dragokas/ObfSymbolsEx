# VTable Reconstruction Output Files

ObfSymbolsEx emits four files dedicated to virtual-table reconstruction, in
addition to the normal `<output>.sym` / `<output>_obfuscated.sym` pair (six
output files in total per run):

| File | Real names? | Organized by |
|------|-------------|--------------|
| `<output>_vtable.sym` | Yes | One block per **vtable** (concrete or reconstructed), slots in slot order |
| `<output>_vtable_obfuscated.sym` | No | Same as `_vtable.sym`, with `CLASS=-`, `METHOD=` omitted |
| `<output>_vtable_classes.sym` | Always (no obfuscated variant) | One block per **class**, methods sorted by vtable index |
| `<output>_vtable_inheritance.sym` | Always (no obfuscated variant) | One block per **class**, its base-class chain as an ASCII tree |

`_vtable.sym`, `_vtable_classes.sym`, and `_vtable_inheritance.sym` are all
generated from the *same* underlying per-symbol/per-class DIA metadata
(`isVirtual`, `vtableIndex`, `vtableShapeId`, `vtableOffset`, `classParentId`,
`className`, and the class base-class graph — see
[VTABLE_FEATURE.md](VTABLE_FEATURE.md) for how the per-method fields are
populated). They answer different questions:

- **`_vtable.sym`** answers *"what does this concrete vtable look like,
  slot by slot, including slots we couldn't identify?"* It is built around
  DIA's `SymTagVTable` symbols (one instance per actual vtable in memory) and
  deliberately shows `UNKNOWN` when a slot can't be matched to a method.
- **`_vtable_classes.sym`** answers *"what virtual methods does this class
  have, in index order?"* It is a convenience view for browsing a class's
  virtual interface and never shows placeholder/unknown entries — it only
  ever lists methods DIA actually reported.
- **`_vtable_inheritance.sym`** answers *"where does this class sit in the
  inheritance graph, and what are all of its ancestors?"* It has nothing to
  do with vtable slots directly; it exists to help a human make sense of the
  `VTABLE_CLASS`/`CLASS` names appearing in the other files, especially for
  a deep or multiply-inherited hierarchy.

Because `_vtable.sym` and `_vtable_classes.sym` are built from different
subsets of the same data (see "Differences in what each file includes"
below), they are not guaranteed to show identical slot counts for the same
class.

## Source of information

The reconstruction is based exclusively on DIA information contained in the
input PDB:

1. `SymTagVTable` identifies concrete VTable symbols (class children of a
   `SymTagUDT`).
2. Each VTable provides its owning `classParent` and its `VTableShape`
   through `get_type()`.
3. `VTableShape::get_count()` provides the number of slots.
4. Virtual methods provide `get_virtualBaseOffset()` and
   `get_virtualTableShapeId()`, including declaration-only methods with no
   compiled body (see [VTABLE_FEATURE.md](VTABLE_FEATURE.md#pure-virtual-introducing-declarations-with-no-compiled-body)).
5. `SymTagBaseClass` (a class child of a `SymTagUDT`) identifies each class's
   direct base classes, in DIA's own declaration order.
6. The target pointer size is taken from the PDB machine type, so an x86 PDB
   analyzed by an x64 ObfSymbolsEx executable is handled correctly.

The implementation does **not** read function pointers from the target
binary and does not infer missing slots, or class relationships, from source
order, SDK headers, third-party data, or (for this project specifically) the
reference game source under `ASWRD_Game_Source_Code/`, which exists only to
help a human verify the result. An unknown slot remains unknown.

---

## `_vtable.sym` / `_vtable_obfuscated.sym`

### Format

A table header identifies one vtable, identity fields first (`CLASS`
through `SLOTS` — what a human is almost always looking for), then
source-tracking fields last (`VTABLE_SOURCE` through `RVA` — whether this
is a concrete DIA table or a reconstructed one, and its own low-level
identifiers):

```text
CLASS=CBaseEntity CLASS_ID=456 SHAPE=789 SLOTS=87 VTABLE_SOURCE=DIA_VTABLE ID=123 RVA=0x107DFD28
```

Each following line describes one slot of that table. `SLOT`, `OFFSET`,
`RVA`, and `SIZE` are left-padded to a column width computed from the
widest value that actually occurs anywhere in this run (see "Column
alignment" below) — shown here without that padding for brevity. In
`_vtable.sym` (real names), the calling convention is a bare value sitting
right before `METHOD=`:

```text
SLOT=26 OFFSET=0x68 RVA=0x1034D3E0 SIZE=123 OBFUSCATED=obf_XXXXXXXX MATCH=CLASS_SHAPE KIND=FUNCTION THUNK=0 THUNK_ORDINAL=0 THUNK_TARGET_RVA=0x0 THIS_ADJUST=0 __thiscall METHOD=SetModel(...) METHOD_RETURN_TYPE=void METHOD_CLASS=CBaseEntity SOURCE_FILE=baseentity.cpp:1902
```

In `_vtable_obfuscated.sym`, there is no `METHOD=` for it to sit in front
of, so it moves earlier instead, right before `OBFUSCATED=`:

```text
SLOT=26 OFFSET=0x68 RVA=0x1034D3E0 SIZE=123 __thiscall OBFUSCATED=obf_XXXXXXXX MATCH=CLASS_SHAPE KIND=FUNCTION THUNK=0 THUNK_ORDINAL=0 THUNK_TARGET_RVA=0x0 THIS_ADJUST=0
```

A slot ObfSymbolsEx could not associate with any method is emitted as (no
calling convention -- there is no matched method to read one from):

```text
SLOT=41 OFFSET=0xA8 RVA=-1 SIZE=-1 OBFUSCATED=- MATCH=NONE METHOD=UNKNOWN
```

Table headers are separated by a blank line.

### Header fields

| Field | Meaning |
|-------|---------|
| `CLASS` | Owning class name. Replaced with `-` in the obfuscated file. |
| `CLASS_ID` | DIA symbol ID of the owning class. |
| `SHAPE` | DIA `VTableShape` symbol ID. |
| `SLOTS` | Number of entries in that `VTableShape`. |
| `VTABLE_SOURCE` | `DIA_VTABLE` — a concrete `SymTagVTable` symbol DIA emitted. `METHOD_METADATA` — a synthetic table ObfSymbolsEx reconstructed because a class/shape pair appeared on virtual methods but DIA never emitted a `SymTagVTable` symbol for it (see "Synthetic tables" below). Named `VTABLE_SOURCE` rather than bare `SOURCE` since, unlike in earlier versions, it no longer sits right after a standalone `VTABLE` line-type marker at the start of the line — a header line is still identifiable as one by starting with `CLASS=` rather than `SLOT=`. |
| `ID` | DIA `SymIndexId` of the VTable symbol, or `-` for a synthetic table. |
| `RVA` | RVA of the VTable, or `-` for a synthetic table (a reconstructed table has no concrete address). |

**`ID`, `CLASS_ID`, and `SHAPE` are not stable identifiers.** They are DIA's
internal `SymIndexId` values, assigned as the DIA session lazily loads and
caches symbol data — not content hashes, and not something ObfSymbolsEx
controls or promises to keep constant. Re-running ObfSymbolsEx against the
*same* PDB can print different numbers for the same class if anything about
the query pattern changes (extraction gained a new DIA call, ran symbols in
a different order, ...); this happened, harmlessly, when `SOURCE_FILE`
support was added (see `OUTPUT_FORMAT.md`'s Format History, Version 8.0) —
every affected class kept the exact same slots, methods, and resolution
correctness, only the `ID`/`CLASS_ID`/`SHAPE` numbers shifted. Match classes
across two runs by `CLASS` name (and `RVA` for concrete tables), not by
these IDs.

### Slot fields

| Field | Meaning |
|-------|---------|
| `SLOT` | Slot index within this table, `0`-based. |
| `OFFSET` | `SLOT * pointer_size` in bytes (see "Pointer size for synthetic tables" below). |
| `RVA`, `SIZE` | The matched method's RVA and byte length, or `-1` if unmatched. Note the header's own `RVA` field (a whole vtable's address) is unrelated and not aligned the same way — see "Column alignment" below. |
| `OBFUSCATED` | The matched method's obfuscated name, or `-` if unmatched. |
| `MATCH` | How the slot was resolved — see "Match types" below. |
| `KIND` | The matched method's `KIND` classification (`FUNCTION`, `THUNK`, `DESTRUCTOR`, ...). Omitted when unmatched. |
| `THUNK`, `THUNK_ORDINAL`, `THUNK_TARGET_RVA` | Present when matched; see [VTABLE_FEATURE.md](VTABLE_FEATURE.md#vtable-thunks). |
| `THIS_ADJUST` | The matched method's DIA `this`-adjustment. |
| *(bare value, no `KEY=`)* | The matched method's calling convention (`__thiscall`, `__cdecl`, ...) — see [OUTPUT_FORMAT.md](OUTPUT_FORMAT.md). ABI metadata, so present in both `_vtable.sym` and the obfuscated file: right before `METHOD=` in the former, right before `OBFUSCATED=` in the latter (there is no `METHOD=` there to precede). Omitted entirely when unmatched. |
| `METHOD` | Real name + signature of the matched method. Present only in `_vtable.sym` (omitted in the obfuscated file). `UNKNOWN` when unmatched. |
| `METHOD_RETURN_TYPE` | The matched method's return type, same resolution as `RETURN_TYPE` in the main `.sym` file. Present only in `_vtable.sym`. |
| `METHOD_CLASS` | The matched method's own `className` (can differ from the table's `CLASS` for an inherited slot). Present only in `_vtable.sym`. |
| `SOURCE_FILE` | The matched method's source file + line (`File.cpp:123`), same resolution as `SOURCE_FILE` in the main `.sym` file — see [OUTPUT_FORMAT.md](OUTPUT_FORMAT.md). The line's last field. Present only in `_vtable.sym` (omitted in the obfuscated file, same as `METHOD`/`METHOD_RETURN_TYPE`/`METHOD_CLASS`); omitted entirely when unmatched. |

### Match types

| Value | Meaning |
|-------|---------|
| `CLASS_SHAPE` | A function matched on `(classParentId, shapeId, slot)` exactly. |
| `CLASS_SHAPE_THUNK` | Same as above, but the matched symbol is a `SymTagThunk` rather than a plain function. |
| `SHAPE` | No exact class match; a function matched by `(shapeId, slot)` alone (useful for inherited slots where the method's own `classParentId` differs from the table's owning class). |
| `SHAPE_THUNK` | Same as `SHAPE`, but the matched symbol is a thunk. |
| `NONE` | DIA did not provide enough information to associate any method with this slot. Reported as `UNKNOWN` rather than guessed. |

When both a thunk and a plain function are candidates for the same slot, the
thunk is preferred (`*_THUNK` match types), because the thunk is typically
the concrete callable entry actually stored in the table. See
[VTABLE_FEATURE.md](VTABLE_FEATURE.md#vtable-thunks).

### Column alignment

`SLOT`, `OFFSET`, `RVA`, and `SIZE` are each left-padded with trailing
spaces to a column width computed once for the whole file, from the widest
value that actually occurs in it — the same technique, and the same
reasoning (a naturally small, bounded value space is safe to align; an
unbounded one like `METHOD`/`METHOD_CLASS` is not), that
[OUTPUT_FORMAT.md](OUTPUT_FORMAT.md)'s "Limitations" section describes for
`VISIBILITY`/`SIZE`/`CALLING_CONVENTION` in the main `.sym` file. `SLOT`'s
and `OFFSET`'s widths are bounded by
the largest vtable's own slot count in this PDB; `RVA`'s and `SIZE`'s are
bounded by the largest RVA/byte-length among the virtual methods this file
could ever match into a slot. An unmatched slot's `RVA=-1`/`SIZE=-1` get the
same padding as a resolved value, so the field that follows still lines up.
This is purely cosmetic — it changes only how much whitespace separates
these fields, never any value — and applies identically to `_vtable.sym`
and `_vtable_obfuscated.sym`.

### Synthetic tables

DIA does not always emit a `SymTagVTable` symbol for every class that has
virtual methods. When a virtual method carries a `(classParentId, shapeId)`
pair with no matching concrete `SymTagVTable` of a usable size, ObfSymbolsEx
adds a synthetic table (`VTABLE_SOURCE=METHOD_METADATA`) so the class's
virtual interface is still visible. Synthetic tables never claim a concrete
symbol ID or RVA (`ID=-`, `RVA=-`) — that information genuinely doesn't
exist for them.

**Pointer size for synthetic tables:** both concrete and synthetic tables use
the same `pointerSize` — the PDB's own target pointer size (4 or 8),
determined once from the PDB machine type during extraction and passed
straight through to the writer. A synthetic table has no vtable of its own
to read a pointer size from, but there is nothing to derive or guess either:
every `VTABLE_OFFSET`/`VTABLE_INDEX` conversion for this PDB, synthetic
table or not, already used this one value, so `OFFSET = SLOT * pointerSize`
is a direct calculation from known-correct DIA data, the same as everywhere
else in this file.

### Sort order

1. **Tables** are ordered by: class name (alphabetical) → `SHAPE` (ascending)
   → concrete tables before synthetic ones → DIA symbol `ID` (ascending).
   A class with more than one vtable (e.g. multiple inheritance) therefore
   gets one header block per vtable, grouped together by name but separated
   by shape.
2. **Slots within a table** are always listed in ascending `SLOT` order,
   `0` through `SLOTS - 1`, with no gaps — an unmatched index still gets a
   `MATCH=NONE` line at its position.

---

## `_vtable_classes.sym`

### Format

```text
CLASS CBaseEntity CLASS_ID=456 VTABLE_SLOTS=87
  [0] RVA=0x1034A100 __thiscall CBaseEntity::~CBaseEntity() -> void SHAPE=789 INTRO=1 THIS_ADJUST=0 KIND=DESTRUCTOR SOURCE_FILE=baseentity.cpp:1780
  [26] RVA=0x1034D3E0 __thiscall CBaseEntity::SetModel(char const*) -> void SHAPE=789 INTRO=0 THIS_ADJUST=0 KIND=FUNCTION SOURCE_FILE=baseentity.cpp:1902
  [41] RVA=0x10351220 __thiscall CBaseEntity::Precache() -> void SHAPE=789 INTRO=1 THIS_ADJUST=0 KIND=THUNK THUNK_ORDINAL=2 THUNK_TARGET_RVA=0x1035A000 SOURCE_FILE=baseentity.cpp:2140
```

- A `CLASS` header starts each group: class name, DIA class-symbol ID, and
  (when known) the largest `VTABLE_SLOTS` seen among the class's methods.
- Each following indented line is one virtual method: its slot index in
  `[brackets]` (left-padded to a column width computed once for the whole
  file, from the widest index that occurs anywhere in it — the same
  technique described for `_vtable.sym`'s `SLOT`/`OFFSET`/`RVA`/`SIZE` in
  "Column alignment" above, applied globally here rather than per class, so
  `[9]` in one class's group lines up with `[238]` in another's), its RVA,
  its calling convention (bare value, no `KEY=`
  prefix), its real name + signature, its return type (inline
  `-> ReturnType`), its `VTABLE_SHAPE`, whether it introduces the slot
  (`INTRO`), its `THIS_ADJUST`, its `KIND`, and (last) its `SOURCE_FILE`
  (`File.cpp:Line`, or `?` if DIA has no line data there — see
  [OUTPUT_FORMAT.md](OUTPUT_FORMAT.md)). `THUNK_ORDINAL` /
  `THUNK_TARGET_RVA` are appended only when `KIND` is a thunk, before
  `SOURCE_FILE`. There is no `OFFSET=` field here — `_vtable.sym` is the
  file for slot-offset detail;
  this file only ever showed `VTABLE_OFFSET`'s derived `RVA`/`SHAPE`/index
  view, and offset added no information beyond what `RVA` and the bracketed
  index already convey, so it was dropped.
- This file **always shows real names and has no obfuscated counterpart** —
  there is no `_vtable_classes_obfuscated.sym`.
- Unlike `_vtable.sym`, there are **no `UNKNOWN` placeholder entries**: only
  methods DIA actually reported as virtual appear. A gap in the index
  sequence (e.g. `[0]` then `[26]`) simply means no method in this class
  introduced or overrode that slot — it does not mean the slot is unknown.
- This file was named `_vtable_methods.sym` in earlier versions of
  ObfSymbolsEx; it was renamed to `_vtable_classes.sym` to read more naturally
  alongside the new `_vtable_inheritance.sym` (below), which reports on
  exactly the same set of classes.

### Grouping and sort order

This is the answer to "what order does this report use": **grouped by class,
then sorted by vtable index within the class** — not by DIA symbol
enumeration order and not by RVA.

1. **Groups** are formed per class, matched primarily by DIA `classParentId`
   (falling back to matching by `className` alone when a symbol's
   `classParentId` is `0`). Groups are then sorted by class name
   (alphabetical), then by `classParentId`.
2. **Methods within a group** are sorted by: `VTABLE_INDEX` (ascending) →
   `VTABLE_SHAPE` (ascending) → plain function before thunk → method name
   (alphabetical) → RVA (ascending). The shape tiebreaker matters for a class
   with multiple vtables: slot `[0]` of one shape and slot `[0]` of another
   both sort to the same index and are distinguished only by their `SHAPE=`
   field, so **read `SHAPE=` alongside the bracketed index for a
   multiple-inheritance class** — don't assume two methods showing the same
   `[N]` belong to the same table. `_vtable.sym` keeps such tables in
   separate header blocks instead, which is the clearer view for that case.

### Differences in what each file includes

`_vtable_classes.sym` includes any virtual method with a valid
`VTABLE_INDEX` and a known `className` — it does **not** require a non-zero
`VTABLE_SHAPE`. `_vtable.sym` slot association, by contrast, requires a
non-zero `VTABLE_SHAPE` (its lookup keys are `(classParentId, shapeId,
slot)` and `(shapeId, slot)`). A method with an index but no shape ID can
therefore appear in `_vtable_classes.sym` while having no corresponding slot
line in `_vtable.sym`. This is expected, not a bug: it reflects what DIA
actually exposed for that particular symbol.

---

## `_vtable_inheritance.sym`

For every class that appears in `_vtable_classes.sym` (i.e. every class DIA
gives at least one virtual method with a valid `VTABLE_INDEX` for), this file
prints its **complete, recursive base-class chain — down to the root class(es)
— as an ASCII tree**, derived entirely from DIA's own `SymTagBaseClass` data.
It carries no vtable information at all; it exists purely to make sense of
class names appearing elsewhere.

### Format

```text
CLASS CASW_Marine CLASS_ID=36
  +-- CASW_VPhysics_NPC
  |   `-- CASW_Inhabitable_NPC
  |       +-- CAI_PlayerAlly
  |       |   `-- ... (continues up to the root)
  |       `-- IASW_Spawnable_NPC
  +-- IASWPlayerAnimStateHelpers
  `-- IASW_Server_Usable_Entity

CLASS CASW_Inhabitable_NPC CLASS_ID=243
  +-- CAI_PlayerAlly
  |   `-- ... (continues up to the root)
  `-- IASW_Spawnable_NPC
```

- A `CLASS <name> CLASS_ID=<id>` header starts each class's tree.
- `+--` marks a branch with siblings still to come at that level; `` `-- ``
  marks the last branch at that level (standard ASCII-tree convention, the
  same one used by e.g. the Unix `tree` command in its non-Unicode mode).
  `|` continues a vertical line for a level that still has more siblings
  below the current line; blank space continues a level whose last sibling
  has already been printed.
- A class with no bases (a genuine root — an interface or base class DIA
  reports no `SymTagBaseClass` for) prints its `CLASS` header with no tree
  lines beneath it.
- A base class ObfSymbolsEx could not name (its ID appears in the graph but
  DIA never provided a name for it — rare) prints as `UnknownClass#<id>`
  instead of being silently dropped.
- Entries are separated by a blank line, and appear in the same
  alphabetical-by-class-name order as `_vtable_classes.sym`.

### Multiple inheritance and repeated ancestors

Multiple direct bases become multiple sibling branches at the same
indentation level, exactly as in the `CASW_Marine` example above (three
direct bases: `CASW_VPhysics_NPC`, `IASWPlayerAnimStateHelpers`,
`IASW_Server_Usable_Entity`). If an ancestor is reachable through more than
one path (diamond inheritance), it is printed once **per path** — this is
intentional: each occurrence represents a distinct inheritance route, and
collapsing them into one would hide which branch actually contributes it. A
defensive cycle guard stops recursion (printing `(cycle detected in PDB
data, stopping)`) if a class ever reappears as its own ancestor along one
path; this should not happen for a well-formed C++ hierarchy, but PDB data
is not proof against anomalies.

### Why every class's tree is printed in full

The tree for `CASW_Marine` above fully repeats `CASW_Inhabitable_NPC`'s own
ancestry, and `CASW_Inhabitable_NPC` gets its own complete top-level entry
too. This duplication is deliberate: the report's purpose is that looking up
*any single class of interest* shows its complete ancestry in one place,
without needing to cross-reference other entries in the file.

---

## Multiple inheritance

A class may have more than one concrete `SymTagVTable` symbol. ObfSymbolsEx
keeps those tables separate using the DIA VTable symbol ID, class-parent ID,
and shape ID — two vtables belonging to one class are never merged merely
because they have the same slot count. `VTABLE_INDEX` is the slot within the
vtable identified by a method's own shape metadata; it is **not** a globally
unique identifier across a class's multiple vtables. See
[VTABLE_FEATURE.md](VTABLE_FEATURE.md#secondary-multiple-inheritance-bases-recovering-the-right-vtable_shape)
for the underlying per-method fields this reconstruction is built from,
including how a secondary interface's own `VTABLE_SHAPE`/`VTABLE_SLOTS` are
recovered when the implementing class can only report one of its several
shapes at its own class level.

## Overriding virtual methods now resolve to their real slot

Both `_vtable.sym` and `_vtable_classes.sym` are built from each method's
`VTABLE_INDEX`/`VTABLE_OFFSET`. An earlier version of ObfSymbolsEx reported
`VTABLE_INDEX=0` for **every** overriding (non-introducing) virtual method,
including every virtual destructor override and every override of a
pure-virtual interface method with no compiled body — because DIA's PDB data
simply has no slot offset recorded for these (see
[VTABLE_FEATURE.md](VTABLE_FEATURE.md#overriding-virtual-methods-vtable_index-inheritance)
for why). That made every such override collide at slot `0` in
`_vtable_classes.sym` and at whatever method happened to occupy slot `0` of
its own class's table in `_vtable.sym`.

ObfSymbolsEx now walks each override's base-class chain (from the same PDB) to
find the ancestor that actually introduces the virtual function — including
an ancestor whose introducing declaration has no compiled body at all — and
adopts that ancestor's slot. An override that still can't be resolved this
way (no matching introducing ancestor found anywhere in the PDB) is reported
as `VTABLE_OFFSET=-1 VTABLE_INDEX=-1` rather than the misleading `0` — such a
method now correctly produces a `MATCH=NONE`/`UNKNOWN` slot entry in
`_vtable.sym` (or is simply absent from `_vtable_classes.sym`, which only
lists methods with a valid index) instead of silently occupying someone
else's slot 0.
