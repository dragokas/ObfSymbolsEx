# Unit-test suite for every feature ObfSymbolsEx adds on top of the original
# ObfSymbols: VTable dump, return types, calling convention, complex/basic
# type detection, source file+line, ICF fixes, destructor/pure-virtual
# override resolution, direct .exe/.dll input, column-aligned output, the
# -fc/-fm report filtering switches, and the simplified sort-by-class/
# sort-by-name reports.
#
# Unlike validate.ps1 (a build-and-eyeball smoke test), every check here
# asserts an exact field value extracted from real .sym output and the
# script exits non-zero if any assertion fails, so it is safe to wire into
# CI. Uses TestApp (EXE) and TestDLL (DLL) as the PDB fixtures; both were
# extended with dedicated constructs for previously-uncovered features
# (multiple inheritance, char16_t/char32_t, fixed arrays, function pointers,
# pointer-to-member-functions, destructor override chains, ICF-foldable
# identical bodies).

param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$obfExe = Join-Path $root "ObfSymbolsEx\x64\Release\ObfSymbolsEx.exe"
$resultsDir = Join-Path $root "UnitTestResults"

$script:passCount = 0
$script:failCount = 0
$script:failures = @()
$script:currentSection = ""

function Section($name) {
    $script:currentSection = $name
    Write-Host ""
    Write-Host "=== $name ===" -ForegroundColor Cyan
}

function Assert-True {
    param([bool]$Condition, [string]$Description, [string]$Detail = "")
    if ($Condition) {
        $script:passCount++
        Write-Host "  [PASS] $Description" -ForegroundColor Green
    } else {
        $script:failCount++
        $script:failures += "[$script:currentSection] $Description $Detail"
        Write-Host "  [FAIL] $Description $Detail" -ForegroundColor Red
    }
}

function Assert-Equal {
    param($Actual, $Expected, [string]$Description)
    $ok = ($Actual -eq $Expected)
    Assert-True $ok $Description "(expected '$Expected', got '$Actual')"
}

function Get-Field {
    param([string]$Line, [string]$FieldName)
    if ($null -eq $Line) { return $null }
    if ($Line -match "$FieldName=(\S*)") { return $Matches[1] }
    return $null
}

function Get-ReturnType {
    param([string]$Line)
    if ($null -eq $Line) { return $null }
    # Non-greedy: some return types contain spaces themselves, e.g.
    # "std::basic_string<char,std::char_traits<char>,std::allocator<char> >".
    if ($Line -match ' -> (.+?) IS_VIRTUAL=') { return $Matches[1] }
    return $null
}

function Get-CallingConvention {
    param([string]$Line)
    if ($null -eq $Line) { return $null }
    if ($Line -match 'obf_[0-9A-Fa-f]+\s+(\S+)\s') { return $Matches[1] }
    return $null
}

function Find-Line {
    param($Lines, [string]$Pattern)
    return ($Lines | Where-Object { $_ -match $Pattern } | Select-Object -First 1)
}

function Invoke-Build {
    param([string]$ProjectDir, [string]$Configuration, [string]$Platform)
    Push-Location $ProjectDir
    try {
        & .\build.ps1 -Configuration $Configuration -Platform $Platform | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "Build failed: $ProjectDir ($Configuration|$Platform)"
        }
    } finally {
        Pop-Location
    }
}

# ============================================================
# Build phase
# ============================================================
if (-not $SkipBuild) {
    Write-Host "Building fixtures..." -ForegroundColor Yellow
    Invoke-Build (Join-Path $root "ObfSymbolsEx") "Release" "x64"
    Invoke-Build (Join-Path $root "TestApp") "Debug" "x64"
    Invoke-Build (Join-Path $root "TestDLL") "Debug" "x64"
    Invoke-Build (Join-Path $root "TestDLL") "Release" "x64"
}

if (-not (Test-Path $obfExe)) {
    Write-Host "ERROR: $obfExe not found. Run without -SkipBuild first." -ForegroundColor Red
    exit 1
}

