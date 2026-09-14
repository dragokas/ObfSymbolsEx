# Build script for ObfSymbolsEx project
# Usage: .\build.ps1 [Configuration] [Platform]
# Example: .\build.ps1 Release x64

param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64"
)

Write-Host "ObfSymbolsEx Build Script" -ForegroundColor Cyan
Write-Host "=========================" -ForegroundColor Cyan
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
& $msbuildPath ObfSymbolsEx.vcxproj /p:Configuration=$Configuration /p:Platform=$Platform /v:minimal

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "BUILD SUCCESSFUL!" -ForegroundColor Green
    Write-Host ""
    $exePath = "$Platform\$Configuration\ObfSymbolsEx.exe"
    if (Test-Path $exePath) {
        Write-Host "Executable created at: $exePath" -ForegroundColor Cyan
        
        # Automatically copy msdia140.dll for standalone distribution
        Write-Host ""
        Write-Host "Copying msdia140.dll for standalone distribution..." -ForegroundColor Yellow
        
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
                Write-Host "  msdia140.dll copied successfully" -ForegroundColor Green
            }
        } else {
            Write-Host "  Warning: msdia140.dll not found in VS installation" -ForegroundColor Yellow
            Write-Host "  You can manually copy it later using: .\copy-dia-dll.ps1" -ForegroundColor Yellow
        }
        
        Write-Host ""
        Write-Host "Usage: .\$exePath <input.pdb> <output.sym>" -ForegroundColor Cyan
    }
} else {
    Write-Host ""
    Write-Host "BUILD FAILED!" -ForegroundColor Red
    exit 1
}

