# Controls inventory (US-T183)

One table per area: action, key, where it works, menu path. The in-game Help screen (`Core/ControlsHelp.cpp`) is the player-facing
copy of this file; when a key changes, change both. Keys are read in `Input/InputState.cpp` (the shared per-frame poll, left and
right Shift / Ctrl / Alt both count), plus the modal panels that own the keys while open (dev menu `UI/CheatMenu.cpp`, comms
`UnitySpaceApp.Comms.cpp`, bottom drawer `UI/Hud.Drawer.cpp`, tour `Tour/ShowcaseTour.cpp`, gas-giant contract
`HarvestPlay.cpp`). There is no gamepad binding: the input seam (`Input/UnrealInputDevice.cpp`) maps the keyboard and mouse only.
`Cpp/Config/DefaultInput.ini` holds engine settings only, no action or key mappings.

**Command view** = the free SpaceGod camera. **Controls** = at any seat (helm, fighter, station, gun, worker). `InputState::Arbitrate`
decides which meaning of a shared key stands: at the controls F, T, Backspace, B, X, Tab (cycle), R (tap) are the craft's; in
command, flight keys (flight assist, lock, flip, drift, power, self-destruct, ping, cruise, tractor) do nothing.

**Modifier convention.** Shift makes the "bigger / other" sibling of a key (Shift+F1 dev menu next to F1 help, Shift+V practice
fighter, Shift+K next sky, Shift+P EVA, Shift+T tractor, Shift+J cruise drive, Shift+F3 go dark, Shift+H comms, Shift+U drawer,
Shift+Insert run hot, Shift+Del roll patches, Shift+Backspace self-destruct, Shift+/ launched craft, Shift+Tab backwards).
Ctrl is "fine / slow" (and mine / docking mode at the controls). Alt is the manoeuvre key (flip, broadside stance); Alt+K reverses
the sky cycle. A plain key never changes meaning with Shift unless the table says so.

## Camera and god view

| Action | Key | Where | Menu path |
|---|---|---|---|
| Swim | W A S D | command | - |
| Down / up | Q / E, or C / ] | command | - |
| Boost / fine | Shift / Ctrl | command | - |
| Swim speed / zoom | Wheel | command, chase | - |
| Look / orbit | Right-drag | command | - |
| Select, focus | Left-click, double-click | command | - |
| Focus selection | F | command | - |
| System view | Home | command | - |
| Planets (twice to focus) | 1-9, 0 | command | - |
| Cycle ships (back) | Tab (Shift+Tab) | command | - |
| Watch a battle | B | command | dev menu > Combat |
| Pause | Space, Pause | anywhere | - |
| Time warp slower / faster | `,` `.`; also `-` `=` and numpad - + | anywhere | dev menu > Time and scale |
| Back to now | T | command | dev menu > Time and scale |
| Gameplay / realistic scale | G, or backtick | anywhere | dev menu > Time and scale |
| Auto-reorient | R (tap) | command | - |
| Orbits / labels / hyperlanes / satellites | O / L / N / K | anywhere | - |
| Next sky (back) | Shift+K or numpad * (Shift+Alt+K or numpad /) | anywhere | sg.Skybox |
| Esc | close panels, leave focus, deselect, then the pause menu | anywhere | - |
| Swim camera tuning | pending (US-T184) | - | - |
| Focus / occluders | pending (US-T185) | - | - |

## Helm and flight