New-Item -ItemType Directory -Path $resultsDir -Force | Out-Null

# ============================================================
# Extraction phase
# ============================================================
$testAppPdb = Join-Path $root "TestApp\x64\Debug\TestApp.pdb"
$testAppExe = Join-Path $root "TestApp\x64\Debug\TestApp.exe"
$testDllPdb = Join-Path $root "TestDLL\x64\Debug\TestDLL.pdb"
$testDllDll = Join-Path $root "TestDLL\x64\Debug\TestDLL.dll"
$testDllReleasePdb = Join-Path $root "TestDLL\x64\Release\TestDLL.pdb"

$outApp = Join-Path $resultsDir "testapp"
$outAppFromExe = Join-Path $resultsDir "testapp_fromexe"
$outDll = Join-Path $resultsDir "testdll"
$outDllFromDll = Join-Path $resultsDir "testdll_fromdll"
$outDllRelease = Join-Path $resultsDir "testdll_release"

foreach ($pair in @(
    @($testAppPdb, "$outApp.sym"),
    @($testAppExe, "$outAppFromExe.sym"),
    @($testDllPdb, "$outDll.sym"),
    @($testDllDll, "$outDllFromDll.sym"),
    @($testDllReleasePdb, "$outDllRelease.sym")
)) {
    $input = $pair[0]
    $output = $pair[1]
    if (-not (Test-Path $input)) {
        Write-Host "ERROR: input not found: $input" -ForegroundColor Red
        exit 1
    }
    & $obfExe $input $output | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: extraction failed for $input" -ForegroundColor Red
        exit 1
    }
}

$appLines = Get-Content "$outApp.sym"
$dllLines = Get-Content "$outDll.sym"
$dllReleaseLines = Get-Content "$outDllRelease.sym"
$appVtableClasses = Get-Content "$outApp`_vtable_classes.sym"
$appVtableInheritance = Get-Content "$outApp`_vtable_inheritance.sym"
$appObfuscated = Get-Content "$outApp`_obfuscated.sym"

# ============================================================
# A. Return types
# ============================================================
Section "Return types"

$line = Find-Line $appLines '\bFunctionWithReturn\(int\)'
Assert-Equal (Get-ReturnType $line) "int" "FunctionWithReturn(int) resolves return type 'int'"

$line = Find-Line $dllLines '\bMathOperations::Add\(double, double\)'
Assert-Equal (Get-ReturnType $line) "double" "MathOperations::Add(double,double) resolves return type 'double'"

$line = Find-Line $appLines '\bWidget::Serialize\(\)'
$ret = Get-ReturnType $line
Assert-True ($ret -and $ret -ne "?" -and $ret -match "basic_string") `
    "Widget::Serialize() resolves a complex STL return type (not '?')" "(got '$ret')"

# ============================================================
# B. Calling convention
# ============================================================
Section "Calling convention"

$line = Find-Line $dllLines '\bIntegerAdd\(int, int\)'
Assert-Equal (Get-CallingConvention $line) "__cdecl" 'extern "C" IntegerAdd reports __cdecl calling convention'

$line = Find-Line $dllLines '\bDoubleMultiply\(double, double\)'
Assert-Equal (Get-CallingConvention $line) "__cdecl" 'extern "C" DoubleMultiply reports __cdecl calling convention'

# ============================================================
# C. Complex/basic type detection
# ============================================================
Section "Complex/basic type detection"

$line = Find-Line $appLines '\bWideCharParameters\('
Assert-True ($line -match '\bWideCharParameters\(char16_t, char32_t\)') `
    "char16_t/char32_t parameters spelled correctly" "(line: $line)"

$line = Find-Line $appLines '\bSumFixedArray\('
Assert-True ($line -match '\bSumFixedArray\(float\[3\] &\) -> double\b') `
    "Fixed-size array-by-reference parameter spelled 'float[3] &'" "(line: $line)"

$line = Find-Line $appLines '\bInvokeCallback\('
Assert-True ($line -match '\bInvokeCallback\(void \(__cdecl \*\)\(int\), int\) -> void\b') `
    "Ordinary function-pointer parameter spelled 'void (__cdecl *)(int)'" "(line: $line)"

