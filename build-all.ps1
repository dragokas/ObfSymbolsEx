# Build script for entire ObfSymbolsEx solution
# Usage: .\build-all.ps1 [Configuration] [Platform]
# Example: .\build-all.ps1 Release x64

param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64"
)

Write-Host "================================" -ForegroundColor Cyan
Write-Host "ObfSymbolsEx Solution Build Script" -ForegroundColor Cyan
Write-Host "================================" -ForegroundColor Cyan
Write-Host ""

$ErrorActionPreference = "Stop"

# Find MSBuild using vswhere
$vsWherePath = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vsWherePath)) {
    Write-Host "ERROR: vswhere.exe not found. Please ensure Visual Studio is installed." -ForegroundColor Red
    exit 1
}

Write-Host "Locating MSBuild..." -ForegroundColor Yellow
$msbuildPath = & $vsWherePath -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1

if (-not $msbuildPath -or -not (Test-Path $msbuildPath)) {
    Write-Host "ERROR: MSBuild not found. Please ensure Visual Studio 2022 is installed with C++ build tools." -ForegroundColor Red
    exit 1
}

Write-Host "Found MSBuild at: $msbuildPath" -ForegroundColor Green
Write-Host ""
Write-Host "Building Configuration: $Configuration | Platform: $Platform" -ForegroundColor Yellow
Write-Host ""

# Build the solution
& $msbuildPath ObfSymbolsEx.sln /p:Configuration=$Configuration /p:Platform=$Platform /v:minimal /m

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "BUILD SUCCESSFUL!" -ForegroundColor Green
    Write-Host "=================" -ForegroundColor Green
    Write-Host ""
    
    # Solution builds output to the solution directory structure
    $obfSymbolsExe = "$Platform\$Configuration\ObfSymbolsEx.exe"
    $testAppExe = "$Platform\$Configuration\TestApp.exe"
    $testAppPdb = "$Platform\$Configuration\TestApp.pdb"
    $testDllDll = "$Platform\$Configuration\TestDLL.dll"
    $testDllPdb = "$Platform\$Configuration\TestDLL.pdb"
    
    if (Test-Path $obfSymbolsExe) {
        Write-Host "ObfSymbolsEx.exe: $obfSymbolsExe" -ForegroundColor Cyan
        
        # Copy msdia140.dll for standalone use
        $searchPaths = @(
            "C:\Program Files\Microsoft Visual Studio\2022\Professional\DIA SDK\bin\amd64\msdia140.dll",
            "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\DIA SDK\bin\amd64\msdia140.dll",
            "C:\Program Files\Microsoft Visual Studio\2022\Community\DIA SDK\bin\amd64\msdia140.dll"
        )
        
        $sourceDll = $null
        foreach ($path in $searchPaths) {
            if (Test-Path $path) {
                $sourceDll = $path
                break
            }
        }
        
        if ($sourceDll) {
            $targetDll = "$Platform\$Configuration\msdia140.dll"
            Copy-Item -Path $sourceDll -Destination $targetDll -Force -ErrorAction SilentlyContinue
            if (Test-Path $targetDll) {
                Write-Host "msdia140.dll:   $targetDll" -ForegroundColor Green
            }
        }
    }
    
    if (Test-Path $testAppExe) {
        Write-Host "TestApp.exe:    $testAppExe" -ForegroundColor Cyan
    }
    
    if (Test-Path $testDllDll) {
        Write-Host "TestDLL.dll:    $testDllDll" -ForegroundColor Cyan
    }
    
    Write-Host ""
    Write-Host "Quick Test Commands:" -ForegroundColor Yellow
    Write-Host "  Run TestApp:        .\$testAppExe" -ForegroundColor Gray
    if ((Test-Path $obfSymbolsExe) -and (Test-Path $testAppPdb)) {
        Write-Host "  Extract EXE syms:   .\$obfSymbolsExe .\$testAppPdb TestApp.sym" -ForegroundColor Gray
    }
    if ((Test-Path $obfSymbolsExe) -and (Test-Path $testDllPdb)) {
        Write-Host "  Extract DLL syms:   .\$obfSymbolsExe .\$testDllPdb TestDLL.sym" -ForegroundColor Gray
    }
    Write-Host "  Run validation:     .\validate.ps1" -ForegroundColor Gray
    
} else {
    Write-Host ""
    Write-Host "BUILD FAILED!" -ForegroundColor Red
    exit 1
}

