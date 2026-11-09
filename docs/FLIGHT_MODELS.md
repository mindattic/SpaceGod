# Flight models: the standardization audit

One function flies every hull in the game: `IFlightModel::Step(PilotState&, ShipCombat, HullDynamics,
PilotTuning, PilotInput, gravity, dt)` (`Cpp/Source/SpaceGodCore/Combat/FlightModels.h`). A family is a
way of flying, not a hull: every `ShipClass` belongs to exactly one `FlightFamily`
(`FlightModelRegistry::FamilyOf`), and every family flies through the one shared interface, never a
branch in `PilotFlight` itself. This page is the audit that confirms every family is actually wired
in — ship class, family, model, handling data and cursor-lead, in one table — since dozens of doc
comments across the combat code point here and the file itself had never been written (US-T287).

## Every class, its family, its model

| `ShipClass` | `FlightFamily` | `IFlightModel` | `PilotControl` |
|---|---|---|---|
| Fighter | LightFighter | `LightFighterFlightModel` (Fighter handling) | FreeAim |
| Interceptor, Recon | Interceptor | `LightFighterFlightModel` (Interceptor handling) | FreeAim |
| Courier | Runner | `LightFighterFlightModel` (Runner handling) | FreeAim |
| Gunship, GatePicket | HeavyFighter | `HeavyFighterFlightModel` | DirectNose |
| Bomber, AssaultShuttle | Bomber | `BomberFlightModel` | DirectNose |
| Freighter, Miner, Explorer, OreTug, CaptureTug, LaneTender | Hauler | `HaulerFlightModel` | Hauler |
| Carrier, Battleship, Cruiser, Destroyer, CommandCruiser, Frigate, Corvette, GateBreaker | Capital | `CapitalFlightModel` (per-class `CapitalHandling`, `CapitalHandling::Of`) | Helm |

