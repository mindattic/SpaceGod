#Requires -Version 5.1
<#
.SYNOPSIS
    Pulls the latest commit, packages the C++ game (Unreal Engine 5.8, Shipping) and publishes a standalone copy
    to C:\Apps\SpaceGod\, then refreshes the Desktop shortcut "SpaceGod".

.DESCRIPTION
    1. git pull --rebase --autostash (skipped with -NoPull; a failed pull deploys the commit checked out).
    2. Skips the rest when the deployed commit already matches every game source (Cpp, Shared, Capture, Vendor; -Force rebuilds).
    3. Stops any running copy of the game.
    4. tools\package.ps1 -Configuration Shipping -> Builds\Cpp\Windows\SpaceGod.exe.
    5. Copies Builds\Cpp\Windows\* -> C:\Apps\SpaceGod\ and records the commit.
    6. Refreshes the Desktop shortcut (target: deploy.bat in this repo, icon: the published exe).

.PARAMETER Launch
    After publishing (or when already up to date), start the published game.

.PARAMETER Force
    Package and republish even when nothing the game is built from changed. deploy.bat's "1) Deploy & Run" passes it.

.PARAMETER NoPull
    Deploy the commit checked out, without pulling.

.PARAMETER LauncherOnly
    Refresh the Desktop shortcut and exit, without pulling, building or stopping anything.

.NOTES
    Never prompts, so agents and /deploy can run it unattended. The choice between deploying and just running lives
    in deploy.bat, which the Desktop shortcut opens. Needs Unreal Engine 5.8 and Visual Studio's C++ tools only.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File run.ps1 -Launch
    powershell -ExecutionPolicy Bypass -File run.ps1 -Force
#>
param([switch]$Launch, [switch]$Force, [switch]$NoPull, [switch]$LauncherOnly, [int]$MaxParallelActions = 4)

$ErrorActionPreference = 'Stop'

$repoDir   = $PSScriptRoot
$out       = 'C:\Apps\SpaceGod'
$exeName   = 'SpaceGod.exe'
$buildDir  = Join-Path $repoDir 'Builds\Cpp\Windows'
$shortcutBat = Join-Path $repoDir 'run.bat'   # the Desktop shortcut's target: 1) Deploy or 2) Deploy & Run (US-T73)
$marker    = Join-Path $out '.deploy-commit'
# US-T74: every launch records (docs/CAPTURE.md); hq, as lossless video falls behind on this machine.
$captureArgs = @('-capture', 'video', 'hq')
$title     = 'SpaceGod'

# GetFolderPath, not $HOME\Desktop: OneDrive may have moved the Desktop folder.
function Set-Shortcut([string]$exe) {
    $shortcut = Join-Path ([Environment]::GetFolderPath('Desktop')) "$title.lnk"
    $link = (New-Object -ComObject WScript.Shell).CreateShortcut($shortcut)
    $link.TargetPath       = $shortcutBat
    $link.WorkingDirectory = $repoDir
    if (Test-Path -LiteralPath $exe) { $link.IconLocation = "$exe,0" }
    $link.Description      = "${title}: 1) Deploy or 2) Deploy & Run"
    $link.Save()
    Write-Host "    Shortcut -> $shortcut" -ForegroundColor Gray
}

Set-Shortcut (Join-Path $out $exeName)
if ($LauncherOnly) { exit 0 }

# ── Pull ───────────────────────────────────────────────────────────────────────
if (-not $NoPull) {
    git -C $repoDir pull --rebase --autostash -q
    if ($LASTEXITCODE -ne 0) { Write-Host "  git pull failed in $repoDir - deploying the current commit." -ForegroundColor DarkYellow }
}
$head = (git -C $repoDir rev-parse HEAD).Trim()

# ── Up to date? ────────────────────────────────────────────────────────────────
# Nothing the game is built from (Cpp, Shared, Capture, Vendor) differs between the deployed commit and HEAD, and the tree has no
# uncommitted change there: docs-only commits don't repackage. -Force skips this.
$upToDate = $false
if (-not $Force -and (Test-Path -LiteralPath (Join-Path $out $exeName)) -and (Test-Path -LiteralPath $marker)) {
    $deployed = (Get-Content -LiteralPath $marker -Raw).Trim()
    git -C $repoDir diff --quiet $deployed -- Cpp Shared Capture Vendor 2>$null
    $upToDate = $LASTEXITCODE -eq 0
}
if ($upToDate) {
    Write-Host "  Already deployed at $head (no game source changed) - skipping the package." -ForegroundColor Green
    if ($Launch) { Start-Process -FilePath (Join-Path $out $exeName) -WorkingDirectory $out -ArgumentList $captureArgs }
    exit 0
}

# ── Stop running copies ────────────────────────────────────────────────────────
Write-Host ''
Write-Host '  Stopping running copies...' -ForegroundColor Yellow
$procs = Get-Process -ErrorAction SilentlyContinue | Where-Object {
    $_.Path -and ($_.Path.StartsWith($out, 'OrdinalIgnoreCase') -or $_.Path.StartsWith($buildDir, 'OrdinalIgnoreCase')) }
if ($procs) { $procs | Stop-Process -Force; Start-Sleep -Milliseconds 800; Write-Host "    Stopped $(@($procs).Count)." -ForegroundColor DarkYellow }
else { Write-Host '    Nothing running.' -ForegroundColor DarkGray }

# ── Package (Shipping) ─────────────────────────────────────────────────────────
Write-Host ''
Write-Host '  Packaging Shipping (tools\package.ps1)...' -ForegroundColor Cyan
& (Join-Path $repoDir 'tools\package.ps1') -Configuration Shipping -MaxParallelActions $MaxParallelActions
if ($LASTEXITCODE) { Write-Host "Package failed ($LASTEXITCODE)." -ForegroundColor Red; exit 1 }
$builtExe = Join-Path $buildDir $exeName
if (-not (Test-Path -LiteralPath $builtExe)) { Write-Host "Packaging finished but $builtExe is missing." -ForegroundColor Red; exit 1 }

# ── Publish ────────────────────────────────────────────────────────────────────
Write-Host ''
Write-Host "  Publishing -> $out" -ForegroundColor Cyan
if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Recurse -Force }
New-Item -ItemType Directory -Force $out | Out-Null
Copy-Item -Recurse -Force (Join-Path $buildDir '*') $out
$exe = Join-Path $out $exeName
if (-not (Test-Path -LiteralPath $exe)) { Write-Host "Publish finished but $exe is missing." -ForegroundColor Red; exit 1 }
Set-Content -LiteralPath $marker -Value $head -NoNewline

Set-Shortcut $exe

Write-Host ''
Write-Host "  Published $title at $head." -ForegroundColor Green
Write-Host "    Exe : $exe" -ForegroundColor Gray
if ($Launch) {
    Write-Host '  Launching...' -ForegroundColor Cyan
    Start-Process -FilePath $exe -WorkingDirectory $out -ArgumentList $captureArgs
}
exit 0
