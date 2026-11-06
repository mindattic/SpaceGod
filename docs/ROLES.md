# Roles x features (US-T167)

Every system has to work with every way the player can be embodied without colliding with it. This is the matrix: what each
feature does for each role, the rules that keep two features from fighting over a key or a camera, and what the audit
(`-SpaceGodAudit -SpaceGodAuditFacade`, probes `roles.*` in `Smoke/FeatureAudit.cpp`) checks. Key detail lives in
`docs/Controls.md`; this file is about meaning per role.

Marks: **yes** = works as described, **no** = deliberately does nothing (and says so where it matters), **n/a** = the role has
no such thing, **pending (US-Txxx)** = being built by another ticket and not on main yet: the cell is the intended behaviour,
not audited.

## The roles

| Role | How you get it | Code |
|---|---|---|
| Free SpaceGod (command view) | nothing held; P lets go of anything | `AtControls() == false` |
| Fighter (light: fighter, interceptor, courier runner; bomber) | P on a hull, Shift+V practice fighter, V spawn-and-pilot (pending US-T186) | `ShipHelm`, `FlightFamily::LightFighter / Interceptor / Bomber / Runner` |
| Heavy (gunship) | P on a gunship | `FlightFamily::HeavyFighter` |
| Capital (battleship, cruiser, destroyer) | P on a hull | `FlightFamily::Capital`, `CapitalHandling`, `CapitalManeuver` |
| Carrier | P on a carrier | capital handling plus `CarrierWings`, `FlightDecks` |
| Hauler (freighter, miner) | P on a hull | `FlightFamily::Hauler`, `HaulerFlight` |
| Station | `'` / `;` / `/` onto a settlement defence platform or a gate fort | `StationSeat` |
| Turret | `/` onto a gun mount of a hull or station in a fight | `MountedTurret` |
| Launched craft | Shift+/ from a carrier helm | `FighterSeat` |
| EVA worker | Shift+P from a capital or station | `RepairWorker`, `CycleWorker` |
| Satellite | take-charge picker (F9) | `SatelliteSeat` |
| Ejected pilot | not embodiable: a battle's ejected pilots are pod clusters (`Crews`, `StrandedWings`) the player rescues; no seat, no pending ticket | n/a |

"At the controls" is any of the seats above (the helm included). `InputState::Arbitrate` hands each shared key to one owner per
frame: the craft's at the controls, the command view's otherwise.

## Matrix

