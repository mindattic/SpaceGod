# Short rewind: a few seconds of every moving thing in a system, played backward (US-T396 research)

Research and design proposal only. No code was changed for this. It builds on [docs/REWIND.md](../REWIND.md) (US-T99), which
covers the long-horizon keyframe ring that is already in the game.

Every claim below is tagged **[Source]** (a cited page says it), **[Repo]** (the code or docs in this repo say it) or
**[Inferred]** (my own reasoning or estimate, not stated by any source).

## Summary

- **What other games did.** No public technical postmortem exists for how *Prince of Persia: The Sands of Time* (2003)
  implemented its rewind. What is on record: it rewinds "up to ten seconds", every use costs a Sand Tank, the feature
  came "first, very early", the team designed the whole game round it, and the sand limit was a **design reward
  ("collect, hoard, and spend")**, not a memory limit. The team peaked at 65 people. Two programmers on the
  series called it hard: the 2020 remake's director said the rewind is "a headache technically", and a Ubisoft
  programmer called it "one of the most complex things I've done in my career". **Braid** (the best-documented case)
  recorded *full world state every frame* (no interpolation), stored as full frames plus deltas "like video encoding",
  and fit 30-60 minutes into about 40 MB. Blow rejected input or event replay as too fragile, and recomputed particles
  deterministically instead of storing them. **Planetary Annihilation** stored every value as a **curve** (linear,
  step or pulse keyframes, made at 10 Hz, plus an extra keyframe at a discontinuity) over each object's lifetime.
  That is the closest model to a whole solar system. Forza 3 and GRID published no internals.
- **Memory is not the hard part.** [Inferred] A visual track of 2,000 sampled objects × 10 Hz × 10 s × 40 B is **8 MB**.
  A realistic large battle (40 capitals, 400 fighters, 600 missiles, rounds as spawn records) is **about 7-8 MB for
  10 s, or 45 MB for 60 s**. One SpaceGod keyframe alone is 7-24 MB raw. A few seconds is a good design limit (it is
  what Sands of Time, GRID and Forza chose), but a 10 s track costs about as much as one keyframe.
- **Most of a solar system needs no record at all.** [Repo] Planets and moons come from the ephemeris. A ship off the
  battlefield is in a `ShipPhase` with a `ParkingOrbit` or a transit `Path`/`ConicTransfer` that `Fleet::StateOf(ship, jd)`
  evaluates in closed form. A `KineticRound` flies a straight line. Only units that `Battle` integrates step by step
  (capitals, fighters, platforms, missiles, decoys) and the player's own hull need samples.
- **The rate to record at (the "polling rate").** [Inferred] Capitals at **5 Hz**, fighters and missiles at **20 Hz**, the hull
  the camera rides at **30 Hz** (or every frame). Store position and velocity for cubic Hermite interpolation, store
  orientation for slerp, and add an extra sample at every discontinuity (spawn, death, hit impulse, boost, FTL
  engage). Rounds, beams, explosions and analytic movers need only one event record each. Linear interpolation's
  position error is a·h²/8: about 0.1 m for a 10 g fighter at 10 Hz. Orientation and discontinuities set the rate, not
  position.
- **Departures and arrivals** are presence intervals on each track: [tEnter, tExit), with how the object entered and
  left (FTL out, gate entry, destroyed, docked, launched, arrived). The track ring belongs to the system, so a capital
  that jumped out 6 s ago is still in it. Its jump-out plays reversed, so the hull re-forms out of the flash.
- **Resuming is the real problem, and the existing system already covers half of it.** A track rewind is visual
  only. To *play on* from the rewound moment, the full world state at that moment has to exist. The repo already has
  that path: restore the keyframe at or before the moment, then replay the logged frames deterministically
  (`RewindSeek`). Three gaps block it in combat: (1) the replay stops at the first frame the player acted on, so the
  input recorder has to come first; (2) keyframes are at least 20 wall-s apart, with a 110-490 ms capture hitch, so
  the restore point can be 30-50 s back (1.5-3 s of replay at the measured 18× real time); (3) no keyframe is taken
  while a `SaveBlocker` holds (under FTL, latched to a hulk).
- **Recommendation.** Build it in three steps. (1) **Instant replay**: the track ring plus reverse playback, ending
  back at "now". It is read-only and needs no sim change. (2) **Rewind and resume**, on top of the existing
  keyframe plus frame log, once the input recorder and a short-horizon anchor keyframe exist. It uses the track to hide
  the restore latency, and to check the replay for drift. (3) Co-op as a host-ordered lockstep command. Rough total:
  **9-13 engineer-weeks**, most of it in resume and reverse effects, not in recording.

## 1. Prior art

### 1.1 Prince of Persia: The Sands of Time (2003), Warrior Within (2004), The Two Thrones (2005)

What the sources say:

- **Limit and cost.** [Source] The rewind lets the Prince "rewind time by up to ten seconds" to undo fatal mistakes.
  "Each use of power uses up one Sand Tank, and when empty, all powers become inaccessible until more Sand is
  collected." ([Wikipedia: The Sands of Time](https://en.wikipedia.org/wiki/Prince_of_Persia:_The_Sands_of_Time))
