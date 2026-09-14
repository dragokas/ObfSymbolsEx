# msdia140.dll Location Strategy (Embedded-Resource Approach Removed)

## Status: Removed

An earlier version of ObfSymbolsEx embedded `msdia140.dll` inside the executable
as a Windows `RCDATA` resource (`ObfSymbols.rc` / `resource.h`) and extracted
it to disk on first run if no local copy was found. **This approach has been
intentionally removed.** There is no `.rc` file, no `resource.h`, and no
extraction code in the current source — `ObfSymbolsEx.exe` does not embed or
extract `msdia140.dll`.

It was removed because bundling a ~2.2 MB Microsoft-owned binary inside the
executable added build complexity (the DLL had to exist at a fixed path
before compiling, or the resource step silently produced a stale copy) and
increased the chance of antivirus false positives on a self-extracting EXE,
without a strong enough benefit over simply searching `%PATH%`.

## Current Strategy

`PdbSymbolExtractor::FindMsdiaDll()` locates `msdia140.dll` by checking, in
order:

1. **The executable's own directory** — `msdia140.dll` next to `ObfSymbolsEx.exe`.
2. **Known Visual Studio 2022 installation paths** — Professional, Enterprise,
   and Community editions, under both `Program Files` and
   `Program Files (x86)`, plus the VS Installer's own `Feedback\amd64` copy.
3. **`%PATH%`** — via `SearchPathW`, so any directory on the process's search
   path (including a Visual Studio Developer Command Prompt environment, or a
   directory the user added manually) is honored as a last resort.

If none of these locate the DLL, initialization fails with a message asking
the user to install Visual Studio 2022 with the DIA SDK or place
`msdia140.dll` next to the executable.

## Distribution

Because the DLL is not embedded, distributing ObfSymbolsEx still means shipping
two files together:

- `ObfSymbolsEx.exe`
- `msdia140.dll`

See [README.md](README.md) and [IMPLEMENTATION_NOTES.md](IMPLEMENTATION_NOTES.md)
for the full DLL-location and distribution details.