| Action | Key | Where | Menu path |
|---|---|---|---|
| Embody / let go | P | anywhere | dev menu > Helm |
| Spawn and pilot grid | V | command | dev menu > Spawn |
| Practice fighter | Shift+V | command | dev menu > Spawn |
| Steer | Mouse | controls | - |
| Thrust / telegraph | W / S | controls | - |
| Strafe, rise | A D, Q E | controls (capital: turn) | - |
| Lateral boost | Shift+A/D, Q/E | light fighters | - |
| Roll | Z / X | controls | - |
| Flight assist | F | controls | - |
| Flip (turn and burn) / broadside stance | Alt (hold) | controls / capital | - |
| Drift | R (hold) | fighters, launched craft | sg.Drift |
| Reactor power | Left / Up / Right / Down (Shift: all to one) | controls | - |
| Docking mode / drop a mine | Ctrl | freighters near a port / combat | - |
| Tactical cruise | J (hold) | helm, out of a fight | - |
| Cruise drive (spool, drop out) | Shift+J | helm | dev menu > Travel |
| Lane: come off / carry on | 1 / 2-9 | on a lane | - |
| Lane: steer in the tube | A D Q E | riding a lane | - |
| Interdiction: surge / drop out | Shift / Ctrl | riding a lane | - |
| Back into the lane | Y | helm | - |
| FTL spool | Enter, numpad Enter | helm, beyond the rim | - |
| Plot a course / clear | PgDn, PgUp / Del | helm, command | galaxy map |
| FTL autopilot | End | helm | Settings > FTL |
| Silent running | F3 | helm | - |
| Active ping (void stop: ping) | Tab | helm | - |
| Go dark / power up | Shift+F3 | helm | - |
| Derelict latch | 1, then 1-4 | alongside a wreck | - |
| Tractor beam | Shift+T | helm | sg.Tractor |
| Gas giant contract / raid a skimmer | Home / Shift+Home | helm at a giant | - |

## Combat

| Action | Key | Where | Menu path |
|---|---|---|---|
| Guns | Left mouse | controls | - |
| Lock hull | T | controls | - |
| Missiles / rockets | Right mouse | controls | - |
| EMP | Middle mouse | controls | - |
| Mine | Ctrl (press) | controls, fight | - |
| Retune phasers | Insert | controls | - |
| Run hot | Shift+Insert | big ships | - |
| Roll weak patches | Shift+Del | capital ships | - |
| Broadside | hold Left mouse | capital ships | - |
| Captain's orders | 1-9, 0 | capital ships | - |
| Boarding panel (picks) | B (1 2 3 0) | controls | - |
| Abilities (carrier launch, shuttle, pods) | Shift+1 ... Shift+0 | helm, in a fight | - |
| Squad stand-down | Shift + the bar's last number | band captain's helm | sg.StandDown |
| Weapon systems (US-T189) | no key of their own (the hardpoint fit, F2 outfitting) | - | F2 |

## Trade, law, comms, fleet

| Action | Key | Where | Menu path |
|---|---|---|---|
| Market | M | piloting near a settlement | - |
| Cargo hold | I | piloting | - |
| Trade desk | \ | anywhere | - |
| Contracts / bounties / outfitting | F7 / F4 / F2 | port in view | market > Outfit |
| News feed | X | command | - |
| Hail polity | F12 | anywhere | galaxy map > Diplomacy |
| Comms: hail any ship | Shift+H (Up/Down, Enter, 1-9, Esc/Left) | any controls | bottom drawer > Ships in area |
| Hail the locked ship | Shift+H (US-T201) | any controls | - |
| Fleet | F10 | anywhere | - |
| Next / previous seat | `'` / `;` | held unit | - |
| Man a gun | / | held unit | - |
| Launched craft | Shift+/ | carrier helm | - |
| Up to host | Backspace | gun, launched craft | - |
| Self-destruct | Shift+Backspace (hold 1 s) | any controls | - |
| Take-charge picker | F9 | anywhere | - |
| EVA | Shift+P | capital, station | - |
| Weld | Left mouse | worker | - |

## Screens, menus, system

