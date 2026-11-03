<#
.SYNOPSIS
  Builds and runs the Bench: every scenario of Bench/scenarios.json (or the ones named), one process each, writing
  Bench/results/<track>-<scenario>.json and .digest.txt. See Bench/README.md.

.EXAMPLE
  tools/bench.ps1                          # build Bench.CSharp (Release), run every scenario
  tools/bench.ps1 -Scenario sol-idle,battle
  tools/bench.ps1 -NoBuild -Ticks 100      # quick smoke run with a tick cap
#>
param(
    [string[]] $Scenario,
    [ValidateSet('CSharp', 'Cpp', 'Both')] [string] $Track = 'CSharp',
    [int] $Ticks = 0,
    [switch] $NoBuild
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
if ($Track -in 'CSharp', 'Both') {
    $proj = Join-Path $bench 'CSharp\Bench.CSharp.csproj'
    if (-not $NoBuild) {
        & dotnet build $proj -c Release -nologo -v q
        if ($LASTEXITCODE -ne 0) { throw "Bench.CSharp build failed" }
    }
    Invoke-Runner (Join-Path $bench 'CSharp\bin\Release\net8.0\Bench.CSharp.exe') 'C#'
}

# ---------------------------------------------------------------- C++ side: HOOK (a later ticket)
# Bench.Cpp runs the same scenarios over Cpp/Source/SpaceGodCore and must write Bench/results/cpp-<scenario>.json and
# cpp-<scenario>.digest.txt in the format Bench/README.md specifies, taking the same arguments as Bench.CSharp
# (--scenario, --scenarios, --out, --ticks). Build it here, then: Invoke-Runner <path to Bench.Cpp.exe> 'C++'
# A compare script then diffs csharp-*.digest.txt against cpp-*.digest.txt and tabulates the timings.
if ($Track -in 'Cpp', 'Both') {
    throw "Bench.Cpp is not written yet: see the C++ hook in tools/bench.ps1 and Bench/README.md"
}
