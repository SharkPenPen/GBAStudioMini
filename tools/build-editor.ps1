# Build the PC editor + player with the bundled MinGW/CMake/Ninja.
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$BundledMingw = Join-Path $Root "toolchain\mingw64\bin"
if (Test-Path $BundledMingw) { $env:PATH = $BundledMingw + ";" + $env:PATH }
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { throw "cmake not found; install CMake or place it in toolchain\mingw64\bin" }

$Build = Join-Path $Root "build\host"
cmake -S $Root -B $Build -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build $Build --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ""
Write-Host "Editor : $Build\bin\PocketLoom-Editor.exe"
