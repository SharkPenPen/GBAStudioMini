# GBAStudioMini environment (dot-source this: . .\tools\env.ps1)
$Root = Split-Path -Parent $PSScriptRoot
$env:GBA_ROOT = $Root
$BundledMingw = Join-Path $Root "toolchain\mingw64\bin"
$BundledDevkit = Join-Path $Root "toolchain\devkitARM"
if (Test-Path $BundledMingw) { $env:PATH = $BundledMingw + ";" + $env:PATH }
if (Test-Path $BundledDevkit) { $env:DEVKITARM = $BundledDevkit }
if ($env:DEVKITARM) { $env:PATH = (Join-Path $env:DEVKITARM "bin") + ";" + $env:PATH }
Write-Host "GBAStudioMini root: $Root"
cmake --version | Select-Object -First 1
if (Get-Command g++ -ErrorAction SilentlyContinue) { g++ --version | Select-Object -First 1 } else { Write-Warning "g++ not found in PATH" }
if ($env:DEVKITARM -and (Test-Path (Join-Path $env:DEVKITARM "bin\arm-none-eabi-g++.exe"))) { & (Join-Path $env:DEVKITARM "bin\arm-none-eabi-g++.exe") --version | Select-Object -First 1 } else { Write-Warning "devkitARM not found (set DEVKITARM)" }
