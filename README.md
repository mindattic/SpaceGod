# SpaceGod.Unreal.Cpp

The real Sol system in Unreal Engine 5, played co-op, written in **C++**. The planets, dwarf planets, moons and
asteroids sit where they really are for today's date, placed from JPL orbital elements, and you and your friends fly
around them together on one shared clock.

This is one of three self-contained copies of the same game (US-T69), each with every feature of the others:

| Repo | Engine / language | Desktop shortcut |
|---|---|---|
| **SpaceGod.Unreal.Cpp** (this one) | Unreal Engine 5.8, C++ | `SpaceGod – Unreal C++` |
| SpaceGod.Unreal.CSharp | Unreal Engine 5.8, C# (UnrealSharp) | `SpaceGod – Unreal C#` |
| SpaceGod.Unity.CSharp | Unity, C# (the original) | `SpaceGod – Unity C#` |

It was split out of `Unreal.SpaceGod` (its full history is kept here), where the game was ported twice from
`Unity.SpaceGod` and the two ports raced (docs/RACE.md). Nothing here reads a sibling repo: a fresh clone builds and
runs with only Unreal Engine 5.8 and Visual Studio's C++ tools.

## Deploy and play

| Script | What it does |
|---|---|
| `run.bat` | The Desktop shortcut's target. Asks **1) Deploy** (`run.ps1`) or **2) Deploy & Run** (`run.ps1 -Launch`); both repackage only when something changed. Arguments skip the menu (`run.bat -Force` deploys, rebuilding anyway, and plays). |
| `deploy.bat` | Asks **1) Deploy & Run** (runs `run.ps1 -Launch -Force`) or **2) Run** (starts the published copy; deploys first if there is none). |
| `run.ps1` | Non-interactive: `git pull`, package Shipping (`tools/package.ps1`), publish to `C:\Apps\SpaceGod.Unreal.Cpp\`, refresh the Desktop shortcut `SpaceGod – Unreal C++`. `-Launch` starts the game; `-Force` packages even when nothing changed; `-NoPull` skips the pull. |

Every game these scripts start records: they pass `-capture video hq` (see Recording below).

```powershell
./run.ps1 -Launch          # deploy and play
./run.bat                  # what the Desktop shortcut runs: the 1) Deploy / 2) Deploy & Run menu
./deploy.bat               # the menu
```

## Requirements

- Unreal Engine 5.8 (found via `UE_ROOT`, the registry, the Epic launcher's install list, or `<drive>:\Games\UE_5.8`;
  see `tools/engine.ps1`).
- Visual Studio 2022 with the "Game development with C++" workload (MSVC x64 tools).
- The .NET 10 SDK, for the MindAttic.Capture host the package carries (built from `Capture/` and `Vendor/`), and
  ffmpeg on PATH to record video with `-capture` (not bundled).

## Building and running from source

```powershell
./tools/build.ps1                                   # the editor target (UBT, MaxParallelActions 4)
./tools/play.ps1                                    # a game window from the editor binaries
./tools/play.ps1 -Mode host                         # host on UDP 7777
./tools/play.ps1 -Mode join -Address 192.168.1.20   # join a host
./tools/package.ps1 -Configuration Shipping         # Builds/Cpp/Windows/SpaceGod.Unreal.Cpp.exe
./tools/bench.ps1                                   # the headless Bench: 5 scenarios, digests checked
./tools/play.ps1 -Mode coop-smoke -PackageDir Builds/Cpp/Windows   # two packaged instances, co-op checked
```

Opening `Cpp/UnrealSpaceGod.uproject` in the editor works too; `Cpp/Content/Python/init_unreal.py` imports the
colour maps from `Cpp/SourceArt/Textures/` on first open (the results are committed under `Cpp/Content/`).

A packaged game hosts with `-CoopHost` and joins with `-CoopJoin=<address>`. In game, `Host` in the `` ` `` console
turns a solo session into a hosted one, and `open <ip>` joins one.

