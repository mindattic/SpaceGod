[CmdletBinding()]
param(
  [ValidateSet('solo', 'host', 'join')][string]$Mode = 'solo',
  [string]$Address = '127.0.0.1',
  [ValidateSet('Cpp', 'CSharp')][string]$Track = 'Cpp',
  [string]$EngineVersion = '5.8'
)

<#
.SYNOPSIS
Run Unreal.SpaceGod in game mode from the editor binaries: solo, as the co-op host, or joining a host.
-Track picks the C++ port (Cpp/) or the C# port (CSharp/).

.EXAMPLE
  ./tools/play.ps1 -Mode host              # listen server on port 7777
  ./tools/play.ps1 -Mode join -Address 192.168.1.20
  ./tools/play.ps1 -Track CSharp
#>

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/engine.ps1"
$repo = Split-Path -Parent $PSScriptRoot
$project = Get-ChildItem -LiteralPath (Join-Path $repo $Track) -Filter '*.uproject' | Select-Object -First 1
if (-not $project) { throw "No .uproject in $Track/." }
$editor = Join-Path (Find-UnrealEngine $EngineVersion) 'Engine\Binaries\Win64\UnrealEditor.exe'

$map = switch ($Mode) {
  'solo' { '/Engine/Maps/Entry' }
  'host' { '/Engine/Maps/Entry?listen' }
  'join' { $Address }
}
& $editor $project.FullName $map -game -windowed -ResX=1600 -ResY=900 -log
