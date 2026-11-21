# Gameplay-scale proportion sheet (US-T179)

`ScaleModel::ShipLength` is now one linear map for all hardware (ships, stations, gates, harvesters, wrecks, debris, pods):
realistic = km/1000 units; gameplay = 7 units/km. The old gameplay map was 12*km^0.5, which squeezed a 43x length ratio into 6.5x.

Only hardware-vs-planet (R^0.45) and orbital distances (AU^0.55, local p=0.44) are compressed. All views listed below call `ShipLength`:
StructureViews, HarvesterView, GraveyardView, DebrisFields, GateView, CargoPodsView, PodBeacons, EmbeddedPods, FighterViews, BioMassView, main ship render.

| Hardware | Length km | Realistic units | Gameplay units | Ratio to carrier |
|---|---|---|---|---|
| Carrier | 3.2 | 0.0032 | 22.40 | 1:1.0 |
| Command cruiser | 1.6 | 0.0016 | 11.20 | 1:2.0 |
| Battleship | 2.4 | 0.0024 | 16.80 | 1:1.3 |
| Gate breaker | 2.0 | 0.0020 | 14.00 | 1:1.6 |
| Cruiser | 1.2 | 0.0012 | 8.40 | 1:2.7 |
| Defence platform | 1.4 | 0.0014 | 9.80 | 1:2.3 |
| Freighter | 0.6 | 0.0006 | 4.20 | 1:5.3 |
| Destroyer | 0.45 | 0.0004 | 3.15 | 1:7.1 |
| Frigate | 0.32 | 0.0003 | 2.24 | 1:10.0 |
| Ore hauler | 0.3 | 0.0003 | 2.10 | 1:10.7 |
| Lane tender | 0.26 | 0.0003 | 1.82 | 1:12.3 |
| Explorer | 0.22 | 0.0002 | 1.54 | 1:14.5 |
| Corvette | 0.22 | 0.0002 | 1.54 | 1:14.5 |
| Ore tug | 0.2 | 0.0002 | 1.40 | 1:16.0 |
| Gunship | 0.14 | 0.0001 | 0.98 | 1:22.9 |
| Bomber | 0.14 | 0.0001 | 0.98 | 1:22.9 |
| Assault shuttle | 0.12 | 0.0001 | 0.84 | 1:26.7 |
| Gate picket | 0.12 | 0.0001 | 0.84 | 1:26.7 |
| Capture tug | 0.12 | 0.0001 | 0.84 | 1:26.7 |
| Fighter | 0.1 | 0.0001 | 0.70 | 1:32.0 |
| Courier | 0.08 | 0.0001 | 0.56 | 1:40.0 |
| Recon | 0.08 | 0.0001 | 0.56 | 1:40.0 |
| Interceptor | 0.07 | 0.0001 | 0.49 | 1:45.7 |
| Breacher round | 0.06 | 0.0001 | 0.42 | 1:53.3 |
| Cargo pod | 0.014 | 0.0000 | 0.10 | 1:228.6 |
| Gate (GateView) | 25 | 0.0250 | 175.0 | 7.8x |
| Station ring (StructureViews) | 10 | 0.0100 | 70.0 | 3.1x |
| Gateway (transfer station) | 10 | 0.0100 | 70.0 | 3.1x |
| Habitat | 9 | 0.0090 | 63.0 | 2.8x |
| Shipyard | 8 | 0.0080 | 56.0 | 2.5x |
| Depot | 6 | 0.0060 | 42.0 | 1.9x |
| Factory station | 4.5 | 0.0045 | 31.5 | 1.4x |

Both scales share identical ratios between rows (blend interpolates in log space between two linear maps, so every blend does too). Earth for reference: radius 255 units gameplay, 6.371 realistic.

## The world drawn round the seat held (US-T434)