## The Bench

`Bench/Cpp` runs the simulation Core headless over `Bench/scenarios.json` (sol-idle, high-warp, economy-year, battle,
galaxy-ftl) and writes a digest of the whole state. `tools/bench.ps1` checks each against
`Bench/expected-digests.json`, the digests Unity.SpaceGod's own C# Core produces, so the C++ Core is proven bit for bit
the original. See Bench/README.md.

## Co-op

One player hosts a listen server and the others join it. The server owns the simulation clock (the date, the time
warp and pause), so everyone sees the planets in the same places; any player can change the time, and everyone is told
who did. Each player flies their own camera and sees the others as green markers labelled with the body they are near.
The scale switch, orbits and labels are your own and don't affect anyone else.

## Recording (MindAttic.Capture)

Off unless asked, exactly as in the other two copies ([docs/CAPTURE.md](docs/CAPTURE.md)):

```powershell
SpaceGod.Unreal.Cpp.exe -capture video              # record the whole run (.mkv + .mp4 on exit)
SpaceGod.Unreal.Cpp.exe -capture video screen 10    # and a PNG every 10 s
SpaceGod.Unreal.Cpp.exe -demo -capture video        # the scripted tour, each stop a screenshot
SpaceGod.Unreal.Cpp.exe -capture -capture-config my.json   # your own rules instead of the shipped ones
```

Output goes to `R:\MindAttic\Capture\SpaceGod.Unreal.Cpp\`. The game raises its gameplay events (battles, arrivals,
gate jumps, trades, hails, saves, co-op joins...) so the shipped rules save replays of victories, defeats and a lost
ship, and screenshots of arrivals and discoveries. While capturing, **Alt+F9** takes a screenshot, **Alt+F10** starts or
stops a recording and **Alt+F11** saves the last 30 s. How the C++ game drives MindAttic.Capture: [Capture/README.md](Capture/README.md).

## Controls

Unity.SpaceGod's own bindings, unchanged (in game, H / F1 shows them all).

## Layout

- `Cpp/Source/SpaceGodCore/`: the engine-independent simulation (pure C++20 on `Cs/Cs.h`, no Unreal types), a file
  for file port of Unity.SpaceGod's `Core/`.
- `Cpp/Source/UnrealSpaceGod/`: the Runtime port. `UnitySpaceApp` (+ one `.cpp` per C# partial) drives the Core each
  frame; `Camera/`, `Input/`, `UnityCompat/` (Unity's value types and statics in C++), `Coop/`, `Game/` (the Unreal
  framework classes).
- `Shared/Plugins/SpaceGodRender/`: the original's shaders ported to Unreal, the materials built on them, and the
  imported Resources (textures, ship models, data).
- `Bench/`: the headless runner and its scenarios. `tools/`: build, package, play, bench and race scripts.
- `Capture/SpaceGod.Capture/`: the MindAttic.Capture host the game drives for `-capture` (C#, .NET 10), built on
  `Vendor/MindAttic.Capture/` (the library's sources, copied in; Vendor/README.md). `Cpp/Content/Capture/` holds the
  shipped default rules.
- `tools/unity_extract.py` and `Shared/Plugins/SpaceGodRender/Content/Python/spacegod_import_resources.py` are
  **optional one-time import tools** that read the original Unity project or player; their output is committed, and
  nothing in the build runs them.
- The game needs no level: it plays on the engine's empty `/Engine/Maps/Entry` and builds everything at runtime.

## History

docs/MIGRATION.md (how the port was made), docs/FAITHFULNESS.md, docs/PERSISTENCE.md and docs/RACE.md (the C++ vs C#
race) are kept as the record of `Unreal.SpaceGod`; they still describe the C# track, which now lives in
SpaceGod.Unreal.CSharp. docs/USER_STORIES.md is Unreal.SpaceGod's ticket log as of the split.
