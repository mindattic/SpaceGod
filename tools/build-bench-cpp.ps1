[CmdletBinding()]
param(
  [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
  # Build SpaceGodCore.lib first (tools/build-core.ps1, whole Core, only what changed).
  [switch]$BuildCore,
  [int]$MaxParallel = 3
)

<#
.SYNOPSIS
Builds Bench.Cpp.exe (Bench/Cpp/src), the headless Bench runner over the C++ Core, linked against the standalone
SpaceGodCore.lib that tools/build-core.ps1 makes.

.DESCRIPTION
Same compiler flags as build-core (C++20, /EHsc /GR /W4 /permissive- /fp:precise /utf-8, SPACEGOD_STANDALONE).
Output: Cpp/Intermediate/CoreStandalone/<Configuration>/Bench.Cpp.exe, where tools/bench.ps1 runs it from.

.EXAMPLE
tools/build-bench-cpp.ps1 -BuildCore # whole Core lib (incremental), then Bench.Cpp
tools/build-bench-cpp.ps1           # Bench.Cpp only, against the lib already built
#>

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$core = Join-Path $repo 'Cpp\Source\SpaceGodCore'
$src = Join-Path $repo 'Bench\Cpp\src'
$out = Join-Path $repo "Cpp\Intermediate\CoreStandalone\$Configuration"
$lib = Join-Path $out 'SpaceGodCore.lib'

if ($BuildCore) {
  & (Join-Path $PSScriptRoot 'build-core.ps1') -Configuration $Configuration -MaxParallel $MaxParallel -SkipParity
  if ($LASTEXITCODE -ne 0) { throw 'build-core.ps1 failed.' }
}
if (-not (Test-Path -LiteralPath $lib)) { throw "No ${lib}: run tools/build-core.ps1 (whole Core) first, or pass -BuildCore." }

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

$flags = @('/nologo', '/c', '/std:c++20', '/EHsc', '/GR', '/W4', '/permissive-', '/utf-8', '/fp:precise',
  '/Zc:__cplusplus', '/Zc:preprocessor', '/bigobj', '/DSPACEGOD_STANDALONE', '/D_CRT_SECURE_NO_WARNINGS', "/I`"$core`"", "/I`"$src`"")
if ($Configuration -eq 'Release') { $flags += @('/O2', '/GL', '/Gw', '/MD', '/DNDEBUG', '/Zi') } else { $flags += @('/Od', '/MDd', '/Zi', '/RTC1') }

$objDir = Join-Path $out 'benchobj'
New-Item -ItemType Directory -Force $objDir | Out-Null
$objs = @()
$failed = $false
foreach ($f in Get-ChildItem -LiteralPath $src -Filter '*.cpp' -File) {
  $obj = Join-Path $objDir ([IO.Path]::ChangeExtension($f.Name, '.obj'))
  $objs += $obj
  & cl.exe @flags "/Fo$obj" "/Fd$(Join-Path $objDir 'bench.pdb')" /FS $f.FullName | Where-Object { $_ -and $_ -ne $f.Name } | ForEach-Object { Write-Host "  $_" }
  if ($LASTEXITCODE -ne 0) { Write-Host "FAILED  $($f.Name)" -ForegroundColor Red; $failed = $true } else { Write-Host "ok      $($f.Name)" }
}
if ($failed) { exit 1 }

$exe = Join-Path $out 'Bench.Cpp.exe'
# Release: link-time code generation across Bench.Cpp and SpaceGodCore.lib (both compiled /GL).
[string[]]$linkOpt = if ($Configuration -eq 'Release') { @('/LTCG', '/OPT:REF', '/OPT:ICF', "/CGTHREADS:$([Math]::Min($MaxParallel, 8))") } else { @() }
& link.exe /nologo /DEBUG @linkOpt "/OUT:$exe" @objs $lib psapi.lib dbghelp.lib
if ($LASTEXITCODE -ne 0) { throw 'link.exe failed (Bench.Cpp).' }
Write-Host "exe     $exe"
exit 0