- **Why it is limited.** [Source] Jordan Mechner's essay presents the sand as a *design* economy: "We want to make the
  player work for this cool ability to turn back time", and the Sands "must function first of all as a reward, a
  substance that the player can collect, hoard, and spend". The essay says nothing about memory. ([Mechner, "The Sands
  of Time: Crafting a Video Game Story", electronic book review,
  2008](https://electronicbookreview.com/essay/the-sands-of-time-crafting-a-video-game-story/))
- **How central it was.** [Source] Mechner: "The rewind came first, very early." Its aim was to stop players dying and
  restarting so often. The engineers took a request for a rewind button "like a videotape" as a challenge, and the
  team designed characters round the feature. ([Ubisoft News, "Creating the Sands of Time
  trilogy"](https://news.ubisoft.com/en-us/article/3oAqiLg5k7GazIw5OWYTO2))
- **Team and engine.** [Source] It was built on an updated Jade engine (from *Beyond Good & Evil*), and the team peaked at
  65 people. Production began in June 2001 and the game shipped in late 2003, about 2.5 years. The lead engineer was
  Claude Langlais. ([Wikipedia](https://en.wikipedia.org/wiki/Prince_of_Persia:_The_Sands_of_Time);
  [Worthplaying team interview, 2003](https://worthplaying.com/article/2003/9/18/interviews/12541-prince-of-persia-the-sands-of-time-development-team-interview/))
- **Sequels.** [Source] Warrior Within kept the rewind and built on its paradoxes (portals between past and present,
  meeting a past self). The Ubisoft retrospective describes no change to the rewind mechanism itself in either sequel.
  ([Ubisoft News](https://news.ubisoft.com/en-us/article/3oAqiLg5k7GazIw5OWYTO2))
- **How hard it is.** [Source] For the 2020 remake on Anvil, the director Pierre-Sylvain Gires said: "The principle of the
  rewind is a headache technically. The Anvil engine was not made at all to do rewind, to do slow motion, to do all
  those time manipulation." ([Sirus Gaming, quoting the Mako Reactor
  interview](https://sirusgaming.com/prince-of-persia-the-sands-of-time-principle-of-rewind-headache)) Lucie
  Dewagnier, game director of *The Rogue Prince of Persia* (2024), on cutting it: "It was sad for me to remove the
  time rewind, especially because as a programmer, it was one of the most complex things I've done in my career."
  ([Ubisoft News, 2024](https://news.ubisoft.com/en-us/article/50BEgIWDER8qtc0tD3N26h/how-prince-of-persia-made-its-double-comeback-in-2024))
  *The Lost Crown* gave the classic rewind to its antagonist. One reason was that the player lost control while it
  played. ([The AU Review interview](https://www.theaureview.com/games/interview-abdelhak-elguess-and-prince-of-persia-the-lost-crown/))
- **Implementation.** No GDC talk, postmortem or developer interview found describes what Sands of Time recorded. The
  2003 Worthplaying interviews name the feature but give no internals. [Inferred] What the game shows points to a
  short per-object state ring: the scene plays backward smoothly with animation poses, the rewind is capped at a fixed
  ~10 s, and enemies and traps rewind with the Prince in a small, linear, level-bounded scene. A few dozen actors ×
  60 Hz × 10 s fits easily in a PS2's 32 MB if each sample is about 100 B. This is a guess, not a source.

### 1.2 Braid (2008), Jonathan Blow

- [Source] Requirements, from the GDC 2010 session listing: rewinding is "robust, exactly reproducing the game scene at
  each given time", it covers "30 to 60 minutes" of play, and it fits in "40 megabytes on consoles".
  ([GDC Vault: The Implementation of Rewind in Braid](https://gdcvault.com/play/1012210/The-Implementation-of-Rewind-in))
- [Source] Blow on Hacker News: "I recorded world state, not events." "I record the positions of all objects every
  frame." It is "a system of full-frames and deltas, like video encoding". Every frame is reproduced exactly, with no
  interpolation. On the Xbox 360, most levels rewind "30-45 minutes before running out of buffer". He chose state over
  events because "CPU was ultimately the biggest problem", and he rejected event recording as fragile, since a
  reversible-event system ends up storing world state anyway. When the buffer is full, the oldest states are dropped.
  ([HN thread 9484197](https://news.ycombinator.com/item?id=9484197))
- [Source] Particles are not stored. They are deterministic, regenerated every frame from a seed, and computed in
  closed form, because they do not affect gameplay. ([HN thread 39773041](https://news.ycombinator.com/item?id=39773041);
  [Blow on X, 2024](https://x.com/Jonathan_Blow/status/1770249973596410286)) A secondary summary gives the keyframe
  (full-frame) interval as about every 2 s. ([J. Reid, IRDC 2016
  slides](https://docs.google.com/presentation/d/1c2yuA8Gji-NqrOFZ2kIWdA4GQMiRAeYsTuYgQHM4hJ0/htmlpresent))
- **Lesson for SpaceGod.** [Inferred] Braid is a small 2D scene of tens to hundreds of objects. Recording state is the
  robust choice when the world is small. SpaceGod's whole-world state (7-24 MB, [Repo] REWIND.md) is far too big to
  record per frame. Its *visible system's moving bodies* are small enough, though, and the particle lesson carries over
  directly to effects.

### 1.3 Planetary Annihilation (2014): ChronoCam

The closest prior art: a whole planetary system, thousands of units, any moment seekable, and playback at any rate
including reverse.

- [Source] Every value the server sends is a **curve** over the object's lifetime, from creation to destruction. A unit
  has curves for position, orientation, health, built fraction, weapon target and weapon angle. Velocity is derived as
  the derivative of position and costs nothing. Curves use three kinds of keyframe: **linear** (interpolated),
  **step** (integers and booleans, such as ammo and orders) and **pulse** (zero-length events: firing, effects,
  sounds). ([Forrest Smith, "The Tech of Planetary Annihilation:
  ChronoCam"](https://www.forrestthewoods.com/blog/tech_of_planetary_annihilation_chrono_cam/))
- [Source] Keyframes are made at the 10 Hz server tick, but only when a value changes, so an idle unit costs zero.
  The server predicts far ahead (a build bar is two keyframes). An extra keyframe goes in at a discontinuity (a ball's
  bounce). A server keeps all curves for the whole game. Seeking needs "no client state depends on the previous frame",
  and the article says particles and animation are the hard part of playing in reverse. (same source)
- **Lesson.** [Inferred] SpaceGod's analytic orbits and transit paths are PA's predicted keyframes taken to the limit:
  the whole future is known until the plan changes. Record the *plan changes* (step curve) and sample only what the
  sim integrates.

### 1.4 Racing games: Forza Motorsport 3 (2009), GRID (2008)

- [Source] GRID's "Flashback" rewinds up to ten seconds, and difficulty sets how many uses you get.
  ([Wikipedia: Race Driver: Grid](https://en.wikipedia.org/wiki/Race_Driver:_Grid)) Forza 3's rewind goes back a few
  seconds per press (Wired says five; a hands-on estimated 10-15). It is not available in multiplayer, and lap times
  set with it are not "certified". ([Kotaku review](https://kotaku.com/forza-motorsport-3-review-definitively-maybe-5390122);
  [Wikipedia: Forza Motorsport 3](https://en.wikipedia.org/wiki/Forza_Motorsport_3))
- No implementation detail was found for either game. [Inferred] Both already recorded every car's transform each frame
  for their replay systems. Rewind is then a replay played backward plus a physics state restore at the resume point,
  and the replay buffer doubles as the visual track. That is the split this doc proposes.

### 1.5 Others

- **Life is Strange** (2015, UE3): [Source] the limit varies with the story, the player keeps her inventory, and a
  co-director called the rewind "a nightmare" technically (reported, wording unverified).
  ([Game Developer](https://gamedeveloper.com/design/my-favorite-features---time-rewind-in-life-is-strange)) It is
  mostly scripted scenes, so it maps poorly onto a simulation.
- **TimeShift** (2007, Saber3D + Havok): [Source] rewind is a suit power with the player immune to it, and the team
  wrote special game-over screens for paradox traps. No internals.
  ([GameSpot designer diary](https://www.gamespot.com/articles/timeshift-designer-diary-3-avoiding-time-travel-conundrums/1100-6142336))
- **Unreal Engine's replay system**: [Source] it rebuilds any moment from the start state plus checkpoints, then
  replays deltas (`GotoTimeInSeconds`, `demo.CheckpointSaveMaxMSPerFrame` spreads a checkpoint across frames, at the
  cost of mixed-frame data). ([Epic: DemoNetDriver and
  Streamers](https://dev.epicgames.com/documentation/unreal-engine/demonetdriver-and-streamers-in-unreal-engine)) It is
  the same idea as US-T99's ring (keyframe plus replay). [Inferred] SpaceGod's simulation lives in Core C++, not in
  replicated actors, so the engine's replay would not capture it. The useful lesson is spreading a capture across
  frames to avoid a hitch.
- **Snapshot interpolation (Glenn Fiedler)**: [Source] at 10 snapshots per second, linear interpolation shows visible
  jitter and pulsing. **Hermite with velocity in the snapshot showed no visible artifacts**. Slerp (not nlerp) on
  quaternions is enough for orientation, with no angular velocity needed. A cube's state quantizes to **80 bits
  (10 B)** (29-bit smallest-three quaternion, ~2 mm positions), and delta coding cuts unchanged objects to 1 bit.
  ([Snapshot Interpolation](https://gafferongames.com/post/snapshot_interpolation/);
  [Snapshot Compression](https://gafferongames.com/post/snapshot_compression/))

### 1.6 What it cost each game

| Game | Window | What it records | What it recomputes | Effort (as reported) |
|---|---|---|---|---|
| Sands of Time | ≤10 s, per Sand Tank | not published | not published | The game was designed round it; a 65-person team over ~2.5 years. A later Ubisoft programmer: "one of the most complex things I've done" |
| Braid | 30-60 min, 40 MB | full state every frame, full frames plus deltas | particles (seeded, closed form) | A GDC talk's worth of engineering; Blow: CPU, not memory, was the hard limit |
| Planetary Annihilation | whole match | per-value curves over lifetimes, 10 Hz, only on change | velocity, any in-between moment | The network architecture is built on it |
| Forza 3 / GRID | ~5-10 s, limited uses | not published (presumably the replay buffer) | physics from the restore point | Not reported |
| SpaceGod today (US-T99) | whole session | whole-world keyframes (3-11 MB compressed) plus a frame log | everything between keyframes, by deterministic replay | Phases 1-3 built |

## 2. The current rewind in this repo, and how a short track differs

[Repo] `docs/REWIND.md`, `Cpp/Source/SpaceGodCore/Persistence/RewindRing.{h,cpp}`,
`Cpp/Source/SpaceGod/UnitySpaceApp.Rewind.{inl,cpp}`, `RewindCommand.cpp`:

- **Keyframes** are the whole `SaveData` object graph (7.4-24 MB raw, 3-11 MB DEFLATE), thinned by game age under 512 MB.
  The session start is pinned. A capture is a synchronous walk taking **110-490 ms on the simulation thread**, so
  `MinWallGapS = 20` keeps keyframes at least 20 real seconds apart. No keyframe is taken while a `SaveBlocker` holds
  (between the stars, under FTL, latched to a hulk).
- **Frame log** (`RewindFrame`, under 112 B each, 40,000 frames ≈ 10 min): each frame's dt, `ClockState`,
  whether the player intervened, a `CoopDigest` hash every 60th frame, and the adventure point.
- **Seek** = restore the keyframe at or before T (0.18-0.46 s), then replay the logged frames through the game's own
  frame at the speed they were played. Real-time combat re-simulates at **about 18-20× real time**. A replay **stops at the
  first frame the player acted on**, because input is marked but not yet replayed. Co-op logs nothing, and a seek
  there lands on a keyframe.
- **Determinism**: all six Bench scenarios re-simulate bit-exactly from a restored keyframe (after US-T111).

| | US-T99 keyframe + log (exists) | Short track rewind (this proposal) |
|---|---|---|
| Purpose | Go back to any moment in the session and play on | Watch the last X seconds run backward, then optionally play on |
| Horizon | Whole session, thinned | X = 5-15 s (configurable) |
| Unit of storage | The whole world, every 20+ s | One small sample per moving body, 5-30 Hz |
| Smooth backward playback | No: it jumps to a keyframe, then plays forward | Yes: that is all it does |
| Scope | The whole galaxy | The system in view, plus whatever left or entered it in the window |
| Changes sim state? | Yes (load) | Playback: no. Resume: through US-T99 |
| Cost | 512 MB RAM, 110-490 ms hitches | ~1-10 MB, well under 1 ms a frame [Inferred] |

The two complement each other. The track is the *picture* of the recent past. US-T99 is how the *world* gets back
there. Braid's single-system approach is not possible here, because SpaceGod's world is two to three orders of
magnitude bigger than a Braid level.

## 3. The sampling rate ("polling rate") for a solar system

### 3.1 What needs no record (analytic)

[Repo] / [Inferred] these are evaluated at any time t straight from saved parameters, so rewinding them is "evaluate at t":

| Object | Why it needs no samples | What to record |
|---|---|---|
| Stars, planets, moons, rings, belts' bulk | Ephemeris (`HelioAt(jd)`) | nothing |
| Stations, gates, docking rings (`ParkingOrbit` with `Fixed`/`Center`) | Closed-form orbit (`OffsetAt(jd)`) | nothing |
| Ships off the battlefield (Parked, in transit, waiting for a window) | `Fleet::StateOf(ship, jd)` from `Phase`, `Orbit`, `PhaseStartJd/EndJd`, `Path` or `Conic` | **plan changes only**: a step record whenever `Phase`/`Orbit`/`Path` changes (a few hundred B, rare). An escort in formation follows its leader's plan plus `FormationSlot` |
| Kinetic rounds (`KineticRound`) | Dumb, non-homing, straight line: Pos(t) = Pos₀ + Vel·(t − t₀) | one spawn record (t₀, pos, vel, ids, fuse) plus one end record (hit, missed or shot down at t₁) |
| Beams, muzzle flashes (`FxShot`) | Presentation | one pulse event (t, from, to, look, duration) |
| Explosions, particles, debris bursts | Seeded and deterministic as a function of age (Braid's lesson) | one pulse event (t, position, kind, seed, scale) |
| Wrecks drifting, decoy clouds, mines | Drift at constant velocity or analytic decay | spawn plus end record (resample at 1-2 Hz if something pushes them) |

Caveat: [Inferred] `StateOf` must be pure for this. It must not depend on per-frame memos the way `Ephemeris.Helio`'s
memo does (REWIND.md notes the digest mutates that cache). Evaluating at past dates from the playback path must not
write the live world's caches, or a later keyframe would differ. Use a read-only evaluation, or save and restore the memo.

### 3.2 What needs samples, and how often

Interpolation: **cubic Hermite** on position using the recorded velocity (C¹-continuous, so no velocity kinks at
samples), and **slerp** on orientation (Fiedler: enough without angular velocity). Step curves for discrete state
(HP, shields, subsystem up or down, boost, throttle band, `DogfightManeuver`). **Event-driven extra samples** at every
discontinuity (spawn, death, collision or hit impulse, boost on or off, FTL engage), as PA added a keyframe at a bounce.
Never interpolate across a discontinuity flag.

**Error bound.** [Inferred] Linear interpolation of position errs by about a·h²/8 (a = acceleration, h = sample
spacing). Hermite with true velocities errs by orders less while the acceleration is smooth.

| Class | Typical accel | Linear error at 10 Hz | Bottleneck | **Recommended rate** |
|---|---|---|---|---|
| Capital ship in battle (`BattleUnit` Ship/Platform, AI) | ≤1 g (0.01 km/s²) | ~1 cm | Slow turns. Position is trivially smooth | **5 Hz** (2 Hz plus Hermite would pass; 5 Hz gives margin for broadside turns) |
| Fighter / small craft (`Dogfighting`, `FighterState`) | up to ~10 g | ~12 cm | **Rotation**: Break, Roll, Loop and StrafeFlip spin fast; at 3-6 rad/s, 10 Hz is 17-34° per interval, enough to show kinks | **20 Hz** |
| Missile, torpedo, interceptor, breacher, boarding pod (`Missile`) | high, homing curves | ~10 cm | Short life, sharp terminal turns, needs a clean impact point | **20 Hz** (rockets that fly straight: spawn record only, as for rounds) |
| The hull the camera rides (the player's craft, or an embodied unit) | any | — | The camera is on it, so any error is on screen | **30 Hz or every frame** |
| Hulls being towed or latched, mech walkers, EVA | low | — | Constraints | 10 Hz |
| Unit in a coarse battle step (`Advance(..., coarse=true)`) | — | — | The sim itself steps coarsely | sample at each sim step, no more |

**Key time samples in game time, rate them in wall time.** [Inferred] Smoothness is about what the eye sees per real
second. At warp 2 (1 min/s) a 10 wall-second track covers 10 game-minutes, and sampling at 20 Hz of *game* time would
mean 12,000 samples where 200 suffice. So the recorder samples at N per real second, and stamps each sample with session
game seconds (the same key the US-T99 ring uses). Above roughly warp 3, almost everything in a system is analytic
anyway, and battles resolve in coarse steps.

### 3.3 Bytes per sample and per object-second

[Inferred] A layout, using Fiedler's quantization ideas but kept simple and uncompressed:

| Field | Bytes |
|---|---|
| Position, float32 ×3, km relative to the ring's per-system origin (precision ~1 cm at 10⁵ km) | 12 |
| Velocity, float32 ×3 | 12 |
| Orientation, smallest-three quaternion (29 bits) plus 3 flag bits (discontinuity, boosting, firing) | 4 |
| Step-state slot: hull and shield fraction (2 × uint8), maneuver or throttle (uint8), spare | 4 |
| **Total per sample** (time is implicit: fixed rate per track, plus a tiny per-track side list for event samples) | **32** |

The designer's 40 B is a fair envelope once a few more fields join (a turret yaw, an engine glow value).

| Class | Rate | B/sample | **B per object-second** |
|---|---|---|---|
| Capital | 5 Hz | 32-40 | 160-200 |
| Fighter | 20 Hz | 32 | 640 |
| Missile | 20 Hz | 32 | 640 |
| Camera hull | 30 Hz (60 if every frame) | 40 | 1,200 (2,400) |
| Kinetic round | spawn plus end | ~64 per round | per round, not per second |
| Pulse event (beam, explosion, flash) | — | ~32-48 each | per event |
| Analytic ship plan change | — | ~200 per change | per change |

## 4. Memory budget (worked numbers)

[Inferred] All for X = 10 s, uncompressed:

- **The designer's envelope:** 2,000 objects × 10 Hz × 10 s × 40 B = **8.0 MB**.
- **A heavy battle in the watched system:**

| What | Count | Rate | Bytes for 10 s |
|---|---|---|---|
| Capitals and platforms | 40 | 5 Hz × 40 B | 80 KB |
| Fighters | 400 | 20 Hz × 32 B | 2.56 MB |
| Missiles in flight (average) | 600 | 20 Hz × 32 B | 3.84 MB |
| Kinetic rounds | 300 fired/s | 64 B each | 192 KB |
| Beams, explosions, flashes | 500 events/s | 40 B | 200 KB |
| Camera hull | 1 | 60 Hz × 40 B | 24 KB |
| Ships off the battlefield (analytic), plan changes | 1,000 | ~0.05 changes/s × 200 B | 100 KB |
| Departed or arrived bookkeeping, ids, presence | — | — | <50 KB |
| **Total** | | | **≈ 7.0 MB** |

- 30 s ≈ 21 MB, 60 s ≈ 42 MB. A quiet system (no battle) costs a few KB a second.
- For comparison, **one** US-T99 keyframe is 7.4-24 MB raw. The whole ring is 512 MB.
- Compression is optional at these sizes. Delta-coding samples against the previous one (Fiedler, Braid's "full
  frames plus deltas") would roughly halve it, but adds a decode chain on the reverse path. Braid was hit by exactly
  that: the codec does not like being read backward. Keep the samples raw in a ring. Revisit only if X grows past about
  60 s.
- **A hard cap**, in the spirit of the sand tank: a fixed byte budget for the ring (e.g. 32 MB). When it is exceeded,
  drop the oldest seconds. The effective X then shrinks in huge battles, and the UI shows the real reach. Keep one
  reserve: never let the camera hull's track be shorter than the others.

**Conclusion.** The reason to keep the window short is not the track's memory. It is (a) the design economy (PoP,
GRID and Forza all chose a few seconds and limited uses), (b) the resume cost (§6), and (c) a rewind longer than about
15 s is already what US-T99's timeline bar does.

## 5. Proposed design: the system track ring

### 5.1 Recorder

[Inferred] One `SystemTrack` per recorded system: the watched system, plus, for X seconds after the camera leaves it,
the one it left. It is written by a read-only observer after each simulation step, in the game layer or in a Core
helper the sim never calls, as with the keyframe ring. That keeps the Bench digests unchanged.

- **Track ids.** `Ship::Id` and `BattleUnit::Id` exist. `Missile` and `KineticRound` have **no id** ([Repo]
  Battle.h). The recorder hands out its own `TrackId` (uint32) per object, from a pointer-to-id map that is pruned when
  an object leaves. It must not add fields to saved Core types, or saves and keyframes would change.
- **`Track`** = {TrackId, kind, stable link (Ship id, faction, spec for the mesh), rate, a ring of `Sample[ceil(X·rate)+2]`,
  a small list of event samples, a step-curve list, **presence**}.
- **Presence** = a list of intervals [tEnter, tExit) in session game seconds, each with an **enter cause** (present at
  record start, launched from carrier C, fired by unit U, built, spawned by an adventure, FTL arrival at point P, gate
  exit from gate G, undocked) and an **exit cause** (destroyed with explosion event E, FTL departure at P with spool
  start ts, gate entry at G, docked or landed at D, recovered aboard carrier C, out of range of the recorder). A ship
  that left and came back in the window has two intervals.
- **Analytic tracks** keep only a step curve of plan snapshots: each holds the `ParkingOrbit`/`Path`/`Conic`/`Phase`
  fields needed to call a *pure* `StateOf` at any t inside its span.
- **Departed objects stay** in their system's ring until tExit < now − X. A capital that jumped out 6 s ago, or a
  freighter that entered a gate 3 s ago, is still drawn. An object that arrived 4 s ago has tEnter = now − 4, so a
  rewind of more than 4 s shows it vanishing back into its arrival effect.
- **Window edges.** If the camera itself changed system in the window, v1 caps the rewind at the camera's arrival
  time. v2 switches the playback view to the previous system's ring across the jump.
- **Cost per frame.** [Inferred] About 1,000-2,000 sample writes of 32 B into preallocated rings: tens of microseconds.
  The real cost is the map from objects to TrackIds, which is a hash lookup per moving object per sample.

### 5.2 Playback (visual rewind)

- **The sim is paused**, as with the pause key. Nothing in Core advances. Playback is a presentation mode.
- **Playback time** τ goes from now down to now − X. The speed accelerates (Braid's VCR feel; PoP's playback ran
  faster than real time), and the player can hold to scrub and release to stop.
- **For each track with τ ∈ presence**: sample the pose (Hermite and slerp, or a pure `StateOf(τ)` for analytic ones)
  and drive the existing render proxies. Planets and stations are evaluated at the `jd` matching τ, through the same
  read-only ephemeris path.
- **Rule from ChronoCam**: the view at τ must depend on τ alone, never on the previous rendered frame. Engine trails,
  smoothing filters, LOD hysteresis, camera lag and HUD easing all have to be rebuilt from the track (draw a trail as
  the last k samples before τ), or turned off during playback.
- **Effects played in reverse** (presence edges and pulse events crossing τ):
  - *FTL departure* plays backward: a flash collapses into the hull, which decelerates out of the spool.
  - *Gate entry* plays backward: the hull backs out of the gate's event surface.
  - *Arrival* plays backward: the hull drains into a reversed arrival flash and is gone before tEnter.
  - *Destruction* plays backward: the explosion contracts, debris converges, and the hull re-forms at the death sample.
  - *Rounds and beams*: a round slides back to its muzzle and disappears at t₀. A beam flickers by its pulse window.
  - [Inferred, unverified for this engine] Niagara systems are not built to run backward. Making each effect a
    function of age (shader- or curve-driven from (seed, age)), as Braid's particles are, is the robust path.
    Otherwise there are two cheaper options: re-spawn the effect each frame and advance it to its age (costly for heavy
    effects), or author a dedicated "reversed" variant of each departure, arrival and explosion effect.
- **HUD during playback.** Freeze the live HUD. Show a "REWIND −6.3 s" band, the timeline (reusing the US-T99 bar
  styling), and hull and shield bars from the step curves.
- **Audio.** Use a reversed whoosh and a filtered bed. Don't attempt reversed SFX per event (PA calls this the hard
  part).

### 5.3 Ending a rewind: three modes

1. **Back to now** (instant replay): end playback and unpause. Nothing changed. It is safe in all situations,
   including when the US-T99 resume is unavailable.
2. **Resume from τ** (the Sands of Time rewind): the world must become the world at τ. See §6.
3. **Checkpoint loop** (US-T169, parked): the same machinery with a pinned keyframe. Out of scope here, but the track
   ring is exactly what draws the "ghost" of earlier passes.

## 6. Resuming after a rewind: restoring the simulation

The track alone cannot resume. It holds positions, not the 7-24 MB of world (economy, crews, AI intents, fighter deck
state, missile targets, subsystem heat). Writing the recorded poses back onto the current world would leave every
other field at "now", which is inconsistent and breaks determinism. [Repo]: US-T111 showed how one stale reference
(a `FlightDeck`) already splits a replay.

**The options:**

| Option | How | Verdict |
|---|---|---|
| A. Snapshot at τ | Record a full keyframe every frame or second, so one exists at τ | Not possible: a capture is 110-490 ms and 3-11 MB. This is the Braid approach at a scale it cannot reach |
| B. Local snapshot (battle-only graph) | Snapshot only the `Battle` and its ships every ~1 s, and on resume restore just those | Breaks consistency: the clock, the rest of the system and the galaxy stay at now. Not recommended |
| **C. Keyframe plus deterministic replay (US-T99)** | Restore the newest keyframe K at or before τ, then replay the logged frames to τ | **Recommended.** It already exists and is measured bit-exact. It needs three fixes (below) |
| D. Shadow world on a worker | As C, but restore and replay a second `SaveData` on a worker thread **while** the visual playback runs, then swap it in | A later optimization that hides C's latency behind the playback. [Repo] `RewindRing::Restore` already builds a fresh graph without touching the live one, and the Bench steps Core headless, so Core can run off the game layer. But replay today goes through the game frame (`RewindFrameStart`), so a headless replay needs a Core-only replay driver |

**What C needs before it works in combat** (all [Repo] gaps, [Inferred] fixes):

1. **Input replay.** A seek stops at the first frame the player acted on. In a dogfight that is the next frame, so
   resume would land on the keyframe, up to tens of seconds before τ. This needs the input recorder (REWIND.md phase 3
   "Not yet"; US-T169 requirement 1: per-`EmbodiableKey` `PilotFrame`s plus orders plus clock keys, logged at the
   device or command seam). **It is the main blocker.**
2. **A keyframe at or before now − X, near it.** With `MinWallGapS = 20`, the newest keyframe at or before τ can be up
   to X + 20 s (worst case about X + 40 s) back. At 18× real-time replay, that is 1.5-3 s of loading for X = 10. Add a
   **short-horizon anchor rule**: when a battle runs in the watched system, keep the keyframe cadence at or below
   X/2 game-seconds, which needs phase 4's off-thread or incremental capture to remove the hitch. Then worst-case
   replay ≈ 1.5·X = 15 s of game time ≈ 0.8 s, plus 0.2-0.46 s of restore. Hide it behind the end of the playback,
   or use D.
3. **No keyframe under a `SaveBlocker`.** If the player's hull is spooling or under FTL, or latched to a hulk, no anchor
   exists. Either lift those blockers for rewind keyframes (they exist for save files), or offer only "back to now"
   in those states.

**The track's second job: drift check.** [Inferred] After replaying to τ, compare each sampled unit's simulated pose
with the track's sample at τ. Any difference beyond a few metres means the replay did not re-live the play. This is a
direct, per-unit version of the frame log's `CoopDigest` checks, and it says *which* unit drifted. On drift, the
toast already used by RewindSeek reports it.

**After the resume**, the single-timeline rule stands ([Repo]: "the old future is replaced"). The track ring after τ is
discarded, and recording restarts at τ.

## 7. Battle state

- **Rounds in flight, missiles, decoys, mines, damage, heat, boarding**: all live in the keyframe ([Repo] `Missile` and
  `KineticRound` are Core battle state). On resume they come back through C exactly. The track only draws them.
- **Visual-only state** (who fired from which mount, hit markers): [Repo] fields like `Missile::Shooter`/`Mount` are
  "presentation only ... not saved". The track's pulse events carry what playback needs.
- **Hits decided at the muzzle**: [Repo] a `KineticRound`'s `Hit` is rolled when it fires and applied on arrival. In
  playback that is just its end record. On resume it is exact, because replay rolls the same RNG.
- **Damage shown on hulls**: step curves of hull and shield fractions per unit, plus the destruction pulse. Section-level
  damage (hull sections knocked loose, US-T91) needs either a step record per section event or a "damage stage"
  byte. A step record is enough for the visuals.
- **Fighters launching and landing**: presence enter and exit causes "launched from C" and "recovered aboard C". The
  deck state itself is not in the track (it is in the keyframe, which US-T111 fixed for replay).

## 8. Co-op

[Repo] Co-op is deterministic lockstep. The host's rewind goes through `CoopLoadingHere` resync. A client's Shift+F8
is refused. Phase 4 plans a client's rewind as a host-ordered lockstep command.

- **Recording**: [Inferred] every peer records its own watched system from its own identical simulation. No track
  data crosses the network.
- **Back to now** (instant replay) can be **local and non-blocking**: a peer watches its own playback while its
  simulation runs on underneath, like a kill-cam. Nobody else is paused.
- **Resume from τ** must be a lockstep command ordered by the host (as phase 4 plans): every peer pauses and plays the
  rewind in sync. On resume, either (a) the host resyncs the world as today, which sends the whole world, or (b) every
  peer does option C locally. Option (b) needs the co-op frame log, which is off today, and the input recorder to
  cover every seat. Start with (a).
- **Policy**: limited charges ("sand") per player or per session, and a vote or host-only option, are design calls.
  Forza disabled rewind in multiplayer entirely ([Kotaku](https://kotaku.com/forza-motorsport-3-review-definitively-maybe-5390122)).

## 9. Phased plan and rough effort

[Inferred] Estimates for one engineer familiar with the codebase, in engineer-weeks (ew).

| Phase | Scope | Depends on | Effort |
|---|---|---|---|
| **0. Measure** | Count sampled objects per class in Bench `battle-warp0` and a big fleet battle. Prototype the recorder headless in Bench.Rewind and measure bytes and cost per frame. Confirm `StateOf` purity (memo writes) | — | 0.5 ew |
| **1. Track ring** | `SystemTrack`, TrackId map, per-class rates, event samples, presence with causes, plan-change records for analytic ships, the byte cap, the previous-system ring | 0 | 1.5-2 ew |
| **2. Instant replay (back to now)** | Pause, backward playback with accelerating speed and scrub, render proxies driven from the tracks, analytic evaluation at τ, trails rebuilt from the track, HUD band. Local and non-blocking in co-op | 1 | 2-3 ew |
| **3. Reverse effects** | Reversed FTL departure and arrival, gate entry and exit, explosion and reassembly, rounds and beams. Effects made functions of (seed, age), or reversed variants authored | 2; art time | 1.5-2.5 ew |
| **4. Resume from τ** | Input recorder (US-T169 req. 1), short-horizon anchor keyframes (needs REWIND.md phase 4's off-thread capture), the `SaveBlocker` policy for rewind keyframes, resume through `RewindSeek(τ)`, the drift check against the track | 2; US-T99 phase 4 | 3-4 ew (plus phase 4's capture work if not done) |
| **5. Co-op resume** | Host-ordered command, synchronized playback, resync on resume | 4 | 1 ew |
| **6. Optional: shadow world** | A headless Core replay driver on a worker during playback, swapped in at release (option D) | 4 | 2 ew |

**Total: about 9-13 ew** for phases 0-5 (2-3 months), plus phase 4 of US-T99 if it is not already built. Phases 0-2 alone
(about 4-5.5 ew) deliver a shippable "watch the last 10 s backward" feature with no risk to the simulation or the
Bench digests.

## 10. Open questions

- Is X counted in wall seconds (what the player saw, recommended) or game seconds? At warp they differ by 60× or more.
- Limited charges (sand tanks), a cooldown (Forza's 30 s), or free use?
- Does "resume" exist in v1 at all, or does v1 ship instant replay only, with US-T99's timeline bar covering
  "go back and play on" until the input recorder lands?
- Rewinding across the player's own jump between systems: cap at arrival (v1), or show the previous system (v2)?
- Do the reverse effects need art time, or can the shader-driven (seed, age) versions be made from existing assets?

## Sources

- Wikipedia, *Prince of Persia: The Sands of Time*: https://en.wikipedia.org/wiki/Prince_of_Persia:_The_Sands_of_Time
- J. Mechner, "The Sands of Time: Crafting a Video Game Story", electronic book review (2008): https://electronicbookreview.com/essay/the-sands-of-time-crafting-a-video-game-story/
- Ubisoft News, "Prince of Persia: Creating the Sands of Time trilogy": https://news.ubisoft.com/en-us/article/3oAqiLg5k7GazIw5OWYTO2
- Ubisoft News, "How Prince of Persia made its double comeback in 2024": https://news.ubisoft.com/en-us/article/50BEgIWDER8qtc0tD3N26h/how-prince-of-persia-made-its-double-comeback-in-2024
- Worthplaying, Sands of Time development team interview (2003): https://worthplaying.com/article/2003/9/18/interviews/12541-prince-of-persia-the-sands-of-time-development-team-interview/
- Sirus Gaming on the remake ("headache technically"): https://sirusgaming.com/prince-of-persia-the-sands-of-time-principle-of-rewind-headache
- The AU Review, Abdelhak Elguess on *The Lost Crown*: https://www.theaureview.com/games/interview-abdelhak-elguess-and-prince-of-persia-the-lost-crown/
- GDC Vault, J. Blow, "The Implementation of Rewind in Braid" (2010): https://gdcvault.com/play/1012210/The-Implementation-of-Rewind-in
- Hacker News, Blow's comments on Braid's rewind: https://news.ycombinator.com/item?id=9484197 and https://news.ycombinator.com/item?id=39773041
- J. Blow on X (2024), particles: https://x.com/Jonathan_Blow/status/1770249973596410286
- J. Reid, "Roguelike Time Traveling", IRDC 2016 slides: https://docs.google.com/presentation/d/1c2yuA8Gji-NqrOFZ2kIWdA4GQMiRAeYsTuYgQHM4hJ0/htmlpresent
- C. LeBlanc, "Recreating the time mechanics of Braid" (Game Developer, 2013): https://gamedeveloper.com/programming/recreating-the-time-mechanics-of-braid-part-1-
- F. Smith, "The Tech of Planetary Annihilation: ChronoCam": https://www.forrestthewoods.com/blog/tech_of_planetary_annihilation_chrono_cam/
- G. Fiedler, "Snapshot Interpolation": https://gafferongames.com/post/snapshot_interpolation/
- G. Fiedler, "Snapshot Compression": https://gafferongames.com/post/snapshot_compression/
- Wikipedia, *Race Driver: Grid*: https://en.wikipedia.org/wiki/Race_Driver:_Grid
- Wikipedia, *Forza Motorsport 3*: https://en.wikipedia.org/wiki/Forza_Motorsport_3
- Kotaku, Forza Motorsport 3 review: https://kotaku.com/forza-motorsport-3-review-definitively-maybe-5390122
- Game Developer, "My favorite features: time rewind in Life is Strange": https://gamedeveloper.com/design/my-favorite-features---time-rewind-in-life-is-strange
- GameSpot, TimeShift Designer Diary #3: https://www.gamespot.com/articles/timeshift-designer-diary-3-avoiding-time-travel-conundrums/1100-6142336
- Epic Games, DemoNetDriver and Streamers: https://dev.epicgames.com/documentation/unreal-engine/demonetdriver-and-streamers-in-unreal-engine
- Repo: `docs/REWIND.md`, `Cpp/Source/SpaceGodCore/Persistence/RewindRing.h`, `Cpp/Source/SpaceGod/UnitySpaceApp.Rewind.inl`,
  `Cpp/Source/SpaceGodCore/Combat/Battle.h` (`BattleUnit`, `Missile`, `KineticRound`, `Battle::Advance`),
  `Cpp/Source/SpaceGodCore/Ships.h` (`ParkingOrbit`, `Ship` phase fields, `ShipState`), `Cpp/Source/SpaceGodCore/Fleet.h`
  (`StateOf`), `Cpp/Source/SpaceGodCore/Systems/GateTransit.h`.