$line = Find-Line $appLines '\bInvokeCalculatorMethod\('
Assert-True ($line -match '\bInvokeCalculatorMethod\(Calculator &, double \(__cdecl Calculator::\*\)\(double\), double\) -> double\b') `
    "Pointer-to-member-function parameter spelled 'double (__cdecl Calculator::*)(double)'" "(line: $line)"

$line = Find-Line $appLines '\bVariadicFunction\('
Assert-True ($line -match '\bVariadicFunction\(int, \.\.\.\) -> void\b') `
    "Variadic '...' parameter preserved" "(line: $line)"

# ============================================================
# D. Source file + line number
# ============================================================
Section "Source file + line number"

$line = Find-Line $appLines '\bFunctionWithReturn\(int\)'
Assert-True ((Get-Field $line "SOURCE_FILE") -match '^TestApp\.cpp:\d+$') `
    "FunctionWithReturn has a resolved TestApp.cpp:<line> SOURCE_FILE" "(got '$(Get-Field $line "SOURCE_FILE")')"

$line = Find-Line $dllLines '\bMathOperations::Add\(double, double\)'
Assert-True ((Get-Field $line "SOURCE_FILE") -match '^TestDLL\.cpp:\d+$') `
    "MathOperations::Add has a resolved TestDLL.cpp:<line> SOURCE_FILE" "(got '$(Get-Field $line "SOURCE_FILE")')"

# ============================================================
# E. Destructor / pure-virtual override resolution
# ============================================================
Section "Destructor / pure-virtual override resolution"

$shapeDtor = Find-Line $appLines '\bShape::~Shape\(\)'
$rectDtor = Find-Line $appLines '\bRectangle::~Rectangle\(\)'
$circDtor = Find-Line $appLines '\bCircle::~Circle\(\)'

Assert-True (($shapeDtor -ne $null) -and ($rectDtor -ne $null) -and ($circDtor -ne $null)) `
    "Shape/Rectangle/Circle destructors all present"

Assert-Equal (Get-Field $rectDtor "VTABLE_INDEX") (Get-Field $shapeDtor "VTABLE_INDEX") `
    "Rectangle::~Rectangle inherits Shape's introducing VTABLE_INDEX"
Assert-Equal (Get-Field $circDtor "VTABLE_INDEX") (Get-Field $shapeDtor "VTABLE_INDEX") `
    "Circle::~Circle inherits Shape's introducing VTABLE_INDEX"
Assert-Equal (Get-Field $rectDtor "VTABLE_SHAPE") (Get-Field $shapeDtor "VTABLE_SHAPE") `
    "Rectangle::~Rectangle shares Shape's VTABLE_SHAPE"
Assert-Equal (Get-Field $rectDtor "KIND") "DESTRUCTOR" "Rectangle::~Rectangle is classified KIND=DESTRUCTOR"
Assert-Equal (Get-Field $shapeDtor "IS_VIRTUAL") "1" "Shape::~Shape is IS_VIRTUAL=1"

$rectArea = Find-Line $appLines '\bRectangle::Area\(\)'
$circArea = Find-Line $appLines '\bCircle::Area\(\)'
Assert-Equal (Get-Field $rectArea "VTABLE_INDEX") (Get-Field $circArea "VTABLE_INDEX") `
    "Rectangle::Area and Circle::Area resolve to the same inherited VTABLE_INDEX (pure-virtual override)"
Assert-Equal (Get-Field $rectArea "VTABLE_SHAPE") (Get-Field $circArea "VTABLE_SHAPE") `
    "Rectangle::Area and Circle::Area share the same VTABLE_SHAPE"
