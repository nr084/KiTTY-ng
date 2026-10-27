@echo off
setlocal
cd /d "%~dp0"
rem Pass the installed stable LLVM-MinGW root, followed by optional build-local.py arguments.
if "%~1"=="" (
  echo Usage: build-windows.cmd C:\tools\llvm-mingw [--tests icons]
  exit /b 2
)
python build-local.py --toolchain %*
exit /b %errorlevel%
