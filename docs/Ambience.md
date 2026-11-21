# Ambience: the sound of space (US-T448)

Under the flight sounds (US-T242), the fights (US-T241) and the score (US-T243) there is now a soundscape for space itself.
It plays on the **Ambience** bus (Settings > Audio > Ambience volume, under the master volume). Like every other sound in
SpaceGod, each bed is synthesized in code (`Audio/SynthSound.h`): no sound files, and no licences. The beds are built once,
off the game thread, the first time sound plays.

| Code | Files |
| --- | --- |
| Pure mixing: the beds, the `Place` read, `MixFor`, the crossfade, **every tunable** (`Ambient::Tune`) | `Cpp/Source/SpaceGod/Audio/AmbientMix.h` (header-only, no engine) |
| The beds' synthesis, reading the place from the app, playing the loops | `Cpp/Source/SpaceGod/Audio/AmbientSounds.h/.cpp` |
| Polled once a frame, after `CombatSounds` | `UnitySpaceAppActor.cpp` |
| Unit checks (71) | `Cpp/Source/SpaceGodCore/Tests/Ambience/AmbientMixTests.cpp`, built and run by `tools/build-core.ps1` |
| Tour and log | `sg.Ambience tour [quit]` (`AmbienceCommand.cpp`) |

## The beds

Each bed is an 8-second seamless loop. Every steady tone in a loop runs a whole number of cycles (a multiple of 1/8 Hz), and
the loop's ends are cross-faded.

