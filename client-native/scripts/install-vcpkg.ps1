# One-time vcpkg setup for the native client.
#
# Clones vcpkg to $env:USERPROFILE\vcpkg (or a path you choose),
# bootstraps it, and sets VCPKG_ROOT permanently for your user account.
#
# Usage (PowerShell, NOT as admin):
#   .\scripts\install-vcpkg.ps1
#   .\scripts\install-vcpkg.ps1 -InstallDir C:\dev\vcpkg

param(
  [string]$InstallDir = "$env:USERPROFILE\vcpkg"
)

$ErrorActionPreference = 'Stop'

if (Test-Path (Join-Path $InstallDir 'vcpkg.exe')) {
  Write-Host "vcpkg already present at $InstallDir"
} else {
  Write-Host "Cloning vcpkg into $InstallDir ..."
  git clone --depth 1 https://github.com/microsoft/vcpkg.git $InstallDir

  Write-Host "Bootstrapping ..."
  & (Join-Path $InstallDir 'bootstrap-vcpkg.bat') -disableMetrics
}

if ($env:VCPKG_ROOT -ne $InstallDir) {
  Write-Host "Setting VCPKG_ROOT (user-level) -> $InstallDir"
  [Environment]::SetEnvironmentVariable('VCPKG_ROOT', $InstallDir, 'User')
  $env:VCPKG_ROOT = $InstallDir   # also set for this shell
}

Write-Host ""
Write-Host "Done."
Write-Host "  VCPKG_ROOT = $InstallDir"
Write-Host ""
Write-Host "OPEN A NEW PowerShell window before running cmake --preset windows,"
Write-Host "otherwise VCPKG_ROOT won't be in the environment of any tool you launch."
