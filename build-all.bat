@echo off
REM Build script for entire ObfSymbolsEx solution
set CONFIG=%1
set PLATFORM=%2

if "%CONFIG%"=="" set CONFIG=Release
if "%PLATFORM%"=="" set PLATFORM=x64

powershell -ExecutionPolicy Bypass -File "%~dp0build-all.ps1" -Configuration %CONFIG% -Platform %PLATFORM%

