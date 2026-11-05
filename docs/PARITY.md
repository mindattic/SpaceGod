# Parity with SpaceGod.Unity.CSharp (US-T70)

SpaceGod is meant to be the same game as SpaceGod.Unity.CSharp, the reference, and
SpaceGod.Unreal.CSharp, so that any one of the three can be frozen and the others archived. The
simulation (`Cpp/Source/SpaceGodCore`, from Unity's `Assets/UnitySpace/Core`) is proven by the
Bench: the five scenario digests are byte-identical in all three repos. This page covers the layer
above it, from Unity's `Assets/UnitySpace/Runtime/` (72 files) to `Cpp/Source/SpaceGod/` (the
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

Nothing is missing there. The weak-table guard noted here first is now in both C# copies: SpaceGod.Unreal.CSharp
re-vendored `ObjectGraph.cs` from the Unity copy (`13bf391`), so the two C# Cores are byte-identical.

## Follow-ups from the C# copies' audits (US-T70)

| Change | Commit | Why |
|---|---|---|
| The Render pipeline page lists URP's post-processing rows first, then `render.msaa`, then `render.pipeline` and HDRP's 6 | `87982ca` | The original's order: in Unity, URP's settings assembly declares them before the game's own (Unreal C# `57c111a`). Here `GameSettingsHost::RegisterRuntimeSources` declares `UrpPostProcessSettings` first; nothing touches a setting before that discovery. The `render.settings` probe expects this order. |
| The pointer-over-UI test reads `Input::InputDevice::current()` | `87982ca` | Both C# copies read Input's device (Unity `ad968a1`). It read the GUI host's Slate mouse, so a scripted run (the audit, the co-op smoke's client) tested its clicks against wherever the real cursor was. In play the device is the real mouse, so nothing changes. |
| The facade probes (`text.*`, `viewer.*`, US-T52/T53) are opt-in: `-SpaceGodAuditFacade`, `tools/play.ps1 -Mode audit -AuditFacade` | `87982ca` | They check this copy's own GUI facade, which the C# copies have no counterpart of. Without them the audit is the 85 probes every copy runs. |

### The audit, row for row

`tools/play.ps1 -Mode audit` here, `tools/play.ps1 -Mode audit` in SpaceGod.Unreal.CSharp (a fresh clone of `57c111a`)
and `tools/audit.ps1` in SpaceGod.Unity.CSharp. All three give **85 rows, the same ids in the same order**. A clean run
of each gives the same result on every row: **80 PASS, 0 FAIL, 5 INFO**. The INFO keys are the same on all three:
helm.ftl-engage, lane-reenter, phasers, run-hot and lock. `render.settings` reads 20/20 in order on all three.

Two probes depend on the live galaxy, whose battles and placements differ from run to run, so a run can fail them on
any copy:

- **`combat.mouse-fire`:** a chase from 10-17 km to inside 1.5 km. It fails when the target outruns the fighter, or
  when the fight it opens draws in other ships.
  - C++: 7 of 13 runs on 2026-11-04 passed.
  - Unity: 3 of 4 of this session's runs passed (`tools/audit.ps1`, player of `e5efa29`).
- **`transit.lane-enter`:** the audit fighter is sometimes caught by a battle at Venus. Unity: 2 of 4 runs failed it
  for that reason ("SECOND BATTLE OF VENUS has begun", "has been destroyed").

These are the probes' own exposure to a random world, shared by all three copies. They are not a difference between
them.

### Saves: every copy loads every copy's

The C# copies' harness, here with SaveCompat wired as the game wires it (`SaveStatics.EnsureFixed` before the first
read, `SaveWiring.RewireIfNeeded` after it), and the same steps on the C++ Bench and Core:

1. The Bench's galaxy at JD 2461348.0, seed 1.
2. 600 ticks of 1/60 s at 1 month/s.
3. A save.

A load then prints the whole-game digest at the save and 600 ticks on.