Assert-True ((Get-Field $rectArea "VTABLE_INDEX") -ne (Get-Field $rectDtor "VTABLE_INDEX")) `
    "Area and destructor occupy distinct vtable slots on the same class"

# ============================================================
# F. VTable methods dump / multiple inheritance
# ============================================================
Section "VTable methods dump / multiple inheritance"

Assert-True (($appVtableClasses -join "`n") -match 'CLASS Renderable CLASS_ID=\d+ VTABLE_SLOTS=\d+') `
    "Renderable gets its own vtable-classes header"
Assert-True (($appVtableClasses -join "`n") -match 'CLASS Serializable CLASS_ID=\d+ VTABLE_SLOTS=\d+') `
    "Serializable gets its own independent vtable-classes header"

$widgetBlockStart = ($appVtableClasses | Select-String -Pattern '^CLASS Widget ' | Select-Object -First 1).LineNumber
Assert-True ($widgetBlockStart -ne $null) "Widget gets its own vtable-classes header"
if ($widgetBlockStart) {
    $widgetBlock = $appVtableClasses[($widgetBlockStart - 1)..([Math]::Min($widgetBlockStart + 5, $appVtableClasses.Count - 1))] -join "`n"
    Assert-True ($widgetBlock -match 'Widget::Render\(\)') "Widget's own vtable block lists Render (Renderable base)"
    Assert-True ($widgetBlock -match 'Widget::Serialize\(\)') "Widget's own vtable block lists Serialize (Serializable base)"
}

$widgetInhStart = ($appVtableInheritance | Select-String -Pattern '^CLASS Widget ' | Select-Object -First 1).LineNumber
Assert-True ($widgetInhStart -ne $null) "Widget appears in the vtable-inheritance dump"
if ($widgetInhStart) {
    $widgetInhBlock = $appVtableInheritance[($widgetInhStart - 1)..([Math]::Min($widgetInhStart + 5, $appVtableInheritance.Count - 1))] -join "`n"
    Assert-True ($widgetInhBlock -match 'Renderable') "Widget's inheritance tree lists Renderable"
    Assert-True ($widgetInhBlock -match 'Serializable') "Widget's inheritance tree lists Serializable"
}

# ============================================================
# G. Identical Code Folding (ICF) fix
# ============================================================
Section "Identical Code Folding (ICF) fix"

$icfA = Find-Line $dllReleaseLines '\bICFTestA::Zero\(\)'
$icfB = Find-Line $dllReleaseLines '\bICFTestB::Zero\(\)'
Assert-True (($icfA -ne $null) -and ($icfB -ne $null)) "Both ICFTestA::Zero and ICFTestB::Zero are present in the Release build"

if ($icfA -and $icfB) {
    $rvaA = ($icfA -split '\s+')[1]
    $rvaB = ($icfB -split '\s+')[1]
    $foldedByLinker = ($rvaA -eq $rvaB)
    Write-Host "  (info) linker folded ICFTestA::Zero/ICFTestB::Zero onto the same RVA: $foldedByLinker" -ForegroundColor Gray

    Assert-True ($icfA -ne $icfB) "ICFTestA::Zero and ICFTestB::Zero are emitted as two distinct, non-corrupted lines"
    $obfA = ($icfA -split '\s+') | Where-Object { $_ -like 'obf_*' } | Select-Object -First 1
    $obfB = ($icfB -split '\s+') | Where-Object { $_ -like 'obf_*' } | Select-Object -First 1
    Assert-True ($obfA -ne $obfB) "ICFTestA::Zero and ICFTestB::Zero get distinct obfuscated names despite a shared RVA"
    Assert-Equal (Get-Field $icfA "VTABLE_CLASS") "ICFTestA" "ICFTestA::Zero keeps its own class attribution (not swapped with ICFTestB)"
    Assert-Equal (Get-Field $icfB "VTABLE_CLASS") "ICFTestB" "ICFTestB::Zero keeps its own class attribution (not swapped with ICFTestA)"
}

