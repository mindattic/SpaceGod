# Dot-source this: . "$PSScriptRoot/engine.ps1"; $ue = Find-UnrealEngine '5.8'
# Finds an installed Unreal Engine root. The launcher does not always write the HKLM key (a 5.8 install to
# D:\Games\UE_5.8 left none), so this checks, in order: $env:UE_ROOT, the registry, the launcher's install
# list, then the usual folders.

function Find-UnrealEngine {
  param([string]$Version = '5.8')

  $candidates = @()
  if ($env:UE_ROOT) { $candidates += $env:UE_ROOT }

  $key = "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$Version"
  $reg = (Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue).InstalledDirectory
  if ($reg) { $candidates += $reg }

  $dat = 'C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat'
  if (Test-Path -LiteralPath $dat) {
    try {
      (Get-Content -LiteralPath $dat -Raw | ConvertFrom-Json).InstallationList |
        Where-Object { $_.AppName -eq "UE_$Version" } |
        ForEach-Object { $candidates += $_.InstallLocation }
    } catch {}
  }

  foreach ($drive in (Get-PSDrive -PSProvider FileSystem).Root) {
    foreach ($dir in @('Games', 'Games\Epic Games', 'Epic Games', 'Program Files\Epic Games')) {
      $candidates += (Join-Path $drive "$dir\UE_$Version")
    }
  }

  foreach ($root in $candidates) {
    if ($root -and (Test-Path -LiteralPath (Join-Path $root 'Engine\Binaries\Win64\UnrealEditor.exe'))) {
      return (Resolve-Path -LiteralPath $root).Path
    }
  }
  throw "Unreal Engine $Version was not found. Install it from the Epic Games Launcher, or set UE_ROOT to its folder."
}
