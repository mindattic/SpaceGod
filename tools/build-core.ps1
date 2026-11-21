[CmdletBinding()]
param(
  # Top-level folders of Cpp/Source/SpaceGodCore to build (e.g. -Only Cs,Shared,Combat; '.' = the loose files at
  # the module root). Default: all of them. Cs is always built: everything else depends on it.
  [string[]]$Only,
  # Most cl.exe processes alive at once (memory is shared with other builds on this machine).
  [int]$MaxParallel = 4,
  [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
  # Recompile everything instead of only sources newer than their .obj (or older than a changed header).
  [switch]$Rebuild,
  # Build CsParity.exe (and SectionCollidersTests.exe) but don't run them.
  [switch]$SkipParity
)

<#
.SYNOPSIS
Standalone build of SpaceGodCore (the C++ port of Unity.SpaceGod's Core) with MSVC, no Unreal involved.

.DESCRIPTION
Compiles every .cpp under Cpp/Source/SpaceGodCore except Module/ (SpaceGodCoreModule.cpp, the one file with
Unreal headers) and Tests/, archives them into SpaceGodCore.lib, then builds Tests/CsParity (the check that Cs/Cs.h
reproduces .NET: formatting, Math.Round, System.Random, List.Sort, Parse) and runs it against the .NET output in
Tests/CsParity/expected.txt. Output goes to Cpp/Intermediate/CoreStandalone/<Configuration> (gitignored).
Flags: C++20, /EHsc /GR /W4 /permissive- /fp:precise /utf-8, SPACEGOD_STANDALONE defined.
Finds Visual Studio 2022 (or a newer one with the C++ x64 tools) through vswhere.

.EXAMPLE
tools/build-core.ps1                       # everything
tools/build-core.ps1 -Only Shared          # Cs + Shared
tools/build-core.ps1 -Only Combat,Economy -MaxParallel 2
#>

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$core = Join-Path $repo 'Cpp\Source\SpaceGodCore'
$out = Join-Path $repo "Cpp\Intermediate\CoreStandalone\$Configuration"
if ($MaxParallel -lt 1) { $MaxParallel = 1 }

# ---------------------------------------------------------------- MSVC environment
function Import-VcVars {
  if ($env:VCToolsInstallDir -and (Get-Command cl.exe -ErrorAction SilentlyContinue)) { return }
  $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
  if (-not (Test-Path -LiteralPath $vswhere)) { throw 'vswhere.exe not found: install Visual Studio 2022 with "Desktop development with C++".' }
  $req = 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64'
  $vs = & $vswhere -products * -version '[17.0,18.0)' -requires $req -latest -property installationPath
  if (-not $vs) { $vs = & $vswhere -products * -requires $req -latest -property installationPath }
  if (-not $vs) { throw 'No Visual Studio with the C++ x64 build tools found (vswhere).' }
  $bat = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
  $lines = & cmd.exe /c "`"$bat`" >nul 2>&1 && set"
  foreach ($line in $lines) {
    $i = $line.IndexOf('=')
    if ($i -gt 0) { [Environment]::SetEnvironmentVariable($line.Substring(0, $i), $line.Substring($i + 1), 'Process') }
  }
  if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw "vcvars64.bat in $vs did not put cl.exe on PATH." }
  Write-Host "MSVC: $vs"
}
Import-VcVars

# ---------------------------------------------------------------- sources
# Module/ holds SpaceGodCoreModule.cpp, the one file with Unreal headers; Tests/ builds separately below.
$dirs = Get-ChildItem -LiteralPath $core -Directory | Where-Object { $_.Name -notin @('Tests', 'Module') } | ForEach-Object Name
if ($Only) {
  $want = @('Cs') + ($Only | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
  foreach ($w in $want) { if ($w -ne '.' -and $dirs -notcontains $w) { throw "No folder '$w' under $core." } }
  $dirs = $dirs | Where-Object { $want -contains $_ }
}
$sources = @()
foreach ($d in $dirs) { $sources += Get-ChildItem -LiteralPath (Join-Path $core $d) -Recurse -Filter '*.cpp' -File }
# Loose .cpp files at the module root (Core's top-level .cs files), with a full build or -Only '.'.
if (-not $Only -or $want -contains '.') { $sources += Get-ChildItem -LiteralPath $core -Filter '*.cpp' -File | Where-Object { $_.Name -ne 'SpaceGodCoreModule.cpp' } }

$started = [DateTime]::UtcNow
$headerTime = (Get-ChildItem -LiteralPath $core -Recurse -File -Include '*.h', '*.inl' |
  Measure-Object -Property LastWriteTimeUtc -Maximum).Maximum

$common = @('/nologo', '/c', '/std:c++20', '/EHsc', '/GR', '/W4', '/permissive-', '/utf-8', '/fp:precise',
  '/Zc:__cplusplus', '/Zc:preprocessor', '/bigobj', '/DSPACEGOD_STANDALONE', '/D_CRT_SECURE_NO_WARNINGS', "/I`"$core`"")
# Release: whole-program optimisation (/GL here, /LTCG on lib and link: Core's small inline-looking members such as DVec3
# operators live in .cpp files, so only link-time code generation can inline them across files). /arch:AVX: VEX-encoded SSE, as RyuJIT
# emits on this hardware (AVX has no FMA, so nothing can be contracted). /fp:precise stays (bit parity).
if ($Configuration -eq 'Release') { $common += @('/O2', '/GL', '/Gw', '/arch:AVX', '/MD', '/DNDEBUG', '/Zi') } else { $common += @('/Od', '/MDd', '/Zi', '/RTC1') }

# SaveGame's raw DEFLATE (docs/PERSISTENCE.md, section 7): the engine's own zlib 1.3 when UE 5.8 is installed (its
# headers, and its prebuilt /MD zlibstatic.lib merged into SpaceGodCore.lib, so whatever links the lib has it). Release
# only (the prebuilt lib is not /MDd); without it SaveGame::Deflate/Inflate throw NotSupportedException.
$zlibLib = $null
if ($Configuration -eq 'Release') {
  try {
    . (Join-Path $PSScriptRoot 'engine.ps1')
    $zlib = Join-Path (Find-UnrealEngine '5.8') 'Engine\Source\ThirdParty\zlib\1.3'
    $zlibCandidate = Join-Path $zlib 'lib\Win64\Release\zlibstatic.lib'
    if ((Test-Path -LiteralPath (Join-Path $zlib 'include\zlib.h')) -and (Test-Path -LiteralPath $zlibCandidate)) {
      $common += @('/DSPACEGOD_WITH_ZLIB', "/I`"$(Join-Path $zlib 'include')`"")
      $zlibLib = $zlibCandidate
      Write-Host "zlib:  $zlib"
    }
  } catch { Write-Host 'zlib:  none (no UE 5.8): saves throw NotSupportedException' }
}
# Objects built with other flags (zlib found or gone) are stale.
New-Item -ItemType Directory -Force $out | Out-Null
$flagsStamp = Join-Path $out 'flags.txt'
$flagsNow = $common -join ' '
if (-not (Test-Path -LiteralPath $flagsStamp) -or (Get-Content -LiteralPath $flagsStamp -Raw).Trim() -ne $flagsNow) { $Rebuild = $true }

# ---------------------------------------------------------------- compile (at most $MaxParallel cl.exe at once)
function Invoke-Compile([object[]]$files, [string]$objRoot) {
  $jobs = @()
  foreach ($f in $files) {
    $rel = $f.FullName.Substring($core.Length).TrimStart('\')
    $obj = Join-Path $objRoot ([IO.Path]::ChangeExtension($rel, '.obj'))
    $stale = $Rebuild -or -not (Test-Path -LiteralPath $obj)
    if (-not $stale) {
      $t = (Get-Item -LiteralPath $obj).LastWriteTimeUtc
      $stale = ($f.LastWriteTimeUtc -gt $t) -or ($headerTime -gt $t)
    }
    $jobs += [pscustomobject]@{ Src = $f.FullName; Rel = $rel; Obj = $obj; Stale = $stale }
  }
  $todo = @($jobs | Where-Object Stale)
  Write-Host ("{0} source(s), {1} to compile, {2} at a time" -f $jobs.Count, $todo.Count, $MaxParallel)
  $running = @(); $failed = @(); $queue = [Collections.Queue]::new(); $todo | ForEach-Object { $queue.Enqueue($_) }
  while ($queue.Count -gt 0 -or $running.Count -gt 0) {
    while ($queue.Count -gt 0 -and $running.Count -lt $MaxParallel) {
      $j = $queue.Dequeue()
      New-Item -ItemType Directory -Force (Split-Path -Parent $j.Obj) | Out-Null
      $pdb = [IO.Path]::ChangeExtension($j.Obj, '.pdb')
      $psi = [Diagnostics.ProcessStartInfo]::new('cl.exe')
      $psi.Arguments = (($common + @("/Fo`"$($j.Obj)`"", "/Fd`"$pdb`"", "`"$($j.Src)`"")) -join ' ')
      $psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
      $psi.WorkingDirectory = $core
      $p = [Diagnostics.Process]::Start($psi)
      $running += [pscustomobject]@{ Job = $j; Proc = $p; StdOut = $p.StandardOutput.ReadToEndAsync(); StdErr = $p.StandardError.ReadToEndAsync() }
    }
    Start-Sleep -Milliseconds 100
    $still = @()
    foreach ($r in $running) {
      if ($r.Proc.HasExited) {
        $text = ($r.StdOut.Result + $r.StdErr.Result) -split "`r?`n" | Where-Object { $_ -and ($_ -ne (Split-Path -Leaf $r.Job.Src)) }
        if ($r.Proc.ExitCode -ne 0) {
          $failed += $r.Job.Rel
          Write-Host "FAILED  $($r.Job.Rel)" -ForegroundColor Red
          if (Test-Path -LiteralPath $r.Job.Obj) { Remove-Item -LiteralPath $r.Job.Obj -Force }
        } else {
          Write-Host "ok      $($r.Job.Rel)"
        }
        $text | ForEach-Object { Write-Host "  $_" }
      } else { $still += $r }
    }
    $running = $still
  }
  return , @{ Objs = @($jobs | ForEach-Object Obj); Failed = $failed }
}

$objRoot = Join-Path $out 'obj'
$result = Invoke-Compile $sources $objRoot
if ($result.Failed.Count -gt 0) {
  Write-Host "`n$($result.Failed.Count) file(s) failed:" -ForegroundColor Red
  $result.Failed | ForEach-Object { Write-Host "  $_" }
  exit 1
}
# US-T49: a file edited while cl.exe ran can be in objects compiled from its old text that are newer than it, so they
# would never be recompiled (mixed headers: an ODR violation). Give such files the time now and build once more.
$edited = @(Get-ChildItem -LiteralPath $core -Recurse -File -Include '*.h', '*.inl', '*.cpp' | Where-Object LastWriteTimeUtc -gt $started)
if ($edited.Count -gt 0) {
  $now = [DateTime]::UtcNow
  foreach ($f in $edited) { $f.LastWriteTimeUtc = $now; Write-Warning "edited during the build: $($f.FullName)" }
  if (-not $env:SPACEGOD_CORE_AGAIN) {
    $env:SPACEGOD_CORE_AGAIN = '1'
    Write-Host 'Building again so every object sees the edited file(s).'
    & $PSCommandPath @PSBoundParameters
    $code = $LASTEXITCODE
    Remove-Item Env:SPACEGOD_CORE_AGAIN
    exit $code
  }
}

# ---------------------------------------------------------------- archive
$lib = Join-Path $out 'SpaceGodCore.lib'
$rsp = Join-Path $out 'lib.rsp'
($result.Objs | ForEach-Object { "`"$_`"" }) -join "`n" | Set-Content -LiteralPath $rsp -Encoding ascii
if ($zlibLib) { "`"$zlibLib`"" | Add-Content -LiteralPath $rsp -Encoding ascii }
[string[]]$ltcg = if ($Configuration -eq 'Release') { @('/LTCG') } else { @() }
& lib.exe /nologo @ltcg "/OUT:$lib" "@$rsp"
if ($LASTEXITCODE -ne 0) { throw 'lib.exe failed.' }
Set-Content -LiteralPath $flagsStamp -Value $flagsNow -Encoding ascii
Write-Host "lib     $lib"

# ---------------------------------------------------------------- CsParity
$parityDir = Join-Path $core 'Tests\CsParity'
$parityObjRoot = Join-Path $out 'testobj'
$pr = Invoke-Compile @(Get-Item -LiteralPath (Join-Path $parityDir 'CsParity.cpp')) $parityObjRoot
if ($pr.Failed.Count -gt 0) { exit 1 }
$exe = Join-Path $out 'CsParity.exe'
[string[]]$linkOpt = if ($Configuration -eq 'Release') { @('/LTCG', '/OPT:REF', '/OPT:ICF', "/CGTHREADS:$([Math]::Min($MaxParallel, 8))") } else { @() }
& link.exe /nologo /DEBUG @linkOpt "/OUT:$exe" $pr.Objs $lib
if ($LASTEXITCODE -ne 0) { throw 'link.exe failed (CsParity).' }
Write-Host "exe     $exe"

if (-not $SkipParity) {
  & $exe (Join-Path $parityDir 'expected.txt')
  if ($LASTEXITCODE -ne 0) { Write-Host 'CsParity: the C++ layer differs from .NET (see above).' -ForegroundColor Red; exit $LASTEXITCODE }
}

# ---------------------------------------------------------------- SectionCollidersTests (US-T177)
$scDir = Join-Path $core 'Tests\SectionColliders'
$sc = Invoke-Compile @(Get-Item -LiteralPath (Join-Path $scDir 'SectionCollidersTests.cpp')) $parityObjRoot
if ($sc.Failed.Count -gt 0) { exit 1 }
$scExe = Join-Path $out 'SectionCollidersTests.exe'
& link.exe /nologo /DEBUG @linkOpt "/OUT:$scExe" $sc.Objs $lib
if ($LASTEXITCODE -ne 0) { throw 'link.exe failed (SectionCollidersTests).' }
Write-Host "exe     $scExe"
if (-not $SkipParity) {
  & $scExe
  if ($LASTEXITCODE -ne 0) { Write-Host 'SectionCollidersTests: failures (see above).' -ForegroundColor Red; exit $LASTEXITCODE }
}

# ---------------------------------------------------------------- LoreLibraryTests (US-T236)
$loreDir = Join-Path $core 'Tests\Lore'
$lt = Invoke-Compile @(Get-Item -LiteralPath (Join-Path $loreDir 'LoreLibraryTests.cpp')) $parityObjRoot
if ($lt.Failed.Count -gt 0) { exit 1 }
$ltExe = Join-Path $out 'LoreLibraryTests.exe'
& link.exe /nologo /DEBUG @linkOpt "/OUT:$ltExe" $lt.Objs $lib
if ($LASTEXITCODE -ne 0) { throw 'link.exe failed (LoreLibraryTests).' }
Write-Host "exe     $ltExe"
if (-not $SkipParity) {
  & $ltExe (Join-Path $loreDir 'fixture')
  if ($LASTEXITCODE -ne 0) { Write-Host 'LoreLibraryTests: failures (see above).' -ForegroundColor Red; exit $LASTEXITCODE }
}

# ---------------------------------------------------------------- EncounterTests (US-T239)
$et = Invoke-Compile @(Get-Item -LiteralPath (Join-Path $loreDir 'EncounterTests.cpp')) $parityObjRoot
if ($et.Failed.Count -gt 0) { exit 1 }
$etExe = Join-Path $out 'EncounterTests.exe'
& link.exe /nologo /DEBUG @linkOpt "/OUT:$etExe" $et.Objs $lib
if ($LASTEXITCODE -ne 0) { throw 'link.exe failed (EncounterTests).' }
Write-Host "exe     $etExe"
if (-not $SkipParity) {
  & $etExe (Join-Path $loreDir 'fixture') (Join-Path $repo 'Data\Prose\prose.config.json')
  if ($LASTEXITCODE -ne 0) { Write-Host 'EncounterTests: failures (see above).' -ForegroundColor Red; exit $LASTEXITCODE }
}

# ---------------------------------------------------------------- HoldRewindCurveTests (US-T410)
$hrDir = Join-Path $core 'Tests\HoldRewind'
$hr = Invoke-Compile @(Get-Item -LiteralPath (Join-Path $hrDir 'HoldRewindCurveTests.cpp')) $parityObjRoot
if ($hr.Failed.Count -gt 0) { exit 1 }
$hrExe = Join-Path $out 'HoldRewindCurveTests.exe'
& link.exe /nologo /DEBUG @linkOpt "/OUT:$hrExe" $hr.Objs $lib
if ($LASTEXITCODE -ne 0) { throw 'link.exe failed (HoldRewindCurveTests).' }
Write-Host "exe     $hrExe"
if (-not $SkipParity) {
  & $hrExe
  if ($LASTEXITCODE -ne 0) { Write-Host 'HoldRewindCurveTests: failures (see above).' -ForegroundColor Red; exit $LASTEXITCODE }
}

# ---------------------------------------------------------------- BindingCoreTests (US-T437)
# The input bindings' pure part (Cpp/Source/SpaceGod/Input/BindingCore.h, header-only, no engine): text round trips,
# the profile text of before US-T437, and the trigger modes' timing. Needs nothing of SpaceGodCore.lib.
$bcDir = Join-Path $core 'Tests\Bindings'
# Its header lives outside SpaceGodCore, so the header stamp above does not see it: an edited header rebuilds it here.
$bcHeader = Join-Path $repo 'Cpp\Source\SpaceGod\Input\BindingCore.h'
$bcObj = Join-Path $parityObjRoot 'Tests\Bindings\BindingCoreTests.obj'
if ((Test-Path -LiteralPath $bcObj) -and (Get-Item -LiteralPath $bcHeader).LastWriteTimeUtc -gt (Get-Item -LiteralPath $bcObj).LastWriteTimeUtc) { Remove-Item -LiteralPath $bcObj -Force }
$bc = Invoke-Compile @(Get-Item -LiteralPath (Join-Path $bcDir 'BindingCoreTests.cpp')) $parityObjRoot
if ($bc.Failed.Count -gt 0) { exit 1 }
$bcExe = Join-Path $out 'BindingCoreTests.exe'
& link.exe /nologo /DEBUG @linkOpt "/OUT:$bcExe" $bc.Objs
if ($LASTEXITCODE -ne 0) { throw 'link.exe failed (BindingCoreTests).' }
Write-Host "exe     $bcExe"
if (-not $SkipParity) {
  & $bcExe
  if ($LASTEXITCODE -ne 0) { Write-Host 'BindingCoreTests: failures (see above).' -ForegroundColor Red; exit $LASTEXITCODE }
}

# ---------------------------------------------------------------- HudDockTests (US-T438)
# The HUD drawers' layout math (Cpp/Source/SpaceGod/UI/HudDock.h, header-only, no engine): edge stacks, splitting, snapping,
# keeping windows on screen, edge tabs. Needs nothing of SpaceGodCore.lib.
$hdDir = Join-Path $core 'Tests\HudDock'
$hdHeader = Join-Path $repo 'Cpp\Source\SpaceGod\UI\HudDock.h'
$hdObj = Join-Path $parityObjRoot 'Tests\HudDock\HudDockTests.obj'
if ((Test-Path -LiteralPath $hdObj) -and (Get-Item -LiteralPath $hdHeader).LastWriteTimeUtc -gt (Get-Item -LiteralPath $hdObj).LastWriteTimeUtc) { Remove-Item -LiteralPath $hdObj -Force }
$hd = Invoke-Compile @(Get-Item -LiteralPath (Join-Path $hdDir 'HudDockTests.cpp')) $parityObjRoot
if ($hd.Failed.Count -gt 0) { exit 1 }
$hdExe = Join-Path $out 'HudDockTests.exe'
& link.exe /nologo /DEBUG @linkOpt "/OUT:$hdExe" $hd.Objs
if ($LASTEXITCODE -ne 0) { throw 'link.exe failed (HudDockTests).' }
Write-Host "exe     $hdExe"
if (-not $SkipParity) {
  & $hdExe
  if ($LASTEXITCODE -ne 0) { Write-Host 'HudDockTests: failures (see above).' -ForegroundColor Red; exit $LASTEXITCODE }
}

# ---------------------------------------------------------------- AmbientMixTests (US-T448)
# The space soundscape's mixing (Cpp/Source/SpaceGod/Audio/AmbientMix.h, header-only, no engine): which beds play where, how
# loud, the crossfade. Needs nothing of SpaceGodCore.lib.
$amDir = Join-Path $core 'Tests\Ambience'
$amHeader = Join-Path $repo 'Cpp\Source\SpaceGod\Audio\AmbientMix.h'
$amObj = Join-Path $parityObjRoot 'Tests\Ambience\AmbientMixTests.obj'
if ((Test-Path -LiteralPath $amObj) -and (Get-Item -LiteralPath $amHeader).LastWriteTimeUtc -gt (Get-Item -LiteralPath $amObj).LastWriteTimeUtc) { Remove-Item -LiteralPath $amObj -Force }
$am = Invoke-Compile @(Get-Item -LiteralPath (Join-Path $amDir 'AmbientMixTests.cpp')) $parityObjRoot
if ($am.Failed.Count -gt 0) { exit 1 }
$amExe = Join-Path $out 'AmbientMixTests.exe'
& link.exe /nologo /DEBUG @linkOpt "/OUT:$amExe" $am.Objs
if ($LASTEXITCODE -ne 0) { throw 'link.exe failed (AmbientMixTests).' }
Write-Host "exe     $amExe"
if (-not $SkipParity) {
  & $amExe
  if ($LASTEXITCODE -ne 0) { Write-Host 'AmbientMixTests: failures (see above).' -ForegroundColor Red; exit $LASTEXITCODE }
}
exit 0
