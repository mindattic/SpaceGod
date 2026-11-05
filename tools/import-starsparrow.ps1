[CmdletBinding()]
param(
  # The pack's FBX zip (starsparrow_fbx.zip, from the owner's library). Only needed until it has been extracted once.
  [string]$Zip = (Join-Path $env:USERPROFILE 'Downloads\starsparrow\starsparrow_fbx.zip'),
  [string]$EngineVersion = '5.8'
)

<#
.SYNOPSIS
Rebuild /Game/ThirdParty/StarSparrow (Ebal Studios' Star Sparrow fighters) from the pack's FBX zip (US-T109).
Extracts the zip into Cpp/SourceArt/ThirdParty/StarSparrow/ if it is not there yet, then runs
tools/import_starsparrow.py headless (UnrealEditor-Cmd -run=pythonscript). Both folders are git-ignored: the pack
must not be redistributed. See docs/ThirdPartyArt.md. Close any editor that has the project open first.
#>

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/engine.ps1"
$repo = Split-Path -Parent $PSScriptRoot
$project = Join-Path $repo 'Cpp\SpaceGod.uproject'
$dest = Join-Path $repo 'Cpp\SourceArt\ThirdParty\StarSparrow'
$source = Join-Path $dest 'StarSparrow_FBX'

if (-not (Test-Path -LiteralPath (Join-Path $source 'StarSparrowModules.fbx'))) {
  if (-not (Test-Path -LiteralPath $Zip)) { throw "Star Sparrow zip not found: $Zip (pass -Zip <path to starsparrow_fbx.zip>)." }
  if ((Test-Path -LiteralPath $dest) -and (Get-ChildItem -LiteralPath $dest -Force | Select-Object -First 1)) {
    throw "$dest exists but has no StarSparrow_FBX/StarSparrowModules.fbx; empty it and run again."
  }
  New-Item -ItemType Directory -Force -Path $dest | Out-Null
  # Data only: nothing from the archive is executed.
  Expand-Archive -LiteralPath $Zip -DestinationPath $dest
  if (-not (Test-Path -LiteralPath (Join-Path $source 'StarSparrowModules.fbx'))) {
    throw "The zip did not contain StarSparrow_FBX/StarSparrowModules.fbx; is it the _fbx pack?"
  }
}

$engine = Find-UnrealEngine $EngineVersion
$editor = Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$script = (Join-Path $PSScriptRoot 'import_starsparrow.py') -replace '\\', '/'
$env:STARSPARROW_SOURCE = $source
& $editor $project -run=pythonscript "-script=$script" -unattended -nop4 -nosplash -stdout -FullStdOutLogOutput |
  Where-Object { $_ -match 'StarSparrow|Error' }
if ($LASTEXITCODE) { throw "Star Sparrow import failed (exit $LASTEXITCODE)." }
