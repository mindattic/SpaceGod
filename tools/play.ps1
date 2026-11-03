[CmdletBinding()]
param(
  [ValidateSet('solo', 'host', 'join', 'smoke', 'shots', 'views', 'viewshot', 'coop-smoke')][string]$Mode = 'solo',
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
  ./tools/play.ps1 -Track CSharp -Mode host      # co-op (US-T39): a listen server, player 1
  ./tools/play.ps1 -Track CSharp -Mode join -Address 192.168.1.20
  ./tools/play.ps1 -Track CSharp -Mode coop-smoke # headless: a host and a client on localhost, their digests compared
#>

  ./tools/play.ps1 -Track Cpp -Mode coop-smoke    # the C++ track's same co-op smoke (US-T44)
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

# US-T39 co-op: the host's save goes to each client that joins (a few MB) over reliable RPCs; at Unreal's default rates
# (about 100 KB/s a client) that takes a minute, so the host and its clients raise them (no Config/*.ini change).
$coopNet = '-ini:Engine:[/Script/Engine.Player]:ConfiguredInternetSpeed=100000000,[/Script/Engine.Player]:ConfiguredLanSpeed=100000000,' +
  '[/Script/OnlineSubsystemUtils.IpNetDriver]:MaxClientRate=100000000,[/Script/OnlineSubsystemUtils.IpNetDriver]:MaxInternetClientRate=100000000'

if ($Mode -eq 'coop-smoke') {
  # -Mode coop-smoke (either track: US-T39 C#, US-T44 C++): two headless instances on localhost, a listen server (-SpaceGodCoopSmoke=host) and a
  # client (-SpaceGodCoopSmoke=client). The client joins and loads the host's save, presses '.' (warp) and '3' (select),
  # takes a station's seat through the picker, and says bye after 1200 frames (tampering with its own Core once at 700: the host must detect it and resync); both log a digest of the shared state every
  # 60 frames and this script compares them. Logs: <Track>/Saved/Logs/CoopSmokeHost.log and CoopSmokeClient.log.
  $cmd = Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
  $logs = Join-Path $project.DirectoryName 'Saved\Logs'
  New-Item -ItemType Directory -Force -Path $logs | Out-Null
  $hostLog = Join-Path $logs 'CoopSmokeHost.log'
  $clientLog = Join-Path $logs 'CoopSmokeClient.log'
  Remove-Item -LiteralPath $hostLog, $clientLog -ErrorAction SilentlyContinue
  $common = @('-game', '-nullrhi', '-nosound', '-unattended', '-nosplash', $coopNet)
  $hostArgs = @("`"$($project.FullName)`"", '/Engine/Maps/Entry?listen') + $common + @('-SpaceGodCoopSmoke=host', "`"-abslog=$hostLog`"")
  # Not -WindowStyle (ShellExecute): CreateProcess, so the instances inherit the PATH with the private .NET SDK on it.
  $hostP = Start-Process -FilePath $cmd -PassThru -NoNewWindow -ArgumentList $hostArgs -RedirectStandardOutput "$hostLog.out"
  # The client starts once the host's app is up (two UnrealSharp startups at once contend for the managed build).
  $deadline = (Get-Date).AddMinutes(4)
  while (-not ((Test-Path -LiteralPath $hostLog) -and (Select-String -LiteralPath $hostLog -Pattern 'UnitySpaceApp up in' -Quiet))) {
    if ($hostP.HasExited -or (Get-Date) -gt $deadline) { throw "The co-op host did not come up (see $hostLog)." }
    Start-Sleep -Seconds 2
  }
  $clientArgs = @("`"$($project.FullName)`"", '127.0.0.1') + $common + @('-SpaceGodCoopSmoke=client', "`"-abslog=$clientLog`"")
  $clientP = Start-Process -FilePath $cmd -PassThru -NoNewWindow -ArgumentList $clientArgs -RedirectStandardOutput "$clientLog.out"
  $deadline = (Get-Date).AddMinutes(8)
  while (-not ($hostP.HasExited -and $clientP.HasExited)) {
    if ((Get-Date) -gt $deadline) {
      foreach ($p in @($hostP, $clientP)) { if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force } }
      Write-Warning 'co-op smoke: timed out; both instances stopped'
      break
    }
    Start-Sleep -Seconds 2
  }
  $digests = @{ host = @{}; client = @{} }
  foreach ($side in @(@{ Name = 'host'; Log = $hostLog }, @{ Name = 'client'; Log = $clientLog })) {
    if (-not (Test-Path -LiteralPath $side.Log)) { Write-Warning "no $($side.Name) log"; continue }
    foreach ($line in (Get-Content -LiteralPath $side.Log | Where-Object { $_ -match 'SpaceGodCoop:|Error:|Fatal' })) {
      "[$($side.Name)] " + ($line -replace '^\[[^\]]*\]\[\s*\d+\]', '')
      if ($line -match 'digest frame (\d+) = ([0-9A-F]{16}) \(') { $digests[$side.Name][[long]$Matches[1]] = $Matches[2] }
    }
  }
  $frames = @($digests.client.Keys | Where-Object { $digests.host.ContainsKey($_) } | Sort-Object)
  $same = @($frames | Where-Object { $digests.host[$_] -eq $digests.client[$_] })
  $range = if ($frames.Count -gt 0) { " (frames $($frames[0])..$($frames[-1]))" } else { '' }
  "co-op smoke: $($frames.Count) frames with both digests, $($same.Count) identical$range"
  # The client tampers with its own Core once (CoopSmoke, frame 700 after the join): exactly that one digest must differ,
  # the host must resync (sync #2), and every digest after it must match again.
  $differ = @($frames | Where-Object { $digests.host[$_] -ne $digests.client[$_] })
  $resynced = Select-String -LiteralPath $hostLog -Pattern 'is in sync #2' -Quiet
  $after = if ($differ.Count -eq 1) { @($frames | Where-Object { $_ -gt $differ[0] }) } else { @() }
  "co-op smoke: injected desync detected at frame $($differ -join ', '); resynced: $resynced; $($after.Count) digests after it, all identical: $(@($after | Where-Object { $digests.host[$_] -ne $digests.client[$_] }).Count -eq 0)"
  if ($frames.Count -eq 0 -or $differ.Count -ne 1 -or -not $resynced -or $after.Count -lt 2 -or
    @($after | Where-Object { $digests.host[$_] -ne $digests.client[$_] }).Count -gt 0) { exit 1 }
  exit 0
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
if ($Mode -eq 'solo') { & $editor $project.FullName $map -game -windowed -ResX=1600 -ResY=900 -log }
else { & $editor $project.FullName $map -game -windowed -ResX=1600 -ResY=900 -log $coopNet } # US-T39: host / join
