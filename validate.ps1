# Validation script for ObfSymbolsEx
# Builds all test projects, runs ObfSymbolsEx on their PDBs (both EXE and DLL), and displays results

param(
    [string]$Configuration = "Debug",
    [string]$Platform = "x64"
)

Write-Host "================================" -ForegroundColor Cyan
Write-Host "ObfSymbolsEx Validation Script" -ForegroundColor Cyan
Write-Host "================================" -ForegroundColor Cyan
Write-Host ""

$ErrorActionPreference = "Stop"

# Step 1: Build ObfSymbolsEx
Write-Host "[1/5] Building ObfSymbolsEx..." -ForegroundColor Yellow
Push-Location "ObfSymbolsEx"
& .\build.ps1 -Configuration Release -Platform $Platform
if ($LASTEXITCODE -ne 0) {
    Write-Host "Failed to build ObfSymbolsEx" -ForegroundColor Red
    Pop-Location
    exit 1
}
Pop-Location
Write-Host ""

# Step 2: Build TestDLL
Write-Host "[2/5] Building TestDLL..." -ForegroundColor Yellow
Push-Location "TestDLL"
& .\build.ps1 -Configuration $Configuration -Platform $Platform
if ($LASTEXITCODE -ne 0) {
    Write-Host "Failed to build TestDLL" -ForegroundColor Red
    Pop-Location
    exit 1
}
Pop-Location
Write-Host ""

# Step 3: Build TestApp
Write-Host "[3/5] Building TestApp..." -ForegroundColor Yellow
Push-Location "TestApp"
& .\build.ps1 -Configuration $Configuration -Platform $Platform
if ($LASTEXITCODE -ne 0) {
    Write-Host "Failed to build TestApp" -ForegroundColor Red
    Pop-Location
    exit 1
}
Pop-Location
Write-Host ""

# Step 4: Extract symbols from TestDLL.pdb
Write-Host "[4/5] Extracting symbols from TestDLL.pdb (DLL)..." -ForegroundColor Yellow
$obfSymbolsPath = "ObfSymbolsEx\$Platform\Release\ObfSymbolsEx.exe"
$testDllPdbPath = "TestDLL\$Platform\$Configuration\TestDLL.pdb"
$dllSymPath = "TestDLL_symbols.sym"

if (-not (Test-Path $obfSymbolsPath)) {
    Write-Host "ERROR: ObfSymbolsEx.exe not found at $obfSymbolsPath" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $testDllPdbPath)) {
    Write-Host "ERROR: TestDLL.pdb not found at $testDllPdbPath" -ForegroundColor Red
    exit 1
}

Write-Host "Running: $obfSymbolsPath $testDllPdbPath $dllSymPath" -ForegroundColor Gray
& $obfSymbolsPath $testDllPdbPath $dllSymPath

if ($LASTEXITCODE -ne 0) {
    Write-Host "Failed to extract symbols from TestDLL" -ForegroundColor Red
    exit 1
}
Write-Host ""

# Step 5: Extract symbols from TestApp.pdb
Write-Host "[5/5] Extracting symbols from TestApp.pdb (EXE)..." -ForegroundColor Yellow
$testAppPdbPath = "TestApp\$Platform\$Configuration\TestApp.pdb"
$exeSymPath = "TestApp_symbols.sym"

if (-not (Test-Path $testAppPdbPath)) {
    Write-Host "ERROR: TestApp.pdb not found at $testAppPdbPath" -ForegroundColor Red
    exit 1
}

Write-Host "Running: $obfSymbolsPath $testAppPdbPath $exeSymPath" -ForegroundColor Gray
& $obfSymbolsPath $testAppPdbPath $exeSymPath

if ($LASTEXITCODE -ne 0) {
    Write-Host "Failed to extract symbols from TestApp" -ForegroundColor Red
    exit 1
}
Write-Host ""

# Analyze DLL results
Write-Host "========================================" -ForegroundColor Green
Write-Host "DLL Symbol Extraction Results (TestDLL)" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green

