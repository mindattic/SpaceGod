# The god view: no collision, and inside things (US-T442)

The command view's free camera (free swim, the god view) has no collision. It swims through planets, moons, stars and
hulls, and from inside one it draws what it is in from within: the skin see-through and the inside as a wireframe, so
the player looks out onto the rest of the game. The helm's chase, a seat (a cockpit, a gun, an EVA worker), a focus
orbit and star travel draw as ever. It is drawing only: nothing in the simulation changes.

## No collision

`Camera/SpaceCamera.cpp`: the free camera was held 0.2% above its frame body's true radius (`ApplySceneDelta`) and
pushed back out onto 1.003 drawn radii of every body (`PushOutOfBodies`); both are gone. Inside a body the scale mapping
is linear (`ScaleModel::GameplayLocalDistance`), so the camera's true offset from its frame body stays exact down to the
centre.

- **Speed.** Free swim scales with `ICameraWorld::SwimDistance` (`UnitySpaceApp.Main2.cpp`): the distance to the nearest
  surface on either side, never under 1% of a body's radius or a quarter of a hull's half-length. So the camera neither
  creeps to a stop as it crosses a skin nor crawls inside (`NearestSurface` is nothing there). The wheel, boost and slow
  scale it as before.
- **Near clip.** `UnitySpaceApp::ClipDistance` sets the near plane (a quarter of it): outside everything it is the old
  `NearestSurface`; inside a body the nearest skin, kept between 0.2% and 5% of the radius (the inside's shells lie
  between, and no depth fighting at planet scale); inside a hull 1% of its half-length.
- **Focus** still frames at its minimum zoom (1.12 radii for a body, 1.6 for a hull); only free swim goes inside.
- The status line says `inside <body>` in place of `near <body>` while the camera is within its frame body.

## Inside a body

`Views/BodyView.cpp`, `SetInterior(fade)` (stars, planets, dwarf planets and moons; a station, void station or void site
draws as ever). Built the first time the camera goes in, under the body's root so it turns with the body:

- **Skin**: the body's own sphere again with `M_GhostTwoSided`, a dark tone of the body's colour (a star's in its own
  light). The opaque sphere is one-sided, so from inside all its faces are culled and only the skin is seen.
- **Wireframe** (`M_Line` pixel lines): the skin's latitude and longitude every 15 degrees in a lighter tone of its colour;
  its layers as coarser shells (a rocky world's crust at 0.9 of the radius, outer core 0.55, inner core 0.19 in brown,
  orange and pale yellow; a gas giant's metallic hydrogen at 0.8 and core at 0.14; a star's radiative zone at 0.7 and
  core at 0.25); the axis through the poles and twelve spokes across the equator.
- **Fade**: `InteriorWire::Fade` over the first 4% of the radius in: the skin nearly opaque (0.85) as the camera breaks
  through, thinning to a veil (0.32) deeper in, the wireframe brightening in. A star's corona is put away while inside it.

Rings, moons, ships, lines and everything else outside stay drawn as they are.

## Inside a hull

`Views/ShipView.cpp`, `SetInterior(fade)`, for a hull drawn as a full model, when the camera is inside the box round
the hull as drawn (`HullBox`, the root's frame, one unit long). The fade runs over a fifth of the box's narrowest
half-width in.

- **Skin**: every solid part of the model (its hull renderers and its hardpoints, masts and fittings; not its glows,
  effects or lines) in `M_GhostTwoSided` (cool blue-grey), thinner than a body's (0.55 at the box's face to 0.12 deep in,
  since many parts overlap), their own materials kept and given back as the camera leaves (one the view changed
  meanwhile is kept again).
- **Wireframe**, built once from the ship's own data: the hull box; its structural modules (`ModularHull`: keel sections
  in cyan, sponsons in green, shattered ones in red; read from the hull's structure if laid out, else laid out apart, so
  the simulation's state is never touched); decks, one every ~12 m of the hull's height (1 to 10), each a frame with
  frames across it every tenth of the hull; its compartments (`Interior`), each a box one deck tall at its height,
  reaching halfway to its neighbours along the keel (side by side where several share a place), coloured by kind
  (bridge gold, engineering orange, hangar green, hold tan, quarters blue, battery red, breacher bay pink, barracks
  violet), with the passages between them as white lines.

## Wiring

- `UnitySpaceApp::UpdateGodInterior` (`UnitySpaceApp.GodInside.cpp`) runs each frame after the views are placed: how far
  the camera is inside each body and each hull goes to its view. Off at the controls, in a seat, in focus, in travel.
- `Views/InteriorWire.h/.cpp`: the line-mesh builder (segments, circles, lat/long spheres, boxes), the skin material and
  the fade.
- `M_GhostTwoSided` (`spacegod_render_materials.py`, `build_ghost_two_sided`; rebuilt alone with
  `SPACEGOD_RENDER_ONLY=M_GhostTwoSided`): M_Ghost's flat translucent colour, two-sided, stronger edge-on. Sort priority
  -20 (`ShaderBinding::SortPriority`), under the wireframe drawn inside it.

## Checking it

```powershell
./tools/play.ps1 -Offscreen -Capture '' -Seconds 200 -ExecCmds "sg.GodInside shot R:/Temp/godinside quit"
```

`sg.GodInside [shot <dir>] [quit]` parks a battleship and a fighter off Earth, then puts the free camera just above
Earth's surface, just under it, in its mantle, by its core looking out at the Moon, inside the Sun, inside the battleship
(aft and forward of its middle) and inside the fighter, screenshotting each (`godinside-*.png`). For each body shot it
checks the camera stayed where it was put (no push back out onto the skin) and logs the swim speed and the clip gap.

## Limits

- The skin is a flat tint, not the body's or hull's own texture made translucent.
- Stations and other structures are not drawn from inside: the camera passes through them (nothing blocks it) but
  sees them as from outside.
- A hull's compartments are the simulation's (a keel position and a height each): their boxes are a diagram of the
  spaces, not modelled rooms.
