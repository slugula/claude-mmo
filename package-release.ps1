# package-release.ps1
# Builds the production client and packages it into a distributable ZIP.
#
# Usage:
#   .\package-release.ps1
#
# Output: ProjectL-<version>.zip in the repo root, ready to send to friends.
# Contents: ProjectL\client-native.exe  +  shaders\  +  assets\  +  required DLLs

$ErrorActionPreference = "Stop"

$repoRoot  = $PSScriptRoot
$cmake     = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$buildDir  = Join-Path $repoRoot "client-native\build-production"
$releaseDir= Join-Path $buildDir "Release"
$version   = (Get-Date -Format "yyyy-MM-dd")
$zipName   = "ProjectL-$version.zip"
$zipPath   = Join-Path $repoRoot $zipName

Write-Host "`n=== Project L — Production Build ===" -ForegroundColor Cyan

# ── 1. Configure (idempotent) ────────────────────────────────────────────────
Write-Host "`n[1/3] Configuring..." -ForegroundColor Yellow
& $cmake -S (Join-Path $repoRoot "client-native") --preset windows-production
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

# ── 2. Build ─────────────────────────────────────────────────────────────────
Write-Host "`n[2/3] Building Release..." -ForegroundColor Yellow
& $cmake --build $buildDir --config Release --target client-native
if ($LASTEXITCODE -ne 0) { throw "CMake build failed." }

# ── 3. Package ───────────────────────────────────────────────────────────────
Write-Host "`n[3/3] Packaging $zipName..." -ForegroundColor Yellow

# Remove old zip if present
if (Test-Path $zipPath) { Remove-Item $zipPath -Force }

# Build a staging folder in memory (Compress-Archive works from a folder)
$stagingRoot = Join-Path $env:TEMP "ProjectL-staging"
$stagingDir  = Join-Path $stagingRoot "ProjectL"

if (Test-Path $stagingRoot) { Remove-Item $stagingRoot -Recurse -Force }
New-Item -ItemType Directory -Path $stagingDir | Out-Null

# Executable
Copy-Item (Join-Path $releaseDir "client-native.exe") $stagingDir

# Copy all DLLs present alongside the exe (vcpkg places only what's needed there)
Get-ChildItem $releaseDir -Filter "*.dll" | ForEach-Object {
    Copy-Item $_.FullName $stagingDir
}

# Shaders and assets (subdirectories)
Copy-Item (Join-Path $releaseDir "shaders") $stagingDir -Recurse
Copy-Item (Join-Path $releaseDir "assets")  $stagingDir -Recurse

# World map — canonical hand-crafted map (not in assets/, lives in public/maps/)
$worldMapSrc = Join-Path $repoRoot "public\maps\worldMap.json"
if (Test-Path $worldMapSrc) {
    Copy-Item $worldMapSrc $stagingDir
    Write-Host "  + worldMap.json"
} else {
    Write-Warning "worldMap.json not found at $worldMapSrc - clients will use procedural map"
}

# Compress
Compress-Archive -Path $stagingDir -DestinationPath $zipPath -CompressionLevel Optimal

# Cleanup staging
Remove-Item $stagingRoot -Recurse -Force

$sizeMB = [math]::Round((Get-Item $zipPath).Length / 1MB, 1)
Write-Host "`n=== Done! ===" -ForegroundColor Green
Write-Host "Output: $zipPath ($sizeMB MB)"
Write-Host "Share this ZIP with friends. They extract it and run ProjectL\client-native.exe"
