# Launch the ROM in the bundled mGBA (falls back to the installed one).
param([string]$Rom = "")
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrEmpty($Rom)) { $Rom = Join-Path $Root "build\gba\game.gba" }
if (-not (Test-Path $Rom)) { Write-Error "ROM not found: $Rom (run tools/build-gba.ps1 first)"; exit 1 }

$Candidates = @(
  (Join-Path $Root "toolchain\mgba\mGBA.exe"),
  "C:\Program Files\mGBA\mGBA.exe"
)
$Mgba = $Candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $Mgba) { Write-Error "mGBA not found"; exit 1 }

Start-Process -FilePath $Mgba -ArgumentList @($Rom)
Write-Host "mGBA: $Mgba"
Write-Host "ROM : $Rom"