| Action | Key | Where | Menu path |
|---|---|---|---|
| Help | H, F1 | anywhere | pause menu > Help |
| Ship catalog | - | - | Help > ship catalog (US-T193) |
| Dev menu | Shift+F1 | anywhere (local) | sg.CheatMenu |
| Spawn menu (dev menu on Spawn) | Shift+F2 (US-T186, landing) | anywhere (local) | dev menu > Spawn |
| Bottom drawer | Shift+U (US-T200, landing) | anywhere | handle, bottom left |
| HUD on / off | U | anywhere | - |
| Clear the screen | F11 | anywhere | - |
| Galaxy map | [ | anywhere | - |
| Quicksave / quickload / slots | F5 / F8 / F6 | anywhere | pause menu > Save / Load |
| Pause menu | Esc (last in the Esc chain) | anywhere | Resume, Help, Save / Load, Settings > Display, Main Menu, Quit |
| Showcase tour | - | - | main menu, dev menu > Showcase, sg.Tour; then Right/N, Left/B, S, Space, 1-9, O, Esc |
| Capture: screenshot, recording, replay | Alt+F9 / F10 / F11 | only while capture runs | - |

## Console

Open with the console key; `sg.BigBattle sg.BioMass sg.CapitalRoll sg.Cheat sg.CheatMenu sg.Comms sg.CruiseRun sg.DogfightDemo
sg.Drift sg.EdgeRun sg.EvaRepair sg.Ghost sg.Harvest sg.HullLighting sg.HyperspaceToll sg.LaneRide sg.Lighting sg.Race sg.Shine
sg.Sky* sg.Squadron sg.StandDown sg.Tour sg.Tractor sg.VoidStation sg.WarpRun sg.WarpTunnel`. Every dev-menu entry is also reachable as a console
command; the console is the only way to some tuning (`sg.Lighting`, `sg.Shine`, `sg.HullLighting`).

Staged checks (US-T218, US-T224), each logging its numbers under LogSpaceGod and, with `shot`, saving screenshots:

| Command | What it stages |
|---|---|
| `sg.Ghost [shot <file>] [quit]` | Two hulls on one line of sight, the far one focused: the near one drawn as the see-through occluder ghost (US-T185); logs each hull's materials; also `<file>-opaque.png` (focus let go) and `<file>-far-alone.png` |
| `sg.LaneRide [shot <dir>] [quit]` | A fighter flown into a space lane, steered about inside the tube (US-T196), then out past its wall; logs the offset, sideways speed and speed every half second, and the speed bleeding down to the hull's own every quarter second; `lane-1..4-*.png` |
| `sg.CapitalRoll [shot <dir>] [seconds=60] [preroll=90] [quit]` | The big battle off Uranus run on `preroll` battle seconds, the camera on the capital under the most fire; each second its roll, the threat's bearing and the shield field it shows the threat against its strongest face and every other roll (US-T187); `roll-NN.png` every 5 s |
| `sg.CruiseRun [shot <dir>] [quit]` | A fighter's helm in open space 1.3 AU out (US-T224): Shift+J spools the cruise drive (US-T127, US-T197), the mouse turns it at cruise for 6 s, Shift+J drops it out; then spooled twice more to be dropped by a hit on the shields and by a hull parked across its path, which is checked drawn clear of the helm's hull and off the camera's line through it (US-T226). Logs the regime, the drive's and the hull's speed, the heading and the distance covered every half second, and PASS/FAIL per check; `cruise-1..6-*.png` (with the HUD) |
| `sg.Squadron [shot <dir>] [quit]` | Two carriers off Uranus 84 km apart, the far one's wing held aboard (US-T224): one of the near one's fighter squadrons followed in formation (US-T198), the far carrier's interceptors put out 4 km ahead of it, the wingmen breaking into dogfights (US-T188), then forming up again. Logs every wingman's distance off its slot (right/up/ahead), speed, broken state, target and manoeuvre each second, and a summary; `squad-1..4-*.png` (with the HUD) |

## Findings and conflicts

- Reserved for work not yet landed, left free: Shift+F2 spawn menu and Shift+U drawer (in progress, listed above as landing),
  swim camera (US-T184), focus / occluders (US-T185). Hyper-lane steering (US-T196) reuses A D Q E while riding.
- No two plain keys are bound to conflicting actions in the same context; every shared key is split by `Arbitrate`, by Shift, or by
  a panel that owns the keys (`Input.Handover()` clears the frame once a panel consumes a key, so Shift+H does not also toggle
  Help and Shift+U does not also toggle the HUD).
- `Cpp/Config/DefaultInput.ini` (the user's, not edited here) has `bF11TogglesFullscreen=True` and `bAltEnterTogglesFullscreen=True`:
  the engine would toggle fullscreen on F11, which the game uses for clear-screen, and on Alt+Enter. Set `bF11TogglesFullscreen=False`.
  `ConsoleKeys=Tilde` is the same physical key as the game's backtick scale toggle (`Key::Backquote` maps to `EKeys::Tilde`);
  the console takes it first. Help still lists "G or backtick"; G is the reliable one.
