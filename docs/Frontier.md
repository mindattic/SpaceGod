# The frontier (US-T449)

> "the ability to explore deeper and less tracked and weirder systems"

Past the neighbourhood people know, the galaxy grows less charted, less travelled and stranger the farther it lies from
Sol. Everything here is the generated galaxy's (`ProceduralSystems`): the hand-built systems, the real catalogue stars
and the void stations are exactly as they were.

## The gradient

`Core/Systems/Frontier.h`. A place's **depth** is 0 inside `Frontier::StartLy` (50 ly from Sol: past the settled
systems and the 21 ly of known space) and rises linearly to 1 at `Frontier::FullLy` (400 ly). The galaxy map names the
bands: known space (inside 50 ly), the near frontier (50 to 100), the far frontier (100 to 200), the deep frontier (200
to 400) and the wilds beyond.

The deeper a place is:

- **Less charted.** `Galaxy::Explore` charts a generated star only within `Frontier::ChartReachLy` of the pilot: the chart
  radius (Settings > Streaming) cut by 70% at full depth, never under 5 ly. Out there you find stars by going.
- **Less tracked.** Nobody's traffic goes there: generated systems are unsettled, off the gate network, and no voyage plans
  to them.
- **Unknown worlds.** A system on uncharted ground (`Frontier::IsUncharted`: on the frontier, unsettled, no gate, not a
  relay) shows no name or marker for a world nobody has surveyed: the HUD draws only "UNCHARTED" beside one big enough to
  see, and the system map leaves it off. A world is revealed (written into the discovery log as surveyed) when the eye or
  the helm comes within `Frontier::RevealKm` (6000 of its radii, at least 2e7 km), when a ping from the helm lights the
  neighbourhood (25 times that), or when it is selected on the scan card. `UnitySpaceApp::BodyUncharted`,
  `RevealFrontierBodies` (UnitySpaceApp.Discoveries.cpp).
