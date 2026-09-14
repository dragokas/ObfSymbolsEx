# Unit-test suite for every feature ObfSymbolsEx adds on top of the original
# ObfSymbols: VTable dump, return types, calling convention, complex/basic
# type detection, source file+line, ICF fixes, destructor/pure-virtual
# override resolution, direct .exe/.dll input, and column-aligned output.
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
