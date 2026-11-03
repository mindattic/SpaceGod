# Unreal.SpaceGod

The real Sol system in Unreal Engine 5, played co-op: the Unreal counterpart of
[Unity.SpaceGod](https://github.com/mindattic/Unity.SpaceGod). The planets, dwarf planets, moons and
asteroids sit where they really are for today's date, placed from JPL orbital elements, and you and
your friends fly around them together on one shared clock.

## What is ported

The simulation core (`Source/UnrealSpaceGod/Sim/`) is a value-for-value C++ port of Unity.SpaceGod's
`Assets/UnitySpace/Core/`:

| Unity.SpaceGod | Unreal.SpaceGod | What it does |
|---|---|---|
| `Astro.cs` | `Sim/Astro.h` | Constants, Julian dates, RA/Dec poles into the ecliptic frame |
| `OrbitalElements.cs`, `Kepler.cs` | `Sim/Orbits.*` | JPL elements with secular rates, Kepler's equation |
| `LunarTheory.cs` | `Sim/LunarTheory.*` | The Moon from ELP-2000/82 (Meeus ch. 47), not an ellipse |
| `BodyDefinition.cs`, `BodyCatalog.cs` | `Sim/BodyCatalog.*` | All 32 Sol bodies, with radii, GM, IAU poles and rotation |
| `Ephemeris.cs` | `Sim/Ephemeris.*` | Positions for any date; the Earth-Moon barycentre; Laplace planes |
| `BodyOrientation.cs` | `Sim/BodyOrientation.*` | IAU prime meridians, so maps face the right way |
| `ScaleModel.cs` | `Sim/ScaleModel.*` | The live GAMEPLAY / REALISTIC scale switch |
| `SimClock.cs` (warp ladder) | `Sim/SimClock.h` | Real time to one year per second |

The NASA/USGS colour maps and their sources are in `SourceArt/Textures/` (see its `SOURCES.md`).
The rest of Unity.SpaceGod (ships, fleets, economy, combat, the other star systems) is not ported yet.

## Co-op

One player hosts a listen server and the others join it. The server owns the simulation clock (the
date, the time warp and pause), so everyone sees the planets in the same places; any player can
change the time, and everyone is told who did. Each player flies their own camera and sees the
others as green markers labelled with the body they are near. The scale switch, orbits and labels
are your own and don't affect anyone else.

A camera is shared as a frame body plus a true offset in km, so a teammate in REALISTIC scale and
you in GAMEPLAY scale still agree on where they are.

## Running it

Needs Unreal Engine 5.5 with Visual Studio 2022 (the "Game development with C++" workload).

1. Right-click `UnrealSpaceGod.uproject` and pick **Generate Visual Studio project files**, then open
   the project. The editor offers to build the module.
2. On first open, `Content/Python/init_unreal.py` imports the maps into
   `/Game/UnrealSpaceGod/Textures` and builds the `M_Body` and `M_Ring` materials. Commit what it
   creates under `Content/UnrealSpaceGod/`. Until it has run, bodies are drawn flat-lit.
3. Press Play. To try co-op in the editor, set **Play > Net Mode** to *Play As Listen Server* with 2
   or more players.

Outside the editor, `tools/play.ps1` starts a game window:

```powershell
./tools/play.ps1 -Mode host                         # host on UDP 7777
./tools/play.ps1 -Mode join -Address 192.168.1.20   # join a host
```

In game, **F9** turns a solo session into a hosted one, and `open <ip>` in the `` ` `` console joins one.

## Controls

| Keys | Action |
|---|---|
| W A S D, Q / E (or C / ]) | swim, down / up (leaves focus) |
| Shift / Ctrl | boost / fine |
| Wheel | zoom when focused, swim speed in free flight |
| Right-drag | look / orbit |
| Left-click | select (double-click: focus) |
| F | focus the selection |
| Home | system view |
| 1-9, 0 | Mercury to Neptune, Pluto, the Sun (twice to focus) |
| Tab / Shift+Tab | next / previous body |
| Space | pause, for everyone |
| , . | time warp slower / faster, for everyone |
| T | back to now, for everyone |
| G | GAMEPLAY / REALISTIC scale, just for you |
| O / L | orbits / labels |
| M | mark the selection for everyone |
| J | jump to the next teammate's view |
| F9 | host |
| H / F1 | help |

## Layout

- `Source/UnrealSpaceGod/Sim/`: the engine-independent simulation (plain C++ on Unreal's core types).
- `Source/UnrealSpaceGod/Game/`: `ASolSystem` draws the bodies, rings and orbit trails;
  `ASpaceGameState` holds the shared clock; `ASpacePawn` is a player's camera; `ASpacePlayerController`
  reads input; `ASpaceHUD` draws labels, teammates, marks and the help screen.
- The game needs no level: it plays on the engine's empty `/Engine/Maps/Entry` and builds everything
  at runtime.