- **Stranger.** The share of generated stars that are strange (`Frontier::OddChance`) is 6% just past 50 ly, rising to
  56% at full depth; the remnants and relics grow commoner with depth (`Frontier::Classify`'s weights).

## The strange kinds

`FrontierKind` (saved on the catalogue entry, `SystemSpec::Oddity`, `SG_OPT`: absent from older saves). Each is made by
`Frontier::Furnish` from the plain generated star, on its own stream.

| Kind | What it is | How it is drawn |
| --- | --- | --- |
| Pulsar | A neutron star (1.2 to 2 Suns, 21 to 27 km across, spectral `PSR`), up to four pulsar planets | A small white-hot core (`BodyView::PulsarCoreScale`, 22% of the radius the view frames it by), a tight blue-white glare on it and a wider soft halo, one long glow through it along the magnetic axis (50 degrees off the spin axis) swept round like a lighthouse; beams, glare and halo brighten as a beam faces the eye (`BodyView::ApplyFrontierBody`) |
| Black hole | 5 to 25 Suns, its horizon its Schwarzschild radius (spectral `BH`); the light is its disc's; an infall ring; a few captured worlds | An unlit black shadow; a flat accretion disc in its equator from 2 to 6.5 shadow radii (`BodyView::AddGlowBand`: hot and sharp at the inner edge, fading out), tilted as its pole lies, the side turning toward the eye brighter and bluer and the other dimmer and redder (Doppler beaming, by the sine of the tilt: `ColourBand`); a thin photon ring hugging the shadow, brighter on the same side |
| White dwarf | A young white dwarf (DA), its inner worlds swallowed, the rest pushed out, a disc of shredded rock close in | Inside a planetary nebula: a shell of glowing puffs (`SpaceFeatures`) |
| Brown dwarf pair | Two L/T dwarfs on a shared orbit, no true star, a dim world or two | Existing star rendering, dull red |
| Rogue planet | A starless gas giant (0.5 to 12 Jupiters, spectral `Rogue`) with its moons | A dark world with a faint warm rim and a dull red glow of its own heat |
| Binary | A wide pair (planets about one star or the other) or a close one (none) | Existing pair rendering about a barycentre |
| Triple | A pair plus a red dwarf 150 to 900 AU out | As a binary, with a wide companion |
| Star in a nebula | The star wrapped in a nebula 6 to 14 AU across | `SpaceFeatures` nebula; it masks sensors (`SensorMask`) |
| Comet cloud | A vast comet disc 40 to 360 AU out and ice fields falling in | Debris disc and `SpaceFeatures` ice fields |
| Ancient relic | A broken ring-world, a quarter of a million km across, orbiting an ordinary star (a planet body named `... Relic`) | `VoidStationModel::Relic`: a broad band between rim walls in arcs with fallen spans, a few lights still burning |
| Wandering worlds | Every world on a steep, eccentric orbit (some retrograde), and an iceball on a comet's orbit | Existing orbits and bodies |

From afar a strange star is charted as an unidentified object (spectral `?`, its light what an observer would see:
`Frontier::Chart`); what it is shows only when you go.

A strange or uncharted system's sky keeps its painted gas but not the painting's giant worlds (`SkyLayers::Frame::Giants`):
those are drawn elsewhere as bodies out near the volume's rim, and here they would be worlds the survey never finds, seen
through the gas as a stray translucent sphere over the star.

## On the galaxy map

The Exploration mode draws the bands as rings about Sol, each named on its lower right where the name is wholly in view
and clear of the find field, the pins panel and the other names. A strange system is ringed and says what it is on the
line under its name ("Black hole  ·  visited"). The names are placed after every star is drawn, the most wanted first
(here, the selected or hovered system, a strange one, the heard-from, the rest; nearer the middle first among equals), each
at the first clear spot round its star (right, left, below, above), with its line under it only where both fit; a name
with no clear spot is left off until zooming in spreads the stars (here and the picked one always show, line and all).
Each placed name sits on a faint dark backing so the contact rings crowding a star don't strike through it.

## Rewards

- **Discoveries.** Coming to a strange system writes a **Phenomenon** into the discovery log (a new kind, after Pilots met:
  `DiscoveryKind::Phenomenon`, "Phenomena" on the screen, Astronomy in the Encyclopedia), with what a surveyor would say of
  it and its readings (a black hole's mass and horizon, a pulsar's spin...). A toast says so.
- **Survey data.** With a hull at the helm, coming within the system (a relic: near it) is a first discovery
  (`SurveyKind::Phenomenon`), worth `Frontier::SurveyValue` before distance: 35,000 cr for a binary up to 200,000 for a
  black hole and 250,000 for a relic, raised as every survey is by how far past known space it lies; sold to a
  cartographer as before (US-T260). The worlds of uncharted ground are surveyed as before too.

## Determinism, saves and the Bench

- Whether a generated star is strange, and how, comes from its own seed and its place on a separately salted stream, so
  every star that is not strange is the star it always was; the cell's stream is untouched. Co-op peers make the same
  systems; a star let go and reached again is the same.
- `SystemSpec::Oddity` is saved (`SG_OPT`); `SpaceFeatures`' frontier features are derived and noted again from the spec
  when a system is defined or loaded (`SG_LOADED`, `Frontier::Note`, `SpaceFeatures::Mark`). The discovery log and the
  survey ledger save as before.
- Nothing within 50 ly of Sol changes and the Bench charts no generated star (it has no viewpoint), so its digests are
  unchanged.

## Checking it: `sg.Frontier`

```
./tools/play.ps1 -Offscreen -Capture '' -Seconds 600 -ExecCmds "sg.Frontier shot R:/Temp/frontier quit"
```

`sg.Frontier [shot <dir>] [quit]` (`FrontierCommand.cpp`) searches outward from Sol for the nearest generated star of each
kind, builds each and travels there, and screenshots its overview (`frontier-<kind>-overview.png`) and a close-up of what
makes it strange (`frontier-<kind>.png`: the pulsar, the black hole, the white dwarf, a brown dwarf, the rogue, a star of
the pair or triple, the nebula's star, the relic), logging the depth, oddity, star, body counts, how many worlds are still
uncharted and the nebulae, ice fields and discs; then the galaxy map in its Exploration mode with the frontier gradient
(`frontier-galaxy-map.png`).

## Limits

- The strangeness is the generated galaxy's alone: no real catalogue star became a pulsar or a black hole.
- Relics pay as survey data; there is no salvage or rare goods trade from them yet.
- A black hole's lensing is suggested (a photon ring), not ray-traced: the disc's far side is not bent up over
  the shadow. The pulsar's sweep is slowed to read.
- The "BIO MASS CONTACT" line in the close-ups is not the frontier's: the Bio Mass seeps into every system (three globules
  at the start), and the line shows whenever one is within 2,000,000 km of the player, as it does anywhere.
