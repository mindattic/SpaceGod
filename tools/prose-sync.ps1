[CmdletBinding()]
param(
  # The interchange file to push (tools/universe-export.ps1 writes it).
  [string]$File,
  # Export first (tools/universe-export.ps1 with these settings), then push.
  [switch]$Export,
  [int]$Seed = 1977,
  [double]$Days = 30,
  [switch]$NoPilots,
  [switch]$NoBuild,
  # Push only: no export back, no diff, no leak check.
  [switch]$NoCheck,
  # Import a second time and require it to change nothing (RFC 0007's idempotency promise). Slow on a big universe.
  [switch]$Idempotency,
  [string]$Hub = 'http://127.0.0.1:5900',
  [string]$Universe = 'spacegod'
)

<#
.SYNOPSIS
US-T171: pushes the SpaceGod universe into Prose through the Hub, then proves the round trip.

.DESCRIPTION
Game -> Prose, by the contract only (docs/PROSE.md): never raw SQL, never Prose's own services.
  1. POST the interchange file to the Hub's /api/universes/<slug>/import with the X-Prose-Key header (the key Prose
     keeps in %LOCALAPPDATA%\MindAttic\Prose\Settings.json). Idempotent upsert by id; the universe is created on first push.
  2. Export it back with Prose's own exporter (the export_universe_file MCP tool, run by the Hub: what
     `prose --universe-export` runs) to Data/Prose/spacegod.prose-export.json.
  3. Diff the two semantically (tools/prose_universe.py diff): every entity, relation, tag and detail must match, and the
     entity and edge counts by type must be equal, so a stale or leaked entity shows at once.
  4. Pull the Hub's graph view (/api/universes/<slug>/snapshot?scope=all, the service the EVE post-mortem caught leaking
     other universes' rows) and require every node in it to be one of the file's entities.
If the Hub is down it is started from Prose's deployed C:\Apps\MindAttic\Prose\Hub.exe (what Prose's own SessionStart hook
.prose\hooks\start-prose-hub.ps1 starts); the Hub stops itself after a few idle minutes, so this is the usual case.

.EXAMPLE
tools/prose-sync.ps1 -Export          # export (seed 1977, 30 days), push, export back, diff, leak check
tools/prose-sync.ps1                  # push the file already exported
#>

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $File) { $File = Join-Path $repo 'Data\Prose\spacegod.universe.json' }
$back = Join-Path (Split-Path -Parent $File) "$Universe.prose-export.json"
$snap = Join-Path (Split-Path -Parent $File) "$Universe.prose-snapshot.json"
$py = Join-Path $PSScriptRoot 'prose_universe.py'

if ($Export) {
  $a = @{ Out = $File; Seed = $Seed; Days = $Days }
  if ($NoPilots) { $a.NoPilots = $true }
  if ($NoBuild) { $a.NoBuild = $true }
  & (Join-Path $PSScriptRoot 'universe-export.ps1') @a
  if ($LASTEXITCODE -ne 0) { throw 'universe-export.ps1 failed.' }
}
if (-not (Test-Path -LiteralPath $File)) { throw "No ${File}: run tools/universe-export.ps1 (or pass -Export)." }
& python $py validate $File
if ($LASTEXITCODE -ne 0) { throw "${File} is not a valid interchange file." }

# ---------------------------------------------------------------- the Hub
function Test-Hub { try { (Invoke-WebRequest "$Hub/api/health" -UseBasicParsing -TimeoutSec 3).StatusCode -eq 200 } catch { $false } }
if (-not (Test-Hub)) {
  $exe = 'C:\Apps\MindAttic\Prose\Hub.exe'
  if (-not (Test-Path -LiteralPath $exe)) {
    throw "The Prose Hub is not running at $Hub and $exe is missing: deploy Prose (its run.ps1 -Apps Hub -Start Hub, or .prose\hooks\start-prose-hub.ps1 in the Prose repo)."
  }
  Write-Host "Prose Hub down: starting $exe"
  Start-Process -FilePath $exe -WorkingDirectory (Split-Path -Parent $exe) -WindowStyle Hidden | Out-Null
  $deadline = (Get-Date).AddSeconds(90)
  while (-not (Test-Hub)) {
    if ((Get-Date) -gt $deadline) { throw "The Prose Hub did not come up at $Hub within 90 s." }
    Start-Sleep -Seconds 2
  }
  Write-Host 'Prose Hub up'
}
$settings = Join-Path $env:LOCALAPPDATA 'MindAttic\Prose\Settings.json'
$key = (Get-Content -LiteralPath $settings -Raw | ConvertFrom-Json).HubApiKey
if (-not $key) { throw "No HubApiKey in $settings (the Hub writes one on first start)." }
$headers = @{ 'X-Prose-Key' = $key }

function Import-Universe {
  $body = [IO.File]::ReadAllBytes($File)
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $r = Invoke-RestMethod -Method Post -Uri "$Hub/api/universes/$Universe/import" -Headers $headers -ContentType 'application/json; charset=utf-8' -Body $body -TimeoutSec 7200
  Write-Host ("import  {0}: created {1}, updated {2}, edges created {3}, stubs {4} ({5:0} s){6}" -f $r.universeSlug, $r.entitiesCreated, $r.entitiesUpdated,
    $r.edgesCreated, $r.stubsCreated, $sw.Elapsed.TotalSeconds, $(if ($r.universeCreated) { ', universe created' } else { '' }))
  if ($r.errors -and @($r.errors).Count) { throw "Prose refused the import: $($r.errors -join '; ')" }
  return $r
}

$first = Import-Universe
if ($first.stubsCreated -gt 0) { Write-Host "WARNING: Prose made $($first.stubsCreated) stub entities: some relation points at an id the file does not hold." -ForegroundColor Yellow }
if ($NoCheck) { exit 0 }

# ---------------------------------------------------------------- the round trip
$exportReq = @{ toolClass = 'UniverseInterchangeTools'; method = 'ExportUniverseFileImpl'; args = @{ slug = $Universe; path = (Resolve-Path -LiteralPath (Split-Path -Parent $File)).Path + "\$Universe.prose-export.json" } } | ConvertTo-Json -Depth 4
$x = Invoke-RestMethod -Method Post -Uri "$Hub/api/mcp-invoke" -Headers $headers -ContentType 'application/json' -Body $exportReq -TimeoutSec 1800
Write-Host "export  $(($x | ConvertTo-Json -Compress -Depth 4))"
if (-not (Test-Path -LiteralPath $back)) { throw "Prose wrote no export at $back." }

$failed = $false
& python $py diff $File $back
if ($LASTEXITCODE -ne 0) { $failed = $true }

Invoke-WebRequest "$Hub/api/universes/$Universe/snapshot?scope=all" -UseBasicParsing -TimeoutSec 600 -OutFile $snap
& python $py leak $File $snap
if ($LASTEXITCODE -ne 0) { $failed = $true }

if ($Idempotency) {
  $again = Import-Universe
  if ($again.entitiesCreated -ne 0 -or $again.edgesCreated -ne 0 -or $again.stubsCreated -ne 0) { Write-Host 'idempotency: the second import changed something' -ForegroundColor Red; $failed = $true }
  else { Write-Host 'idempotency: the second import created nothing' }
}
if ($failed) { Write-Host "`nround trip FAILED" -ForegroundColor Red; exit 1 }
Write-Host "`nround trip clean: the file and Prose's '$Universe' universe hold the same entities and edges"
exit 0