# ============================================================
# H. Direct .exe/.dll input (loadDataForExe)
# ============================================================
Section "Direct .exe/.dll input"

$appFromExeLines = Get-Content "$outAppFromExe.sym"
$dllFromDllLines = Get-Content "$outDllFromDll.sym"

$appDiff = Compare-Object $appLines $appFromExeLines
Assert-True ($appDiff -eq $null) "TestApp.exe input produces byte-identical output to TestApp.pdb input" `
    "($(if ($appDiff) { $appDiff.Count } else { 0 }) differing lines)"

$dllDiff = Compare-Object $dllLines $dllFromDllLines
Assert-True ($dllDiff -eq $null) "TestDLL.dll input produces byte-identical output to TestDLL.pdb input" `
    "($(if ($dllDiff) { $dllDiff.Count } else { 0 }) differing lines)"

# ============================================================
# I. Column-aligned output
# ============================================================
Section "Column-aligned output"

$misaligned = $appLines | Where-Object { $_.Length -lt 10 -or $_.Substring(8, 2) -ne "0x" }
Assert-True ($misaligned.Count -eq 0) "Every mapping-file line's address starts at a fixed column (8), regardless of PUBLIC/PRIVATE width" `
    "($($misaligned.Count) misaligned lines)"

# ============================================================
# J. Obfuscated name / dual-output-file consistency
# ============================================================
Section "Obfuscated name / dual-output-file consistency"

$badFormat = $appLines | Where-Object { $_ -notmatch 'obf_[0-9A-Fa-f]{8}' }
Assert-True ($badFormat.Count -eq 0) "Every symbol has an 'obf_XXXXXXXX' obfuscated name" "($($badFormat.Count) malformed)"

Assert-Equal $appObfuscated.Count $appLines.Count "Obfuscated file has exactly one line per mapping-file line"

$mappingLine = Find-Line $appLines '\bFunctionWithReturn\(int\)'
$mappingObfName = ($mappingLine -split '\s+')[3]
$obfCounterpart = Find-Line $appObfuscated ([regex]::Escape($mappingObfName))
Assert-True ($obfCounterpart -ne $null) "FunctionWithReturn's obfuscated name also appears in the obfuscated-only file"
if ($obfCounterpart) {
    Assert-True ($obfCounterpart -notmatch 'FunctionWithReturn') "Obfuscated file does not leak the real function name"
    Assert-True ($obfCounterpart -notmatch ' -> ') "Obfuscated file omits the return type"
    Assert-True ($obfCounterpart -notmatch 'SOURCE_FILE=') "Obfuscated file omits SOURCE_FILE"
}

# ============================================================
# K. Report filtering (-fc/-fm)
# ============================================================
Section "Report filtering (-fc/-fm)"

# -fc+: substring match, not exact -- "Rect" alone should still pull in
# Rectangle's own lines and nothing else's.
$outFcInclude = Join-Path $resultsDir "filter_fc_include"
& $obfExe $testAppPdb "$outFcInclude.sym" -fc+Rect | Out-Null
$fcIncludeLines = Get-Content "$outFcInclude.sym"
Assert-True (($fcIncludeLines | Where-Object { $_ -match 'VTABLE_CLASS=Rectangle\b' }).Count -gt 0) `
    "-fc+Rect keeps Rectangle's own lines (substring match, not exact)"
Assert-True (($fcIncludeLines | Where-Object { $_ -match 'VTABLE_CLASS=(Circle|Shape|Widget|Calculator)\b' }).Count -eq 0) `
    "-fc+Rect drops every other class's lines"
Assert-True (($fcIncludeLines | Where-Object { $_ -match '\bFunctionWithReturn\(' }).Count -eq 0) `
    "-fc+Rect drops free functions (no class name to match against)"

