# Clean script for entire ObfSymbolsEx solution
# Removes all build artifacts from all projects

Write-Host "================================" -ForegroundColor Cyan
Write-Host "ObfSymbolsEx Solution Clean Script" -ForegroundColor Cyan
Write-Host "================================" -ForegroundColor Cyan
Write-Host ""

$dirsToRemove = @(
    "ObfSymbolsEx\x64",
    "ObfSymbolsEx\x86",
    "ObfSymbolsEx\Debug",
    "ObfSymbolsEx\Release",
    "ObfSymbolsEx\ObfSymbolsEx\x64",
    "ObfSymbolsEx\ObfSymbolsEx\x86",
    "ObfSymbolsEx\ObfSymbolsEx\Debug",
    "ObfSymbolsEx\ObfSymbolsEx\Release",
    "TestApp\x64",
    "TestApp\x86",
    "TestApp\Debug",
    "TestApp\Release",
    "UnitTestResults",
    ".vs",
    "x64",
    "x86",
    "Debug",
    "Release"
)

$filesRemoved = 0
$dirsRemoved = 0

foreach ($dir in $dirsToRemove) {
    if (Test-Path $dir) {
        Write-Host "Removing directory: $dir" -ForegroundColor Yellow
        Remove-Item -Path $dir -Recurse -Force -ErrorAction SilentlyContinue
        $dirsRemoved++
    }
}

# Remove specific file patterns
$patterns = @("*.user", "*.suo", "*.log", "*.sym")
foreach ($pattern in $patterns) {
    $files = Get-ChildItem -Path . -Filter $pattern -Recurse -ErrorAction SilentlyContinue
    foreach ($file in $files) {
        if ($file.Name -ne "README.md" -and $file.Name -ne "IMPLEMENTATION_NOTES.md") {
            Write-Host "Removing file: $($file.Name)" -ForegroundColor Yellow
            Remove-Item -Path $file.FullName -Force -ErrorAction SilentlyContinue
            $filesRemoved++
        }
    }
}

Write-Host ""
Write-Host "Clean complete!" -ForegroundColor Green
Write-Host "Directories removed: $dirsRemoved" -ForegroundColor Cyan
Write-Host "Files removed: $filesRemoved" -ForegroundColor Cyan

