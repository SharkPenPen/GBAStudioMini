# Build, test, and package the Windows release.
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Version = (Get-Content (Join-Path $Root "VERSION.txt") -Raw).Trim()
$Build = Join-Path $Root "build\host"
$Dist = Join-Path $Root "dist"
$Name = "PocketLoom-$Version-win64"
$Stage = Join-Path $Dist $Name

& (Join-Path $PSScriptRoot "build-editor.ps1")
if ($LASTEXITCODE -ne 0) { throw "build failed" }

& (Join-Path $Build "bin\capabilities.exe")
if ($LASTEXITCODE -ne 0) { throw "capabilities failed" }

if (Test-Path $Stage) { Remove-Item -LiteralPath $Stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path (Join-Path $Stage "bin") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $Stage "docs") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $Stage "licenses") | Out-Null

Copy-Item (Join-Path $Build "bin\PocketLoom-Editor.exe") (Join-Path $Stage "bin")
Copy-Item (Join-Path $Build "bin\PocketLoom-Play.exe") (Join-Path $Stage "bin")
Copy-Item (Join-Path $Build "bin\ploom.exe") (Join-Path $Stage "bin")
Copy-Item (Join-Path $Build "bin\make-demo.exe") (Join-Path $Stage "bin")
Copy-Item (Join-Path $Root "VERSION.txt") $Stage
Copy-Item (Join-Path $Root "README.md") $Stage
Copy-Item (Join-Path $Root "LICENSE") $Stage
Copy-Item (Join-Path $Root "THIRD_PARTY_NOTICES.md") $Stage
Copy-Item (Join-Path $Root "docs\*") (Join-Path $Stage "docs") -Recurse
Copy-Item (Join-Path $Root "templates") (Join-Path $Stage "templates") -Recurse
Copy-Item (Join-Path $Root "samples") (Join-Path $Stage "samples") -Recurse

# Licenses: keep the font source/attribution files even though runtime uses generated headers.
Copy-Item (Join-Path $Root "THIRD_PARTY_NOTICES.md") (Join-Path $Stage "licenses\THIRD_PARTY_NOTICES.md") -Force
if (Test-Path (Join-Path $Root "third_party\NOTICE.md")) {
  Copy-Item (Join-Path $Root "third_party\NOTICE.md") (Join-Path $Stage "licenses\third_party-NOTICE.md")
}
$FontDir = Join-Path $Root "third_party\font"
if (Test-Path $FontDir) {
  Copy-Item (Join-Path $FontDir "README.md") (Join-Path $Stage "licenses\font-README.md")
  Copy-Item (Join-Path $FontDir "manifest.json") (Join-Path $Stage "licenses\font-manifest.json")
  Copy-Item (Join-Path $FontDir "LICENSE-OFL.txt") (Join-Path $Stage "licenses\font-LICENSE-OFL.txt")
  Copy-Item (Join-Path $FontDir "changban12\OFL.txt") (Join-Path $Stage "licenses\changban12-OFL.txt")
  Copy-Item (Join-Path $FontDir "changban12\SOURCE.txt") (Join-Path $Stage "licenses\changban12-SOURCE.txt")
  Copy-Item (Join-Path $FontDir "firefly14\ARPHIC-LICENSE.txt") (Join-Path $Stage "licenses\firefly14-ARPHIC-LICENSE.txt")
  Copy-Item (Join-Path $FontDir "firefly14\SOURCE.txt") (Join-Path $Stage "licenses\firefly14-SOURCE.txt")
  Copy-Item (Join-Path $FontDir "zhengge16\OFL.txt") (Join-Path $Stage "licenses\zhengge16-OFL.txt")
  Copy-Item (Join-Path $FontDir "zhengge16\SOURCE.txt") (Join-Path $Stage "licenses\zhengge16-SOURCE.txt")
}

$Zip = Join-Path $Dist "$Name.zip"
if (Test-Path $Zip) { Remove-Item -LiteralPath $Zip -Force }
Compress-Archive -Path $Stage -DestinationPath $Zip -CompressionLevel Optimal -Force
$Hash = Get-FileHash -LiteralPath $Zip -Algorithm SHA256
"$($Hash.Hash)  $Name.zip" | Set-Content -LiteralPath "$Zip.sha256" -Encoding ASCII

Write-Host ""
Write-Host "Package : $Zip"
Write-Host "SHA256  : $($Hash.Hash)"
