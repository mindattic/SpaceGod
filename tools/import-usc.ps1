[CmdletBinding()]
param(
  # The pack's FBX zip (usc_fbx.zip, from the owner's library). Only needed until it has been extracted once.
  [string]$Zip = (Join-Path $env:USERPROFILE 'Downloads\usc_fbx.zip'),
  # Blender 3.3 or later (FBX in and out only); the newest under Program Files\Blender Foundation when not given.
  [string]$Blender = '',
  [string]$EngineVersion = '5.8',
  # Re-import into Unreal only, from the pieces already cut.
  [switch]$SkipPrepare
)

<#
.SYNOPSIS
Rebuild /Game/ThirdParty/USC (Ebal Studios' Ultimate Spaceships Creator) from the pack's FBX zip (US-T142).
Extracts the zip into Cpp/SourceArt/ThirdParty/USC/ if it is not there yet, cuts the ships the game uses into pieces
with Blender (tools/usc_prepare.py: one frame per ship, warships split into bow and stern at ShipMeshFactory::SplitZ),
then runs tools/import_usc.py headless (UnrealEditor-Cmd -run=pythonscript). Every folder involved is git-ignored: the
pack must not be redistributed. See docs/ThirdPartyArt.md. Close any editor that has the project open first.
#>

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/engine.ps1"
$repo = Split-Path -Parent $PSScriptRoot
$project = Join-Path $repo 'Cpp\SpaceGod.uproject'
$dest = Join-Path $repo 'Cpp\SourceArt\ThirdParty\USC'
$source = Join-Path $dest 'USC_FBX'
$pieces = Join-Path $dest 'Pieces'

if (-not (Test-Path -LiteralPath (Join-Path $source 'Readme.txt'))) {
  if (-not (Test-Path -LiteralPath $Zip)) { throw "USC zip not found: $Zip (pass -Zip <path to usc_fbx.zip>)." }
  if ((Test-Path -LiteralPath $dest) -and (Get-ChildItem -LiteralPath $dest -Force | Select-Object -First 1)) {
    throw "$dest exists but has no USC_FBX/Readme.txt; empty it and run again."
  }
  New-Item -ItemType Directory -Force -Path $dest | Out-Null
  # Data only: nothing from the archive is executed.
  tar -xf $Zip -C $dest
  if (-not (Test-Path -LiteralPath (Join-Path $source 'Readme.txt'))) { throw "The zip did not contain USC_FBX/; is it the _fbx pack?" }
}

if (-not $SkipPrepare) {
  if (-not $Blender) {
    $Blender = Get-ChildItem 'C:\Program Files\Blender Foundation' -Directory -ErrorAction SilentlyContinue |
      Sort-Object Name -Descending | ForEach-Object { Join-Path $_.FullName 'blender.exe' } |
      Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
  }
  if (-not $Blender -or -not (Test-Path -LiteralPath $Blender)) { throw 'Blender not found (pass -Blender <path to blender.exe>, 3.3 or later).' }
  # Blender writes harmless notices to stderr, which Windows PowerShell would otherwise turn into a terminating error.
  $ErrorActionPreference = 'Continue'
  & $Blender -b --factory-startup --python (Join-Path $PSScriptRoot 'usc_prepare.py') -- $source $pieces 2>&1 |
    ForEach-Object { "$_" } | Where-Object { $_ -match '^USC' }
  $ErrorActionPreference = 'Stop'
  if (-not (Get-ChildItem -LiteralPath $pieces -Filter '*.fbx' -ErrorAction SilentlyContinue)) { throw 'The Blender step wrote no pieces.' }
}

$engine = Find-UnrealEngine $EngineVersion
$editor = Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$script = (Join-Path $PSScriptRoot 'import_usc.py') -replace '\\', '/'
$env:USC_PACK = 'usc'
$env:USC_SOURCE = $source
$env:USC_PIECES = $pieces
& $editor $project -run=pythonscript "-script=$script" -unattended -nop4 -nosplash -stdout -FullStdOutLogOutput |
  Where-Object { $_ -match 'USC:|Error' }
if ($LASTEXITCODE) { throw "USC import failed (exit $LASTEXITCODE)." }
