@echo off
REM Host unit tests — avoids PowerShell execution-policy blocks on .ps1 files.
setlocal EnableExtensions

set "ROOT=%~dp0.."
cd /d "%ROOT%" || exit /b 1

set "CMAKE=C:\Program Files\CMake\bin\cmake.exe"
if not exist "%CMAKE%" (
  where cmake >nul 2>&1 || (
    echo cmake not found. Install CMake or add it to PATH.
    exit /b 1
  )
  set "CMAKE=cmake"
)

set "BUILDDIR=build\host-tests"
set "FILTER=%~1"

REM Prefer compiler bin from an existing CMake cache (llvm-mingw DLLs live there).
set "MINGW_BIN="
if exist "%BUILDDIR%\CMakeCache.txt" (
  for /f "tokens=1,* delims==" %%A in ('findstr /B /C:"CMAKE_CXX_COMPILER:STRING=" "%BUILDDIR%\CMakeCache.txt"') do (
    for %%F in ("%%B") do set "MINGW_BIN=%%~dpF"
  )
)

if not defined MINGW_BIN (
  for /f "delims=" %%P in ('dir /s /b "%LOCALAPPDATA%\Microsoft\WinGet\Packages\*x86_64-w64-mingw32-clang++.exe" 2^>nul') do (
    set "MINGW_BIN=%%~dpP"
    goto :have_mingw
  )
)
:have_mingw

if defined MINGW_BIN (
  set "PATH=%MINGW_BIN%;%PATH%"
  echo PATH prepend: %MINGW_BIN%
) else (
  echo WARNING: llvm-mingw bin not found — tests may fail with missing libc++.dll
)

if not exist "%BUILDDIR%" (
  "%CMAKE%" -S tests\host -B "%BUILDDIR%" || exit /b 1
)
"%CMAKE%" --build "%BUILDDIR%" || exit /b 1

set "CTEST=C:\Program Files\CMake\bin\ctest.exe"
if not exist "%CTEST%" set "CTEST=ctest"

if "%FILTER%"=="" (
  "%CTEST%" --test-dir "%BUILDDIR%" --output-on-failure
) else (
  "%CTEST%" --test-dir "%BUILDDIR%" --output-on-failure -R "%FILTER%"
)
exit /b %ERRORLEVEL%
