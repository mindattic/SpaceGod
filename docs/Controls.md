# Controls inventory (US-T183)

One table per area: action, key, where it works, menu path. The in-game Help screen (`Core/ControlsHelp.cpp`) is the player-facing
copy of this file; when a key changes, change both. Keys are read in `Input/InputState.cpp` (the shared per-frame poll, left and
right Shift / Ctrl / Alt both count), plus the modal panels that own the keys while open (dev menu `UI/CheatMenu.cpp`, comms
`UnitySpaceApp.Comms.cpp`, bottom drawer `UI/Hud.Drawer.cpp`, tour `Tour/ShowcaseTour.cpp`, gas-giant contract
`HarvestPlay.cpp`). The keys (and the mouse's and pad's bindings) are the action map's, `Input/KeyBindings.h`, rebindable under
Settings > Controls > Key bindings (see Bindings below); the input seam (`Input/UnrealInputDevice.cpp`) reads the keyboard, the mouse
and the pad.
`Cpp/Config/DefaultInput.ini` holds engine settings only, no action or key mappings.

**Command view** = the free SpaceGod camera. **Controls** = at any seat (helm, fighter, station, gun, worker). `InputState::Arbitrate`
decides which meaning of a shared key stands: at the controls F, T, Backspace, B, X, Tab (cycle), R (tap) are the craft's; in
command, flight keys (flight assist, lock, flip, drift, power, self-destruct, ping, cruise, tractor) do nothing.

**Modifier convention.** Shift makes the "bigger / other" sibling of a key (Shift+F1 dev menu next to F1 help, Shift+V practice
fighter, Shift+K next sky, Shift+P EVA, Shift+T tractor, Shift+J cruise drive, Shift+F3 go dark, Shift+H comms, Shift+U drawer,
Shift+Insert run hot, Shift+Del roll patches, Shift+Backspace self-destruct, Shift+/ launched craft, Shift+Tab backwards).
Ctrl is "fine / slow" (and docking mode at the controls) when held on its own, and the roll's modifier: Ctrl+A / Ctrl+D roll, and a held binding needing more modifiers takes its key from a plainer one while they are held (Ctrl+A rolls only; A alone strafes; Ctrl held as the roll's modifier is not fine movement). A mine is Ctrl+M (US-T445); Z and X are free for systems, power and quick-use slots. Alt is the manoeuvre key (flip, broadside stance); Shift+Alt+K
reverses the sky cycle. A plain key never changes meaning with Shift unless the table says so.

## Bindings (US-T249, US-T437)

Settings > Controls > Key bindings lists every action by group (Move, Flight, Combat, Sensors, Reactor, Travel, Seats, Command,
Time, View, Screen, Game), each with three slots, **Primary**, **Secondary** and **Tertiary**, and Reset per action and for the page.
Click a slot (or pick the row with Up / Down, the slot with Left / Right, and press Enter; on the pad, A on it) and press anything:

