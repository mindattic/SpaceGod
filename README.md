# Unreal.SpaceGod

The real Sol system in Unreal Engine 5, played co-op: the Unreal counterpart of
[Unity.SpaceGod](https://github.com/mindattic/Unity.SpaceGod). The planets, dwarf planets, moons and
asteroids sit where they really are for today's date, placed from JPL orbital elements, and you and
your friends fly around them together on one shared clock.

> **Migration in progress (US-T4):** the full game is being ported twice, to C++ (`Cpp/`) and C# (`CSharp/`), co-op, then benchmarked against each other. See [docs/MIGRATION.md](docs/MIGRATION.md).

## What is ported

C++ track: the whole simulation Core is translated file for file into `Cpp/Source/SpaceGodCore/` (rules in
[docs/MIGRATION.md](docs/MIGRATION.md)), and Unity.SpaceGod's `Runtime/` layer (UnitySpaceApp and its partials,
the camera rig, input with the original bindings, settings, profiles, launch) into `Cpp/Source/UnrealSpaceGod/`,
mirroring `Runtime/`'s layout. Views, UI and Rendering follow in their own tickets.

The NASA/USGS colour maps and their sources are in `Cpp/SourceArt/Textures/` (see its `SOURCES.md`).

## Co-op

One player hosts a listen server and the others join it. The server owns the simulation clock (the
date, the time warp and pause), so everyone sees the planets in the same places; any player can
change the time, and everyone is told who did. Each player flies their own camera and sees the
others as green markers labelled with the body they are near. The scale switch, orbits and labels
are your own and don't affect anyone else.

A camera is shared as a frame body plus a true offset in km, so a teammate in REALISTIC scale and
you in GAMEPLAY scale still agree on where they are.

## Running it

Needs Unreal Engine 5.8 with Visual Studio 2022 (the "Game development with C++" workload).

1. Right-click `Cpp/UnrealSpaceGod.uproject` (or run `tools/build.ps1`) and pick **Generate Visual Studio project files**, then open
   the project. The editor offers to build the module.
2. On first open, `Cpp/Content/Python/init_unreal.py` (or headless: `setup_assets.py`) imports the maps into
   `/Game/UnrealSpaceGod/Textures` and builds the `M_Body` and `M_Ring` materials. Commit what it
   creates under `Cpp/Content/UnrealSpaceGod/`. Until it has run, bodies are drawn flat-lit.
3. Press Play. To try co-op in the editor, set **Play > Net Mode** to *Play As Listen Server* with 2
   or more players.

Outside the editor, `tools/play.ps1` starts a game window:

```powershell
./tools/play.ps1 -Mode host                         # host on UDP 7777
./tools/play.ps1 -Mode join -Address 192.168.1.20   # join a host
```

In game, `Host` in the `` ` `` console turns a solo session into a hosted one, and `open <ip>` joins one.

### The C# track (`CSharp/`)

Unity.SpaceGod's own C# Core running inside Unreal through [UnrealSharp](https://github.com/UnrealSharp/UnrealSharp)
(why, and the build details: [docs/MIGRATION.md](docs/MIGRATION.md#c-track)). Needs Unity.SpaceGod and Unity.Shared
cloned next to this repo; the Core is compiled from there, not copied.

```powershell
git submodule update --init                   # the UnrealSharp plugin (build.ps1 does this too)
./tools/build.ps1 -Track CSharp               # C++ (plugin) + C# glue + Script/ + the Core; installs a private .NET SDK if the machine's can't build UnrealSharp
./tools/play.ps1 -Track CSharp -Mode smoke    # headless: build the Galaxy, step it, log Earth's position, quit
./tools/play.ps1 -Track CSharp                # game window (engine side still to come)
```

## Controls

Unity.SpaceGod's own bindings, unchanged (its Core `ControlsHelp` and `Runtime/Input/InputState.cs`; in game,
H / F1 shows them all).

## Layout

- `Cpp/Source/SpaceGodCore/`: the engine-independent simulation (pure C++20 on `Cs/Cs.h`, no Unreal types).
- `Cpp/Source/UnrealSpaceGod/`: the Runtime port. `UnitySpaceApp` (+ one `.cpp` per C# partial) drives the Core
  each frame; `Camera/` (SpaceCamera, StarTravel), `Input/` (InputState + the Unreal device feed), `UnityCompat/`
  (Unity's value types and statics in C++), `UnrealSpace.h` (the one Unity -> Unreal axes/units conversion).
  `AUnitySpaceAppActor` hosts the app; `Game/` holds the Unreal framework classes (game mode, the GameState that
  mirrors the host's clock for co-op, the player controller, the HUD that runs OnGUI).
- The game needs no level: it plays on the engine's empty `/Engine/Maps/Entry` and builds everything
  at runtime (`?Scene=MainMenu` or `?Scene=UnitySpace` picks Unity's scene).