| Bed | Where it plays | Sound |
| --- | --- | --- |
| Void | Anywhere in a system: the floor under everything else. It gives way (to 40%) to the loudest place bed. | Two deep tones beating every 4 s over a darker rumble. Low, slow, barely there. |
| Star | Full within 15 of the star's radii (0.07 AU from the Sun), gone past 250 radii. Earth's orbit gets only a breath of it. | A hot rush of noise fluttering over a deep hum, with the solar wind's crackle. The pitch rises a little as you near the star. |
| Planet | Within 3 radii it is full. A rocky world (70%) fades out by 25 radii, a moon (50%) the same, and a gas giant (100%) not until 90. | Magnetosphere whistlers falling from 4 kHz, chorus chirps, radio static and two slow radio sweeps. A gas giant's sit lower. |
| Nebula | By the nebula's density at the listener, read through `SpaceFeatures::SensorMask` (how much the gas masks sensors there). | Airy shimmering: bright breath of noise, high tones beating, a soft open pad. |
| Port | A station or a busy port: full within 300 km, gone by 30,000 km. Docked, it plays at full strength whatever the distance. | Machinery thrumming, a far murmur of comms chatter, squelch blips, a clank, docking clamps biting. |
| Belt | Sol's main belt (2.1 to 3.3 AU, near the ecliptic), any ice field, or a belt rock (an asteroid or dwarf planet) within 300,000 km. | Occasional far ticks of rock and ice over the faintest hiss of dust. |
| Between | Out between the stars and under way: on the FTL drive, or during the god's own flight across. | A wide, low chord breathing slowly, with a wind of noise opening and closing over it. |
| Deep | Between the stars, far from any simulated system (none within 1 ly, all of it from 4 ly), or stopped out there at a void stop. A starless system (a void station's well) gets 60%. | Eerie and sparse: a low tone barely there, a glassy voice rising once a loop, a lone ping and its echo. |
| Hull | Inside: a helm, a gun mount (100%), a station's seat (85%), a craft's cockpit, a satellite or a mech (70%). | The reactor's hum and its harmonics throbbing, the air handlers, a creak of the frame. The pitch falls with the hull's mass, from 1.2 for a fighter to 0.6 at the floor. |
| Suit | A person out on EVA: a repair worker, an ejected pilot or a marine. | Slow breathing (in brighter, out longer and darker), the suit's fan, the radio's faint hiss. |

## How the place is picked

Four times a second `AmbientSounds` reads an `Ambient::Place` from the app:

- **The listener.** This is `PlayerHelioKm`: the hull at the helm, otherwise the camera, which follows any other seat held.
- **Between the stars.** The listener counts as outside a system in two cases. One is an FTL cruise (`InFtlCruise`): under way
  if the cruise is `Engaged`, stopped if it sits at a void stop. The other is the god's star travel on its interstellar leg.
  The distance to the nearest system in `Galaxy::Systems` comes from the cruise's position or the travel camera's `CameraLy`.
  A trip through the jump gates counts as near a charted system.
- **In a system.**
  - Every visible body is checked. For the nearest star, the reading is its distance in that star's radii. Of the
    planets, moons and dwarf planets, the one that would be heard loudest is kept. Station bodies count as ports.
  - Every `Infrastructure` structure that ships use counts as a port. Telescopes and probes don't. A relay counts as a
    quiet port (40%), a crewed station at 80%.
  - Being docked (`DockedAt` / `DockedStation`) also counts as a port.
  - Nebula density, ice fields and Sol's belt are read from where the listener is.
  - A system with no star at all is marked starless.
- **Inside.** What the listener is inside of comes from the kind of seat held (`HeldSeat()->Kind()`). The hull's heft is
  the flight feel's mass scale for a hull or craft flown, or a stand-in by kind otherwise.

`Ambient::MixFor(place)` turns that into a level and a pitch for every bed:

- Every in-system bed is scaled by how far inside a system the listener is. The between-the-stars beds are scaled the other
  way.
- Inside a hull, outer space comes through at 40% (a cockpit 55%, a suit 70%). A docked port's machinery and clamps are the
  exception: they come through the hull unmuffled.

Each frame every bed eases toward its wanted level, exponentially, so it never overshoots. A place bed fades at 0.7/s up and
0.5/s down. The between-the-stars beds fade at 0.5/0.4. Stepping into a hull or a suit is quicker, at 2.5/2.0. A loop starts
once it is wanted above 0.003 and stops once it has faded under 0.002 with nothing wanted.

## Local and silent where it should be

- **Local only.** It only reads the app, never writes it. Nothing enters the Core, the co-op frames or the bench digests.
  Each peer hears its own place.
- **Silent wherever `GameAudio` is.** That means no device, `-muted`, headless or offscreen, the smoke, shots and audits,
  `-demo` and `-capture`, unless `-GameAudio` is given. In the main menu every bed fades out.
- **The tour can still log while silent.** `sg.Ambience` sets `AmbientSounds::Watch`, so even without sound the place and
  the wanted mix are read and logged. Every bed then reads silent.

## The tour

```
UnrealEditor.exe Cpp/SpaceGod.uproject /Engine/Maps/Entry -game -Multiprocess --headless -RenderOffScreen -unattended
  -nosplash -GameAudio -log -abslog=<log> -ExecCmds="sg.Ambience tour quit"
```

The tour visits these places in order:

1. Sol's void, 70 AU out.
2. Beside the Sun, at 0.04 AU.
3. Off Jupiter, at 4 radii.
4. Off Mars, at 3 radii.
5. The main belt, at 2.75 AU.
6. Beside Earth's orbital habitat, 150 km off.
7. A frigate's helm by that habitat.
8. A defence platform's seat.
9. A repair worker out on a hull in the fight that comes to the platform.
10. The god view again.
11. Then it flies to Luhman 16 and sets down inside its nebula. It logs every 0.6 s on the way.
12. Last, a starless system, if one is simulated.

At each place, `sg.Ambience: [step]` logs the place it read, the beds playing (volume as applied and pitch), and the beds
wanted.

From the US-T448 run (`-GameAudio`, a real device; volume@pitch):

| Step | Playing |
| --- | --- |
| Deep void, 70 AU | Void 0.300 |
| Beside the Sun (8.7 R) | Star 0.443@1.04, Void 0.129 |
| Off Jupiter (4.1 R, giant) | Planet 0.384@0.85, Void 0.125 |
| Off Mars (3.0 R) | Planet 0.285@1.10, Void 0.173, Port 0.042 (Ares Prime Arsenal, 11,700 km) |
| Main belt, 2.75 AU | Belt 0.443, Void 0.123 |
| Beside Earth Orbital Habitat (145 km) | Port 0.393, Planet 0.276 (Earth 1.9 R), Void 0.120 |
| Frigate's helm | Hull 0.320@0.70, Port 0.171, Planet 0.120, Void 0.052 (space muffled to 40%) |
| Defence platform's seat | Hull 0.272@0.75 (station), Void 0.110 |
| Repair worker | Suit 0.420, Void 0.190 (space at 70%) |
| Interstellar leg, through the gates | Between rising 0.064 → 0.216 while the void fades |
| Inside Luhman 16's nebula | Nebula 0.394, Void 0.129 |
