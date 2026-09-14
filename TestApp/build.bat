@echo off
REM Build script for TestApp project
set CONFIG=%1
set PLATFORM=%2

if "%CONFIG%"=="" set CONFIG=Debug
if "%PLATFORM%"=="" set PLATFORM=x64

powershell -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Configuration %CONFIG% -Platform %PLATFORM%

