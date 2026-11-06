# tools/build-psp.ps1
#
# 构建 PSP 版。
# 需要环境变量 PSPDEV 指向 pspdev 根目录（含 bin/psp-gcc、psp-fixup-imports、mksfo、pack-pbp）。
param(
  [string]$PSPDEV = $env:PSPDEV,
  [string]$BuildDir = "build/psp",
  [switch]$Clean
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

if (-not $PSPDEV) {
  Write-Host "PSPDEV is not set." -ForegroundColor Red
  Write-Host "Example: PSPDEV = 'C:/pspdev'" -ForegroundColor Yellow
  exit 2
}

$gcc = Join-Path $PSPDEV "bin/psp-gcc.exe"
if (-not (Test-Path $gcc)) { $gcc = Join-Path $PSPDEV "bin/psp-gcc" }
if (-not (Test-Path $gcc)) {
  Write-Host "psp-gcc not found under $PSPDEV/bin" -ForegroundColor Red
  exit 2
}

$build = Join-Path $root $BuildDir
if ($Clean -and (Test-Path $build)) { Remove-Item $build -Recurse -Force }

$env:PSPDEV = $PSPDEV
$cmakeArgs = @(
  "-S", $root,
  "-B", $build,
  "-G", "Ninja",
  "-DCMAKE_TOOLCHAIN_FILE=$root/cmake/toolchain-psp.cmake",
  "-DCMAKE_BUILD_TYPE=Release"
)
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& cmake --build $build
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$elf = Join-Path $build "bin/game_psp.elf"
$eboot = Join-Path $build "EBOOT.PBP"
Write-Host ""
Write-Host "ELF   : $elf"
if (Test-Path $eboot) { Write-Host "EBOOT : $eboot" }
