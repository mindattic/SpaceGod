[CmdletBinding()]
param(
  [ValidateSet('solo', 'host', 'join', 'smoke', 'shots')][string]$Mode = 'solo',
  [string]$Address = '127.0.0.1',
  [ValidateSet('Cpp', 'CSharp')][string]$Track = 'Cpp',
  [string]$EngineVersion = '5.8'
)

<#
.SYNOPSIS
Run Unreal.SpaceGod in game mode from the editor binaries: solo, as the co-op host, or joining a host.
-Track picks the C++ port (Cpp/) or the C# port (CSharp/; build it first with tools/build.ps1 -Track CSharp).
-Mode smoke (CSharp) runs headless (-nullrhi): the C# game mode starts Unity.SpaceGod's Core, logs a few ticks
under LogSpaceGod and quits; the log is CSharp/Saved/Logs/UnrealSpaceGodCSharp.log.

.EXAMPLE
  ./tools/play.ps1 -Mode host              # listen server on port 7777
  ./tools/play.ps1 -Mode join -Address 192.168.1.20
  ./tools/play.ps1 -Track CSharp
  ./tools/play.ps1 -Track CSharp -Mode smoke
  ./tools/play.ps1 -Track CSharp -Mode shots   # windowed: the UI's screenshots into docs/images
#>

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/engine.ps1"
$repo = Split-Path -Parent $PSScriptRoot
$project = Get-ChildItem -LiteralPath (Join-Path $repo $Track) -Filter '*.uproject' | Select-Object -First 1
if (-not $project) { throw "No .uproject in $Track/." }
$engine = Find-UnrealEngine $EngineVersion

if ($Track -eq 'CSharp') {
  if (-not (Test-Path -LiteralPath (Join-Path $project.DirectoryName 'Binaries\Managed\net10.0\UserCode.LoadOrder.json'))) {
    throw 'The C# code is not built yet: run ./tools/build.ps1 -Track CSharp first.'
  }
  # The editor binaries rebuild stale C# through UnrealSharp's UAT commands, which need a usable .NET SDK.
  . "$PSScriptRoot/dotnet-sdk.ps1"
  Use-UnrealSharpDotNet -PrivateRoot (Join-Path $project.DirectoryName 'Intermediate\DotNet')
}

if ($Mode -eq 'shots') {
  # -Mode shots (CSharp): a window and the UI's screenshot tour (UiSmoke.Shots: main menu, HUD, Help, galaxy map,
  # market + cargo, pause menu), each saved as docs/images/csharp-ui-*.png; the game quits when done.
  if ($Track -ne 'CSharp') { throw '-Mode shots is the C# track''s UI screenshot tour: use -Track CSharp.' }
  $editor = Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor.exe'
  $images = Join-Path $repo 'docs\images'
  $p = Start-Process -FilePath $editor -PassThru -Wait -ArgumentList @("`"$($project.FullName)`"", '/Engine/Maps/Entry', '-game',
    '-windowed', '-ResX=1600', '-ResY=900', '-nosound', '-unattended', '-log', '-SpaceGodUiShots', "`"-capturedir=$images`"")
  Get-ChildItem -LiteralPath $images -Filter 'csharp-ui-*.png' -ErrorAction SilentlyContinue | ForEach-Object FullName
  exit $p.ExitCode
}

if ($Mode -eq 'smoke') {
  if ($Track -ne 'CSharp') { throw '-Mode smoke is the C# track''s headless check: use -Track CSharp.' }
  $cmd = Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
  & $cmd $project.FullName '/Engine/Maps/Entry' -game -nullrhi -nosound -unattended -nosplash -stdout -FullStdOutLogOutput -SpaceGodSmoke |
    Select-String -Pattern 'LogSpaceGod|Error:|Fatal' | ForEach-Object Line
  exit $LASTEXITCODE
}

$editor = Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor.exe'
$map = switch ($Mode) {
  'solo' { '/Engine/Maps/Entry' }
  'host' { '/Engine/Maps/Entry?listen' }
  'join' { $Address }
}
& $editor $project.FullName $map -game -windowed -ResX=1600 -ResY=900 -log
