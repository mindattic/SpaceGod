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
  # Tolerate entities Prose still holds from an older export (it never deletes on import; docs/PROSE.md): listed, not failed.
  [switch]$AllowStale,
  # Entities per import request (see Import-Universe).
  [int]$BatchSize = 400,
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
function Start-Hub {
  if (Test-Hub) { return }
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
Start-Hub
$settings = Join-Path $env:LOCALAPPDATA 'MindAttic\Prose\Settings.json'
$key = (Get-Content -LiteralPath $settings -Raw | ConvertFrom-Json).HubApiKey
if (-not $key) { throw "No HubApiKey in $settings (the Hub writes one on first start)." }
$headers = @{ 'X-Prose-Key' = $key }

# One import is one request, and Prose saves entity by entity and edge by edge, so a universe this size is a long request:
# a Hub that stops part way (it did, on the first push) loses it. So the file goes in batches (tools/prose_universe.py
# chunk): every entity first without its relations, then every entity whole, so no relation ever points at an entity
# Prose has not got (no stubs), and a batch that fails is retried once on a restarted Hub. Each batch is an ordinary,
# idempotent import of a valid interchange file.
$batchDir = Join-Path (Split-Path -Parent $File) "$Universe.import-batches"
function Import-Universe {
  $batches = @(& python $py chunk $File $batchDir $BatchSize)
  if ($LASTEXITCODE -ne 0) { throw 'Could not split the file into import batches.' }
  $tot = [ordered]@{ created = 0; updated = 0; edges = 0; stubs = 0 }
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $i = 0
  foreach ($batch in $batches) {
    $i++
    $body = [IO.File]::ReadAllBytes($batch)
    $r = $null
    foreach ($attempt in 1, 2) {
      try { $r = Invoke-RestMethod -Method Post -Uri "$Hub/api/universes/$Universe/import" -Headers $headers -ContentType 'application/json; charset=utf-8' -Body $body -TimeoutSec 3600; break }
      catch {
        if ($attempt -eq 2) { throw "Batch $(Split-Path -Leaf $batch) failed twice: $($_.Exception.Message)" }
        Write-Host "  $(Split-Path -Leaf $batch): $($_.Exception.Message); retrying" -ForegroundColor Yellow
        Start-Hub
      }
    }
    if ($r.errors -and @($r.errors).Count) { throw "Prose refused $(Split-Path -Leaf $batch): $($r.errors -join '; ')" }
    $tot.created += $r.entitiesCreated; $tot.updated += $r.entitiesUpdated; $tot.edges += $r.edgesCreated; $tot.stubs += $r.stubsCreated
    Write-Host ("  {0} ({1}/{2}): created {3}, updated {4}, edges {5}, stubs {6}  [{7:0} s]" -f (Split-Path -Leaf $batch), $i, $batches.Count,
      $r.entitiesCreated, $r.entitiesUpdated, $r.edgesCreated, $r.stubsCreated, $sw.Elapsed.TotalSeconds)
  }
  Remove-Item -LiteralPath $batchDir -Recurse -Force -ErrorAction SilentlyContinue
  Write-Host ("import  {0}: created {1}, updated {2}, edges created {3}, stubs {4} in {5} batches ({6:0} s)" -f $Universe, $tot.created, $tot.updated, $tot.edges, $tot.stubs, $batches.Count, $sw.Elapsed.TotalSeconds)
  return [pscustomobject]$tot
}

$first = Import-Universe
if ($first.stubs -gt 0) { Write-Host "WARNING: Prose made $($first.stubs) stub entities: some relation points at an id it did not hold." -ForegroundColor Yellow }
if ($NoCheck) { exit 0 }

# ---------------------------------------------------------------- the round trip
# Prose's own exporter (the export_universe_file MCP tool, what `prose --universe-export` runs), run by the Hub. A tool that
# writes needs its universe named explicitly.
$exportReq = @{ toolClass = 'UniverseInterchangeTools'; method = 'ExportUniverseFileImpl'; universe = $Universe; scopeExplicit = $true
  args = @{ slug = $Universe; path = (Resolve-Path -LiteralPath (Split-Path -Parent $File)).Path + "\$Universe.prose-export.json" } } | ConvertTo-Json -Depth 4
$x = Invoke-RestMethod -Method Post -Uri "$Hub/api/mcp-invoke" -Headers $headers -ContentType 'application/json' -Body $exportReq -TimeoutSec 1800
Write-Host "export  $(($x | ConvertTo-Json -Compress -Depth 4))"
if (-not (Test-Path -LiteralPath $back)) { throw "Prose wrote no export at $back." }
$failed = $false
$staleArg = if ($AllowStale) { @('--allow-stale') } else { @() }
& python $py diff $File $back @staleArg
if ($LASTEXITCODE -ne 0) { $failed = $true }

Invoke-WebRequest "$Hub/api/universes/$Universe/snapshot?scope=all" -UseBasicParsing -TimeoutSec 600 -OutFile $snap
if ($AllowStale) { & python $py leak $File $snap $back } else { & python $py leak $File $snap }
if ($LASTEXITCODE -ne 0) { $failed = $true }

if ($Idempotency) {
  $again = Import-Universe
  if ($again.created -ne 0 -or $again.edges -ne 0 -or $again.stubs -ne 0) { Write-Host 'idempotency: the second import changed something' -ForegroundColor Red; $failed = $true }
  else { Write-Host 'idempotency: the second import created nothing' }
}
if ($failed) { Write-Host "`nround trip FAILED" -ForegroundColor Red; exit 1 }
Write-Host "`nround trip clean: the file and Prose's '$Universe' universe hold the same entities and edges"
exit 0
