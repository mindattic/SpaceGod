# Dot-source this: . "$PSScriptRoot/dotnet-sdk.ps1"; Use-UnrealSharpDotNet -PrivateRoot <dir>
#
# Makes sure the dotnet that UnrealSharp's build scripts pick can build UnrealSharp:
#  * UnrealSharp (since 2026-09-16) compiles its source generators against Roslyn 5.6, which only the .NET SDK
#    10.0.3xx feature band and later carry (10.0.2xx has Roslyn 5.3: error CS9057 then CS0117 on Bind_FMsg).
#  * Its scripts run `dotnet.exe <sdk>\dotnet.dll` with <sdk> the highest-NAMED 10.x folder under <dotnet>\sdk,
#    without checking that folder is a complete SDK; a failed uninstall can leave an empty one behind.
#  * They take UE's bundled dotnet never, then the first other dotnet.exe on PATH (Build.bat puts the bundled one
#    first and resets DOTNET_ROOT, so PATH is the one lever).
# If the machine's dotnet passes both checks nothing changes. Otherwise the pinned SDK below is installed privately
# (Microsoft's dotnet-install.ps1, no admin, nothing registered system-wide) into PrivateRoot and put on PATH.

$UnrealSharpSdkVersion = '10.0.403'
$UnrealSharpMinimumSdk = [version]'10.0.300'

function Get-DotNetSdkProblem {
  param([string]$Root)
  if (-not $Root -or -not (Test-Path -LiteralPath (Join-Path $Root 'dotnet.exe'))) { return 'no dotnet.exe' }
  $sdks = @(Get-ChildItem -LiteralPath (Join-Path $Root 'sdk') -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -match '^10\.' } |
    ForEach-Object { [pscustomobject]@{ Dir = $_; Version = [version](($_.Name -split '-')[0]) } } |
    Sort-Object Version -Descending)
  if (-not $sdks) { return 'no .NET 10 SDK' }
  $top = $sdks[0]
  if (-not (Test-Path -LiteralPath (Join-Path $top.Dir.FullName 'dotnet.dll'))) { return "sdk\$($top.Dir.Name) is an incomplete install" }
  if ($top.Version -lt $UnrealSharpMinimumSdk) { return "SDK $($top.Dir.Name) is older than $UnrealSharpMinimumSdk (Roslyn 5.6)" }
  return $null
}

function Use-UnrealSharpDotNet {
  param([Parameter(Mandatory)][string]$PrivateRoot)

  $system = (Get-Command dotnet.exe -All -ErrorAction SilentlyContinue |
    Where-Object { $_.Source -notmatch '[\\/]Binaries[\\/]ThirdParty[\\/]DotNet[\\/]' } | Select-Object -First 1).Source
  if (-not $system) { $system = Join-Path $env:ProgramFiles 'dotnet\dotnet.exe' }
  $problem = Get-DotNetSdkProblem (Split-Path -Parent $system)
  if (-not $problem) { return }

  if (Get-DotNetSdkProblem $PrivateRoot) {
    Write-Host "UnrealSharp cannot build with the machine's dotnet ($problem): installing .NET SDK $UnrealSharpSdkVersion into $PrivateRoot"
    $installer = Join-Path ([IO.Path]::GetTempPath()) "dotnet-install-$PID\dotnet-install.ps1"
    New-Item -ItemType Directory -Force (Split-Path -Parent $installer) | Out-Null
    Invoke-WebRequest -UseBasicParsing 'https://dot.net/v1/dotnet-install.ps1' -OutFile $installer
    & powershell -NoProfile -ExecutionPolicy Bypass -File $installer -Version $UnrealSharpSdkVersion -InstallDir $PrivateRoot -NoPath
    if ($LASTEXITCODE) { throw "dotnet-install failed ($LASTEXITCODE)." }
    Remove-Item -Recurse -Force (Split-Path -Parent $installer) -ErrorAction SilentlyContinue
    if ($left = Get-DotNetSdkProblem $PrivateRoot) { throw "The private .NET SDK at $PrivateRoot is unusable: $left" }
  }
  Write-Host "Using the private .NET SDK at $PrivateRoot for UnrealSharp (machine dotnet: $problem)."
  $env:PATH = "$PrivateRoot;$env:PATH"
}
