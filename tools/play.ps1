[CmdletBinding()]
param(
  [ValidateSet('solo', 'host', 'join', 'smoke', 'shots', 'views', 'viewshot')][string]$Mode = 'solo',
  [string]$Address = '127.0.0.1',
  [ValidateSet('Cpp', 'CSharp')][string]$Track = 'Cpp',
  [string]$EngineVersion = '5.8'
)

<#
.SYNOPSIS
Run Unreal.SpaceGod in game mode from the editor binaries: solo, as the co-op host, or joining a host.
-Track picks the C++ port (Cpp/) or the C# port (CSharp/; build it first with tools/build.ps1 -Track CSharp).
-Mode smoke runs headless (-nullrhi): the UI tour (every screen, clicking the real buttons) and the app loop with a
scripted keyboard and mouse (warp key, camera move, selection, save/load round trip), logged under LogSpaceGod, then
quits; the log is <Track>/Saved/Logs/<project>.log. Both tracks log the same lines, so the two can be compared.
-Mode views / viewshot / shots likewise run the same checks on either track; images are named <track>-*.png
(cpp-*.png for Cpp, csharp-*.png for CSharp).

.EXAMPLE
  ./tools/play.ps1 -Mode host              # listen server on port 7777
  ./tools/play.ps1 -Mode join -Address 192.168.1.20
  ./tools/play.ps1 -Track CSharp
  ./tools/play.ps1 -Track CSharp -Mode smoke
  ./tools/play.ps1 -Track Cpp -Mode smoke      # the C++ track's same smoke (US-T38)
  ./tools/play.ps1 -Track Cpp -Mode shots      # docs/images/cpp-ui-*.png
  ./tools/play.ps1 -Track CSharp -Mode shots   # windowed: the UI's screenshots into docs/images
  ./tools/play.ps1 -Track CSharp -Mode views   # headless: Sol's views (US-T27) built for a frame, counts logged
  ./tools/play.ps1 -Track CSharp -Mode viewshot # windowed: those views on screen, docs/images/csharp-views-sol.png
#>

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/engine.ps1"
$repo = Split-Path -Parent $PSScriptRoot
$project = Get-ChildItem -LiteralPath (Join-Path $repo $Track) -Filter '*.uproject' | Select-Object -First 1
if (-not $project) { throw "No .uproject in $Track/." }
$engine = Find-UnrealEngine $EngineVersion
# Images are named after the track (cpp-ui-hud.png, csharp-ui-hud.png) so the two can be compared side by side.
$prefix = $Track.ToLowerInvariant()

if ($Track -eq 'CSharp') {
  if (-not (Test-Path -LiteralPath (Join-Path $project.DirectoryName 'Binaries\Managed\net10.0\UserCode.LoadOrder.json'))) {
    throw 'The C# code is not built yet: run ./tools/build.ps1 -Track CSharp first.'
  }
  # The editor binaries rebuild stale C# through UnrealSharp's UAT commands, which need a usable .NET SDK.
  . "$PSScriptRoot/dotnet-sdk.ps1"
  Use-UnrealSharpDotNet -PrivateRoot (Join-Path $project.DirectoryName 'Intermediate\DotNet')
}

if ($Mode -eq 'shots') {
  # -Mode shots: a window and the UI's screenshot tour (main menu, HUD, Help, galaxy map, market + cargo, pause menu),
  # each saved as docs/images/<track>-ui-*.png; the game quits when done.
  $editor = Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor.exe'
  $images = Join-Path $repo 'docs\images'
  $p = Start-Process -FilePath $editor -PassThru -Wait -ArgumentList @("`"$($project.FullName)`"", '/Engine/Maps/Entry', '-game',
    '-windowed', '-ResX=1600', '-ResY=900', '-nosound', '-unattended', '-log', '-SpaceGodUiShots', "`"-capturedir=$images`"")
  Get-ChildItem -LiteralPath $images -Filter "$prefix-ui-*.png" -ErrorAction SilentlyContinue | ForEach-Object FullName
  exit $p.ExitCode
}

if ($Mode -eq 'views' -or $Mode -eq 'viewshot') {
  # US-T27: the ported views without the app (ASpaceGameMode.RunViews / ViewsSmoke): Sol's bodies, sky, starfield,
  # interstellar stars, every ship's model, an orbit line; one frame headless (-Mode views) or a window that saves a
  # screenshot (-Mode viewshot).
  if ($Mode -eq 'views') {
    $cmd = Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
    & $cmd $project.FullName '/Engine/Maps/Entry' -game -nullrhi -nosound -unattended -nosplash -stdout -FullStdOutLogOutput -SpaceGodViews |
      Select-String -Pattern 'LogSpaceGod|Error:|Fatal' | ForEach-Object Line
    exit $LASTEXITCODE
  }
  $editor = Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor.exe'
  $shot = Join-Path $repo "docs\images\$prefix-views-sol.png"
  $p = Start-Process -FilePath $editor -PassThru -Wait -ArgumentList @("`"$($project.FullName)`"", '/Engine/Maps/Entry', '-game',
    '-windowed', '-ResX=1600', '-ResY=900', '-nosound', '-unattended', '-log', "`"-SpaceGodShot=$shot`"")
  if (Test-Path -LiteralPath $shot) { $shot }
  exit $p.ExitCode
}

if ($Mode -eq 'smoke') {
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