# -fc-: the mirror image -- Rectangle disappears, everything else survives.
$outFcExclude = Join-Path $resultsDir "filter_fc_exclude"
& $obfExe $testAppPdb "$outFcExclude.sym" -fc-Rectangle | Out-Null
$fcExcludeLines = Get-Content "$outFcExclude.sym"
Assert-True (($fcExcludeLines | Where-Object { $_ -match 'VTABLE_CLASS=Rectangle\b' }).Count -eq 0) `
    "-fc-Rectangle drops Rectangle's own lines"
Assert-True (($fcExcludeLines | Where-Object { $_ -match '\bCircle::Area\(' }).Count -gt 0) `
    "-fc-Rectangle keeps other classes' lines"
Assert-True (($fcExcludeLines | Where-Object { $_ -match '\bFunctionWithReturn\(' }).Count -gt 0) `
    "-fc-Rectangle keeps free functions"

# -fc is always case-insensitive.
$outFcUpper = Join-Path $resultsDir "filter_fc_case_upper"
& $obfExe $testAppPdb "$outFcUpper.sym" -fc+RECTANGLE | Out-Null
$outFcMixed = Join-Path $resultsDir "filter_fc_case_mixed"
& $obfExe $testAppPdb "$outFcMixed.sym" -fc+ReCtAnGlE | Out-Null
$caseDiff = Compare-Object (Get-Content "$outFcUpper.sym") (Get-Content "$outFcMixed.sym")
Assert-True ($caseDiff -eq $null) "-fc matching is case-insensitive (RECTANGLE == ReCtAnGlE)"

# -fm+/-fm- on server.sym test the WHOLE line, so they can match PUBLIC/
# PRIVATE -- not just the method name/signature.
$outFmInclude = Join-Path $resultsDir "filter_fm_include"
& $obfExe $testAppPdb "$outFmInclude.sym" -fm+PUBLIC | Out-Null
$fmIncludeLines = Get-Content "$outFmInclude.sym"
Assert-True (($fmIncludeLines | Where-Object { $_ -notmatch '^PUBLIC' }).Count -eq 0) `
    "-fm+PUBLIC keeps only PUBLIC lines (matches on the whole line, not just the name)"
Assert-True ($fmIncludeLines.Count -gt 0) "-fm+PUBLIC still keeps at least one line"

$outFmExclude = Join-Path $resultsDir "filter_fm_exclude"
& $obfExe $testAppPdb "$outFmExclude.sym" -fm-PRIVATE | Out-Null
$fmExcludeLines = Get-Content "$outFmExclude.sym"
$baselinePublicCount = ($appLines | Where-Object { $_ -match '^PUBLIC' }).Count
Assert-True (($fmExcludeLines | Where-Object { $_ -match '^PRIVATE' }).Count -eq 0) `
    "-fm-PRIVATE drops every PRIVATE line"
Assert-Equal ($fmExcludeLines | Where-Object { $_ -match '^PUBLIC' }).Count $baselinePublicCount `
    "-fm-PRIVATE leaves every PUBLIC line untouched"

# Combined -fc+ -fm+ -fm- against server_vtable_classes.sym, mirroring the
# README's own worked example (-fc+CBaseEntity -fm+model -fm-Index): -fc
# keeps/drops a whole group (header + members), -fm then keeps/drops
# individual member lines within whatever group survived -fc.
$outCombined = Join-Path $resultsDir "filter_combined"
& $obfExe $testAppPdb "$outCombined.sym" -fc+Rectangle -fm+RVA -fm-Perimeter | Out-Null
$combinedText = (Get-Content "$outCombined`_vtable_classes.sym") -join "`n"
Assert-True ($combinedText -match 'CLASS Rectangle CLASS_ID=\d+') `
    "-fc+Rectangle keeps the Rectangle group in server_vtable_classes.sym"
Assert-True ($combinedText -notmatch 'CLASS Circle ') `
    "-fc+Rectangle drops the Circle group entirely (header and members)"
Assert-True ($combinedText -match 'Rectangle::(~Rectangle|Area)\(') `
    "-fm+RVA (a token common to every member line) keeps Rectangle's other lines"
Assert-True ($combinedText -notmatch 'Perimeter') `
    "-fm-Perimeter removes just the Perimeter line from the surviving group"

