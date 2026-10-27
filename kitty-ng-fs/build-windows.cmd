@echo off
setlocal
cd /d "%~dp0"
rem Run from a Visual Studio 2022 developer command prompt with CMake installed.
cmake -S . -B build\msvc-x86 -A Win32
if errorlevel 1 exit /b 1
cmake --build build\msvc-x86 --config Release
if errorlevel 1 exit /b 1
cmake -S . -B build\msvc-x64 -A x64
if errorlevel 1 exit /b 1
cmake --build build\msvc-x64 --config Release
if errorlevel 1 exit /b 1
if not exist dist mkdir dist
copy /y build\msvc-x86\Release\kitty-ng-fs.wfx dist\kitty-ng-fs.wfx
copy /y build\msvc-x64\Release\kitty-ng-fs.wfx64 dist\kitty-ng-fs.wfx64
endlocal
