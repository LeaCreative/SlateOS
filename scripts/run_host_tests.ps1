# Run host unit tests with llvm-mingw runtime DLLs on PATH.
# Usage (from repo root):
#   powershell -File scripts/run_host_tests.ps1
#   powershell -File scripts/run_host_tests.ps1 -Filter session

param(
    [string]$Filter = "",
    [string]$BuildDir = "build/host-tests"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

$cmake = "C:\Program Files\CMake\bin\cmake.exe"
if (-not (Test-Path $cmake)) {
    $cmake = (Get-Command cmake -ErrorAction SilentlyContinue)?.Source
}
if (-not $cmake) { throw "cmake not found" }

# Prefer the compiler already recorded in the build tree.
$compilerHint = $null
$cache = Join-Path $BuildDir "CMakeCache.txt"
if (Test-Path $cache) {
    $line = Select-String -Path $cache -Pattern "^CMAKE_CXX_COMPILER:STRING=(.+)$" | Select-Object -First 1
    if ($line) { $compilerHint = $line.Matches.Groups[1].Value }
}

$mingwBin = $null
if ($compilerHint) {
    $mingwBin = Split-Path -Parent $compilerHint
}
if (-not $mingwBin -or -not (Test-Path (Join-Path $mingwBin "libc++.dll"))) {
    $found = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter "x86_64-w64-mingw32-clang++.exe" -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($found) { $mingwBin = $found.DirectoryName }
}

if ($mingwBin) {
    $env:Path = "$mingwBin;$env:Path"
    Write-Host "PATH prepend: $mingwBin"
} else {
    Write-Warning "llvm-mingw bin not found — tests may fail with missing libc++.dll / libunwind.dll"
}

if (-not (Test-Path $BuildDir)) {
    & $cmake -S tests/host -B $BuildDir
}
& $cmake --build $BuildDir
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$ctestArgs = @("--test-dir", $BuildDir, "--output-on-failure")
if ($Filter) { $ctestArgs += @("-R", $Filter) }
& (Join-Path (Split-Path $cmake) "ctest.exe") @ctestArgs
exit $LASTEXITCODE
