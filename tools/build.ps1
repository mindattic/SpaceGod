[CmdletBinding()]
param(
  [ValidateSet('Cpp')][string]$Track = 'Cpp',
  [ValidateSet('Development', 'DebugGame', 'Shipping')][string]$Configuration = 'Development',
  [int]$MaxParallelActions = 4,
  [string]$EngineVersion = '5.8'
)

<#
.SYNOPSIS
Build the game's editor target (Cpp/SpaceGod.uproject) with UnrealBuildTool (no editor needed). Close any
editor holding the project first: UBT refuses while Live Coding is active. MaxParallelActions is kept low so the
build leaves memory free; -WaitMutex queues this build behind any other UE build running on the machine.
#>

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/engine.ps1"
$repo = Split-Path -Parent $PSScriptRoot
$project = Get-ChildItem -LiteralPath (Join-Path $repo $Track) -Filter '*.uproject' | Select-Object -First 1
if (-not $project) { throw "No .uproject in $Track/." }
$engine = Find-UnrealEngine $EngineVersion
$build = Join-Path $engine 'Engine\Build\BatchFiles\Build.bat'

$target = "$($project.BaseName)Editor"
# US-T49: UBT calls an object up to date when it is newer than every file it includes. A source or header edited while
# UBT compiles (several agents share this checkout) can be in objects compiled from its *old* text that finish after the
# edit and so look newer than it: they stay in the build, mixing two versions of the header (an ODR violation; it crashed
# the C++ track in cs::Dictionary::BucketOf). So any file under the module sources written during the build gets the
# time now and the build runs again (at most twice more), which recompiles everything that includes it.
$sourceRoots = @((Join-Path $project.DirectoryName 'Source'), (Join-Path $repo 'Shared\Plugins')) | Where-Object { Test-Path -LiteralPath $_ }
for ($pass = 1; ; $pass++) {
  $started = [DateTime]::UtcNow
  & $build $target Win64 $Configuration "-Project=$($project.FullName)" -WaitMutex "-MaxParallelActions=$MaxParallelActions"
  if ($LASTEXITCODE) { exit $LASTEXITCODE }
  $edited = @(Get-ChildItem -LiteralPath $sourceRoots -Recurse -File -Include '*.h', '*.hpp', '*.inl', '*.cpp' |
    Where-Object LastWriteTimeUtc -gt $started)
  if ($edited.Count -eq 0) { break }
  $now = [DateTime]::UtcNow
  foreach ($f in $edited) { $f.LastWriteTimeUtc = $now; Write-Warning "edited during the build: $($f.FullName)" }
  if ($pass -ge 3) { Write-Warning 'sources kept changing during the build; run tools/build.ps1 again once they settle.'; break }
  Write-Host "Building again so every object sees the edited file(s) (pass $($pass + 1))."
}
