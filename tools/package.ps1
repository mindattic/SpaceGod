[CmdletBinding()]
param(
  # Kept for the old command lines; the C++ game is the only track in this repo.
  [ValidateSet('Cpp')][string]$Track = 'Cpp',
  [ValidateSet('Development', 'Shipping')][string]$Configuration = 'Development',
  [int]$MaxParallelActions = 4,
  [string]$EngineVersion = '5.8',
  # Skip the editor build that cooking needs (use when tools/build.ps1 has just run).
  [switch]$SkipEditorBuild,
  # After packaging, boot the packaged exe headless (-nullrhi -unattended) and print its LogSpaceGod lines.
  [switch]$Verify,
  # Only boot-check the existing package in Builds/Cpp (implies -Verify; nothing is built).
  [switch]$VerifyOnly
)

<#
.SYNOPSIS
Package the game as a standalone Win64 game: UAT BuildCookRun (build, cook, stage, pak, archive) into
Builds/Cpp/Windows/ (gitignored), with the game exe named SpaceGod.exe.

.DESCRIPTION
Steps:
 1. tools/build.ps1 (Development editor target): the cooker runs the project's editor modules.
 2. RunUAT BuildCookRun -platform=Win64 -clientconfig=<Configuration> -build -cook -stage -pak -archive.
    UBT runs with -WaitMutex (queued behind any other UE build) and -MaxParallelActions (default 4).
 3. Renames the launcher exe (see below).
 4. dotnet publish of the MindAttic.Capture host (Capture/SpaceGod.Capture) into <archive>/Capture/ (US-T72).

Exe name. UE names a game exe after its target, and a target name is a C# class name, so it cannot contain dots:
UAT writes <archive>/<Target>.exe, a small bootstrap launcher, plus the real game in
<archive>/<Target>/Binaries/Win64/<Target>[-Win64-Shipping].exe. The launcher does not derive anything from its own
file name: it carries the relative path of the real exe as an embedded resource (BootstrapPackagedGame), and the real
exe finds its content from its own folder and the compiled-in project name. So the launcher is renamed to
SpaceGod.exe and the inner exe keeps the target name; -Verify boots the renamed launcher to prove it.

.EXAMPLE
  ./tools/package.ps1
  ./tools/package.ps1 -Configuration Shipping -Verify
#>

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/engine.ps1"
$repo = Split-Path -Parent $PSScriptRoot
$project = Get-ChildItem -LiteralPath (Join-Path $repo 'Cpp') -Filter '*.uproject' | Select-Object -First 1
if (-not $project) { throw "No .uproject in Cpp/." }
$engine = Find-UnrealEngine $EngineVersion
$uat = Join-Path $engine 'Engine\Build\BatchFiles\RunUAT.bat'
$target = $project.BaseName                       # the Game target: Source/<Target>.Target.cs
$archiveRoot = Join-Path $repo 'Builds\Cpp'
$archive = Join-Path $archiveRoot 'Windows'       # UAT appends the cooked platform folder
$exeName = 'SpaceGod.exe'
$renamed = Join-Path $archive $exeName
if ($VerifyOnly) {
  if (-not (Test-Path -LiteralPath $renamed)) { throw "No package at ${renamed}: run without -VerifyOnly first." }
  $Verify = $true
} else {

# 1. The editor build the cooker needs.
if (-not $SkipEditorBuild) {
  & (Join-Path $PSScriptRoot 'build.ps1') -Configuration Development -MaxParallelActions $MaxParallelActions -EngineVersion $EngineVersion
  if ($LASTEXITCODE) { throw "Editor build failed ($LASTEXITCODE)." }
}

# 2. Build, cook, stage, pak, archive. A stale archive would leave old files (and an old renamed exe) behind.
if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Recurse -Force }
& $uat BuildCookRun "-project=$($project.FullName)" -noP4 -utf8output -unattended `
  -platform=Win64 "-clientconfig=$Configuration" "-target=$target" `
  -build -cook -stage -pak -archive "-archivedirectory=$archiveRoot" `
  "-ubtargs=-WaitMutex -MaxParallelActions=$MaxParallelActions"
if ($LASTEXITCODE) { throw "BuildCookRun failed ($LASTEXITCODE)." }

$launcher = Join-Path $archive "$target.exe"
if (-not (Test-Path -LiteralPath $launcher)) { throw "BuildCookRun archived no launcher at $launcher." }

# 3. The exe name.
Move-Item -LiteralPath $launcher -Destination $renamed -Force

# 4. The MindAttic.Capture host (US-T72; Capture/README.md): built from Capture/SpaceGod.Capture and the vendored
#    MindAttic.Capture sources, next to the package's Engine/ folder, where the game looks for Capture/capture.exe.
#    Framework-dependent: it runs on the .NET 10 runtime the SDK that builds it installs.
$captureProject = Join-Path $repo 'Capture\SpaceGod.Capture\SpaceGod.Capture.csproj'
$captureOut = Join-Path $archive 'Capture'
& dotnet publish $captureProject -c Release -r win-x64 --self-contained false -o $captureOut -nologo -v q '-p:GenerateDocumentationFile=false'
if ($LASTEXITCODE) { throw "Publishing the capture host failed ($LASTEXITCODE)." }
if (-not (Test-Path -LiteralPath (Join-Path $captureOut 'capture.exe'))) { throw "The capture host publish wrote no capture.exe in $captureOut." }
}

$size = (Get-ChildItem -LiteralPath $archive -Recurse -File | Measure-Object Length -Sum).Sum
Write-Host ("Packaged ({0}): {1} ({2:N1} MB staged)" -f $Configuration, $renamed, ($size / 1MB))

if (-not $Verify) { exit 0 }

# Boot check: headless, unattended, until the map has loaded, then stopped. Shipping builds write no log, so for
# Shipping the check is that the game stays up for 30 s.
$log = Join-Path $archive "$target\Saved\Logs\$target.log"
Remove-Item -LiteralPath $log -ErrorAction SilentlyContinue
$gameArgs = @('-nullrhi', '-nosound', '-unattended', '-nosplash', '-log')
$done = 'LogLoad: Took .* to LoadMap'
$proc = Start-Process -FilePath $renamed -ArgumentList $gameArgs -PassThru -WindowStyle Hidden
$deadline = (Get-Date).AddMinutes(10)
$started = Get-Date
$ok = $false
while ((Get-Date) -lt $deadline) {
  Start-Sleep -Seconds 2
  if ((Test-Path -LiteralPath $log) -and (Select-String -LiteralPath $log -Pattern $done -Quiet)) { $ok = $true; break }
  $inner = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($archive, 'OrdinalIgnoreCase') }
  if ($proc.HasExited -and -not $inner) { break }
  if ($Configuration -eq 'Shipping' -and $inner -and ((Get-Date) - $started).TotalSeconds -ge 30) { $ok = $true; break }
}
Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($archive, 'OrdinalIgnoreCase') } |
  Stop-Process -Force -ErrorAction SilentlyContinue
if (Test-Path -LiteralPath $log) {
  Select-String -LiteralPath $log -Pattern 'LogSpaceGod|Game class is|Fatal error|LogLoad: Took' | ForEach-Object Line
}
if (-not $ok) { throw "The packaged game did not reach '$done' (log: $log)." }
Write-Host "Boot check passed: $renamed"