`FlightModelRegistry::Default()` (`FlightModels.cpp`) is where this table actually lives in code: every
line above is an `Assign(class, family)` plus a `Register(family, model)`. No family falls through to
`FlightModelRegistry::Fallback` (the shared Newtonian model, `NewtonianFlightModel`) — every one of the
seven families has a model registered for it. A class not assigned at all would fly as a capital
(`FlightModelRegistry::FamilyOf`'s documented default), but every class in the catalog is assigned.

## Cursor lead: one question, two different mechanics

The MechWarrior-style "the crosshair gets ahead of the nose, and by how much depends on the hull" is
really two separate, standardized mechanics, not one:

1. **The string aim** (`PilotAim::StringFor`, US-T168): how far and how fast the *camera/crosshair*
   itself may run ahead of the nose as the mouse moves it, picked from `PilotControl`:
   - **FreeAim** (light fighter, interceptor, runner): the string is slack — `StringAim::MaxLeadDeg`
     defaults to 180 (anywhere, even dead astern) and the view turns as fast as the mouse moves. This
     *is* "spin around and already have the enemy under the crosshair before the hull turns to match":
     the view is free, and the hull (`LightFighterFeel::Whip`) chases it, its top swing multiplied up to
     `WhipFactor` (1.8 for dogfighters) once the aim is more than `WhipStartDeg` (30°) off the nose.
   - **DirectNose / Hauler** (heavy fighter, bomber, hauler): the string is short and taut —
     `PilotAim::HeavyMaxLeadDeg` (25°) at `HeavyLookRateFactor` (1.3×) of the hull's own turn rate, so the
     gap between where you look and where the nose points stays subtle. This is the "offset from turn to
     actual bearing is small" MechWarrior-cursor feel the heavy fighter asks for.
   - **Helm** (capital): no string at all — the mouse looks around the hull freely (command-view style)
     while the keys turn the hull; see `IFlightModel::AimLeadRad` below for what actually paces a
     capital's turn.

2. **`IFlightModel::AimLeadRad(hull, tuning)`** (a virtual on every model, US-T281): how far the
   *commanded heading* (`PilotInput.DesiredForward`, what nose-steering and the capital's helm actually
   feed the flight model) may run ahead of the real nose before the hull would overshoot braking its
   swing — a physics safety clamp, not the visible cursor-gap feel above. The default
   (`PilotFlight::AimLeadRad`) is the shared Newtonian brake distance (ω²/2α on `tuning.TurnRateDegS`);
   `HeavyFighterFlightModel` and `CapitalFlightModel` override it with the same formula on their own
   capped turn rate and spin-up instead, so the clamp matches the hull actually flying, not a generic
   number. **Only `DirectNose`, `Hauler` and `Helm` control ever read it** (`PilotAim::Nose`/`Helm`); the
   `FreeAim` string-aim path (`PilotAim::Look`) never does. That is why `LightFighterFlightModel`,
   `BomberFlightModel` and `HaulerFlightModel` have no override of their own: for the two that fly
   FreeAim (light fighter, interceptor, runner) it would be dead code with no caller; for the bomber and
   the hauler, nobody has yet needed a figure sharper than the shared Newtonian one, and the virtual
   means one can be added with no change anywhere else the day it is.

## Weapon groups: one input, every craft

Every craft a player can fire from reads the same numbered-group input (`Alt+1-9`, toggled per group
with `Alt+Shift+1-9`), fitted to what that craft actually carries:

- **Capital ships** (US-T279, `Broadsides::FireGroupMounts`): nine standing groups built from the
  hull's own layout — 1-6 its battery guns nearest each of the six hull-frame arcs
  (`FireCones::Bow/Stern/Starboard/Port/Dorsal/Ventral`), 7 its spinal mounts, 8 its missile and torpedo
  launchers, 9 every weapon together. `Alt+1-9` fires one at the locked target now, wherever it bears,
  apart from the look-direction trigger (`Battle::HelmVolley`) and whatever arc order (`BatteryOrder`,
  US-T276's fourth state, `Chain`) its mounts stand under; `Alt+Shift+1-9` toggles that group between
  chained fire (`Broadsides::GroupChainFires`, one mount a press) and a volley (every bearing, loaded
  mount together).
- **Launched fighters, interceptors, heavy fighters and bombers** (US-T284,
  `EmbodimentInput::FireGroup`): a fighter's one gun mount (`SubsystemKind::FighterGuns`, always a
  single hardpoint — there is nothing to split into arcs the way a capital's turrets are) stays on the
  primary trigger; `Alt+1-9` instead picks which guided weapon (`WeaponGroups::Secondaries`: missiles,
  torpedoes, rockets, hive) the second trigger fires, directly instead of only cycling one at a time on
  the plain `1` key (`FighterSeat::CycleSecondaryGroup`, still available).
- **Directly piloted, non-capital hulls with more than one direct-fire mount** (US-T289,
  `Battle::PlayerWeaponGroupFires`): the Gunship and GatePicket (`FlightFamily::HeavyFighter`) are not
  single-gun craft the way the rest of the small-craft roster is — their layout carries a spinal
  railgun fixed along the keel plus independently-cued turrets (`CombatSpecs.cpp`'s Gunship entry:
  `LightRailgun`, two `LaserTurret`s, a `MissileLauncher`). Left mouse still fires every direct-fire
  mount that bears, as always; `Alt+1-9` additionally fires one specific mount on its own, numbered in
  hull-layout order, so a railgun can be held back from the constant fire-at-will and loosed only when
  it is worth the shot. A hull of one such mount (every ordinary fighter, interceptor, courier) never
  answers this: there is nothing to pick among, and the main trigger already fires it.

`Alt` and `Alt+Shift` were picked deliberately, not arbitrarily: Ctrl is already bound to `Slow` (held:
fine control / docking mode) and `DropMine` (an edge that spends a mine), so `Ctrl+digit` would dock or
spend a mine on every press. `Alt` (`Manoeuvre`, held) and `Shift` (`Boost`, held) spend nothing, so
sharing them for the one frame of a tap is harmless — the same reasoning the existing `Shift+digit`
ability-slot system (`UnitySpaceApp.Embodiment.cpp`) already relies on.

## Tickets

U.SPC-T17/U.SPC-T59 (the original Newtonian model and the `IFlightModel` split), US-T79/US-T80/US-T81
(the player-facing feel: the light fighter's snap and dash, the heavy fighter's weight and surge, the
capital's broadside translation), U.SPC-T440/U.SPC-T441/U.SPC-T484/U.SPC-T499 (the light fighter, heavy
fighter, interceptor/bomber/runner and capital handling tables), US-T276 (chain fire), US-T279/US-T284/US-T289
(weapon groups), US-T281 (`AimLeadRad` standardized onto `IFlightModel`), US-T287 (this page).

Note on prefixes: `U.SPC-Txxx` is this feature line's own older ticket series, named that way in the
source comments it is cited from; `US-Txxx` is the current `tickets` tool's series. Both are carried
here unchanged from wherever each fact was sourced, rather than one guessed into the other's scheme.
