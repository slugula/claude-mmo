# deploy.ps1 — Build and push to production
# Run from the project root on main: .\deploy.ps1
#
# What it does:
#   1. Ensures you're on main with no uncommitted changes
#   2. Merges main into production
#   3. Builds the client dist with production env vars
#   4. Commits the new dist and pushes production
#   5. Returns you to main

Set-StrictMode -Version Latest
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

Write-Host "`n[1/4] Merging main into production..." -ForegroundColor Cyan
git -C $ProjectRoot checkout production
git -C $ProjectRoot merge main --no-edit

# ── 3. Build client ──────────────────────────────────────────────────────────
Write-Host "`n[2/4] Building client dist (production)..." -ForegroundColor Cyan
$node = "C:\Program Files\nodejs\node.exe"
& $node "$ProjectRoot\node_modules\vite\bin\vite.js" build --config "$ProjectRoot\vite.config.ts"
if ($LASTEXITCODE -ne 0) {
    git -C $ProjectRoot checkout main
    Write-Error "Build failed. Returned to main."
    exit 1
}

# ── 4. Commit dist and push ──────────────────────────────────────────────────
Write-Host "`n[3/4] Committing dist and pushing production..." -ForegroundColor Cyan
git -C $ProjectRoot add dist/
$timestamp = Get-Date -Format "yyyy-MM-dd HH:mm"
git -C $ProjectRoot diff --cached --quiet
if ($LASTEXITCODE -ne 0) {
    # There are staged changes (dist changed)
    git -C $ProjectRoot commit -m "Deploy: rebuild dist $timestamp"
} else {
    Write-Host "  dist unchanged - no new commit needed." -ForegroundColor DarkGray
}
git -C $ProjectRoot push origin production

# ── 5. Return to main ────────────────────────────────────────────────────────
Write-Host "`n[4/4] Returning to main..." -ForegroundColor Cyan
git -C $ProjectRoot checkout main

Write-Host "`nDone! Production is live. Run 'git pull' on the Lightsail server." -ForegroundColor Green
