# Script to copy msdia140.dll to the executable directory
# This allows distribution of ObfSymbolsEx without requiring Visual Studio installation

param(
    [string]$Platform = "x64",
    [string]$Configuration = "Release"
)

Write-Host "Copying msdia140.dll to executable directory" -ForegroundColor Cyan
Write-Host "=============================================" -ForegroundColor Cyan
Write-Host ""

# Find msdia140.dll in Visual Studio installation
$searchPaths = @(
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\DIA SDK\bin\amd64\msdia140.dll",
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\DIA SDK\bin\amd64\msdia140.dll",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\DIA SDK\bin\amd64\msdia140.dll",
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\Professional\DIA SDK\bin\amd64\msdia140.dll",
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\Enterprise\DIA SDK\bin\amd64\msdia140.dll",
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\DIA SDK\bin\amd64\msdia140.dll"
)

$sourceDll = $null
foreach ($path in $searchPaths) {
    if (Test-Path $path) {
        $sourceDll = $path
        break
    }
}

if (-not $sourceDll) {
    Write-Host "ERROR: Could not find msdia140.dll in any Visual Studio installation" -ForegroundColor Red
    Write-Host "Please ensure Visual Studio 2022 with DIA SDK is installed" -ForegroundColor Red
    exit 1
}

Write-Host "Found msdia140.dll at:" -ForegroundColor Green
Write-Host "  $sourceDll" -ForegroundColor Gray
Write-Host ""

# Copy to executable directory (relative to script location)
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$targetDir = Join-Path $scriptDir "$Platform\$Configuration"

if (-not (Test-Path $targetDir)) {
    Write-Host "ERROR: Target directory does not exist: $targetDir" -ForegroundColor Red
    Write-Host "Please build the project first" -ForegroundColor Red
    exit 1
}

$targetDll = Join-Path $targetDir "msdia140.dll"
Write-Host "Copying to:" -ForegroundColor Yellow
Write-Host "  $targetDll" -ForegroundColor Gray
Write-Host ""

try {
    Copy-Item -Path $sourceDll -Destination $targetDll -Force
    Write-Host "SUCCESS: msdia140.dll copied successfully!" -ForegroundColor Green
    Write-Host ""
    Write-Host "ObfSymbolsEx.exe can now be distributed with msdia140.dll" -ForegroundColor Cyan
    Write-Host "without requiring Visual Studio installation on the target machine." -ForegroundColor Cyan
}
catch {
    Write-Host "ERROR: Failed to copy DLL" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}

