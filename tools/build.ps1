[CmdletBinding()]
param(
  [ValidateSet('Cpp', 'CSharp')][string]$Track = 'Cpp',
  [ValidateSet('Development', 'DebugGame', 'Shipping')][string]$Configuration = 'Development',
  [int]$MaxParallelActions = 4,
  [string]$EngineVersion = '5.8'
)

<#
.SYNOPSIS
Build a track's editor target with UnrealBuildTool (no editor needed). Close any editor holding the project
first: UBT refuses while Live Coding is active. MaxParallelActions is kept low so the build leaves memory free.
#>

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/engine.ps1"
$repo = Split-Path -Parent $PSScriptRoot
$project = Get-ChildItem -LiteralPath (Join-Path $repo $Track) -Filter '*.uproject' | Select-Object -First 1
if (-not $project) { throw "No .uproject in $Track/." }
$build = Join-Path (Find-UnrealEngine $EngineVersion) 'Engine\Build\BatchFiles\Build.bat'
$target = "$($project.BaseName)Editor"
& $build $target Win64 $Configuration "-Project=$($project.FullName)" -WaitMutex "-MaxParallelActions=$MaxParallelActions"
exit $LASTEXITCODE