# WORD may be quoted; behavior must be identical to the unquoted form.
$outFcExact = Join-Path $resultsDir "filter_fc_exact"
& $obfExe $testAppPdb "$outFcExact.sym" -fc+Rectangle | Out-Null
$outQuoted = Join-Path $resultsDir "filter_quoted"
& $obfExe $testAppPdb "$outQuoted.sym" '-fc+"Rectangle"' | Out-Null
$quoteDiff = Compare-Object (Get-Content "$outFcExact.sym") (Get-Content "$outQuoted.sym")
Assert-True ($quoteDiff -eq $null) '-fc+"Rectangle" (quoted WORD) behaves identically to -fc+Rectangle'

# An unrecognized switch is a hard error (non-zero exit), not a silent no-op.
$outBadArg = Join-Path $resultsDir "filter_bad_arg"
& $obfExe $testAppPdb "$outBadArg.sym" -bogus+X | Out-Null
Assert-True ($LASTEXITCODE -ne 0) "An unrecognized filter switch fails with a non-zero exit code"

# ============================================================
# L. Simple reports (_simple_sort_by_class / _simple_sort_by_name)
# ============================================================
Section "Simple reports (_simple_sort_by_class / _simple_sort_by_name)"

function Get-SimpleBareName {
    param([string]$Line)
    $qualified = $Line.Substring(0, $Line.IndexOf('('))
    $idx = $qualified.LastIndexOf('::')
    if ($idx -ge 0) { return $qualified.Substring($idx + 2) }
    return $qualified
}

$simpleByClassLines = Get-Content "$outApp`_simple_sort_by_class.sym"
$simpleByNameLines = Get-Content "$outApp`_simple_sort_by_name.sym"

Assert-Equal $simpleByClassLines.Count $appLines.Count `
    "_simple_sort_by_class.sym has exactly one line per server.sym symbol"
Assert-Equal $simpleByNameLines.Count $appLines.Count `
    "_simple_sort_by_name.sym has exactly one line per server.sym symbol"

# Format is just "Name(Signature) -> ReturnType" -- none of server.sym's
# other fields (visibility, address, SIZE, VTABLE_*, SOURCE_FILE, ...)
# survive.
$rectAreaSimple = $simpleByClassLines | Where-Object { $_ -match '^Rectangle::Area\(\) -> ' }
Assert-True ($rectAreaSimple.Count -eq 1) "Rectangle::Area appears as a plain 'Name() -> ReturnType' line"
if ($rectAreaSimple.Count -eq 1) {
    Assert-True ($rectAreaSimple[0] -notmatch '^(PUBLIC|PRIVATE)') "Simple line omits PUBLIC/PRIVATE"
    Assert-True ($rectAreaSimple[0] -notmatch 'VTABLE_') "Simple line omits VTABLE_* fields"
    Assert-True ($rectAreaSimple[0] -notmatch '0x[0-9A-Fa-f]+ SIZE=') "Simple line omits address/SIZE"
}

# _simple_sort_by_class.sym: plain (ordinal) alphabetical order of the
# fully-qualified name -- a class's own methods (sharing its "Class::"
# prefix) end up contiguous, and classes themselves fall in alphabetical
# order too.
$namesByClass = $simpleByClassLines | ForEach-Object { $_.Substring(0, $_.IndexOf('(')) }
$namesByClassSorted = [string[]]$namesByClass.Clone()
[array]::Sort($namesByClassSorted, [StringComparer]::Ordinal)
Assert-True ((($namesByClass -join "`n")) -ceq ($namesByClassSorted -join "`n")) `
    "_simple_sort_by_class.sym is sorted by the fully-qualified name (ordinal)"

# _simple_sort_by_name.sym: ordinal alphabetical order of just the bare
# method name, ignoring whatever class/namespace it belongs to.
$bareNames = $simpleByNameLines | ForEach-Object { Get-SimpleBareName $_ }
$bareNamesSorted = [string[]]$bareNames.Clone()
[array]::Sort($bareNamesSorted, [StringComparer]::Ordinal)
Assert-True (($bareNames -join "`n") -ceq ($bareNamesSorted -join "`n")) `
    "_simple_sort_by_name.sym is sorted by the bare method name (ordinal), ignoring class/namespace"

