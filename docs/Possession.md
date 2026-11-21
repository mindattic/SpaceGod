# Possession: the show, and the possessed view (US-T446)

The owner: "the effect of SpaceGod inhabiting/possessing a person/ship/station needs to be an impressive show: an almost
imperceptible energy enters the vehicle and changes what can be seen based on what the pilot sees".

Whenever this machine's player takes a seat of any kind (a ship's helm, a launched craft, a gun, a station, a satellite,
a mech, an EVA repair worker, an ejected pilot) a faint energy crosses from the god's eye to it and seeps into the hull,
and what can be seen becomes what that pilot perceives. Letting go, the energy comes back out and the god's full view
returns. It is view only: nothing in the simulation, the sensors' pictures or the co-op digest changes, and each co-op
peer plays its own player's show and sees through its own player's seat.

Every tunable (timings, sizes, brightness, colours, the sensor suites of the seats, the frame's alphas) is in one place:
`Views::PossessionTune` (`Cpp/Source/SpaceGod/Views/PossessionFx.h`).

## How a seat take is seen

`UnitySpaceApp::UpdatePossession` (`UnitySpaceApp.Possession.cpp`), first thing in `UpdateViews`, compares what is held
now (the helm flown here, `IsPiloting()`, else the session seat, `HeldNonHelm()`, compared with `EmbodiableEquals`) with
last frame. A change starts the show: going in when something is now held (at `SwitchSpeed` from one seat straight to
another, as the camera hurries), coming out when nothing is. So every path plays it alike: the keys, the picker, a
co-op frame granting a seat (`CoopSeatTakenHere`), the helm's co-op take, a load.

## The show going in (about 2.7 s, in step with the camera's 1.6 s fly-in)

- **The energy crossing** (`Views/PossessionFx`): twenty-two motes, small soft streaks of pale blue-white light (the hull
  rim's own hue), leave the edge of the god's view one after another (golden-angle spaced round it) and curl in to land
  along the near side of the hull (round the suit, for a person, whose glow is smaller and softer, as the eye is close by). Their path is worked out each frame from the camera to
  the hull as both move, so they keep flowing from the viewer into the vessel all through the fly-in.
- **The seep**: a rim glow just behind the hull (its silhouette's edge lights, peaking about 1 s in), a sheen sliding
  over the plating bow to stern, and the hull model's own rim term (the selection rim, `_Highlight` in `M_Ship`, now a
  float through `ShipView::Apply`) brought up to 2.2 and down again: the light goes into the hull and fades there.
- **The breath**: the field of view narrows by 2.6 degrees and back (a sine squared over 1.7 s, `PossessionFovBreath` in
  `UpdateClipPlanes`), and the screen takes the faintest wash of the energy's colour (5.5% at its peak, `Hud::DrawPossession`).
- **The view changes** from 1.25 s: the possessed view eases in over 0.9 s (`PerceptionShown`, 0 god .. 1 possessed);
  the filtering by the pilot's picture switches over half way.

Letting go plays it backwards (about 2 s): the rim flares briefly and fades, the sheen slides back stern to bow, the
motes leave the hull and drift out towards the eye, thinning away; the view breathes wider; the god's full view is back
at once in what is drawn, and its orbit lines fade back in over 1.5 s.

Reduce flashes and shake / reduced motion (Settings > Accessibility): the motes and the breath are off (the rim and the
glow only fade), and the light is dimmed (`PlayerOptions::FlashScale`, never under 0.35-0.4). A scripted run (the
screenshot commands) still plays the show: it reads only the profile's setting (`UiMotion::ProfileReducesMotion`), not
`UiMotion::Enabled`, which is off in every scripted run.

## The possessed view: what the pilot perceives

`UnitySpaceApp::PerceivedPicture()` is the picture the view is filtered by:

| Held | Seen through | Picture |
|---|---|---|
| a ship's helm | its sensors | the simulation's own, `Sensors::Player` (unchanged) |
| a launched craft, a gun, a satellite, a mech | its (or its hull's) sensors | the seat's view-side picture: the craft's hull's `SensorSuite::Of`, or `CraftRangeKm`; a gun the hull's fire control (a station's gun the station's); a satellite `SatelliteRangeKm`; a mech `MechRangeKm` |
| a station | traffic control's panorama | the seat's view-side picture with `SensorSuite::Station` |
| an EVA worker, an ejected pilot | eyes alone | the seat's view-side picture by eye only |

The seat's view-side picture is a `ContactBook` of the app's own (never the simulation's), swept every half second
(`SweepPerception`): each ship's place and velocity as drawn (its battle unit's in a fight, else its schedule's state)
against the seat's pose. A sensor seat resolves it with `Sensors::Resolve` (its signature, its transponder, or its own
flag's IFF); every seat also sees by eye: within visual range (one arcminute, `VisualRangeKm`) a blip, its class within a
third of that, who it is within 8%. Each track is put where the light it was seen by left it (back along its course by
the delay) and keeps its lag, so its label says "(1.3 s ago, light lag)"; a track not seen goes cold and is held 180
sim seconds as a "?" where it was last seen. No pursuit, fleet cache or sensor state is stepped: only drawn state is read.

What the picture filters:

- **Ship models**: possessed, a hull is drawn only if the picture holds it live, it is the hull held, or it is close
  enough to see by eye (`PerceivesShipModel`). In the god's view everything is drawn as before.
- **Icons, labels, the lost "?" marks**: the HUD's existing helm filtering (U.SPC-T66) now runs for every seat
  (`Hud::Picture`), and a live track's icon and label are drawn light-lagged (`PerceivedLagOffset`, `Hud::ContactScene`).
- **Picking with the mouse**: only what is perceived (or close enough to see).
- **The god's charts**: the orbit lines and the disc fade out (as in a fight) and the lanes hide while possessed.

The view's character on screen (`Hud::DrawPossession`, eased in with the view, shown with the HUD off too):

- **Sensors**: thin corner brackets round the view with a tick at each edge's middle, and the caption "THROUGH ITS
  SENSORS · REACH 400,000 km".
- **Eyes**: a helmet visor's dark rounded rim, and "THROUGH THE VISOR · EYES ONLY".
- **Panorama**: soft bands top and bottom, a bearing scale along the lower one (ticks every 5 degrees, labels every 45),
  and "TRAFFIC CONTROL · PANORAMIC · REACH 600,000 km".

## Checking it

```powershell
./tools/play.ps1 -Offscreen -Capture '' -Seconds 240 -ExecCmds "sg.Possess shot R:/Temp/possess quit"
```

`sg.Possess [shot <dir>] [quit]` puts a battleship, a fighter, a freighter about 380,000 km out (squawking) and a courier
of the same flag running silent 260,000 km out by a world with a defence platform, then screenshots (`possess-*.png`):
the god's view; the battleship's helm taken at 0.35, 0.7, 1.05 and 1.5 s into the show; the helm's own view; a fight
started by the platform and the platform taken (0.6 s in, then its view); a repair worker taken (0.7 s in, then its
view); the helm again and then let go (0.3 and 0.8 s out, then the god's view again). Each shot logs the view's kind, how
far it is in, the show's time, the motes lit, the rim and sheen, the field of view, and what the picture holds of the
freighter, the courier and the fighter.

## Limits

- Weapons fire, wrecks, launched craft, mechs and the satellites are not filtered by the picture: a fight's fire shows
  from an undetected hull.
- Line of sight (a world between the seat and a contact) and nebulae count only at the helm (the simulation's picture);
  a seat's view-side picture ignores them.
- A track's light-lagged place is its course run back by the delay, not its true past (its schedule is not asked, so no
  fleet cache is stepped); turning hard within the delay is not shown.
- Bodies, stations and their labels are not filtered: every seat has the charts.
- The energy is drawn with the existing additive Glow quads and the hull's rim term; there is no new material.
