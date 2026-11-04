# The race: C++ against C# (US-T46)

Unreal.SpaceGod is Unity.SpaceGod remade in Unreal Engine 5.8 twice (docs/MIGRATION.md):

- **C++** (`Cpp/`): the original's Core translated file for file into C++20 and kept bit-identical to it, the Runtime
  translated onto a C++ Unity facade.
- **C#** (`CSharp/`): UnrealSharp on .NET 10, running the original's own Core source unchanged and its Runtime source on a
  C# Unity facade.

Both play the whole game and both have lockstep co-op. This page races them: the same simulation, the same scripted game,
the same machine. The page version: https://claude.ai/artifact/9FPW33Y1uUubSXkZHeXhgB

## Verdict

**Neither track is faster across the board. The differences that hold up across runs:**

| Where | Winner | By how much | Signal or noise |
|---|---|---|---|
| Headless simulation, heavy ticks (a year at 1 yr/s, a year of Sol's economy) | level | within the spread of the runs | noise |
| Headless simulation, light ticks (Sol idle, a battle): median tick | **.NET** | 0.6 ms vs 1.1 ms (about 1.7×) | signal (every run) |
| Building the galaxy (New Game's `Galaxy.CreateDefault`, headless) | **C++** | 3.9–4.0 s vs 6.1–6.5 s | signal |
| Simulation memory (headless peak working set) | **C++** | 30–60 MB vs 90–150 MB | signal |
| In game, simulation time per frame at 1 week/s | **C++** | 35 ms vs 82 ms p50 (2.4×) | signal (every run) |
| In game, simulation time per frame at the default warp | **C++** | 1.0 ms vs 1.5 ms p50 | signal |
| In game, the rest of the frame (views, facade, push to Unreal) | **C#** in the race; **C++** since US-T62 | race: ≈6.9 ms vs ≈10.8 ms at Sol idle (whole Unity frame 8.4 vs 11.8 ms p50, minus the simulation). After US-T62's facade fix: C++ whole Unity frame 3.7 ms p50 at Sol idle (10.3 before, same machine, interleaved) | signal: it was a C++ facade cost, not the language (see "After US-T62" in section 2) |
| In game, frame time at Sol idle | **C#** in the race; **C++** since US-T62 | race: 12.2 ms vs 15.0 ms p50; C++ after US-T62: 6.3 ms (12.8 before) | signal, from the line above |
| In game, frame time in a battle | **C++** | race: p95 27 ms vs 226 ms, GPU 2.5 vs 116 ms (a C# packaging defect, US-T61). After US-T61: C# 11.4 / 21.4 ms p50 / p95, GPU 0.9 ms; C++ after US-T62: 5.2 / 14.6 ms | signal (see the battle notes) |
| In game, frame time at 1 week/s | **C++** | 53 ms vs 97 ms p50 | signal |
| Startup: launch to the main menu / to playable | **C++** | 2.4 s vs 4.1 s / 8.8 s vs 20.5 s | signal |
| Memory in game (peak working set) | **C++** | 2.3 GB vs 5.4 GB | signal |
| Save / load (the original's format, 4 MB) | **C#** | 282 / 254 ms vs 332 / 387 ms (means) | signal |
| Shipping package without symbols | **C++** | 411 MB vs 483 MB | the .NET runtime (150 MB) |
| Lines of code written | **C#** | ≈15,100 vs ≈138,000 | the C# track runs the original's 87,600 lines as they are |
| Iterating on the code | mixed | Runtime file 4.9 s vs 11.6 s; a build with nothing changed 149 s vs 1.7 s | signal |
| Faithfulness to the original | level | both 0 missing; 1 divergence each (the one renderer); simulation bit-identical | — |

**Recommendation: make C++ the primary track from now on.** The usual case for C# in Unreal is that it lets someone who
knows only C# write the game. That does not apply here: the author of both tracks is an AI that writes C++ as fluently
as C#, and Unreal is a C++ engine, whose own API, samples, debugger, profilers, Live Coding and packaging all assume C++.
The measurements show no real C# win that would outweigh that. Where C# is ahead, the cause is fixable in the C++ code
(the engine-side frame, since fixed by US-T62) or small (save/load by 50–130 ms, the lighter headless tick). Where C++ is ahead it is
large and structural: startup 2.3× faster, half the memory, the in-game simulation 1.5–2.4× faster, no 150 MB runtime to
ship, no young binding layer (UnrealSharp is pre-1.0, pinned by commit) with its build-time and packaging workarounds.

**What the C# track is still worth.** It runs Unity.SpaceGod's own Core (184 files, 67,469 lines) and most of its
Runtime unchanged, so it is the reference the C++ translation is checked against: the Bench's five digests compare the
C++ Core with the original's own code bit for bit, and the C# game shows what the original's Runtime does on Unreal
whenever the C++ track's behaviour is in doubt. Keep it building as that parity check (it needs no translation, only its facade kept in step),
not as a second game to finish features in.

## Method

**Machine.** Intel Core i9-12900KF (16 cores, 24 threads), 32 GB RAM, NVIDIA GeForce RTX 3080 Ti (driver 32.0.16.1742),
Windows 11 Pro 26200; UE 5.8 (installed build), MSVC 19.44 / 19.50 (UE's toolchain for the games), .NET 10.0.14 (the C#
game's runtime under UnrealSharp), .NET 8 for reference.

**What was built.** Every game, package and build time comes from `Builds/Snap`, an export of commit `3689308` with an
empty `Intermediate` (so "clean" means clean, and no other agent's uncommitted work is in it), plus the race tour as
committed in `876ad3d` and `0a28b8e` and the one-line compile fix to `ShaderBinding.cpp` that `3689308` lacked. The race's
instruments came in `64b64b8`. The headless Bench uses the Core at `0c2c7f6` (US-T49), the same Core as `3689308`.

**A shared machine.** Other agents shared the workstation all evening (an Unreal game of another repo, shader compiles,
test runs). Every run first waited until the rest of the machine used under 8% of the CPUs (up to 10 minutes) and then
recorded what was still running and how much CPU it took while the run lasted. The in-game runs could rarely get a
quiet machine: another repo's Unreal game was running for all six (31–72% of the CPUs elsewhere before they started).
Both tracks were run interleaved (C++, C#, C++, C#…), so the load fell on both alike, and a difference is only called a
signal when every run of one track beats every run of the other.

**One measuring instrument.** The frame sampler (`Shared/Plugins/SpaceGodRender/Source/SpaceGodRace`) is one C++ module
loaded by both games. Each frame it records the wall time between two frame ends, the engine's game, render and RHI
thread times and the GPU frame time (the values Unreal computes in every configuration, Shipping included, for dynamic
resolution), and the working set. Each track adds its own three probes (marked `US-T46` in the code): the simulation
steps of `UnitySpaceApp.Update` (`Clock.Advance`, `Galaxy.Update`, the adventures), the whole Unity frame (the C# game
mode's `Step`, the C++ `AUnitySpaceAppActor::Step`) and the IMGUI pass. Percentiles are nearest-rank, as the Bench's.

**The same game on both.** `-SpaceGodRace` runs one script on each track, line for line the same
(`CSharp/.../Runtime/Race/RaceRun.cs`, `Cpp/.../Smoke/SpaceGodRace.cpp`): the main menu for 30 frames, New Game at the
screenshots' fixed moment and seed (2026-11-03 12:00 UTC, seed 1), 120 frames to settle, then

1. **sol-idle**: 1,200 frames of Sol at the default warp (1 hour/s), the camera beside Earth;
2. **week-warp**: 30 frames at 1 week/s (3.5 days of game time; every system stepped every frame);
3. **battle**: two battleships and a cruiser each for Sol and the Reach spawned off Uranus (SceneTour's broadside
   showcase), watched at battle time, then 1 minute/s, 1,200 frames;
4. **galaxy-ftl**: the view's own trip to Alpha Centauri (StarTravel and the system's promotion), until it arrives and 300
   frames more; then home again, untimed;
5. three saves and three loads, each call timed.

The games run with `-UseFixedTimeStep`, so every frame is the same slice of game time on both tracks (the Shipping
games ignore `-FPS=60` and use Unreal's default fixed step, 1/30 s: their clock advances twice as far per frame as the
editor runs') and both simulate the very same game frame for frame:
the same ship counts and dates at every phase end on all six runs (271 ships after Sol idle, the battle over with 7
units, the trip 268 frames long). With a fixed step Unreal does not wait for the next frame, and VSync and the frame
cap are off, so a frame takes as long as its work. Window 1600×900, no sound. `tools/race.ps1` runs the packaged
Shipping game three times per track.

The highest warps are not in the in-game tour: at 1 year/s, and at 1 month/s for more than a couple of weeks, both
tracks reach a parked ship that has no parking orbit (US-T59, in the original Core: an escort's marker or a paid-off
hired escort left leaderless with no motion of its own, read as Parked with a null `Orbit`; fixed on the C++ track in
`eacc7d4` after these runs, U.SPC-T602 in Unity.SpaceGod for the C# track), where the C++ game crashes and the C# game throws every frame. The headless Bench does not reach it
(it never asks every ship's position each frame) and covers those warps instead.

## 1. Headless simulation (the Bench)

`Bench.Cpp` (the C++ Core, Release: `/O2 /GL /arch:AVX`, LTCG) against `Bench.CSharp` on .NET 10 (the C# game's runtime)
and .NET 8, the five scenarios of Bench/README.md, interleaved (each scenario: C++, .NET 10, .NET 8 back to back), three
rounds; 45 runs, **all 45 digest-identical** (sol-idle `B30509691F52C9E0`, battle `ABD80179E1D7F1AC`, galaxy-ftl
`F1E614170D1B1960`, economy-year `7F6533416B74D14D`, high-warp `A8A790EC13FD23C8`). The first two rounds ran beside other
agents' builds (up to 30–37% of the CPUs elsewhere during a run, which shows as runs of 1.5–2× the quiet ones), the third
on a quiet machine. Two tables: the medians of all three rounds, and of the quiet runs only (the rest of the machine under
12% during the run; one or two runs per cell). Tick times in ms; build is `Galaxy.CreateDefault`.

**Quiet runs** (the numbers to compare):

| Scenario | Runner | Build s | Tick mean | p50 | p95 | p99 | Alloc MB | Peak WS MB |
|---|---|---|---|---|---|---|---|---|
| sol-idle (3,600 ticks) | C++ | **4.0** | 2.09 | 1.07 | 2.09 | 4.9 | 462 | **32** |
| | .NET 10 | 6.1 | 2.04 | **0.63** | 1.63 | 4.3 | 398 | 98 |
| | .NET 8 | 6.4 | 1.85 | 0.64 | 1.69 | 5.0 | 409 | 97 |
| battle (2,149 ticks) | C++ | **4.0** | 1.93 | 1.29 | 5.19 | **11.8** | 284 | **30** |
| | .NET 10 | 6.5 | 1.99 | **0.71** | 6.54 | 27.9 | 224 | 95 |
| | .NET 8 | 6.2 | 1.80 | 0.61 | 5.49 | 33.2 | 264 | 91 |
| galaxy-ftl (745 ticks) | C++ | **3.9** | 7.71 | 2.99 | 24.8 | **49.7** | 850 | **35** |
| | .NET 10 | 6.5 | 8.63 | 2.70 | 24.9 | 69.0 | 557 | 121 |
| | .NET 8 | 6.1 | 7.81 | 2.56 | 24.8 | 50.9 | 590 | 109 |
| economy-year (731 ticks) | C++ | **3.9** | 114.6 | 109.7 | 148.1 | 201 | 4,661 | **62** |
| | .NET 10 | 6.3 | 108.4 | 101.9 | 144.2 | 196 | 4,041 | 151 |
| | .NET 8 | 6.1 | 107.3 | 102.5 | 140.3 | 186 | 4,211 | 149 |
| high-warp (60 ticks) | C++ | **3.9** | 1,255 | 1,227 | 1,533 | 1,638 | 3,818 | **62** |
| | .NET 10 | 6.5 | 1,213 | 1,168 | 1,586 | 1,865 | 3,457 | 138 |
| | .NET 8 | 6.4 | 1,249 | 1,209 | 1,577 | 1,695 | 3,624 | 134 |

**All three rounds** (medians; the busy rounds included):

| Scenario | C++ mean / p50 / p99 | .NET 10 mean / p50 / p99 | .NET 8 mean / p50 / p99 |
|---|---|---|---|
| sol-idle | 2.11 / 1.08 / 4.97 | 2.07 / 0.63 / 4.48 | 1.85 / 0.64 / 4.97 |
| battle | 2.02 / 1.36 / 12.6 | 2.04 / 0.71 / 34.9 | 1.82 / 0.63 / 33.0 |
| galaxy-ftl | 7.71 / 2.99 / 49.7 | 8.75 / 2.75 / 77.0 | 7.85 / 2.57 / 52.3 |
| economy-year | 204 / 114 / 843 (115–209) | 110 / 103 / 200 | 113 / 108 / 192 |
| high-warp | 1,297 / 1,269 / 1,638 | 2,183 / 1,341 / 6,215 (1,213–2,433) | 1,267 / 1,220 / 1,742 |

How to read it:

- **Heavy ticks are level.** A year at 1 yr/s and a year of Sol's economy differ by 3–6% between C++ and .NET 10 on
  the quiet runs, inside the spread a single busy run adds (the C++ economy-year mean of 204 ms and the .NET 10
  high-warp mean of 2,183 ms are each one busy round). About a third of both runners' time is the same CRT maths
  library (MIGRATION.md, "C++ performance").
- **.NET wins the median light tick** (0.63 vs 1.07 ms at Sol idle, 0.71 vs 1.29 ms in the battle) on every run, as in
  US-T42: small per-object work, where a JIT with a bump allocator and no reference counting is cheaper than
  `shared_ptr`s and the CRT heap. Its means are not lower, because **.NET's tail is longer**: p99 28 vs 12 ms in the
  battle, 69 vs 50 ms on the trip (GC pauses and tier-up).
- **C++ builds the galaxy 1.6× faster** (3.9–4.0 s against 6.1–6.5 s) and **uses a third of the memory** (30–62 MB peak
  against 91–151 MB), on every run.
- **.NET 10 against .NET 8**: no consistent difference (each wins some scenarios by a few percent).
- **US-T49's question** (a single C++ run after the Dictionary fix came out ~10% slower than US-T42's): not confirmed.
  The quiet C++ high-warp tick here is 1,255 ms mean / 1,227 ms p50, against US-T42's 1,379 / 1,282 ms; the ~10% was the
  machine.

## 2. In game (Shipping packages)

Medians of three runs per track (the range of the three in brackets where it matters). ms per frame.

**Sol idle** (1,200 frames, the default warp):

| | C++ | C# |
|---|---|---|
| Frame, p50 / p95 / p99 | **15.0** / 28.1 / 33.6 | **12.2** / 23.3 / 36.0 |
| Game thread p50 | 15.3 | 12.5 |
| Render thread / RHI thread p50 | 2.1 / 1.2 | 2.1 / 1.2 |
| GPU p50 / p95 | 1.4 / 3.8 | 0.7 / 1.7 |
| Simulation per frame p50 / p95 / mean | **1.0** / 2.0 / 1.3 | **1.5** / 4.3 / 2.5 |
| Whole Unity frame (app) p50 / p95 | **11.8** / 22.3 | **8.4** / 18.0 |
| IMGUI pass p50 | 1.2 | 1.0 |

**1 week/s** (30 frames, each 2.8 hours of game time):

| | C++ | C# |
|---|---|---|
| Frame, p50 / p95 / p99 | **53.4** [44–54] / 73.3 / 158 | **97.2** [91–98] / 179 / 371 |
| Simulation per frame p50 / p95 / mean | **34.8** / 59.5 / 39.7 | **81.9** / 165 / 97.1 |
| Whole Unity frame p50 | 47.5 | 89.9 |

**Battle** (1,200 frames):

| | C++ | C# |
|---|---|---|
| Frame, p50 / p95 / p99 / mean | 15.1 / **27.2** / 32.7 / 16.5 | 11.4 / **21.4** / 27.9 / 13.2 (US-T61; was 16.7 / 225.5 / 228.8 / 77.9) |
| Game thread p50 / p95 | 15.2 / 25.7 | 11.7 / 20.2 |
| GPU p50 / p95 | **2.5** / 5.0 | **0.9** / 4.9 (was 115.6 / 120.2) |
| Simulation per frame p50 / p95 | 1.0 / 1.4 | 1.4 / 2.5 |
| Whole Unity frame p50 | 12.4 | 8.2 |

**The trip to Alpha Centauri** (567 frames):

| | C++ | C# |
|---|---|---|
| Frame, p50 / p95 / p99 / mean | 7.7 / 16.1 / **27.0** / 8.7 | 6.3 / 17.6 / **107.3** / 12.4 |
| Simulation per frame p50 / mean | 0.23 / 0.54 | 0.38 / 1.02 |
| Whole Unity frame p50 | 5.9 | 4.0 |

**Startup, memory, saves:**

| | C++ | C# |
|---|---|---|
| Launch to the first frame (the main menu drawn) | **2.4 s** [2.3–3.0] | **4.1 s** [3.4–4.5] |
| New Game to playable (galaxy built, first game frame run) | **6.1 s** [5.6–6.3] | **16.2 s** [15.4–16.3] |
| Launch to playable | **8.8 s** | **20.5 s** |
| Peak working set (whole run) | **2.3 GB** | **5.4 GB** |
| Working set during Sol idle / battle | 1.6 / 1.5 GB | 2.8 / 4.1 GB |
| Save (mean of 3; min) | 332 ms (319) | **282 ms** (230) |
| Load (mean of 3; min) | 387 ms (356) | **254 ms** (219) |

How to read it:

- **The simulation is cheaper in C++ in game**, at every warp, and by much more than in the headless Bench (2.4× at 1
  week/s, where the Bench has the two level). The C# game runs the Core inside a 5 GB process with Unreal's own threads
  and allocations around it; its GC pauses show in the p95 and p99 (165 ms p95 at 1 week/s against C++'s 59 ms).
- **The rest of the frame is cheaper in C#.** The whole Unity frame minus the simulation is about 10.8 ms (C++) against
  6.9 ms (C#) at Sol idle. Both run the same original Runtime code over a facade, so this is the C++ facade's cost
  (views pushed into `UDynamicMeshComponent`s, the scene sync), not C++ itself: US-T62. It is what makes the C# idle
  frame 2.8 ms shorter. Fixed since: see "After US-T62" below.

**After US-T62** (the C++ facade fix, `ff65b0e`; measured later on the same machine, not part of the race above). The cost
was `UnityScene`'s, not the views': LaneView's hyperlanes mesh (12,296 vertices, rebuilt every frame) was converted to a
new `FDynamicMesh3`, copied and `SetMesh`'d every frame, which also rebuilds its scene proxy (in battle also the shield
rings and battle effects), and the scene's renderers and particle systems were found each frame by casting every
component of every GameObject. A mesh rebuilt with the same triangles now has its vertex values written in place and
pushed to its render buffers (`FastNotifyPositionsUpdated`), and the lists are kept between changes of the scene. The
race's C++ Shipping package (`3689308`) against one with the fix, three runs each, interleaved, same tour (271 ships after
Sol idle, the battle over with 7 units, the trip 268 frames); medians, ms, the range of the three in brackets; every run
of the fix beats every run without it:

| | C++ before | C++ after US-T62 | C# (the race's runs) |
|---|---|---|---|
| Sol idle: whole Unity frame p50 / p95 | 10.3 [8.9–10.8] / 20.3 | **3.7** [3.3–3.7] / 14.3 | 8.4 / 18.0 |
| Sol idle: frame p50 / p95 | 12.8 [11.4–13.6] / 24.8 | **6.3** [5.5–6.3] / 18.6 | 12.2 / 23.3 |
| Battle: whole Unity frame p50 | 11.0 [9.2–12.1] | **3.4** [3.4–3.6] | 7.9 |
| Battle: frame p50 / p95 | 13.3 / 24.6 | **5.2** / 14.6 | 16.7 / 225.5 |
| Trip: whole Unity frame p50 | 5.3 [5.3–5.4] | **1.5** [1.4–1.5] | 4.0 |
| Trip: frame p50 | 7.0 | **2.6** | 6.3 |
| Simulation per frame p50 (Sol idle / battle) | 0.88 / 0.92 | 0.93 / 0.87 | 1.5 / 1.4 |

The p95s keep the frames on which the hyperlanes' triangles change (a ship joins or leaves a lane, a ring passes the
2.5-pixel cut-off): those still convert in full. At 1 week/s the frame is the simulation's (41 ms p50 both).

- **The C# battle was GPU-bound** at ~116 ms a frame from the battle's end on (C++: 2.5 ms on the same frames): a
  packaging defect, not the battle view (US-T61). The C# package cooked none of the SpaceGodRender plugin's content
  (the facade loads it by path only), so every renderer drew with the engine's default material: lit, hence black
  under no lights, and without World Position Offset, so the asteroid belt's 55 chunks rasterised their raw
  placeholder triangles around the camera, which covered the screen 225,000 times over once the near plane, hugging
  the flagship after the fight, fell inside them (`ProfileGPU` in a Development package: 120 of the frame's 134 ms in
  those draws). `CSharp/Config/DefaultGame.ini` now cooks `/SpaceGodRender`, and the chunks' placeholder triangles
  have zero area, as the C++ track's. Re-measured, three Shipping runs (same ships, dates and battle outcome): frame
  11.4 / 21.4 ms p50 / p95, GPU 0.9 / 4.9 ms; battle frames at 300 and 600 match the editor's pixel for pixel (0 of
  1,440,000 pixels over threshold 24). The race's C# in-game numbers above were all taken on that black, default-material
  scene. The C++ package (`Builds/Cpp`, rebuilt 2026-11-04) has no `/SpaceGodRender` assets in its container either
  (its ShaderBinding loads them by path too), so its in-game numbers are open to the same question.
- **Startup and memory are C++'s by a wide margin.** The C# game JIT-compiles the Core on first use (UnrealSharp ships no
  ReadyToRun or Native AOT) and builds the galaxy on a cold JIT: New Game takes 16 s against 6 s.
- **Saves and loads are faster in C#**, by 50 ms (save) and 130 ms (load): the original's serializer on .NET against its
  C++ translation.

A Development package of each was also built (sizes below); the in-game numbers are the Shipping packages'.

## 3. Co-op (host and client on localhost)

`tools/play.ps1 -Mode coop-smoke` on each track from the snapshot's editor binaries: two headless instances (`-nullrhi`,
Development), a listen server and a client, the same script on both tracks (the client joins and loads the host's save,
warps, selects, takes a station's seat and keeps it through a resync it asks for, flies a freighter's helm on its own
input, trades, takes a fighter's cockpit, speaks for a polity on the galaxy map, hails, sends a gift, takes the party to
Alpha Centauri, has the host load its quicksave). The shared sampler wrote each instance's frames and the net driver's
byte totals (`SPACEGOD_RACE_DIR`). Headless there is no game-thread or GPU split, and the host paces itself at 60 fps
(the smoke's `targetFrameRate`), so a frame's wall time is its pacing plus any hitch.

| | C++ host | C++ client | C# host | C# client |
|---|---|---|---|---|
| Frame p50 / p95 / p99, ms | 16.9 / 31.0 / 37.3 | 16.8 / 242 / 272 | 17.1 / 42.6 / 76.7 | 4.4 / 41.2 / 89.8 |
| Bytes sent over the session | 19.0 MB | 0.32 MB | 21.5 MB | 0.32 MB |
| of which the three syncs' saves (base64) | ≈17.5 MB (3 × 4.3–4.4 MB) | | ≈20.3 MB (3 × 4.9–5.2 MB) | |
| Steady lockstep traffic, host to client | ≈11 KB/s | | ≈12 KB/s | |
| Resync on the host: capture / its own reload | 329–434 ms / 307–454 ms | | 257–362 ms / 239–330 ms | |
| Resync on the client: load the host's save | | 389–472 ms | | 283–463 ms |
| Digests compared, identical | 59 / 59 | | 55 / 55 | |
| The smoke's 15 checks | 10 passed (15 since US-T64) | | 15 passed | |
| Peak working set | 2.2 GB | 2.2 GB | 4.5 GB | 3.2 GB |

Both tracks keep the lockstep exact (every digest identical, no DESYNC) at the same cost on the wire; the frames are a
few hundred bytes each and a resync is the save, 4–5 MB, about a second end to end on localhost. The C# save is about
15% larger (5.0 vs 4.4 MB for the same scenario). Resyncs are a little faster on
C#, as single-player saves and loads are. The C++ client's long p95 is its catch-up after each load (it replays the
130–145 frames that arrived meanwhile in fewer, longer ticks; the C# client takes more, shorter ticks). In this run the
C++ smoke missed five of its fifteen checks; the digests still matched throughout, and the host carried out every ask.
US-T64 found the cause in the script: it pressed each step a fixed number of frames after the ask it follows, but an ask
comes back only once the client has replayed as far as the host was when it arrived, and in the fight this client fell
up to about 15 s behind, so the cockpit's keys were pressed in no cockpit and the host's load, 700 frames ahead of the
client, replaced the frames that carried the statecraft. The script now waits for each ask to come back (docs/MIGRATION.md,
Co-op, US-T64). Since then the C++ smoke passes 15 of 15 in every run: 3 of 3 from the editor binaries (as here), 3 of 3
from a Shipping package and 3 of 3 from a Development package (`-PackageDir`; a Shipping game could not host or join
from its command line before, and a packaged client's camera went NaN on its first focus, both fixed there too), and 2
of 2 with the client pinned to one or two cores (299 and 173 frames behind at most).

## 4. Developer side

All builds in the snapshot of `3689308` (`Builds/Snap`, an export of the commit with a fresh `Intermediate`, so a clean
build is really clean), UBT at `-MaxParallelActions=4` (the repo's default), MSVC 19.50 for the games (UBT's choice; the standalone Core and Bench.Cpp use 19.44),
.NET SDK 10.0.403. One build at a time; the CPU the rest of the machine used is in the last column (other repos' agents
were running an Unreal game and test suites for part of the evening, which is noted where it happened).

| Step | C++ | C# | Rest of machine |
|---|---|---|---|
| Clean build, Development editor target (all modules, from an empty Intermediate) | **355 s** (329 actions: SpaceGodCore + UnrealSpaceGod + plugin) | **293 s** (245 actions: UnrealSharp's C++ + glue, then the managed publish) | 3–5% |
| Build with nothing changed | **1.7 s** | **149 s** (UHT 133 s: UnrealSharp's glue generator found the engine glue changed and rebuilt its C# bindings) | 2–5% |
| One Core file touched (`Fleet.cpp` / the original's `Fleet.cs`) | **7.8 s** (compile + relink SpaceGodCore) | **8.1 s** (managed rebuild, `BuildUserSolution`) | 2–5% |
| One Runtime file touched (`UnitySpaceApp.Hauler.cpp` / `.cs`) | **11.6 s** | **4.9 s** (managed rebuild) | 3–8% |
| In the editor | Live Coding (relink the module, patch the running editor) | hot reload (the watcher rebuilds `Script/` and swaps the collectible AssemblyLoadContext) | not timed |
| Standalone Core + headless runner, Release, from clean | Core lib 71–110 s (4 × cl, /GL) + Bench.Cpp link 94–155 s (LTCG) | Bench.CSharp + the original Core, both TFMs: **9.6 s** | busy |
| Package, Development (BuildCookRun + C#'s PackageProject) | 1,119 s (first cook on the forward renderer: every shader compiled cold) | 422 s (shaders already in the shared DDC) | 5% |
| Package, Shipping | 407 s (warm DDC); 97 s re-packaged after one source file changed | 642 s and 799 s, both with another repo's game and shader compiles running (57–123% of a CPU-second per second elsewhere) | busy |

The package times are not a fair race: the engine's derived-data cache is shared, so the first track cooked paid for the
forward renderer's shaders and the others did not, and the C# Shipping packages ran under other load. Read them as
"both package in 7–15 minutes, the C# one has one more UAT step (the managed publish, about 1–2 minutes)".

**Package size** (`Builds/Snap/Builds/<Track>…`):

| | C++ | C# |
|---|---|---|
| Development, everything | 1,195 MB | 1,069 MB |
| Development, without .pdb | 614 MB | 681 MB |
| Shipping, everything | 835 MB | 732 MB |
| Shipping, without .pdb | **411 MB** | **483 MB** |
| of which the game exe (Shipping) | 175 MB | 159 MB |
| of which cooked content (.pak/.utoc/.ucas) | 170 MB | 132 MB |
| of which the managed folder (.NET 10 runtime, UnrealSharp, the game's assemblies) | — | 150 MB (223 files) |

The C# game is 72 MB bigger without symbols (the .NET runtime) and 103 MB smaller with them (fewer native symbols:
its game code is managed). The cooked content differs by what each project's own `Content/` holds.

**Lines of code each side wrote** (non-blank lines at `3689308`; the original's sources the C# track links or copies are
listed separately):

| | C++ | C# |
|---|---|---|
| Core | 96,191 (407 files, hand translation; incl. the `Cs/` runtime, 2,865) | 0 (links the original's Core) |
| Engine side | 40,948 (214 files; Unity facade 6,993, co-op 2,322) | 13,970 written (52 files; Unity facade 6,285) + 530 lines of C++ in its primary module (Slate GUI overlay, audio, near-plane console commands) |
| Headless runner | 866 | 621 |
| **Total written** | **≈138,000** | **≈15,100** |
| Original sources used as they are | — | Core 67,469 lines (184 files) + Unity.Shared Core 1,308, compiled unchanged; 18,823 lines of the original Runtime kept verbatim in 58 copied files |
| Shared by both | SpaceGodRender plugin, 2,016 lines (shaders, materials' scripts, display modes, the race sampler) | same |

## 5. Faithfulness

Speed only counts if both games are the original. The checks, as of `3689308`:

| Check | C++ | C# | Source |
|---|---|---|---|
| Core simulation against the original's own Core (five Bench scenarios, every run of this race) | byte-identical digests | is the original's Core | Bench, this page |
| The `Cs` runtime against .NET 10 (CsParity) | 31,998 lines, 0 mismatches | — | MIGRATION.md |
| Feature checklist, 88 rows: verified by a run / code present / missing / divergent | 73 / 14 / 0 / 1 | 74 / 13 / 0 / 1 | FAITHFULNESS.md (US-T48, US-T50–58) |
| Binding audit (every key of the original's ControlsHelp pressed, its state checked) | 47 PASS, 0 FAIL | 47 PASS, 0 FAIL | FAITHFULNESS.md |
| The same game frame for frame in this race (ships, dates, battle, trip at every phase end) | identical to C# on all 6 runs | identical to C++ | this page |
| Co-op lockstep digests | 59 / 59 identical | 55 / 55 identical | this page |

The one divergence left on each track is the same row: the render pipeline choice and HDRP's options are kept but
restart nothing, because Unreal has one renderer (US-T51). Everything US-T48 found (C# volume and mute, C# render
settings, C++ text fields, C++ model viewer camera, C# text clipping, MSAA, display refresh rates, lights / glTF PBR /
Unity's default skin) was fixed on both tracks before the in-game numbers here were taken; both now render forward with
the original's 4x MSAA.

**Screenshots at the same game moment** (US-T45/T54: `tools/play.ps1 -Mode shots`, then `tools/imgdiff.py`; share of
pixels differing by more than 24/255 in any channel between the C++ and the C# shot):

| Shot | US-T38 (first shots) | US-T45 | now (US-T54, C# GUI through Slate) |
|---|---|---|---|
| views-sol | 0.44% | 0.00% (2 px) | 0.00% (2 px) |
| ui-main-menu | 0.87% | 0.40% | 0.21% |
| ui-hud | 24.46% | 4.23% | 3.57% |
| ui-help | 5.33% | 4.91% | 0.01% |
| ui-galaxy-map | 4.17% | 2.23% | 0.02% |
| ui-market-cargo | 23.56% | 4.30% | 3.03% |
| ui-pause-menu | 7.46% | 1.61% | 1.18% |

Screens of GUI alone now match within a few hundred pixels; what remains in the HUD, market and pause shots is world
labels and orbit lines over the scene. Against the original's own player (the model viewer, the same model, orbit and
zoom): 0.35% of pixels differ on C++ and 0.37% on C# (US-T57).

**Shared with the original:** US-T59 (a parked ship without a parking orbit at the highest warps) is in the original's
Core, so both tracks have it; the C++ game crashes on it and the C# one (like Unity) logs an exception every frame and
stops updating the rest of the frame.

## 6. What each track cost

### C++: a hand translation, and keeping it bit-identical

- **Translation volume.** The original's 184 Core files (67,469 non-blank lines of C#) became 407 C++ files and 96,191
  lines (1.43×; headers and the `Cs/` compatibility runtime, 2,865 lines, account for most of the growth). The 61 Runtime
  files (21,028 lines) became the 40,948-line `UnrealSpaceGod` module, which also holds the Unity facade (6,993 lines),
  co-op (2,322) and the checks.
- **Rules the translation needed** (docs/MIGRATION.md, "The C++ Core: translation rules" and "Integration decisions"):
  C#'s left-to-right evaluation order kept by hoisting side-effecting operands into locals; .NET's Dictionary
  enumeration order reproduced; `Dictionary::operator[]` reads that would insert; nullable lists versus empty lists;
  `shared_from_this` for objects that hand out `this`; partial classes across files; .NET's `Random`, `List.Sort`
  (introsort) and string formatting ported exactly (CsParity: 31,998 lines, 0 mismatches against .NET 10).
- **Parity defects found by the Bench** (Parity findings): an evaluation-order slip inside one call's arguments
  (a raider band's leader a Cruiser instead of a Destroyer, visible from the galaxy build onward) and a `const cs::Ref&`
  parameter aliasing a field the callee nulls (an access violation in the first boarding). A third, US-T49, was not in the
  code: a header edited while UBT compiled left objects from two `cs::Dictionary` layouts in one DLL; it now has a
  load-time layout check and a build re-stamp.
- **Performance work** (US-T42): untuned, the C++ Core was 1.1–1.9× slower per tick than .NET (interleaved); whole-program
  optimisation, inline IEEE tests, an open-addressing Dictionary index and fewer reference-count copies brought it level.
  None of that work exists on the C# track, which runs the original Core as it is.
- **Every future change to the original's Core** has to be translated again and re-proven against the five digests.

### C#: a large runtime, and a young binding layer

- **What ships.** The packaged C# game carries the .NET 10 runtime self-contained next to the exe (the managed folder; see
  Packaging) on top of the same UE engine binaries the C++ game ships.
- **Startup.** The managed side is JIT-compiled at startup (UnrealSharp ships no ReadyToRun or Native AOT), and
  `Galaxy.CreateDefault` runs on a cold JIT.
- **UnrealSharp is pre-1.0, pinned by commit** (`115e6753`, 2026-10-26, no tags). Workarounds this repo carries in its
  own scripts: the `UE_5_8_OR_LATER` symbol hook for installed engines (upstream PR #717 closed unmerged), a private
  .NET SDK 10.0.403 install because UnrealSharp takes the highest-named SDK folder and needs Roslyn 5.6,
  `uebp_UATMutexNoWait=1` around BuildCookRun because UnrealSharp's editor module runs a nested UAT inside the cooker,
  and a second UAT step (`PackageProject`) to publish the managed code into the package.
- **Engine features the C# side could not reach directly**, each worked around in the facade: no float textures or
  full-precision UVs (the asteroid belt goes through a DDS written by hand and 2048-asteroid chunks), half-precision UVs
  that made far line ends infinite (lines scaled into range), `r.SetNearClipPlane`'s 1 cm floor (a console command in
  the C++ primary module), MID parenting rules, Unreal's GC not seeing C# references (a holder component), no glyph
  clipping on Canvas (text dropped whole glyphs at clip edges; drawing a canvas into a render target crashed the
  renderer). The fix for that last one (US-T54) was to leave C#: the C# track's GUI now draws through a Slate overlay
  written in C++ in its primary module (SpaceGodGuiOverlay.cpp, 350 lines), as do its audio volume and near-plane
  commands; 530 lines of C++ in all.
- **Hot reload** is the C# track's real developer advantage: a managed rebuild swaps the collectible
  AssemblyLoadContext in the running editor; the C++ track rebuilds and relinks a module (Live Coding in the editor).

## Reproduce

```powershell
tools/bench.ps1 -Track Both                                # headless (Bench/README.md); interleave rounds by hand
tools/package.ps1 -Track Cpp -Configuration Shipping       # Builds/Cpp/Windows/Unreal.SpaceGod.Cpp.exe
tools/package.ps1 -Track CSharp -Configuration Shipping    # Builds/CSharp/Windows/Unreal.SpaceGod.CSharp.exe
tools/race.ps1 -Track Cpp -Runs 3                          # Builds/Race/game/cpp-N.json
tools/race.ps1 -Track CSharp -Runs 3
$env:SPACEGOD_RACE_DIR = 'Builds/Race/coop'; tools/play.ps1 -Track Cpp -Mode coop-smoke   # and -Track CSharp
```
