# PocketLoom environment (dot-source this: . .\tools\env.ps1)
$Root = Split-Path -Parent $PSScriptRoot
$env:POCKETLOOM_ROOT = $Root
$BundledMingw = Join-Path $Root "toolchain\mingw64\bin"
if (Test-Path $BundledMingw) { $env:PATH = $BundledMingw + ";" + $env:PATH }
Write-Host "PocketLoom root: $Root"
cmake --version | Select-Object -First 1
if (Get-Command g++ -ErrorAction SilentlyContinue) { g++ --version | Select-Object -First 1 } else { Write-Warning "g++ not found in PATH" }
