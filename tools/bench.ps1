<#
.SYNOPSIS
  Builds and runs the Bench: every scenario of Bench/scenarios.json (or the ones named), one process each, writing
  Bench/results/cpp-<scenario>.json and .digest.txt, then checks each digestHash against Bench/expected-digests.json
  (the hashes Unity.SpaceGod's own C# Core produces; see Bench/README.md). Exits 1 if any differs.

.EXAMPLE
  tools/bench.ps1                          # build Bench.Cpp (Release, whole Core), run every scenario, check digests
  tools/bench.ps1 -Scenario sol-idle,battle
  tools/bench.ps1 -NoBuild -Ticks 100      # quick smoke run with a tick cap (digests then differ; not checked)
  tools/bench.ps1 -Compare                 # only check the results already in Bench/results
  tools/bench.ps1 -Rewind                  # US-T99: build Bench.Rewind.exe, run --rewind-probe per scenario (keyframe
                                           # sizes, deltas, replay speed, restore determinism; docs/REWIND.md)
#>
param(
    [string[]] $Scenario,
    # Kept for the old command lines; the C++ runner is the only one in this repo.
    [ValidateSet('Cpp')] [string] $Track = 'Cpp',
    [int] $Ticks = 0,
    [switch] $NoBuild,
    # Only check the results already on disk, running nothing.
    [switch] $Compare,
    # The rewind probe (US-T99) instead of the digest check: Bench/results/rewind-<scenario>.json per scenario. Exits 1
    # if any restored keyframe re-simulates to a different digest than the live run, or keyframing changed the run.
    [switch] $Rewind,
    [int] $RewindSamples = 4
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bench = Join-Path $root 'Bench'
$scenarios = Join-Path $bench 'scenarios.json'
$results = Join-Path $bench 'results'
$expected = Get-Content (Join-Path $bench 'expected-digests.json') -Raw | ConvertFrom-Json
New-Item -ItemType Directory -Force $results | Out-Null

$names = if ($Scenario) { $Scenario | ForEach-Object { $_ -split ',' } | Where-Object { $_ } }
         else { (Get-Content $scenarios -Raw | ConvertFrom-Json).scenarios | ForEach-Object { $_.name } }

if ($Rewind) {
    if (-not $NoBuild) {
        & (Join-Path $PSScriptRoot 'build-bench-cpp.ps1') -BuildCore -Rewind
        if ($LASTEXITCODE -ne 0) { throw "Bench.Rewind build failed" }
    }
    $exe = Join-Path $root 'Cpp\Intermediate\CoreStandalone\Release\Bench.Rewind.exe'
    $bad = @()
    foreach ($n in $names) {
        & $exe --scenario $n --scenarios $scenarios --out $results --rewind-probe --rewind-samples $RewindSamples
        if ($LASTEXITCODE -ne 0) { $bad += $n }
        $j = Get-Content (Join-Path $results "rewind-$n.json") -Raw | ConvertFrom-Json
        if ($j.plainDigest -ne $expected.$n) { Write-Host "$n : the plain pass gave $($j.plainDigest), expected $($expected.$n)" -ForegroundColor Red; $bad += $n }
    }
    if ($bad) { Write-Host "`nrewind probe: determinism problems in $($bad -join ', ')" -ForegroundColor Red; exit 1 }
    Write-Host "`nrewind probe: every restore re-simulated to the live digest"
    exit 0
}

# Bench.Cpp over Cpp/Source/SpaceGodCore: the standalone Core lib (tools/build-core.ps1, incremental) plus the runner
# (tools/build-bench-cpp.ps1).
if (-not $Compare) {
    if (-not $NoBuild) {
        & (Join-Path $PSScriptRoot 'build-bench-cpp.ps1') -BuildCore
        if ($LASTEXITCODE -ne 0) { throw "Bench.Cpp build failed" }
    }
    $exe = Join-Path $root 'Cpp\Intermediate\CoreStandalone\Release\Bench.Cpp.exe'
    foreach ($n in $names) {
        $runArgs = @('--scenario', $n, '--scenarios', $scenarios, '--out', $results)
        if ($Ticks -gt 0) { $runArgs += @('--ticks', "$Ticks") }
        & $exe @runArgs
        if ($LASTEXITCODE -ne 0) { throw "scenario '$n' failed (exit $LASTEXITCODE)" }
    }
}
if ($Ticks -gt 0) { Write-Host "`n-Ticks caps the runs, so the digests are not checked."; exit 0 }

Write-Host ''
$same = 0
foreach ($n in $names) {
    $file = Join-Path $results "cpp-$n.json"
    if (-not (Test-Path $file)) { Write-Host ("{0,-14} (no result)" -f $n) -ForegroundColor Red; continue }
    $j = Get-Content $file -Raw | ConvertFrom-Json
    $want = $expected.$n
    $t = $j.timings
    $line = "{0,-14} {1} {2}  (expected {3})  tick mean {4:0.000} p50 {5:0.000} p95 {6:0.000} ms, total {7:0.0} ms, {8} ticks ({9})" -f
        $n, $(if ($j.digestHash -eq $want) { 'IDENTICAL' } else { 'DIFFERS  ' }), $j.digestHash, $want,
        $t.tickMeanMs, $t.tickP50Ms, $t.tickP95Ms, $t.totalMs, $j.ticksRun, $j.stopReason
    if ($j.digestHash -eq $want) { $same++; Write-Host $line -ForegroundColor Green } else { Write-Host $line -ForegroundColor Red }
}
Write-Host "`n$same of $(@($names).Count) scenario(s) match the recorded digests"
if ($same -ne @($names).Count) { exit 1 }
exit 0
