# Build script for TestDLL project
# Usage: .\build.ps1 [Configuration] [Platform]
# Example: .\build.ps1 Debug x64

param(
    [string]$Configuration = "Debug",
    [string]$Platform = "x64"
)

Write-Host "TestDLL Build Script" -ForegroundColor Cyan
Write-Host "====================" -ForegroundColor Cyan
Write-Host ""

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

# Build the project
& $msbuildPath TestDLL.vcxproj /p:Configuration=$Configuration /p:Platform=$Platform /v:minimal

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "BUILD SUCCESSFUL!" -ForegroundColor Green
    Write-Host ""
    $dllPath = "$Platform\$Configuration\TestDLL.dll"
    $pdbPath = "$Platform\$Configuration\TestDLL.pdb"
    if (Test-Path $dllPath) {
        Write-Host "DLL created at: $dllPath" -ForegroundColor Cyan
        Write-Host "PDB file created at: $pdbPath" -ForegroundColor Cyan
    }
} else {
    Write-Host ""
    Write-Host "BUILD FAILED!" -ForegroundColor Red
    exit 1
}