| Written by → loaded by | C++ | Unreal C# | Unity |
|---|---|---|---|
| **C++** | `36421EA0A26852F4` → `3CFE5E6B204D5740` | `36421EA0A26852F4` → `3CFE5E6B204D5740` (rewired) | `36421EA0A26852F4` → `3CFE5E6B204D5740` (rewired) |
| **Unreal C#** | `36421EA0A26852F4` → `3CFE5E6B204D5740` | `36421EA0A26852F4` → `3CFE5E6B204D5740` | `36421EA0A26852F4` → `3CFE5E6B204D5740` |
| **Unity** | `36421EA0A26852F4` → `3CFE5E6B204D5740` | `36421EA0A26852F4` → `3CFE5E6B204D5740` | `36421EA0A26852F4` → `3CFE5E6B204D5740` |

Each writer's own run gave the same two digests before it saved.

The one format difference is at the source, and it stays. A C++ save carries no delegate fields: C++ has no C# method
to name, so the file is 10.4 MB against the C# copies' 11.7 MB (the two C# saves are byte-identical). A C# copy
rebuilds them on load (`rewired`, only for C++ saves). The C++ copy wires every galaxy it loads again, its own
saves and the C# copies' (`Persistence/Registrations/Wiring.cpp`, Galaxy's load hook), and ignores the delegate
entries a C# save carries.

### Proof after these changes

- **Editor build:** clean.
- **Audit:** 85 rows; clean runs are 80 / 0 / 5 INFO (above).
- **Bench:** sol-idle and high-warp re-checked identical. These changes leave the Core untouched, and the 5 digests
  were identical at `ad93e37`.
- **Still to do:** the Shipping package, its co-op smoke and the `run.ps1 -Force` redeploy. The machine's low-memory
  guard stopped that run on 2026-11-04. `C:\Apps\SpaceGod` still holds `ad93e37`; its window title reads
  "SpaceGod". Writing C# delegate names from C++ would add nothing a load uses.

## Recording through MindAttic.Capture (US-T72)

Per [docs/CAPTURE.md](CAPTURE.md), mirrored from SpaceGod.Unity.CSharp at `2158228`. This copy has what the Unity one has:
- **Flags:** `-capture` (`video`, `screen N`, `hq`, a duration) and `-capture-config`.
- **Tour shots:** with `-capture`, the `-demo` tour's screenshots go through MindAttic.Capture.
- **Rules:** the shipped `SpaceGod.capture.json`.
- **Events:** the 20-event catalogue, with the same names, fields and order, at the same code points.
- **Log line:** the `[capture] event` line.
- **Hotkeys:** Alt+F9/F10/F11 through `InputState`.
- **Gating:** capture-enabled published builds.

The engine side differs (Capture/README.md). MindAttic.Capture runs in a host process (`Capture/SpaceGod.Capture`,
on the vendored library) that records the game window through Windows Graphics Capture. The game drives it over a pipe,
where Unity runs the package inside the player and reads the GPU frame. The `[capture] event` lines also go to the
session folder's `.game.log`, because a Shipping game writes no log.

**Proof** at `e89d460`, from a Shipping package:

| Check | Result |
|---|---|
| Bench | 5 of 5 digests identical |
| Audit | 80 PASS, 0 FAIL, 5 INFO |
| Co-op smoke | 15/15, 54 of 54 digests identical |
| `-demo -capture video screen 10` | Exited by itself after 297 s. Its session holds the `.mkv` and `.mp4`, 47 PNGs (every tour stop, the 10 s shots, two `arrival` rule shots), a sidecar for each, and `session.json` with 6 events and 6 markers. `.game.log` has the `[capture] event` lines (`time-warp`, `system-arrival`). |
| `coop-smoke -CaptureHost` | The host raised `coop-joined`, `coop-resync` x3, `time-warp` x2, `game-saved`, `trade`, `battle-started`, `system-arrival`, `game-loaded` and `coop-left`. |

Notes on the demo run:
- The 60 fps lossless tape fell behind on the loaded machine. MindAttic.Capture's valve (encoder lag over 60 s) stopped
  recording 2:16 in, as designed, and the later tour shots were skipped. `hq` avoids that.
- The smoke's `hail` comes from the client, so only the client raises it (section 3: the peer whose player hailed).
- `run.ps1 -Force` (the C:\Apps republish) was stopped by the machine's low-memory guard on 2026-11-04;
  C:\Apps\SpaceGod still holds `f2daceb`. The next Deploy & Run publishes this.