The gameplay units above are the free view's. Taking a seat draws the world round what is inhabited: each seat declares
the true size of the thing (`IEmbodiable::InhabitedSizeKm`: a hull's or station's length, a gun its hull's, a person
1.8 m, a mech its height, a satellite 8 m; the helm is the flown hull's length), which gives a **seat zoom**
(`ScaleModel::SeatZoomFor`):

    zoom = clamp((size / 4 km) ^ 0.4, 0.04, 1)          (1 with no seat: the free view)

The zoom shrinks everything near a world relative to the world itself, with two mappings, both in `ScaleModel`:

- **Hardware**: `ShipLength` draws every hull, station, gate, wreck and battle (and the battles' own km, `BattleKmScale`)
  at zoom x 7 units/km, so the table's ratios between rows never change.
- **Altitude**: `LocalDistance` multiplies the drawn height of hardware above a planet's or moon's surface by a factor that
  is the zoom at the surface and eases (smoothstep in true distance) to 1 at the edge of its neighbourhood
  (`NeighbourhoodKm`, where its parked hulls, stations and battles are laid out). The worlds themselves never move:
  `Layout` (every body's place) and `BodyLocalOffset` (orbit lines, discs) use the unzoomed mapping, the Sun's mapping is
  never zoomed, and no body's drawn radius changes.

Every camera rig is sized in the held thing's drawn radii, so from the seat the held thing reads as before and the world
round it swells: the camera, its hull and its neighbours sit proportionally lower over the world, which fills more of the
sky. Hardware shrinks at least as much as the altitude, so nothing is drawn nearer a world than in the free view (a parked
hull or a battle never grows into a planet). Changing seat eases the zoom in log space (time constant 0.6 s), never a snap.

| Inhabited | Size km | Seat zoom |
|---|---|---|
| Person (EVA worker, ejected pilot) | 0.0018 | 0.046 |
| Mech | 0.0072 | 0.080 |
| Satellite | 0.008 | 0.083 |
| Launched fighter | 0.016 | 0.110 |
| Interceptor | 0.07 | 0.198 |
| Fighter (helm) | 0.1 | 0.229 |
| Frigate | 0.32 | 0.364 |
| Cruiser | 1.2 | 0.618 |
| Defence platform | 1.4 | 0.657 |
| Battleship | 2.4 | 0.815 |
| Carrier | 3.2 | 0.915 |
| 4 km and up | 4+ | 1 |

**With the G toggle**: both mappings are log-blended with the realistic ones by the scale blend, so the realistic scale
stays true proportions whatever is held, and between the two the seat's effect is zoom^blend. **Drawing only**: the
simulation never reads the zoom (the static `GameplayLocalDistance` it uses is never zoomed); the fleet's drawn hull
separation (`Fleet::DrawnSeatZoom`) is view-only like `DrawnBlend`; in co-op each peer draws round its own seat and nothing
goes into the lockstep frames. Every tuning number is in `ScaleModel.h` (`SeatZoomFullKm`, `SeatZoomExponent`,
`SeatZoomMin`, `SeatZoomEaseSeconds`); the non-hull sizes in `Embodiment.h` (`InhabitedSizes`).
`sg.SeatScale [quit]` frames one world from a fighter's helm, a battleship's helm, a defence platform and a repair worker,
each at zoom 1 and at its own, into `Saved/Screenshots/seat-scale-*.png`.

## Stations and their berths (US-T435)

A station ships call at is built big enough to hold dozens of them at once, and that capacity is real. Every number is in
`BerthTuning` (`Cpp/Source/SpaceGodCore/Infrastructure/Berths.h`):

| Station | Length km | Berths | Layout |
|---|---|---|---|
| Gateway (transfer station) | 10 | 40 | despun spine through the wheel's hub, 5 a side of the wheel, 4 ways round |
| Habitat | 9 | 40 | the same |
| Shipyard | 8 | 36 | 4 piers down its length (one per flank), 9 each |
| Depot | 6 | 24 | 4 piers, 6 each |
| Factory station | 4.5 | 20 | 4 piers, 5 each |
| Relay, real spacecraft | as before | 0 | they take no ships |

A berth every `PitchKm` = 0.75 km along a pier (a freighter's 0.6 km beam with room), so capacity grows with length:
`Piers` (4) x `floor(PierShare (0.85) x length / pitch)`, or for a spinning station `4 x 2 x floor((1 - WheelShare) / 2 x length /
pitch)`. Settlement structures, the Lagrange depots and gateway and the stations a growing port builds (GalaxyLife) are all
built to these lengths (`Berths::LengthKmFor`); a game saved before keeps its old sizes and the fewer berths they give.

**Who berths where** (`Infrastructure::UpdateBerths`, every world tick after the fleet moves; saved with the structures,
`OrbitalStructure::Berthed` and `Holding`): a civilian hull parked at a world calls at that world's stations whose port clears
its faction (not a warship on patrol, a pirate, a wanderer, a stranded hull, one in a fight or the one flown from a helm). It
keeps the berth it has; a new caller takes the lowest free berth at the first of its world's stations with one free (starting
from one picked by its id, so traffic spreads over them) and holds off its own station only when every berth at all of them
is taken, cleared in, in the order it came, as one frees; a ship that sails (or stops calling) frees its berth. It reads the
ships and writes only the berths, drawing on no random stream, so the simulation's outcomes are unchanged (Bench 5/5).

**Solid core**: what passing traffic steers round (`Fleet::StructuresOf`) and what a hull flown into a station hits
(`SectionColliders::StationsNear`) is its core (`Berths::SolidKm`: `HabitatCoreKm` 2.2, `GatewayCoreKm` 1.8, `ShipyardCoreKm`
3.5, `FactoryCoreKm` 1.6, `DepotCoreKm` 1.4, the sizes the stations had before they grew their piers), not the open docking
space round it where traffic control clears ships in and out. Giving those obstacles the full length instead moves the
economy-year Bench digest (to 1AA40625765BC95E): a change of the simulation to approve and re-baseline on its own.

**Drawn**: `StructureViews` measures each station's model as drawn (the pack's station, else the procedural one) and lays the
berths round it: piers along the flanks just off the plating, or the spine along a habitat's or gateway's axis out past its
wheel, with a docking arm and a light at every berth (`ShipMeshFactory::GetBerthGantry`). A berthed hull is drawn nose-in at
the end of its arm, square to the pier, at its own drawn length (`StructureViews::BerthPose`); ships waiting hold in a line
off the station's end (`HoldPose`). Both scale with the station's drawn size and the hulls' drawn length (one linear map), so
docked ships fit the model at every blend and seat zoom. A habitat's wheel spins on its own; its spine and the ships on it
are despun. A yard's slip draws the hull it builds at that hull's own length against the yard (at most the cradle's 80%).

**The player**: docking at a port (Shift+M) takes a berth at its station (its habitat, else gateway, yard, depot or works)
through `Infrastructure::RequestBerth`. The hull is brought into the berth and moored there (moving with the station) until
any stick input casts off, which frees the berth. With every berth taken the docking is held: the toast says the hull's place
in line, and it is cleared in and docks as one frees, while it stays alongside. A port with nothing in orbit that takes ships
docks as before.

`sg.Berths [quit]` fills a port's habitat and its yard (Earth's in Sol) with calling hulls, every berth taken and a few
holding, frames each whole and along a pier into `Saved/Screenshots/berths-*.png`, logs the counts, then sails one caller
to show the line moving up.