# Rectangle::Area and Circle::Area (the pure-virtual override pair used
# elsewhere in this suite) share the bare name "Area" but belong to
# different classes -- sort_by_name should put them next to each other;
# sort_by_class should not (they fall under their own separate classes).
$areaIndicesByName = @()
for ($i = 0; $i -lt $simpleByNameLines.Count; $i++) {
    if ($simpleByNameLines[$i] -match '^(Rectangle|Circle)::Area\(\) -> ') { $areaIndicesByName += $i }
}
Assert-True ($areaIndicesByName.Count -eq 2 -and ($areaIndicesByName[1] - $areaIndicesByName[0]) -eq 1) `
    "_simple_sort_by_name.sym places Circle::Area and Rectangle::Area next to each other (same bare name, different classes)"

$areaIndicesByClass = @()
for ($i = 0; $i -lt $simpleByClassLines.Count; $i++) {
    if ($simpleByClassLines[$i] -match '^(Rectangle|Circle)::Area\(\) -> ') { $areaIndicesByClass += $i }
}
Assert-True ($areaIndicesByClass.Count -eq 2 -and ($areaIndicesByClass[1] - $areaIndicesByClass[0]) -ne 1) `
    "_simple_sort_by_class.sym does NOT place them next to each other (grouped by class instead)"

# -fc/-fm filter both simple reports the same way they filter server.sym.
$outSimpleFc = Join-Path $resultsDir "simple_filter_fc"
& $obfExe $testAppPdb "$outSimpleFc.sym" -fc+Rectangle | Out-Null
$simpleFcByClass = Get-Content "$outSimpleFc`_simple_sort_by_class.sym"
$simpleFcByName = Get-Content "$outSimpleFc`_simple_sort_by_name.sym"
Assert-True (($simpleFcByClass | Where-Object { $_ -notmatch '^Rectangle::' }).Count -eq 0) `
    "-fc+Rectangle keeps only Rectangle's lines in _simple_sort_by_class.sym"
Assert-True (($simpleFcByName | Where-Object { $_ -notmatch '^Rectangle::' }).Count -eq 0) `
    "-fc+Rectangle keeps only Rectangle's lines in _simple_sort_by_name.sym"

$outSimpleFm = Join-Path $resultsDir "simple_filter_fm"
& $obfExe $testAppPdb "$outSimpleFm.sym" -fm+Area | Out-Null
$simpleFmByClass = Get-Content "$outSimpleFm`_simple_sort_by_class.sym"
Assert-True (($simpleFmByClass | Where-Object { $_ -notmatch 'Area' }).Count -eq 0) `
    "-fm+Area keeps only Area-containing lines in _simple_sort_by_class.sym"
Assert-True ($simpleFmByClass.Count -gt 0) "-fm+Area still keeps at least one line"

# ============================================================
# Summary
# ============================================================
Write-Host ""
Write-Host "====================================" -ForegroundColor Magenta
Write-Host "UNIT TEST SUMMARY" -ForegroundColor Magenta
Write-Host "====================================" -ForegroundColor Magenta
Write-Host "Passed: $script:passCount" -ForegroundColor Green
Write-Host "Failed: $script:failCount" -ForegroundColor $(if ($script:failCount -gt 0) { "Red" } else { "Green" })

if ($script:failCount -gt 0) {
    Write-Host ""
    Write-Host "Failures:" -ForegroundColor Red
    foreach ($f in $script:failures) {
        Write-Host "  - $f" -ForegroundColor Red
    }
    exit 1
}

Write-Host ""
Write-Host "ALL TESTS PASSED" -ForegroundColor Green
exit 0