- a key, with Shift / Ctrl / Alt held for a chord (a modifier pressed and let go alone binds the modifier itself);
- a mouse button: the side buttons (Mouse 4, Mouse 5) and the middle as they are, the left and right only with a modifier held
  (alone they fire, select and look, and press the page's buttons); a wheel step up or down, with modifiers too;
- a pad button, a trigger (at the pad's own click) or a stick pushed one way (past 60% to be taken; it then counts past 50%).

Esc cancels; so does waiting 6 s (a pad has no Esc: its B binds). Clear empties the slot (Backspace on the picked slot does it
from the keys). Under each slot its **trigger mode** steps on a click (M from the keys): **Press**, **Hold**, **Double-tap**,
**Long press**. Each action has its own mode (a held action, thrust or boost, is Hold; a toggle or a screen is Press), shown unlit;
another mode is lit:

| Mode | A held action (thrust, roll, boost, fire...) | A pressed action (a toggle, a screen...) |
|---|---|---|
| Press | a tap latches it on, the next off | fires on the press |
| Hold | held while the input is | fires on the press and again on the release (on only while held) |
| Double-tap | the second press within 0.3 s holds it while that press is held | fires on the second press within 0.3 s |
| Long press | held from 0.5 s into a hold until let go | fires once a hold reaches 0.5 s |

A clash (an input another action live in the same place answers to with the same touch) is named under the action as
"CLASH: ...". Pairs shared by default (F focus / flight assist, T reset time / lock, pad Y focus / lock...) are told apart by where
each is live, and a double-tap or long press on an input is told apart from its press, so neither is flagged.

The pad's buttons are the actions' default bindings beside the keys (they were a map of their own before US-T437): fire RT, second
trigger LT, roll LB / RB (sink / rise in command), boost A (a mech's jets), drift B (back out in command), flip X, lock target and
focus Y, flight assist RS click, embody LS click, back / pause menu Menu, help View. Fixed: Esc, the number keys, the left and right
mouse buttons' own fire, select and look, the sticks' movement and aim, the d-pad's reactor / time / ship steps, and the pad in menus.

**HUD mode** (`KeyAction::HudMode`, `InputState::HudMode`): switches between the minimal HUD (how a game starts) and the full HUD with its
cursor and pull-out drawers on every edge (US-T439; docs/HUD.md).
The plan named Z, but Z is roll left at the controls (and every plain key is taken), so it defaults to **Caps Lock** (a key added for
it, with the numpad's digits and point); bind Z (or anything) to it on the page.

The text a profile keeps for an action (`controls.key.<action>` in the Key bindings group; `Input/BindingCore.h`):
bindings joined by ", ", `None` when every slot is empty. One binding is `[Mode:]input`, the mode one of `Press`, `Hold`,
`DoubleTap`, `LongPress` (none: the action's own), the input `[Ctrl+][Shift+][Alt+]KeyName` (`T`, `Shift+T`, `NumpadMultiply`),
`[mods]MouseLeft|MouseRight|MouseMiddle|Mouse4|Mouse5|WheelUp|WheelDown`, `Pad:A|B|X|Y|LB|RB|View|Menu|LS|RS|DPadUp|DPadDown|DPadLeft|DPadRight`,
`Pad:LT|RT[>NN]` or `Pad:LeftStickUp ... Pad:RightStickRight[>NN]` (`>NN`: past NN%). Examples: `Shift+K, NumpadMultiply`,
`Mouse4, DoubleTap:Pad:A`, `LongPress:Shift+R`. A line from before US-T437 (keys only, no `Pad:`) loads exactly as it did and gains
the action's default pad binding in a free slot, as the old pad map still worked; once a player clears an action's pad binding,
`Pad:None` is written to keep it cleared. The double-tap window, the long-press time, a stick's default threshold, the capture
threshold and the listening timeout are `BindingTuning` (`Input/BindingCore.h`; the one instance is `KeyBindings::Tuning()`).
The pure part is unit-checked by `BindingCoreTests` (tools/build-core.ps1); `sg.Bindings [quit]` screenshots the page.

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
| System view | Home, Backspace | command | - |
| Planets (twice to focus) | 1-9, 0 | command | - |
| Cycle ships (back) | Tab (Shift+Tab) | command | - |
| Watch a battle | B (command only; at the controls B is the boarding panel and Shift+B locks an attacker) | command | dev menu > Combat |
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
| Roll | Ctrl+A / Ctrl+D (US-T445; A/D alone still strafe) | controls | - |
| Flight assist | F | controls | - |
| Flip (turn and burn) / broadside stance | Alt (hold) | controls / capital | - |
| Drift | R (hold) | fighters, launched craft | sg.Drift |
| Reactor power | Left / Up / Right / Down (Shift: all to one) | controls | - |
| Docking mode (held) / drop a mine | Ctrl held / Ctrl+M (US-T445) | freighters near a port / combat | - |
| Tactical cruise | J (hold) | helm, out of a fight | - |
| Cruise drive (spool, drop out) | Shift+J | helm | dev menu > Travel |
| Lane: come off / carry on | 1 / 2-9 | on a lane | - |
| Lane: steer in the tube | A D Q E | riding a lane | - |
| Interdiction: surge / drop out | Shift / Ctrl | riding a lane | - |
| Back into the lane | Y | helm | - |
| Eject in a pressure suit | Shift+Y | launched craft's cockpit | - |
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
| Lock attacker (again: the next one engaging you) | Shift+B (US-T307; controls only, B in command watches a battle) | controls | - |
| Missiles / rockets | Right mouse | controls | - |
| EMP | Middle mouse | controls | - |
| Mine | Ctrl (press) | controls, fight | - |
| Retune phasers | Insert | controls | - |
| Run hot | Shift+Insert | big ships | - |
| Roll weak patches | Shift+Del | capital ships | - |
| Broadside | hold Left mouse | capital ships | - |
| Captain's orders | 1-9, 0 (5 line ahead, 6 line abreast, 7 echelon and 7 again a wedge, 8 screen, 9 engage at will; US-T372) | capital ships | - |
| Boarding panel (picks; 4 sends boarders over the hull, US-T82) | B (1 2 3 4 0) | controls | - |
| Abilities (carrier launch, shuttle, pods) | Shift+1 ... Shift+0 (a bar of more than 10 orders is paged nine at a time: Shift+1 ... Shift+9 the page shown, Shift+0 the next page; US-T359) | helm, in a fight | - |
| Squad stand-down | Shift + the bar's last number | band captain's helm | sg.StandDown |
| Pirates demand your cargo: hand it over / refuse | 1 / 2 | while the demand stands | - |
| Pirate haven's defences down: board and take it / destroy it | 1 (marines aboard) / 2 (guns or marines) | at the haven | - |
| Mech out of the bay | Shift+N | in a fight, capital or station held | - |
| Weapon systems (US-T189) | no key of their own (the hardpoint fit, F2 outfitting) | - | F2 |

## Trade, law, comms, fleet

| Action | Key | Where | Menu path |
|---|---|---|---|
| Market | M | piloting near a settlement | - |
| Cargo hold | I | piloting | - |
| Trade desk | \ | anywhere | - |
| Trade desk: next / previous page, scroll the runs | Tab / Shift+Tab, Page Up / Page Down on Runs (US-T329) | trade desk open | the page buttons |
| Contracts / bounties / outfitting | F7 / F4 / F2 | port in view | market > Outfit |
| News feed | X | command | - |
| Hail polity | F12 | anywhere | galaxy map > Diplomacy |
| Comms: hail the locked ship, or any ship (US-T201) | Shift+H (Up/Down, Enter, 1-9, Esc/Left) | any controls | bottom drawer > Ships in area |
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
| Encyclopedia: next / previous article, jump | Up / Down, Page Up / Page Down / Home / End (not while typing in the search) (US-T336) | Encyclopedia | - |
| Encyclopedia / Discoveries: search | Ctrl+F (US-T394) | Encyclopedia, Discoveries | - |
| Manual: next / previous chapter, scroll it | Up / Down (the contents follow), Page Up / Page Down, Home / End (US-T324) | manual | - |
| Ship catalog | - | - | Help > ship catalog (US-T193) |
| Ship catalog: next / previous hull, jump | Up / Down, Page Up / Page Down / Home / End (US-T302) | ship catalog | - |
| Ship catalog: next / previous category tab | Right / Left (US-T302) | ship catalog | - |
| Ship catalog: mark the hull picked to compare (again: unmark); any other hull picked then shows beside it | C (US-T340) | ship catalog | the Compare button over the hull |
| Society screen: mark the thing picked (again: clear); another person, house, organisation or business picked then shows beside it | C (US-T344; not while typing in the search) | Society screen | the Mark button under THE SPACEGOD'S HAND |
| Society screen: the next sort of the Houses, People, Organisations or Worlds list (founding or renown, name, age, wealth or size, reputation or strength...) | S (US-T354; not while typing in the search) | Society screen | the Sort button by the list's heading |
| Dev menu | Shift+F1 | anywhere (local) | sg.CheatMenu |
| Spawn menu (dev menu on Spawn) | Shift+F2 (US-T186, landing) | anywhere (local) | dev menu > Spawn |
| Bottom drawer | Shift+U (US-T200, landing) | anywhere | handle, bottom left |
| Notice history: the last 100 notices with their game time; F (Shift+F back) steps the kind shown | Shift+X (US-T345) | anywhere the drawer shows | the drawer's Notices tab |
| Battle plot on / off: a top-down plot at the left edge of every hull around you by side, your lock ringed and your fire, paint and cripple calls marked | Shift+F10 (US-T367) | in a fight | the plot (on by default) |
| HUD on / off | U | anywhere | - |
| Clear the screen | F11 | anywhere | - |
| Galaxy map | [ | anywhere | - |
| System map | Shift+[ | anywhere | - |
| Dock / port screen | Shift+M | alongside a port | - |
| Recovered logs | Shift+L | anywhere | - |
| Discoveries | Shift+O | anywhere | - |
| Encyclopedia | Shift+I | anywhere | - |
| Society screen | Shift+G | anywhere | - |
| Galaxy map: find a system or star | Ctrl+F, then Up / Down / Enter (US-T306) | galaxy map | the field at the map's top left |
| Galaxy map: pin the system picked (again: unpin) / go to a pin | Ctrl+1..9 / 1..9 (US-T377; not while typing in the find; Ctrl held over the map drops no mine) | galaxy map | the PINS panel at the map's bottom left |
| Galaxy map: centre on the system you are in | Home (US-T393; not while typing in the find) | galaxy map | the Here button by Reset |
| Take-charge picker / save panel: move through the seats or slots, take the seat or load the slot | Up / Down, Enter (US-T393) | picker (F9) / save panel (F6) | the row clicked |
| System map: find a world, station, gate, ship or field | Ctrl+F, then Up / Down / Enter (US-T310) | system map | the field at the map's top left |
| Quicksave / quickload / slots | F5 / F8 / F6 | anywhere | pause menu > Save / Load |
| Rewind (again within a few seconds: further back) | Shift+F8 (US-T99) | anywhere | sg.Rewind |
| Hold to rewind: real time, the last seconds play backward (easing up to 3x, up to 10 s, never past the last rewind); let go to play on from there; then a 10 s rest | Shift+R held (US-T409, US-T410; the frame is the rewind's, so R's drift does nothing meanwhile) | flying a hull or watching a fight, single-player | - |
| Pause menu | Esc (last in the Esc chain) | anywhere | Resume, Help, Save / Load, Settings > Display, Main Menu, Quit |
| Pause menu: pick / press a button | Up / Down or W / S, then Enter or Space; Left / Right on the leave question, Stay first (US-T311) | pause menu | - |
| Mechlab: pick a hardpoint or socket, pick what to fit, fit it | Up / Down, Left / Right, Enter (US-T331) | Mechlab view of the outfitting panel (F2) | the mouse |
| Port screen: open a tab | 1-9, 0: the tabs from the left (US-T335) | docked, port screen open (Shift+M) | the tab strip |
| Settings: pick / change / reset a setting, step the groups, search | Up / Down pick, Left / Right change, Enter or Space flips (or listens, on a key binding), Delete resets, Page Up / Page Down group, Ctrl+F search (Enter or Down leaves it) (US-T322); on Key bindings Left / Right pick the slot, Enter listens, M steps its mode, Backspace clears it (US-T437) | Settings page | the mouse, the pad |
| Main menu: pick / press a button | Up / Down or W / S, then Enter or Space (US-T314) | main menu, its Co-op and Extras pages | - |
| New Game page: starting situation, start | Left / Right (Up / Down a row) while no field is typed in, Enter to start or host (US-T317) | New Game setup page | - |
| Showcase tour | - | - | main menu, dev menu > Showcase, sg.Tour; then Right/N, Left/B, S, Space, 1-9, O, Esc |
| Capture: screenshot, recording, replay | Alt+F9 / Alt+F10 / Alt+F11 (in place of the picker, fleet and clear screen) | only while capture runs | - |

## Console

Open with the console key; `sg.BigBattle sg.BioMass sg.CapitalRoll sg.Cheat sg.CheatMenu sg.Comms sg.CruiseRun sg.DogfightDemo
sg.Drift sg.EdgeRun sg.EvaRepair sg.Ghost sg.Harvest sg.HullLighting sg.HyperspaceToll sg.LaneRide sg.Lighting sg.Race sg.Shine
sg.Sky* sg.Squadron sg.StandDown sg.Tour sg.Tractor sg.VoidStation sg.WarpRun sg.WarpTunnel`. Every dev-menu entry is also reachable as a console
command; the console is the only way to some tuning (`sg.Lighting`, `sg.Shine`, `sg.HullLighting`).

Staged checks (US-T218, US-T224), each logging its numbers under LogSpaceGod and, with `shot`, saving screenshots:

| Command | What it stages |
|---|---|
| `sg.Bindings [quit]` | Settings > Controls > Key bindings opened (US-T437): the top, the Combat group, the View group with the HUD mode bound to Z, Mouse 4 and a double-tapped stick (and Z's clash with roll left), and a slot listening; the HUD mode's binding is put back after; `Saved/Screenshots/bindings-*.png` |
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
