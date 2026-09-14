@echo off
REM Validation script for ObfSymbolsEx (tests both DLL and EXE)
set CONFIG=%1
set PLATFORM=%2

if "%CONFIG%"=="" set CONFIG=Debug
if "%PLATFORM%"=="" set PLATFORM=x64

powershell -ExecutionPolicy Bypass -File "%~dp0validate.ps1" -Configuration %CONFIG% -Platform %PLATFORM%
