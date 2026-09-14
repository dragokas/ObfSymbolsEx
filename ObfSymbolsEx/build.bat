@echo off
REM Build script for ObfSymbolsEx project
REM Usage: build.bat [Configuration] [Platform]
REM Example: build.bat Release x64

set CONFIG=%1
set PLATFORM=%2

if "%CONFIG%"=="" set CONFIG=Release
if "%PLATFORM%"=="" set PLATFORM=x64

del "%~dp0%PLATFORM%\%CONFIG%\ObfSymbolsEx.exe"

powershell -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Configuration %CONFIG% -Platform %PLATFORM%
