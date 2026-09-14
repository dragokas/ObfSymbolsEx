# Clean script for ObfSymbolsEx project
# Removes all build artifacts and intermediate files

Write-Host "ObfSymbolsEx Clean Script" -ForegroundColor Cyan
Write-Host "=========================" -ForegroundColor Cyan
Write-Host ""

$dirsToRemove = @(
    "x64",
    "x86",
    "Debug",
    "Release",
    "ObfSymbolsEx\x64",
    "ObfSymbolsEx\x86",
    "ObfSymbolsEx\Debug",
    "ObfSymbolsEx\Release",
    ".vs"
)

$filesRemoved = 0
$dirsRemoved = 0

foreach ($dir in $dirsToRemove) {
    if (Test-Path $dir) {
        Write-Host "Removing directory: $dir" -ForegroundColor Yellow
        Remove-Item -Path $dir -Recurse -Force
        $dirsRemoved++
    }
}

# Remove specific file patterns
$patterns = @("*.user", "*.suo", "*.log")
foreach ($pattern in $patterns) {
    $files = Get-ChildItem -Path . -Filter $pattern -Recurse -ErrorAction SilentlyContinue
    foreach ($file in $files) {
        Write-Host "Removing file: $($file.FullName)" -ForegroundColor Yellow
        Remove-Item -Path $file.FullName -Force
        $filesRemoved++
    }
}

Write-Host ""
Write-Host "Clean complete!" -ForegroundColor Green
Write-Host "Directories removed: $dirsRemoved" -ForegroundColor Cyan
Write-Host "Files removed: $filesRemoved" -ForegroundColor Cyan

