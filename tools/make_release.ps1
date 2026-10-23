# Kiadás készítése: fordítás, majd dist\AvatarCam-<verzió>-win64.zip és dist\AvatarCam-<verzió>-setup.exe
# Futtatás: powershell -ExecutionPolicy Bypass -File tools\make_release.ps1
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$ver = (Select-String -Path "$root\src\core\Version.h" -Pattern 'AVATARCAM_VERSION "([^"]+)"').Matches[0].Groups[1].Value
Write-Host "AvatarCam $ver"

& "$root\build.bat"
if ($LASTEXITCODE -ne 0) { throw "build failed" }

$dist = Join-Path $root "dist"
New-Item -ItemType Directory -Force $dist | Out-Null

# hordozható zip
$stage = Join-Path $dist "AvatarCam-$ver"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory $stage | Out-Null
Copy-Item "$root\bin\AvatarCam.exe", "$root\bin\onnxruntime.dll", "$root\bin\onnxruntime_providers_shared.dll" $stage
Copy-Item -Recurse "$root\bin\models", "$root\bin\licenses" $stage
Copy-Item "$root\LICENSE", "$root\README.md", "$root\README.hu.md" $stage
$zip = Join-Path $dist "AvatarCam-$ver-win64.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip, [System.IO.Compression.CompressionLevel]::Optimal, $true)
Remove-Item -Recurse -Force $stage

# telepítő
$iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
if (-not (Test-Path $iscc)) { throw "Inno Setup 6 not found: $iscc" }
& $iscc /Q "/DAppVersion=$ver" "$root\installer\AvatarCam.iss"
if ($LASTEXITCODE -ne 0) { throw "installer build failed" }

Get-ChildItem $dist -File | ForEach-Object { "{0,-36} {1,8:N1} MB" -f $_.Name, ($_.Length / 1MB) }