if (Test-Path $dllSymPath) {
    $dllSymbols = Get-Content $dllSymPath
    $dllTotalSymbols = $dllSymbols.Count
    $dllPublicSymbols = ($dllSymbols | Where-Object { $_ -match "^PUBLIC" }).Count
    $dllPrivateSymbols = ($dllSymbols | Where-Object { $_ -match "^PRIVATE" }).Count

    Write-Host "Output file: $dllSymPath" -ForegroundColor Cyan
    Write-Host "Total symbols: $dllTotalSymbols" -ForegroundColor Cyan
    Write-Host "  - PUBLIC symbols: $dllPublicSymbols" -ForegroundColor Green
    Write-Host "  - PRIVATE symbols: $dllPrivateSymbols" -ForegroundColor Yellow
    Write-Host ""

    Write-Host "Sample DLL symbols (first 15):" -ForegroundColor Cyan
    Write-Host "-----------------------------" -ForegroundColor Cyan
    $dllSymbols | Select-Object -First 15 | ForEach-Object {
        if ($_ -match "^PUBLIC") {
            Write-Host $_ -ForegroundColor Green
        } else {
            Write-Host $_ -ForegroundColor Yellow
        }
    }
    Write-Host ""

    Write-Host "DLL-Specific Symbols to Verify:" -ForegroundColor Cyan
    Write-Host "-------------------------------" -ForegroundColor Cyan
    
    $dllFunctionsToCheck = @(
        "MathOperations",
        "IntegerAdd",
        "DoubleAdd",
        "Point3D",
        "Container",
        "CircleArea",
        "SphereVolume"
    )

    foreach ($func in $dllFunctionsToCheck) {
        $found = $dllSymbols | Where-Object { $_ -match $func }
        if ($found) {
            Write-Host "  [OK] Found symbols containing '$func' ($($found.Count) occurrences)" -ForegroundColor Green
        } else {
            Write-Host "  [!!] No symbols found for '$func'" -ForegroundColor Red
        }
    }
    Write-Host ""
}

# Analyze EXE results
Write-Host "========================================" -ForegroundColor Green
Write-Host "EXE Symbol Extraction Results (TestApp)" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green

if (Test-Path $exeSymPath) {
    $exeSymbols = Get-Content $exeSymPath
    $exeTotalSymbols = $exeSymbols.Count
    $exePublicSymbols = ($exeSymbols | Where-Object { $_ -match "^PUBLIC" }).Count
    $exePrivateSymbols = ($exeSymbols | Where-Object { $_ -match "^PRIVATE" }).Count

    Write-Host "Output file: $exeSymPath" -ForegroundColor Cyan
    Write-Host "Total symbols: $exeTotalSymbols" -ForegroundColor Cyan
    Write-Host "  - PUBLIC symbols: $exePublicSymbols" -ForegroundColor Green
    Write-Host "  - PRIVATE symbols: $exePrivateSymbols" -ForegroundColor Yellow
    Write-Host ""

    Write-Host "Sample EXE symbols (first 15):" -ForegroundColor Cyan
    Write-Host "-----------------------------" -ForegroundColor Cyan
    $exeSymbols | Select-Object -First 15 | ForEach-Object {
        if ($_ -match "^PUBLIC") {
            Write-Host $_ -ForegroundColor Green
        } else {
            Write-Host $_ -ForegroundColor Yellow
        }
    }
    Write-Host ""

    Write-Host "EXE-Specific Symbols to Verify:" -ForegroundColor Cyan
    Write-Host "-------------------------------" -ForegroundColor Cyan
    
    $exeFunctionsToCheck = @(
        "SimpleFunction",
        "OverloadedFunction",
        "Calculator",
        "Rectangle",
        "Circle",
        "main"
    )

    foreach ($func in $exeFunctionsToCheck) {
        $found = $exeSymbols | Where-Object { $_ -match $func }
        if ($found) {
            Write-Host "  [OK] Found symbols containing '$func' ($($found.Count) occurrences)" -ForegroundColor Green
        } else {
            Write-Host "  [!!] No symbols found for '$func'" -ForegroundColor Red
        }
    }
    Write-Host ""
}

# Summary
Write-Host "====================================" -ForegroundColor Magenta
Write-Host "VALIDATION SUMMARY" -ForegroundColor Magenta
Write-Host "====================================" -ForegroundColor Magenta
Write-Host ""
Write-Host "DLL Symbols (TestDLL.pdb):" -ForegroundColor Cyan
Write-Host "  File: $dllSymPath" -ForegroundColor Gray
Write-Host "  Total: $dllTotalSymbols symbols ($dllPublicSymbols PUBLIC, $dllPrivateSymbols PRIVATE)" -ForegroundColor Gray
Write-Host ""
Write-Host "EXE Symbols (TestApp.pdb):" -ForegroundColor Cyan
Write-Host "  File: $exeSymPath" -ForegroundColor Gray
Write-Host "  Total: $exeTotalSymbols symbols ($exePublicSymbols PUBLIC, $exePrivateSymbols PRIVATE)" -ForegroundColor Gray
Write-Host ""
Write-Host "VALIDATION COMPLETE!" -ForegroundColor Green
Write-Host ""
Write-Host "To view all DLL symbols: Get-Content $dllSymPath" -ForegroundColor Gray
Write-Host "To view all EXE symbols: Get-Content $exeSymPath" -ForegroundColor Gray
