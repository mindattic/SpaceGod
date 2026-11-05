[CmdletBinding()]
param(
  # Read without consuming (GET ...?peek=true): nothing is marked delivered and nothing is written.
  [switch]$Peek,
  # Process a saved array of outbox events (the GET's JSON) instead of draining the Hub: for tests and replays.
  [string]$FromFile,
  # Where accepted proposals go for review (default Data/Lore).
  [string]$LoreDir,
  [string]$Hub = 'http://127.0.0.1:5900',
  [string]$Consumer = 'spacegod'
)

<#
.SYNOPSIS
US-T171: the way back from Prose. Drains Prose's Outbox for consumer 'spacegod' and writes each ACCEPTED proposal as a
versioned data file under Data/Lore/ for a human to review and commit. Nothing here touches the simulation.

.DESCRIPTION
Prose never edits game data (docs/PROSE.md). It proposes: a new technology, a character arc, a story template, a mystery
hook, a piece of lore, posted to its Outbox for consumer 'spacegod' as an event

    { "kind": "proposal.<category>", "summary": "...", "data": "<JSON text>" }

whose data is one interchange-style entity plus a status:

    { "id": "tech-graviton-lance", "type": "technology", "name": "...", "summary": "...", "details": { ... },
      "relations": [ { "to": "faction-sol-federation", "kind": "researched-by" } ], "tags": [ ... ],
      "status": "accepted", "rationale": "..." }

Only status "accepted" (or a kind ending ".accepted") is written, to Data/Lore/<category>/<id>.v<N>.json, N one past the
newest version already there; an identical proposal is not written again. Every event drained, proposal or not
(Prose's own import/export notices among them), is appended to the gitignored journal Data/Lore/.outbox/<consumer>.jsonl
first, so draining never loses one. A file under Data/Lore reaches the game only when someone reviews it, commits it and
writes the Core loader that reads it (none exists yet: nothing reaches the simulation without a commit, so the Bench and
rewind stay deterministic).

.EXAMPLE
tools/prose-outbox.ps1           # drain and write accepted proposals
tools/prose-outbox.ps1 -Peek     # look without consuming
#>

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $LoreDir) { $LoreDir = Join-Path $repo 'Data\Lore' }
$journalDir = Join-Path $LoreDir '.outbox'
$stable = '^[a-z0-9]+(-[a-z0-9]+)*$'

if ($FromFile) {
  $parsed = Get-Content -LiteralPath $FromFile -Raw | ConvertFrom-Json
} else {
  $settings = Join-Path $env:LOCALAPPDATA 'MindAttic\Prose\Settings.json'
  $key = (Get-Content -LiteralPath $settings -Raw | ConvertFrom-Json).HubApiKey
  $uri = "$Hub/api/outbox/$Consumer" + $(if ($Peek) { '?peek=true' } else { '' })
  try { $parsed = Invoke-RestMethod -Uri $uri -Headers @{ 'X-Prose-Key' = $key } -TimeoutSec 30 }
  catch { throw "Could not reach the Prose Outbox at $uri ($($_.Exception.Message)). Is the Hub running? tools/prose-sync.ps1 starts it." }
}
# A JSON array of one comes back as the object itself, and an empty array as nothing.
# (Windows PowerShell hands a parsed array on as one object: unroll it from the variable.)
$events = @(@($parsed) | ForEach-Object { $_ } | Where-Object { $_ -ne $null })
Write-Host "$($events.Count) outbox event(s) for '$Consumer'$(if ($Peek) { ' (peek)' })"
if ($events.Count -eq 0) { exit 0 }

if (-not $Peek) {
  New-Item -ItemType Directory -Force $journalDir | Out-Null
  $journal = Join-Path $journalDir "$Consumer.jsonl"
  foreach ($e in $events) { Add-Content -LiteralPath $journal -Value ($e | ConvertTo-Json -Compress -Depth 20) -Encoding utf8 }
}

$written = 0
foreach ($e in $events) {
  $kind = [string]$e.kind
  Write-Host ("- [{0}] {1}" -f $kind, $e.summary)
  if (-not $kind.StartsWith('proposal')) { continue }
  $p = $null
  try { $p = if ($e.data -is [string]) { $e.data | ConvertFrom-Json } else { $e.data } } catch { }
  if ($null -eq $p) { Write-Host '    skipped: data is not a JSON proposal' -ForegroundColor Yellow; continue }
  $accepted = $p.status -eq 'accepted' -or $kind.EndsWith('.accepted')
  if (-not $accepted) { Write-Host "    not accepted yet (status '$($p.status)'): journalled only"; continue }
  $id = [string]$p.id
  if ($id -notmatch $stable) { Write-Host "    skipped: id '$id' is not a stable slug" -ForegroundColor Yellow; continue }
  foreach ($f in 'type', 'name', 'summary') { if (-not $p.$f) { Write-Host "    skipped: no $f" -ForegroundColor Yellow; $id = $null; break } }
  if (-not $id) { continue }

  # proposal.technology -> technology; proposal.character.accepted -> character; plain "proposal" -> the entity's type.
  $category = ($kind -replace '^proposal\.?', '' -replace '\.accepted$', '')
  if (-not $category) { $category = [string]$p.type }
  $category = ($category.ToLowerInvariant() -replace '[^a-z0-9]+', '-').Trim('-')
  if ($Peek) { Write-Host "    would write Data/Lore/$category/$id.v<N>.json"; continue }

  $dir = Join-Path $LoreDir $category
  New-Item -ItemType Directory -Force $dir | Out-Null
  $versions = @(Get-ChildItem -LiteralPath $dir -Filter "$id.v*.json" -ErrorAction SilentlyContinue |
    ForEach-Object { if ($_.Name -match '\.v(\d+)\.json$') { [int]$Matches[1] } } | Sort-Object)
  $proposalJson = $p | ConvertTo-Json -Depth 20
  if ($versions.Count) {
    $latest = Get-Content -LiteralPath (Join-Path $dir "$id.v$($versions[-1]).json") -Raw | ConvertFrom-Json
    if (($latest.proposal | ConvertTo-Json -Depth 20) -eq $proposalJson) { Write-Host "    unchanged since v$($versions[-1])"; continue }
  }
  $n = if ($versions.Count) { $versions[-1] + 1 } else { 1 }
  $doc = [ordered]@{
    schema = 'spacegod-lore/1'
    id = $id
    version = $n
    category = $category
    status = 'pending-review'
    source = 'prose'
    universe = $Consumer
    outboxEvent = [ordered]@{ id = $e.id; ts = $e.ts; kind = $kind; summary = $e.summary }
    proposal = $p
  }
  $path = Join-Path $dir "$id.v$n.json"
  [IO.File]::WriteAllText($path, ($doc | ConvertTo-Json -Depth 20) + "`n", (New-Object System.Text.UTF8Encoding($false)))
  Write-Host "    wrote $path"
  $written++
}
Write-Host "$written proposal file(s) written for review under $LoreDir"
exit 0
