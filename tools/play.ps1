[CmdletBinding()]
param(
  [ValidateSet('solo', 'host', 'join')][string]$Mode = 'solo',
  [string]$Address = '127.0.0.1',
  [string]$EngineVersion = '5.8'
)

<#
.SYNOPSIS
Run Unreal.SpaceGod in game mode from the editor binaries: solo, as the co-op host, or joining a host.

.EXAMPLE
  ./tools/play.ps1 -Mode host              # listen server on port 7777
  ./tools/play.ps1 -Mode join -Address 192.168.1.20
#>

$ErrorActionPreference = 'Stop'
$project = Join-Path (Split-Path -Parent $PSScriptRoot) 'UnrealSpaceGod.uproject'

$key = "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$EngineVersion"
$root = (Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue).InstalledDirectory
if (-not $root) { throw "Unreal Engine $EngineVersion is not installed (no $key). Install it from the Epic Games Launcher." }
$editor = Join-Path $root 'Engine\Binaries\Win64\UnrealEditor.exe'
if (-not (Test-Path -LiteralPath $editor)) { throw "$editor not found." }

$map = switch ($Mode) {
  'solo' { '/Engine/Maps/Entry' }
  'host' { '/Engine/Maps/Entry?listen' }
  'join' { $Address }
}
& $editor $project $map -game -windowed -ResX=1600 -ResY=900 -log
