[CmdletBinding()]
param(
  [ValidateSet('Development', 'DebugGame', 'Shipping')][string]$Configuration = 'Development',
  [string]$EngineVersion = '5.8'
)

<#
.SYNOPSIS
Build the CSharp track's managed code without the editor: UnrealSharp's own BuildUserSolution UAT command (what
the editor runs on launch and on hot reload) over CSharp/Script/*.csproj, which pulls in CSharp/Core (Unity.SpaceGod's
Core, compiled in place). Output: CSharp/Binaries/Managed/net10.0 plus the UserCode.LoadOrder.json the plugin reads.
tools/build.ps1 -Track CSharp runs this after the C++ build (which builds UnrealSharp's own bindings).
#>

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/engine.ps1"
. "$PSScriptRoot/dotnet-sdk.ps1"
$repo = Split-Path -Parent $PSScriptRoot
$projectDir = Join-Path $repo 'CSharp'
$project = (Get-ChildItem -LiteralPath $projectDir -Filter '*.uproject' | Select-Object -First 1).FullName
$uat = Join-Path (Find-UnrealEngine $EngineVersion) 'Engine\Build\BatchFiles\RunUAT.bat'
Use-UnrealSharpDotNet -PrivateRoot (Join-Path $projectDir 'Intermediate\DotNet')

# Same arguments as UnrealSharp::Build::BuildUserSolution (CSBuildUtilties.cpp) for a plugin inside the project.
$output = Join-Path $projectDir 'Binaries\Managed\net10.0'
& $uat BuildUserSolution "-ScriptDir=$projectDir" "-ScriptsForProject=$project" "-Project=$project" `
  "-OutputPath=$output" "-TargetConfiguration=$Configuration"
exit $LASTEXITCODE
