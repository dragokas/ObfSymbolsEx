@echo off
REM Script to copy msdia140.dll to the executable directory
set PLATFORM=%1
set CONFIG=%2

if "%PLATFORM%"=="" set PLATFORM=x64
if "%CONFIG%"=="" set CONFIG=Release

powershell -ExecutionPolicy Bypass -File "%~dp0copy-dia-dll.ps1" -Platform %PLATFORM% -Configuration %CONFIG%

