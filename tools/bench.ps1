<#
.SYNOPSIS
  Builds and runs the Bench: every scenario of Bench/scenarios.json (or the ones named), one process each, writing
  Bench/results/<track>-<scenario>.json and .digest.txt. See Bench/README.md.

.EXAMPLE
  tools/bench.ps1                          # build Bench.CSharp (Release), run every scenario
  tools/bench.ps1 -Scenario sol-idle,battle
  tools/bench.ps1 -NoBuild -Ticks 100      # quick smoke run with a tick cap
  tools/bench.ps1 -Track Both              # both runners, then the digest compare and timings side by side
  tools/bench.ps1 -Compare                 # compare the results already in Bench/results
#>
param(
    [string[]] $Scenario,
    [ValidateSet('CSharp', 'Cpp', 'Both')] [string] $Track = 'CSharp',
    # Bench.CSharp's runtime: net10.0 is what the C# game track runs (UnrealSharp, UE 5.8); net8.0 for comparison.
    [ValidateSet('net10.0', 'net8.0')] [string] $DotNet = 'net10.0',
    [int] $Ticks = 0,
    [switch] $NoBuild,
    # Only compare the results already on disk (both tracks), running nothing.
    [switch] $Compare
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bench = Join-Path $root 'Bench'
$scenarios = Join-Path $bench 'scenarios.json'
$results = Join-Path $bench 'results'
New-Item -ItemType Directory -Force $results | Out-Null

$names = if ($Scenario) { $Scenario | ForEach-Object { $_ -split ',' } | Where-Object { $_ } }
         else { (Get-Content $scenarios -Raw | ConvertFrom-Json).scenarios | ForEach-Object { $_.name } }

function Invoke-Runner([string] $exe, [string] $label) {
    foreach ($n in $names) {
        $runArgs = @('--scenario', $n, '--scenarios', $scenarios, '--out', $results)
        if ($Ticks -gt 0) { $runArgs += @('--ticks', "$Ticks") }
        & $exe @runArgs
        if ($LASTEXITCODE -ne 0) { throw "$label scenario '$n' failed (exit $LASTEXITCODE)" }
    }
}

# ---------------------------------------------------------------- C# side (Bench.CSharp over Unity.SpaceGod's own Core)
if ($Track -in 'CSharp', 'Both' -and -not $Compare) {
    $proj = Join-Path $bench 'CSharp\Bench.CSharp.csproj'
    if (-not $NoBuild) {
        & dotnet build $proj -c Release -nologo -v q
        if ($LASTEXITCODE -ne 0) { throw "Bench.CSharp build failed" }
    }
    Invoke-Runner (Join-Path $bench "CSharp\bin\Release\$DotNet\Bench.CSharp.exe") 'C#'
}

# ---------------------------------------------------------------- C++ side (Bench.Cpp over Cpp/Source/SpaceGodCore)
# Same arguments and outputs as Bench.CSharp (cpp-<scenario>.json / .digest.txt). The build is the standalone Core lib
# (tools/build-core.ps1, incremental) plus the runner (tools/build-bench-cpp.ps1).
if ($Track -in 'Cpp', 'Both' -and -not $Compare) {
    if (-not $NoBuild) {
        & (Join-Path $PSScriptRoot 'build-bench-cpp.ps1') -BuildCore
        if ($LASTEXITCODE -ne 0) { throw "Bench.Cpp build failed" }
    }
    Invoke-Runner (Join-Path $root 'Cpp\Intermediate\CoreStandalone\Release\Bench.Cpp.exe') 'C++'
}

# ---------------------------------------------------------------- compare (both tracks' results on disk)
# Per scenario: digest identical or the first differing line (with the system / ship / battle it belongs to and a few
# lines of context from both), then both runners' timings side by side.
function Compare-Scenario([string] $n) {
    $cs = Join-Path $results "csharp-$n.digest.txt"
    $cp = Join-Path $results "cpp-$n.digest.txt"
    if (-not (Test-Path $cs) -or -not (Test-Path $cp)) { Write-Host ("{0,-14} (missing {1})" -f $n, $(if (Test-Path $cs) { 'cpp' } else { 'csharp' })); return $false }
    $a = [IO.File]::ReadAllLines($cs); $b = [IO.File]::ReadAllLines($cp)
    $ja = Get-Content (Join-Path $results "csharp-$n.json") -Raw | ConvertFrom-Json
    $jb = Get-Content (Join-Path $results "cpp-$n.json") -Raw | ConvertFrom-Json
    $first = -1
    for ($i = 0; $i -lt [Math]::Max($a.Count, $b.Count); $i++) {
        if ($i -ge $a.Count -or $i -ge $b.Count -or $a[$i] -cne $b[$i]) { $first = $i; break }
    }
    if ($first -lt 0) {
        Write-Host ("{0,-14} IDENTICAL  digest {1}  ({2} lines)" -f $n, $ja.digestHash, $a.Count) -ForegroundColor Green
    } else {
        Write-Host ("{0,-14} DIFFERS at line {1} (C# {2} lines, C++ {3}); build digests {4}" -f $n, ($first + 1), $a.Count, $b.Count,
            $(if ($ja.buildDigestHash -eq $jb.buildDigestHash) { 'equal' } else { 'differ' })) -ForegroundColor Red
        $owner = $null
        for ($k = [Math]::Min($first, $a.Count - 1); $k -ge 0; $k--) { if ($a[$k] -match '^(system|skirmish|battle) ') { $owner = $a[$k]; break } }
        if ($owner) { Write-Host "  in: $(if ($owner.Length -gt 160) { $owner.Substring(0, 160) + '...' } else { $owner })" }
        $lo = [Math]::Max(0, $first - 2)
        foreach ($side in @(@('C#', $a), @('C++', $b))) {
            Write-Host "  --- $($side[0])"
            $lines = $side[1]
            for ($k = $lo; $k -le [Math]::Min($lines.Count - 1, $first + 2); $k++) {
                $mark = if ($k -eq $first) { '>' } else { ' ' }
                $text = $lines[$k]; if ($text.Length -gt 220) { $text = $text.Substring(0, 220) + '...' }
                Write-Host "  $mark $text"
            }
        }
        # The differing fields of the first differing line, token by token.
        if ($first -lt $a.Count -and $first -lt $b.Count) {
            $ta = $a[$first] -split ' '; $tb = $b[$first] -split ' '
            for ($k = 0; $k -lt [Math]::Max($ta.Count, $tb.Count); $k++) {
                if ($k -ge $ta.Count -or $k -ge $tb.Count -or $ta[$k] -cne $tb[$k]) {
                    Write-Host "  first differing token #${k}: C# '$(if ($k -lt $ta.Count) { $ta[$k] })'  C++ '$(if ($k -lt $tb.Count) { $tb[$k] })'"; break
                }
            }
        }
    }
    $t1 = $ja.timings; $t2 = $jb.timings
    Write-Host ("  timings ms     C#: build {0,8:0.0} tick mean {1,8:0.000} p50 {2,8:0.000} p95 {3,8:0.000} total {4,9:0.0}  ticks {5} ({6})" -f
        $t1.buildMs, $t1.tickMeanMs, $t1.tickP50Ms, $t1.tickP95Ms, $t1.totalMs, $ja.ticksRun, $ja.stopReason)
    Write-Host ("                C++: build {0,8:0.0} tick mean {1,8:0.000} p50 {2,8:0.000} p95 {3,8:0.000} total {4,9:0.0}  ticks {5} ({6})" -f
        $t2.buildMs, $t2.tickMeanMs, $t2.tickP50Ms, $t2.tickP95Ms, $t2.totalMs, $jb.ticksRun, $jb.stopReason)
    return ($first -lt 0)
}
if ($Track -eq 'Both' -or $Compare) {
    Write-Host ''
    $same = 0
    foreach ($n in $names) { if (Compare-Scenario $n) { $same++ } }
    Write-Host "`n$same of $(@($names).Count) scenario(s) identical"
}
