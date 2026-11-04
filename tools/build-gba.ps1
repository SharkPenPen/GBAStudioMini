# Build a GBA ROM with the bundled devkitARM.
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Devkit = if ($env:DEVKITARM) { $env:DEVKITARM } else { Join-Path $Root "toolchain\devkitARM" }
$Gxx = Join-Path $Devkit "bin\arm-none-eabi-g++.exe"
$Objcopy = Join-Path $Devkit "bin\arm-none-eabi-objcopy.exe"
if (-not (Test-Path $Gxx)) { throw "devkitARM not found; set DEVKITARM or place it in toolchain\devkitARM" }

$OutDir = Join-Path $Root "build\gba"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$Elf = Join-Path $OutDir "game.elf"
$Rom = Join-Path $OutDir "game.gba"

$GbaArgs = @(
  "-mcpu=arm7tdmi", "-mthumb", "-O2", "-Wall", "-Wextra",
  "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics", "-fno-use-cxa-atexit",
  "-ffreestanding", "-fno-builtin", "-nostdlib", "-I.",
  "-T", "gba.ld", "crt0.s", "main.cpp",
  "-o", $Elf, "-lgcc"
)

Push-Location (Join-Path $Root "gba")
try {
  & $Gxx @GbaArgs
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

  & $Objcopy -O binary $Elf $Rom
  if ($LASTEXITCODE -ne 0) {
    Write-Host "objcopy failed. If game.gba is open in mGBA, close it first." -ForegroundColor Yellow
    exit $LASTEXITCODE
  }
} finally {
  Pop-Location
}

# ROM header complement byte (0xBD): (0x19 + sum(0xA0..0xBC) + c) & 0xFF == 0
$bytes = [System.IO.File]::ReadAllBytes($Rom)
$sum = 0
for ($i = 0xA0; $i -le 0xBC; $i++) { $sum += $bytes[$i] }
$c = (0x100 - (($sum + 0x19) -band 0xFF)) -band 0xFF
$bytes[0xBD] = [byte]$c
[System.IO.File]::WriteAllBytes($Rom, $bytes)

$size = (Get-Item $Rom).Length
Write-Host "ROM    : $Rom ($size bytes)"