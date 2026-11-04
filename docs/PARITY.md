# Parity with SpaceGod.Unity.CSharp (US-T70)

SpaceGod.Unreal.Cpp is meant to be the same game as SpaceGod.Unity.CSharp, the reference, and
SpaceGod.Unreal.CSharp, so that any one of the three can be frozen and the others archived. The
simulation (`Cpp/Source/SpaceGodCore`, from Unity's `Assets/UnitySpace/Core`) is proven by the
Bench: the five scenario digests are byte-identical in all three repos. This page covers the layer
above it, from Unity's `Assets/UnitySpace/Runtime/` (72 files) to `Cpp/Source/UnrealSpaceGod/` (the
same files, file for file, plus the `UnityCompat` shims and the Unreal `Game/` and `Smoke/` glue).
The audit was run on 2026-11-04 against SpaceGod.Unity.CSharp `7c81c68`.

## How it was checked

Six mechanical sweeps over the whole runtime source, each read item by item where it flagged
anything. The scripts were throwaway, and every flagged item is accounted for below.

| Sweep | Unity side | Result in C++ |
|---|---|---|
| Every string literal of 4+ characters with a word in it, interpolations split into their literal parts: menu items, buttons, labels, HUD readouts and their formats, toasts, notifications, help text, settings text | 2,566 strings | 2,521 present verbatim and 4 present with UTF-8 escapes (`\xC2\xB7`, `\xC2\xB2`). The other 41 are engine-only (below). |
| Every method name, per file family | 865 methods with bodies | All present. 60 have no same-named C++ body: constructors, local functions that are lambdas in C++ (`Row`, `Consider`, `Started`, `P`), the URP/HDRP back ends, and Unity's TCP co-op link (below). |
| Every method body: the capitalised names it assigns or calls, against its C++ namesake | 865 bodies | 213 flagged. All were read: engine or BCL spellings (`StringComparison`, `UnityEngine`, `GraphicsBuffer`), C# properties that C++ spells as accessors (`Visible` → `SetVisible`, `Blend` → `SetBlend`, `Propellant` → `SetPropellant`, `Lighting` → `SetLighting`, `PlayerControlled` → `SetPlayerControlled`), a C# type test written in C++ as a nested `if` (run hot), or the engine-only files. None was a missing behaviour. |
| Numeric literals per file family (tuning values, layout, timings, limits) | every literal except 0, 1 and 2 | All present except the HDRP exposure range and Unity's default TCP port 7777 (both engine-only). |
| Key and mouse bindings: `InputState.Poll`, each of 71 fields and its expression | 71 bindings, 69 fields cleared by `Reset` | All 71 identical after the device syntax is normalised. **`Reset` lacked `HailPolity`, now fixed (below).** Mouse-button, wheel and axis calls per family are identical too. |
| Settings: every `*Setting` construction, comparing key, group, label, default, range, step, help and keywords | 196 settings | 191 byte-identical. 4 differ only in spelling (`1` vs `1.0`, an escaped `²`, or a key built at run time and checked by hand). 1 is HDRP-only. |
| Command-line switches | 14 | All read in C++ through Unreal's `FParse`, except `-pipeline`, `-pipelineRelaunch` (Unity's render-pipeline restart) and `-port` (Unity's TCP link; Unreal's listen server takes the map URL's port). C++ adds its own test switches: `-SpaceGodSmoke`, `-SpaceGodAudit`, `-SpaceGodRace`, `-SpaceGodUiShots` and the others. |

## Inventory

| Feature | Unity (reference) | C++ | Status |
|---|---|---|---|
| Startup and main menu (Continue, New Game, Load Game with Load / Use / Delete "Sure?", Profiles with Create, Settings, Help, Asset Library POC with Launch demo, Quit); `-continue`, `-demo`, `-capture` | `Bootstrap.cs`, `GameLaunch.cs`, `UI/MainMenu.cs` | `Bootstrap.cpp`, `GameLaunch.cpp`, `UI/MainMenu.cpp` | same (Unity's scene load becomes Unreal's map; `AfterSkippingMenu` is Unity's scene hook) |
| Pause menu (Resume, Help, Save / Load, Settings, Main Menu, Quit); settings pages with On/Off, < >, Reset, Apply / Cancel / Back, and the Display page's Keep / Revert | `UI/PauseMenu.cs` | `UI/PauseMenu.cpp` | same |
| Help screen and controls help | `UI/HelpScreen.cs` | `UI/HelpScreen.cpp` | same |
| HUD: navigator, selection, readouts, toasts, law notices, news feed (X), clear screen (F11), HUD toggle (U) | `UI/Hud.cs` | `UI/Hud.cpp`, `Hud.Panels.cpp`, `Hud.Boards.cpp` | same |
| Boards: market (M), cargo (I), outfit (F2), bounties (F4), contracts (F7), fleet and holdings (F10), saves (F6) | `UI/Hud.cs` | `UI/Hud.Boards.cpp`, `Hud.Panels.cpp` | same |
| Trade desk (\\): runs, brokers, news, charters, insurance, warehouses, back door | `UI/Hud.Trade.cs` | `UI/Hud.Trade.cpp` | same |
| Galaxy map ([): systems, command, diplomacy, envoy | `UI/GalaxyMapScreen.cs` | `UI/GalaxyMapScreen.cpp` | same |
| Hail and statecraft (F12, envoy screen) | `UI/GalaxyMapScreen.cs`, `Input/InputState.cs` | same files | **fixed here (`InputState::Reset` now clears `HailPolity`)** |
| Display options (resolution, window mode, vsync; Keep / Revert) | `UI/DisplayOptions.cs` | `UI/DisplayOptions.cpp`, `UI/DisplayBackend.cpp` | same (Unreal applies them through `UGameUserSettings`) |
| Icons and ship thumbnails | `UI/Icons.cs`, `Rendering/ShipThumbnails.cs` | `UI/Icons.cpp`, `Rendering/ShipThumbnails.cpp` | same |
| Camera: orbit, focus (F), scale toggle (G / \`), home (Home / Backspace), auto-reorient (R), look sensitivity, invert look | `Camera/SpaceCamera.cs`, `UnitySpaceApp.cs` | `Camera/SpaceCamera.cpp`, `UnitySpaceApp.Main2.cpp` | same |
| Time: warp steps (. = + / , - −), pause (Space / Pause), reset (T), starting warp setting | `UnitySpaceApp.cs` | `UnitySpaceApp.cpp`, `Main2.cpp` | same |
| Piloting: take the helm (P), WASD/QE/ZX, boost and slow, cruise (J), flight assist (F), capital ship engine telegraph (W/S steps), fire, EMP (middle mouse), mines (Ctrl), silent running (F3) | `UnitySpaceApp.cs`, `Input/InputState.cs` | `UnitySpaceApp.Main2.cpp`, `Input/InputState.cpp` | same |
| Battle: watch (B), seats (' ;), man a gun (/), move to host (Backspace), phasers (Insert), run hot (Shift+Insert), roll the weak patches (Shift+Delete), boarding (B), power to weapons/shields/engines (7/8/9/0) | `UnitySpaceApp.Embodiment.cs`, `.Broadsides.cs`, `.Boarding.cs` | the same names `.cpp` | same |
| Collisions | `UnitySpaceApp.Collisions.cs` | `UnitySpaceApp.Collisions.cpp` | same |
| Course and lanes (N), haulers | `UnitySpaceApp.Course.cs`, `.Lanes.cs`, `.Hauler.cs` | the same names `.cpp` | same |
| Gates (Y) and FTL: engage (Enter), autopilot (End), target (PgUp / PgDn), clear course (Delete), star travel banner | `UnitySpaceApp.Ftl.cs`, `Camera/StarTravel.cs` | `UnitySpaceApp.Ftl.cpp`, `Camera/StarTravel.cpp` | same, including Unreal.SpaceGod US-T58 (gate arrival) |
| Practice fighter (Shift+V), spawn panel (V), model viewer (F9) | `UnitySpaceApp.Practice.cs`, `ModelViewer.cs` | `UnitySpaceApp.Practice.cpp`, `ModelViewer.cpp` | same |
| Satellites (K), skyboxes (Shift+K, Shift+Alt+K, numpad * /), drop-in skies, nebula detail | `Views/SkyView.cs`, `Views/SatelliteSwarm.cs` | `Views/SkyView.cpp`, `Views/SatelliteSwarm.cpp` | same (how a drop-in sky file is read is engine-specific) |
| Saves: slots, quicksave (F5), quickload (F8), autosave, describe, delete; the static-object walk leaves the `ConditionalWeakTable` caches out (Unreal.SpaceGod US-T41) | `UnitySpaceApp.Saves.cs`, `Core/Persistence/ObjectGraph.cs` | `UnitySpaceApp.Saves.cpp`, `SpaceGodCore/Persistence` (`StaticTables.cpp`: "Not walked, in either build") | same |
| Settings registry and profiles: 196 settings | `EngineSettings.cs`, `GameSettingsHost.cs`, `ActiveProfile.cs`, Core settings | the same names `.cpp` | same |
| Audio: master volume, session mute (`-muted` / `MINDATTIC_CLI_MUTED`) | `SessionAudio.cs` | `SessionAudio.cpp` | same, line for line |
| Streaming of ship models and textures | `UnitySpaceApp.Streaming.cs`, `Rendering/AssetStreamer.cs` | the same names `.cpp` | same |
| Views: bodies, ships, fighters, structures, gates, lanes, pods, beacons, debris, discs, belts, starfield, interstellar, battle effects | `Views/*.cs` | `Views/*.cpp` | same (GPU buffer and mesh calls are engine-only) |
| Co-op: host, join, seats, helm take and hand-back with the up frame, frame records, digests, resync, saves through the host, the client's HUD, galaxy map and envoy actions, `-CoopHost`, `-CoopJoin=`, `-CoopName=`, `-SpaceGodCoopLog=`, `-CoopNoHostReload`, `-SpaceGodCoopSmoke=` | `Coop/*.cs`, `UnitySpaceApp.Coop.cs`, `UI/*.Coop.cs` | `Coop/*.cpp`, `UnitySpaceApp.Coop.cpp`, `UI/*.Coop.cpp`, `Game/SpaceGameMode.cpp` | same. The link is engine-only: Unreal's listen server here, a TCP link in Unity (`CoopNet.cs`). The 15-check co-op smoke passes in all three repos. |

## Engine-only differences (not visible to the player)

- **Render pipelines.** Unity's URP and HDRP back ends, the pipeline picker and its relaunch
  (`Rendering/Pipeline/*`, `-pipeline`, `-pipelineRelaunch`) and their settings (HDRP exposure,
  volumetrics, ray tracing, physical sky, URP physical camera). Unreal has one renderer, behind
  `Rendering/Pipeline/RenderBackend.cpp` and the SpaceGodRender plugin.
- **Co-op transport.** `Coop/CoopNet.cs` and `UnitySpaceApp.CoopNet.cs` (TCP, `-port`, the client's
  frame pacing) take the place of `Coop/SpaceCoopLink.cpp` and Unreal replication.
- **Platform.** Developer log lines, the screenshot runner's MindAttic.Capture call, editor-only quit
  paths and Unity's scene hooks.

## Fixed here

- **`InputState::Reset` clears `HailPolity`.** Unity's `Reset` clears every edge that `Poll` sets.
  The C++ port missed F12's, so on a suspended frame (`Poll` returns straight after `Reset`, for
  example while the model viewer holds the keyboard) the last frame's hail stayed set. Both C#
  copies already had it.

## Found on the Unity side

Nothing is missing there. One note for whoever freezes the copies: SpaceGod.Unity.CSharp's
`Core/Persistence/ObjectGraph.cs` carries the US-T41 weak-table guard, which Unity.SpaceGod
`45dc06b` lacks. SpaceGod.Unreal.CSharp's vendored Core came from `45dc06b`, so it should be
checked for the same guard. Its own SaveCompat may cover it.