| Feature | Free SpaceGod | Fighter | Heavy | Capital / carrier | Hauler | Station / turret | Launched craft | EVA worker | Satellite |
|---|---|---|---|---|---|---|---|---|---|
| Camera | free swim (pending US-T184 swim tuning); F / double-click focus | chase, locked on the hull (`PilotLocked`) | chase | chase, wider | chase | the mount's rig (down the barrel) | chase on the craft | shoulder rig | boresight rig |
| Mouse | look (right-drag), select | string mouse aim (US-T129/T168): nose follows a slack string, left fires | taut string, short lead | steers the heading target, broadside on hold | helm | aims the barrel | as fighter | torch (left = weld) | points the boresight |
| W A S D Q E | swim the camera | thrust, strafe, rise (Shift: lateral boost on light hulls) | same, no boost | W S the engine telegraph, A D Q E turn | telegraph | no | as fighter | walk the hull | station-keeping |
| Roll (Z / X) | no | roll | roll | roll; Shift+Del rolls weak patches round; the helm rolls to turn damaged facings away | roll | no | roll | no | no |
| Flight model | n/a | Newtonian dogfight (US-T188): facing apart from velocity, catalog turn, roll, thrust and braking; named manoeuvres | heavier, same model | `CapitalFlightModel`: limited turn rate, come-about when the engaged broadside is depleted (US-T187) | `HaulerFlight`: slow, deliberate | no motion | as fighter | no | station-keeping |
| Flight assist (F) | no: F focuses | toggles assist | toggles | toggles | toggles | no | toggles | no | no |
| Flip (Alt hold) | no | turn and burn | turn and burn | broadside stance | no | no | turn and burn | no | no |
| Drift (R hold) | no (R taps auto-reorient) | yes | yes | no | no | no | yes | no | no |
| Reactor power (arrows) | no | yes | yes | yes | yes | n/a | no | no | no |
| Cruise drive (Shift+J), tactical cruise (J) | no | yes, out of a fight | yes | yes | yes (long haul) | no | no | no | no |
| Drive regime (`DriveRegime`: Normal, CruiseSpool, Cruise, Lane, Ftl) | n/a | all five | all five | all five | all five | n/a | Normal | n/a | n/a |
| Hyper-lane flight (caae6e2, tunnels 33f7c52) | no | ride, steer A D Q E, surge / drop out Shift / Ctrl | yes | yes | yes | no | no | no | no |
| Lane wiring (US-T196) | pending | pending | pending | pending | pending | n/a | n/a | n/a | n/a |
| Hyperspace toll (ed7979f) | n/a | paid on entry | paid | paid | paid (cargo-weighted) | n/a | n/a | n/a | n/a |
| FTL (Enter, PgDn, End) | no | yes, past the rim | yes | yes | yes | no | no | no | no |
| Tractor (Shift+T) | no | yes (cargo pods) | yes | yes | yes | no | no | no | no |
| Time dial (`,` `.` Space) | yes | yes; held at real time in a fight or when hunted | same | same | same | yes (a seat in a fight holds real time while the battle steps) | same | same | yes |
| Reset time (T) | yes | no: T locks the hull | no | no | no | no | no | no | no |
| Scale (G) and orbit lines (O), labels (L), lanes (N), satellites (K) | yes | yes (view only) | yes | yes | yes | yes | yes | yes | yes |
| Focus (F, 1-9, Home, Tab, double-click, drawer, panel rows) | yes; (US-T185 occluders pending) | never: the camera is the craft's | never | never | never | never | never | never | never |
| HUD | command HUD, selection panel | flight HUD, sights, power, lock | same | helm HUD, broadside, captain's orders | helm, market, cargo | gun reticle, station panel | flight HUD | repair panel, weld bar | satellite panel |
| Bottom drawer (Shift+U, US-T200) | pending: lists everything | pending: hail / cruise to a row, no focus | pending | pending | pending | pending | pending | pending | pending |
| Comms (Shift+H hail any ship) | no | yes | yes | yes | yes | yes | yes | yes (host's) | no |
| Hailing the locked ship (US-T201) | no | yes | yes | yes | yes | n/a | yes | n/a | n/a |
| Weapons (weapon families, US-T189) | no | guns, missiles, EMP, mine | same | broadsides, captain's orders | defensive | station's guns / the barrel | craft's guns | n/a | n/a |
| Lock target (T), boarding (B), self-destruct | no | yes | yes | yes | yes | self-destruct yes | yes | no | no |
| Squadrons (922e43d) | n/a | band captain: Shift+last number stands the band down | same | commands the wings | n/a | n/a | wingman | n/a | n/a |
| Carrier launch (Shift+1..) | n/a | n/a | n/a | yes (carrier) | n/a | station hangars: second trigger | n/a | n/a | n/a |
| Biomass visuals (US-T192), economy / territory (US-T190), fighter visibility (US-T202), spawn-and-pilot (US-T186) | pending | pending | pending | pending | pending | pending | pending | pending | pending |
| Ship catalog handling (7d618e9) | n/a | each hull flies its catalog turn, roll, thrust and braking | same | same | same | n/a | craft's catalog | n/a | n/a |
| Esc | closes panels, then leaves focus, then deselects, then the pause menu | panels, then the pause menu (never leaves the chase) | same | same | same | same | same | same | same |

## Rules that keep the features apart

1. **One owner per input.** At the controls F, T, Backspace, B, X, Tab, R and the number keys are the craft's; in command the
   flight keys (assist, lock, flip, drift, power, self-destruct, ping, cruise, tractor) do nothing (`InputState::Arbitrate`).
2. **Focus never touches a craft's camera.** F, 1-9, Home, Tab, the map's double-click, panel rows and the drawer's rows all
   refuse at the controls (`AtControls()`, not `IsPiloting()`: a gunner or worker is held too). A planet is never grabbed as a
   fighter.
3. **Time.** T (back to now) and the rebuild it can cause refuse at the controls. A helm in a fight holds real time; a hunted
   helm holds real time; lane rides and FTL set the warp for the crossing and hand it back.
4. **Drive regimes are exclusive.** Cruise refuses in a fight, on a lane, between the stars, with the drives out or powered
   down, and deep in a gravity well. The tractor beam refuses (and lets go) in the same regimes it can't work in: on a lane,
   under the cruise drive, between the stars, powered down, latched on a wreck.
5. **Drift is a fighter's.** Light and heavy fighters, bombers, launched craft; a capital or hauler cancels it.
6. **Capital handling.** Turn rate is limited by its catalog; it rolls to turn a damaged facing away and comes about when the
   engaged broadside is depleted; it does not drift, flip-and-burn or lateral-boost.
7. **Panels own the keys.** Comms, the dev menu, the drawer and the tour consume their keys (`Input.Handover()`), so Shift+H
   does not also toggle Help.

## Audit

`roles.*` probes (opt-in with `-SpaceGodAuditFacade`, after `input.*`):

| Probe | Checks |
|---|---|
| `roles.fighter / heavy / capital / carrier / hauler .family` | the hull's `FlightFamily` is the one this matrix says |
| `roles.<hull>.focus-never-grabs` | with Earth selected, F, 3 and Home leave the chase locked on the hull |
| `roles.<hull>.drift` | R held drifts a fighter / heavy and never a capital, carrier or hauler |
| `roles.turret / worker / launched-craft .focus-never-grabs` | the same keys leave a seat's camera alone (INFO when no seat could be taken) |
| `roles.free.flight-keys-idle` | Shift+J and Shift+T in command change nothing |
| `roles.free.focus-grabs` | F with Earth selected focuses it |

## Findings from the audit (US-T167)

Fixed here:

- The tractor beam could be locked, and kept, on a lane, under the cruise drive, between the stars and powered down
  (`UnitySpaceApp.Tractor.cpp`): now refused, and a held beam lets go when the regime changes.
- A double-click on a row of the HUD selection list focused the command camera for anyone not at the helm, so a gunner or
  worker lost their seat's view (`UI/Hud.Panels.cpp`): now `AtControls()`.

Filed as tickets (files other agents are editing):

- Bottom drawer rows focus the command camera for a seated player, and its cruise branch only runs for a helm
  (`UI/Hud.Drawer.cpp` checks `IsPiloting()`, not `AtControls()`).

Not changed, by design: a seat in a fight can raise the time warp (the battle steps in sim seconds and the seat's rig follows
it; only a piloted hull is held at real time because it flies in real seconds).
