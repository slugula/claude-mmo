# deploy.ps1 — Push latest server code to production
# Run from the project root on main: .\deploy.ps1
#
# What it does:
#   1. Ensures you're on main with no uncommitted changes
#   2. Merges main into production and pushes
#   3. Returns you to main
#
# After this, SSH to 34.204.12.71 and run:
#   cd /home/ubuntu/app && git pull && npm install && pm2 restart all

$ErrorActionPreference = 'Stop'
$ProjectRoot = $PSScriptRoot

# ── 1. Must be on main ──────────────────────────────────────────────────────
$branch = git -C $ProjectRoot rev-parse --abbrev-ref HEAD
if ($branch -ne 'main') {
    Write-Error "Must be on main to deploy. Currently on: $branch"
    exit 1
}

# ── 2. No uncommitted changes ────────────────────────────────────────────────
$dirty = git -C $ProjectRoot status --porcelain
if ($dirty) {
    Write-Error "Uncommitted changes detected. Commit or stash them first."
    exit 1
}

Write-Host "`n[1/2] Merging main into production..." -ForegroundColor Cyan
git -C $ProjectRoot checkout production
git -C $ProjectRoot merge main --no-edit
git -C $ProjectRoot push origin production

# ── 3. Return to main ────────────────────────────────────────────────────────
Write-Host "`n[2/2] Returning to main..." -ForegroundColor Cyan
git -C $ProjectRoot checkout main

Write-Host "`nDone! Now SSH to the server and run:" -ForegroundColor Green
Write-Host "  ssh ubuntu@34.204.12.71" -ForegroundColor White
Write-Host "  cd /home/ubuntu/app && git pull && npm install && pm2 restart all" -ForegroundColor White
