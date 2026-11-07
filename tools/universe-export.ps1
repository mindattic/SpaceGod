[CmdletBinding()]
param(
  # Where the interchange file goes. Default Data/Prose/spacegod.universe.json (gitignored: see docs/PROSE.md).
  [string]$Out,
  [int]$Seed = 1977,
  # Days the galaxy runs on (coarse detail, as the Bench's) before it is catalogued: wrecks, derelicts, wars and grudges
  # need a little history. 0 catalogues the galaxy as built.
  [double]$Days = 30,
  # The fixed start date (Bench/scenarios.json's startJd), so the export never depends on the clock.
  [double]$StartJd = 2461324.0,
  # Skip meeting every ship's pilot (much smaller file; no characters or crew-of edges).
  [switch]$NoPilots,
  # Rebuild SpaceGodCore.lib first (tools/build-core.ps1, incremental).
  [switch]$BuildCore,
  # Use the exe already built.
  [switch]$NoBuild,
  [int]$MaxParallel = 2
)

<#
.SYNOPSIS
US-T171: builds tools/universe-export (a headless Core runner, no Unreal) and writes the SpaceGod universe as a Prose
Universe Interchange file (RFC 0007), then checks its manifest (tools/prose_manifest.py). It is the Prose consumer kit's
mapper (Data/Prose/prose.config.json, US-T236): `kit.ps1 push --map --prune` runs it.

.DESCRIPTION
Links tools/universe-export/UniverseExport.cpp against the standalone SpaceGodCore.lib (tools/build-core.ps1) into
Cpp/Intermediate/CoreStandalone/Release/UniverseExport.exe, runs it with the build hash (git HEAD, "+dirty" when Core
has uncommitted edits) recorded in the file's source block and manifest entity, and checks the manifest's counts with
tools/prose_manifest.py (the kit's validate checks the rest of the contract). Same seed + same build = the same file,
byte for byte. Push it to Prose with the consumer kit (docs/PROSE.md).

.EXAMPLE
tools/universe-export.ps1                 # build the exporter against the lib already built, export, validate
tools/universe-export.ps1 -BuildCore      # rebuild Core first
tools/universe-export.ps1 -Days 0 -NoPilots
#>

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$core = Join-Path $repo 'Cpp\Source\SpaceGodCore'
$src = Join-Path $PSScriptRoot 'universe-export\UniverseExport.cpp'
$outDir = Join-Path $repo 'Cpp\Intermediate\CoreStandalone\Release'
$lib = Join-Path $outDir 'SpaceGodCore.lib'
$exe = Join-Path $outDir 'UniverseExport.exe'
if (-not $Out) { $Out = Join-Path $repo 'Data\Prose\spacegod.universe.json' }
New-Item -ItemType Directory -Force (Split-Path -Parent $Out) | Out-Null

if (-not $NoBuild) {
  if ($BuildCore) {
    & (Join-Path $PSScriptRoot 'build-core.ps1') -Configuration Release -MaxParallel $MaxParallel -SkipParity
    if ($LASTEXITCODE -ne 0) { throw 'build-core.ps1 failed.' }
  }
  if (-not (Test-Path -LiteralPath $lib)) { throw "No ${lib}: run tools/build-core.ps1 first, or pass -BuildCore." }
  if (-not ($env:VCToolsInstallDir -and (Get-Command cl.exe -ErrorAction SilentlyContinue))) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $req = 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64'
    $vs = & $vswhere -products * -version '[17.0,18.0)' -requires $req -latest -property installationPath
    if (-not $vs) { $vs = & $vswhere -products * -requires $req -latest -property installationPath }
    if (-not $vs) { throw 'No Visual Studio with the C++ x64 build tools found (vswhere).' }
    $lines = & cmd.exe /c "`"$(Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat')`" >nul 2>&1 && set"
    foreach ($line in $lines) {
      $i = $line.IndexOf('=')
      if ($i -gt 0) { [Environment]::SetEnvironmentVariable($line.Substring(0, $i), $line.Substring($i + 1), 'Process') }
    }
  }
  # The same flags the lib was built with (tools/build-core.ps1), /GL included so it links with LTCG.
  $flags = @('/nologo', '/c', '/std:c++20', '/EHsc', '/GR', '/W4', '/permissive-', '/utf-8', '/fp:precise', '/Zc:__cplusplus',
    '/Zc:preprocessor', '/bigobj', '/DSPACEGOD_STANDALONE', '/D_CRT_SECURE_NO_WARNINGS', "/I`"$core`"",
    '/O2', '/GL', '/Gw', '/arch:AVX', '/MD', '/DNDEBUG', '/Zi')
  $objDir = Join-Path $outDir 'exportobj'
  New-Item -ItemType Directory -Force $objDir | Out-Null
  $obj = Join-Path $objDir 'UniverseExport.obj'
  & cl.exe @flags "/Fo$obj" "/Fd$(Join-Path $objDir 'export.pdb')" /FS $src | Where-Object { $_ -and $_ -ne 'UniverseExport.cpp' } | ForEach-Object { Write-Host "  $_" }
  if ($LASTEXITCODE -ne 0) { throw 'UniverseExport.cpp failed to compile.' }
  & link.exe /nologo /DEBUG /LTCG /OPT:REF /OPT:ICF "/CGTHREADS:$MaxParallel" "/OUT:$exe" $obj $lib psapi.lib dbghelp.lib
  if ($LASTEXITCODE -ne 0) { throw 'link.exe failed (UniverseExport.exe).' }
  Write-Host "exe     $exe"
}
if (-not (Test-Path -LiteralPath $exe)) { throw "No $exe (run without -NoBuild)." }

# The build hash: HEAD, marked dirty when Core (what the export reads) has uncommitted edits.
$build = 'unknown'
try {
  $build = (git -C $repo rev-parse --short=12 HEAD).Trim()
  $dirty = git -C $repo status --porcelain -- Cpp/Source/SpaceGodCore
  if ($dirty) { $build += '+dirty' }
} catch { }

$runArgs = @('--out', $Out, '--seed', "$Seed", '--days', "$Days", '--start-jd', "$StartJd", '--build', $build)
if ($NoPilots) { $runArgs += @('--pilots', 'none') }
$sw = [Diagnostics.Stopwatch]::StartNew()
& $exe @runArgs
if ($LASTEXITCODE -ne 0) { throw "UniverseExport.exe failed (exit $LASTEXITCODE)." }
Write-Host ("exported in {0:0.0} s" -f $sw.Elapsed.TotalSeconds)

& python (Join-Path $PSScriptRoot 'prose_manifest.py') $Out
if ($LASTEXITCODE -ne 0) { throw 'The interchange file does not match its manifest.' }
exit 0
