# Rewind: measurements for the time-reversal budget (US-T99, phase 1)

US-T99 proposes rewinding the session to any earlier moment from **deterministic keyframes plus an intervention log**:
keyframes are denser during battles, older ones are thinned under a fixed budget, and the session start is always
kept. The first version has one timeline, stored so that it can branch later. Before a horizon is picked ("how far
back can you go"), this phase measures what keyframes cost, how fast the simulation re-simulates, and whether a
restored keyframe re-simulates bit-exactly. Phase 1 built nothing in the game. Phase 2, the in-game keyframe ring
and rewind to a keyframe, is in [Build phases](#build-phases) below.

All numbers come from the headless Bench (the C++ Core, `Bench/`) on this workstation (2026-11-05). The machine was
shared with other builds and agents, so treat differences under about 20% as noise. MB means MiB.

## Method

**Tooling.** `tools/bench.ps1 -Rewind` builds `Bench.Rewind.exe` (`tools/build-bench-cpp.ps1 -Rewind`: the Bench
sources with `/DBENCH_REWIND`, the whole Core lib linked so that every ObjectGraph registration is present, and the
engine's Oodle SDK) and runs `--rewind-probe` once per scenario. Each run writes `Bench/results/rewind-<scenario>.json`.
`python tools/rewind-project.py` turns those files into the projections below. The plain `Bench.Cpp.exe` is unchanged.

**Keyframe.** A keyframe is the game's own save graph (`SaveData`: clock, galaxy, session) written by `ObjectGraph`.
It sits inside a tiny wrapper root that also carries the Bench app shell's handles (the followed ship, the skirmish
and the crew course), which adds a few dozen bytes. Restoring one means reading the graph (Core rewires its
delegates), rebuilding the shell around it and running on. For reference, the probe also writes a real
`SaveGame` file (header plus raw DEFLATE level 1) of the final state.

**Per scenario, in one process** (`Bench/Cpp/src/RewindProbe.inl`):

1. **Plain pass.** The scenario runs as `tools/bench.ps1` runs it, with no keyframes. This pass gives the tick cost
   and game time per tick, grouped by warp, which together are the **replay speed**, and the final digest. The
   final digest must equal `Bench/expected-digests.json`.
2. **Capture pass.** The same run is repeated, and keyframes are captured at pairs of points (base, base + interval)
   spread over the run, for intervals of 10 s, 1 min, 10 min, 1 h, 1 day, 1 week and 30 days, wherever the tick is
   fine enough. For every keyframe the pass records the raw size, the time to capture it, and the size and
   encode/decode time of each codec:
   - `deflate-1`: the save file's raw DEFLATE level 1 from zlib 1.3;
   - Oodle `mermaid-fast`, `kraken-fast` and `kraken-normal` (Oodle 2.9.16, the version bundled with UE 5.8,
     single-threaded, no job system).

   For every pair it records the **delta**: the later keyframe compressed with the earlier one as a preset
   dictionary (Oodle's `dictionaryBase`, which works like zstd's `--patch-from`). DEFLATE's 32 KB window cannot use a
   4 MB dictionary. The pass also records a naive same-offset binary diff. zstd is not vendored anywhere in the repo
   or the engine, so Oodle is the best compressor that is already available, and it is the one the game would ship.
3. **Determinism.** At 1/3 and 2/3 of the run, the capture pass records the live digest at the checkpoint, 60 ticks
   later and at the end of the run. Each checkpoint keyframe is then decompressed and restored into a fresh
   Simulation, and the replay is checked three times: its digest must equal the live one at that tick, again after
   60 re-simulated ticks, and again at the end of the run (same tick count, same stop reason). The capture pass's
   own final digest must also equal the plain pass's, which shows that taking keyframes does not perturb the run.
   (Writing a graph compacts dictionaries, so this was not obvious.) Before anything reads it, the restored world is
   also written again, and those bytes must equal the keyframe's own (the save round trip, US-T228). It is written
   first because reading the world is not free of writes: the digest asks the ephemeris for positions at dates off the
   clock, and the ephemeris keeps each body's last answer (the `HelioAt` and lunar memos, which the save carries).
   Writing it after the digest gave different bytes even though nothing was lost. A failed round trip fails the
   probe; `--rewind-graph-diff N` lists the graph entries that differ.

Scenarios: the five Bench scenarios, plus `battle-warp0`. This is the battle scenario left at real time
(`--rewind-then-warp 0`, 20,000 ticks = 5.6 game-minutes), because the shipped `battle` scenario speeds up to
1 min/s and so never runs a real-time dogfight.

## Results

### Full keyframes

| Scenario | Raw (start -> end) | Kraken-Normal | Kraken-Fast | Mermaid-Fast | DEFLATE-1 (save file) | Capture (write graph) avg / max | Restore (read + rewire) |
|---|---|---|---|---|---|---|---|
| sol-idle (2.5 d at 1 h/s) | 7.4 -> 9.9 MB | 2.6 -> 3.6 MB | 3.7 MB | 4.0 MB | 3.2 -> 4.5 MB | 112 / 129 ms | 178-193 ms |
| battle (36 min at 1 min/s) | 7.4 -> 7.5 MB | 2.6 -> 2.7 MB | 2.7 MB | 3.0 MB | 3.2 -> 3.3 MB | 122 / 148 ms | 181-250 ms |
| battle-warp0 (5.6 min real time) | 7.4 -> 7.7 MB | 2.6 -> 2.8 MB | 2.8 MB | 3.1 MB | 3.2 -> 3.4 MB | 116 / 133 ms | 192-197 ms |
| galaxy-ftl (12.4 d at 1 d/s) | 7.4 -> 11.3 MB | 2.6 -> 4.1 MB | 4.1 MB | 4.5 MB | 3.2 -> 5.0 MB | 130 / 155 ms | 228-238 ms |
| economy-year (1 yr at 1 month/s) | 7.4 -> 24.3 MB | 2.6 -> 9.4 MB | 9.5 MB | 10.2 MB | 3.2 -> 11.1 MB | 235 / 377 ms | 306-399 ms |
| high-warp (1 yr at 1 yr/s) | 7.4 -> 24.2 MB | 2.6 -> 9.2 MB | 9.2 MB | 10.0 MB | 3.2 -> 10.9 MB | 265 / 490 ms | 321-457 ms |

Codec time per keyframe (averaged over a run): encode DEFLATE-1 85-200 ms, Mermaid-Fast 47-129 ms, Kraken-Fast
88-219 ms, Kraken-Normal 167-384 ms. Decode Kraken 5-11 ms, DEFLATE 21-50 ms. Kraken-Normal is about 20% smaller than
the save file's DEFLATE and decodes 4x faster. A save file (`SaveGame`, end state) is 3.3-11.1 MB and takes 183-620 ms
to write, which agrees with the ticket's "about 4 MB, 240-290 ms" for an early session.

**The world grows with play.** A keyframe is 7.4 MB raw at the start and about 24 MB raw (9-10 MB compressed) a game-year
in, as the economy, colonies, wrecks and event logs pile up. Capture and restore grow with it.

### Deltas (Kraken-Normal against the previous keyframe as dictionary)

| Interval | Quiet (sol-idle, galaxy-ftl, economy-year, high-warp) | Battle (battle, battle-warp0) |
|---|---|---|
| 10 s | - | 42-361 KB (median 196 KB) |
| 1 min | 12-98 KB with no war anywhere, 345 KB with wars running elsewhere (median 55 KB) | 155-431 KB (median 256 KB) |
| 10 min | 40-170 KB (395 KB with wars elsewhere; median 123 KB) | 215-445 KB (median 293 KB) |
| 1 h | 131-533 KB (median 293 KB) | (the battle ends first) |
| 1 day | 1.0-1.4 MB | |
| 1 week | 1.9-3.1 MB | |
| 30 days | 3.0-3.7 MB | |

- **With interval:** delta size grows roughly with the log of the interval, from tens of KB per minute to about a
  full early keyframe at a month. Beyond a week a delta saves little over a whole keyframe.
- **With activity:** a running battle sets a **floor of about 150-400 KB per keyframe** even 10 game-seconds apart,
  because every unit's position, velocity and subsystem state changes every tick. Quiet play costs 12-100 KB a
  minute. In the quiet scenarios most of the delta is other systems' wars, so even "quiet" pays the battle rate while
  the galaxy is at war.
- **A naive binary diff is useless:** 5-21 MB of an 8-25 MB keyframe differ at the same offset, because ObjectGraph
  numbers objects breadth-first, so one new object shifts everything after it. A delta needs a long-range LZ with the
  previous keyframe as dictionary.
- **Kraken-Fast cannot be used for deltas:** its match finder misses long-range matches. On a near-identical pair
  (6.8 KB actually changed) Fast gave 137 KB where Normal gave 12 KB. Use Normal (or better) for deltas.

### Replay (seek) speed

Game-seconds re-simulated per wall-second, headless, at the scenario's warp (the replay after a restore; the plain
pass, which includes warm-up, is in brackets):

| Warp (tick = 1/60 s of play) | Scenario | ms per tick | Game time per tick | Replay speed |
|---|---|---|---|---|
| 0 (real time) | battle-warp0 | 0.99 | 1/60 s | **18-20 game-s/s** (17) |
| 2 (1 min/s) | battle | 1.18 | 1 s | 1,061-1,206 (851) |
| 4 (1 h/s) | sol-idle | 1.31 | 1 min | 85,800-92,300 (45,900) |
| 6 (1 day/s) | galaxy-ftl | 4.29 | 24 min | 476,000-484,000 (336,000) |
| 8 (1 month/s) | economy-year | 62.2 | 12 h | 647,000-688,000 (694,000) |
| 9 (1 yr/s) | high-warp | 739 | 6.1 days | 550,000-558,000 (711,000) |

Seek cost therefore depends on **how many ticks** separate the keyframe from the target, not on game time. A tick
costs about 1 ms in battles and quiet play and 60-740 ms at the top warps. Rewinding real-time combat is the
expensive case: a dogfight replays at only about 18x real time.

### Determinism

| Scenario | Keyframing changed the run? | Restore at 1/3: at restore / +60 ticks / end | Restore at 2/3 |
|---|---|---|---|
| sol-idle | no (`B30509691F52C9E0`) | match / match / match | match / match / match |
| battle | no (`ABD80179E1D7F1AC`) | match / match / match | match / match / match |
| galaxy-ftl | no (`F1E614170D1B1960`) | match / match / match | match / match / match |
| economy-year | no (`7F6533416B74D14D`) | match / match / match | match / match / match |
| high-warp | no (`A8A790EC13FD23C8`) | match / match / match | match / match / match |
| **battle-warp0** | no (`4F5F142016FD1783`) | match / match / **MISMATCH** | match / match / **MISMATCH** |

All five Bench scenarios re-simulate from a restored keyframe bit-exactly to the end of the run, and taking keyframes
leaves every digest as recorded. The digests also agree with `expected-digests.json`.

**Blocker: carrier flight decks (filed as US-T111).** In the real-time battle, the replays from both checkpoints
diverge at exactly the same tick, 5229 of a 7,000-tick run (`--rewind-trace 1`). The first differing digest line is a
Fighter's position and velocity in Sol's battle; at the same tick, several ships in that battle differ in their last
bits. A
structural diff of the live and replayed graphs at tick 5228 (`--rewind-dump-at 5228`, then `--graph-diff-files`)
shows where the two worlds part. A fighter's `FighterState.Deck` (a `DeckVisit`) points at a different `FlightDeck`
entry, and the replayed graph has two extra entries.

The cause is that a carrier's `FlightDeck`s live in `FlightDecks::Of`'s static table (C#'s ConditionalWeakTable),
which saves deliberately skip (docs/PERSISTENCE.md section 11). The fighter's `DeckVisit` does save the deck it
references, though. After a load, `Of(carrier)` builds new decks, so the fighter and the carrier now use two different
copies of the same deck, and the deck's landing and launch state (`Final`, `Aboard`, `NextLaunch`) splits between them.
This is not specific to rewind: any save/load or co-op resync taken while a fighter holds a deck visit loses it. It is
inherited from the original game's design.

**Fixed (US-T111).** DeckVisit's `SG_LOADED` hook now hands each loaded deck back to its carrier
(`FlightDecks::Adopt`, docs/PERSISTENCE.md section 11). `Bench.Rewind.exe --scenario battle --rewind-probe
--rewind-then-warp 0 --ticks 7000 --rewind-trace 1` now restores from ticks 2333 and 4666 and matches the live digest
at the restore tick, +60 and the end (7000): `determinism: ALL MATCH`. The five Bench digests are unchanged.

Two smaller findings:

- **The digest writer mutates state.** `Digest::Write` calls `Ephemeris.Helio`, which updates the saved
  `memoHelio`/`memoJd` caches. A keyframe captured right after a digest therefore differs from one captured before it
  (only in those memo entries). It is a pure cache, and every probe compares like with like, but a rewind feature
  must not use the digest as a free "observer" on the live world.
- **The intervention log must hold each frame's dt.** The headless replay is bit-exact because the Bench steps a fixed
  `dt`. The game steps `Time.unscaledDeltaTime`, so the log must record the frame time of every tick (or the
  simulation must move to a fixed step) for a replay to reproduce the live run.

## Projections

`python tools/rewind-project.py` uses this model. The thinned schedule is the one recommended below. Each retained
keyframe is stored as a delta against the retained keyframe before it, sized by its gap (log-log interpolation over
the measured medians, capped at a full keyframe). One keyframe in 16 is stored whole, which bounds the decode chain.
The session start is kept whole, and the newest keyframe is also held raw, as the dictionary for the next delta. The
battle-heavy profile uses the battle deltas (and the 10 s tier) wherever they apply. Two world sizes are modelled:
early in a session (3.1 MB compressed keyframe) and a game-year in (9.7 MB).

**How far back the budget reaches** (every tier filled):

| | Quiet, early | Quiet, a year in | Battle-heavy, early | Battle-heavy, a year in |
|---|---|---|---|---|
| **512 MB RAM** | 2.5 game-years (544 keyframes) | 286 game-days (453) | 2.0 game-years (593) | 82 game-days (492) |
| **4 GB disk** | 32 game-years (2,082) | 26 game-years (1,759) | 31 game-years (2,131) | 25 game-years (1,802) |

For scale, 82 game-days is 33 wall-minutes at 1 h/s and 2.5 game-years is about 6 wall-hours at 1 h/s. **512 MB of
RAM covers at least the last 2-3 game-months even in a war-torn, year-old world, and the 4 GB disk spill covers any
realistic session in full.** With thinning, the horizon is the whole session; the budget only decides how sparse the
oldest part gets.

**Rewind latency** = restore (0.18-0.46 s, growing with the world) + delta chain decode (about 0.05 s) + replay from the
keyframe before the target. The worst case of that replay is one full tier spacing at that age:

| Back | Real-time battle (warp 0) | Battle at 1 min/s | Quiet at 1 h/s | 1 day/s .. 1 yr/s |
|---|---|---|---|---|
| 1 min (10 s spacing in battle, 1 min quiet) | 0.8 s | 0.3 s | 0.3 s | 0.3-0.5 s |
| 1 h (1 min spacing) | 3.4 s | 0.35 s | 0.3 s | 0.3-0.5 s |
| 1 day (10 min spacing) | 32 s (if that whole day were spent in real-time combat) | 0.8 s | 0.3 s | 0.3-0.5 s |

At warp 4 and above, latency is restore-bound (under half a second) at any distance. Only rewinding through long
stretches of real-time combat is slow, and the replay-cost rule below caps that.

## Recommendation

**Default horizon: the whole session, thinned, never cut off.** The session start is always kept. Hold 512 MB in RAM
and spill older keyframes to up to 4 GB under the save folder. Offer the horizon setting as 15 min / 1 h / whole
session, defaulting to whole session. The numbers show no need to cap it.

**Keyframe schedule** (game-time tiers by age, plus a replay-cost floor):

| Age | Spacing |
|---|---|
| last 15 min, while a battle runs in the watched system (or the player is embodied) | 10 game-s |
| last 1 h | 1 game-min |
| last 24 h | 10 game-min |
| last 7 days | 1 game-h |
| last 90 days | 1 game-day |
| older | 1 game-week |
| session start | always kept, whole |

- **Replay-cost floor.** Also take a keyframe whenever the simulation has spent about 0.5 s of tick time since the last
  one. This bounds the replay of any recent seek to about 0.5 s whatever the warp: in real-time combat it means a
  keyframe every ~8 game-s (about 500 ticks at 1 ms), and at 1 month/s every 8 ticks. To limit the frame hitches
  (below), take at most one keyframe per 2 wall-seconds, which lets a seek cost up to about 2 s at the top warps. At
  high warp the dense game-time tiers therefore fill only as fast as wall time allows, which costs nothing, since seeks
  there are restore-bound.
- **Storage.** Store each keyframe as a delta against the previous retained keyframe (Oodle Kraken, level Normal, with
  the previous keyframe as dictionary), and store every 16th whole. When thinning drops a keyframe, re-encode its
  successor against its new predecessor (one decode plus one encode, 0.2-0.4 s, on a worker). Keyframes keyed by tick
  with a parent link keep this branch-friendly: a timeline tree only adds more children per node.
- **Codec.** Use Oodle Kraken from the engine's own SDK: Normal for deltas, Fast or Mermaid for the whole keyframes if
  encode time matters. Keep DEFLATE for save files (format compatibility).

**Open problems for the build phase:**

1. ~~US-T111 (the flight-deck split) is a blocker~~ for rewinding any battle that has carriers. Fixed: see above.
2. **Capture is a synchronous 110-490 ms graph walk on the simulation thread.** At the 10 s battle cadence that means a
   visible hitch every 10 game-s of real-time combat. Phase 2 needs an incremental or off-thread capture (or a cheaper
   per-battle snapshot) before the dense battle tier can ship.
3. **The intervention log must record each frame's dt**, or the game must step a fixed dt, or replays will not be
   bit-exact.

## Build phases

The owner approved the build on 2026-11-19. The phases below each land on their own.

**Phase 1: measurement. Done** (8eecc20; see above).

**Phase 2: the keyframe ring in the game, and rewinding to a keyframe. Done** (US-T99).

- *Store.* `Core::RewindRing` (`Cpp/Source/SpaceGodCore/Persistence/RewindRing.h`). A keyframe is the `SaveData`
  object graph, the same bytes a save holds. Capturing walks the world on the simulation thread, as a save does, and
  then compresses on a worker (`std::async`) with the save file's raw DEFLATE. Each keyframe is keyed by **session game
  seconds**, which is the game time the clock has moved forward since the session or its loaded save began. It is not
  keyed by date, so a clock set back by hand does not reorder the ring. Each keyframe also carries a branch id (always
  0 for now) and a parent link to the retained keyframe before it, so the store can grow into a tree for US-T104
  without a rewrite.
- *Schedule and budget.* The tiers in the Recommendation are 10 s while a battle runs in the watched system or a craft
  is held (last 15 min), 1 min (1 h), 10 min (1 day), 1 h (7 days), 1 day (90 days), then 1 week. Thinning runs from
  oldest to newest, so decisions about old keyframes do not churn as new ones arrive. The budget is 512 MB of RAM.
  While the ring is over budget, every spacing doubles. The session start is pinned, so the horizon is always the whole
  session.
- *Cadence.* A keyframe is due when the newest one is at least one tier spacing of game time old (10 s in battle, else
  1 min) **and** at least `MinWallGapS` = 20 real seconds old. Capture is not yet off the simulation thread (open
  problem 2), so the 20 s floor stands in for the recommended 2 s. It keeps the 110-490 ms walk to one hitch every
  20 s or more. The dense 10 s battle tier therefore fills only at real time or slower. Keyframes are taken only when a
  save could be made (`SaveBlocker`: not between the stars, under FTL or latched to a hulk), and never with
  `-norewind`, `-demo` or `-capture`.
- *Rewind.* **Shift+F8** (key action `rewind`, rebindable) loads the newest keyframe before now in place, through the
  same path a load takes (`ApplyLoaded`). Pressed again within 6 real seconds, it steps back from the moment it just
  rewound to. `sg.Rewind [list | back <game s> | to <id>]` lists the ring or rewinds by game time or keyframe id.
  Keyframes after the moment rewound to stay until the next keyframe is taken, so stepping back further works. Once
  play goes on, that capture drops them: **one timeline, and the old future is replaced** (the owner's call, 2026-11-05).
  Loading a save starts a new ring rooted at it.
- *Co-op.* The host's rewind goes through `CoopLoadingHere`, so every client follows through the existing resync. A
  client's Shift+F8 is refused for now; see phase 4.
- *The live simulation is unchanged.* A keyframe is the same read-only walk an autosave makes (the probe showed taking
  keyframes leaves every digest as it was). Nothing in Core's simulation calls the ring, so the Bench digests cannot
  move.
- *What it is not yet.* A rewind lands **exactly on a keyframe**, not on any tick between them (phase 3's seek adds
  that), and keyframes are whole
  DEFLATE graphs (3-11 MB), not Oodle deltas. At 512 MB the ring holds about 50-150 keyframes, which the thinning
  spreads over the whole session.

**Phase 3: the frame log, seeking by replay and the timeline bar. Part done** (US-T99).

- *Frame log.* Each frame of single-player play is logged in the game layer (`UnitySpaceApp.Rewind.cpp`,
  `RewindFrame`): its start on the timeline (session game seconds), the real seconds it stepped (open problem 3: the
  logged dt), the clock's whole state just before it advanced (co-op's `ClockState`) and the date after, and whether the
  player touched a key, a mouse button, the wheel or the pad that frame. Frames carry consecutive serials, and each
  keyframe records the serial of the frame its world steps into next. The log holds about 10 minutes of frames (40,000,
  under 112 bytes each). Play going on after a rewind drops the old future from the log as it goes.
- *Seek.* `RewindSeek(T)` (`sg.Rewind seek <s back>`, `sg.Rewind at <session s>`, a click on the timeline bar) restores
  the keyframe at or before T, then replays the logged frames through the game's own frame: each replayed frame gets
  its logged dt and has its clock put in the logged state first (as a co-op client does with the host's frame), and the
  player's input is handed over (idle). It stops at the frame that reaches T, or short of it at the first frame the
  player acted on (that input is not replayed yet), at a gap in the log, or after 90 s of play replayed (replay runs at
  the speed it was played). Esc stops a replay where it is. A replayed frame whose date differs from the log's is
  counted and logged as a warning.
- *Timeline bar.* Under the top of the screen after Shift+F8, after any rewind and while a seek replays (`sg.Rewind bar`
  shows it for 30 s): the session from its start to now, a tick per keyframe (the session start wider), the replayable
  stretch shaded blue, now in amber. Hovering a moment says how far back it is and whether it replays exactly; a click
  seeks there (when the cursor is free, not at the controls).
- *Replay checks.* Every 60th logged frame also keeps a hash of the world just after it (FNV-1a over co-op's desync
  digest, `CoopDigest`: the clock, every system's counts and treasuries, the watched system's ships and settlements).
  A seek's replay recomputes it at the same frames and counts the ones that differ, so whether a replay re-lived the
  play exactly is measured in the game, not assumed: the arrival toast says when it drifted, the log gets a line
  either way, and `sg.Rewind list` shows the last replay's verdict ("exact (N checks matched)", or where it first
  drifted). The check only reads the world; nothing in the simulation calls it.
- *Adventures replay round the logged point.* The world's adventures spawn round the camera (`PlayerHelioKm`), and a
  restored view need not stand where the play's did. Each logged frame now also keeps whether it ran the adventures
  and round which point, and a replayed frame runs them round that point (`RewindAdventures`), as a co-op client runs
  them round the host's (`FrameRecord::AdventurePoint`). Live play calls them exactly as before; the log only notes it.
- *Not yet.* The player's own input in the log is only marked, not replayed, so a seek past an action lands just before
  it; replaying it is the input recorder (per `EmbodiableKey` frames, orders, clock keys; US-T169's requirement 1).
  Much of the game reads the devices directly (the screens' keys and IMGUI clicks) rather than `InputState`, so logging
  `InputState` alone would replay only part of an action; the recorder has to sit at the device seam
  (`Input/InputDevice.h`) or log commands. Console commands typed at the console are marked as input (their keys
  are), but ones run from the command line (`-ExecCmds`) or a script are not; those runs are tours and captures, which
  keep no rewind. The replay checks above show any drift that remains. In co-op the log is off and a seek lands on the
  keyframe. The analytic preview on the bar, and the US-T104 fork at the first intervention after a seek, are still to
  come.

**Phase 4: cheaper storage and capture.** Use Oodle Kraken deltas against the parent keyframe, with every 16th
keyframe whole. That needs the engine's Oodle SDK linked into the game module, not only into Bench.Rewind. Add the
4 GB disk spill under the save folder, and an incremental or off-thread capture, which lets the 20 s floor drop to the
recommended 2 s and lets the dense battle ring ship. Then a client's rewind becomes a command in the co-op lockstep
stream, ordered by the host: any player may rewind, and the later request wins.

## Hold to rewind (US-T409, US-T410)

The designer's short rewind, beside the keyframe ring above. The rules (2026-11-19, final; US-T410 revised 2 and
added 4 and 5):

1. Pressing it first sets the time warp to real time (1x), then rewinds in **real** seconds.
2. It is free, with a **10 s** cooldown between uses (was 60).
3. **Holding** the key rewinds, up to **10 real seconds**; **letting go** resumes play from the moment reached.
4. **Never revisit a time already rewound through:** a resume point is a floor, and no later hold goes back past it.
   With the 10 s cooldown, you can always go back up to 10 s of fresh play, never into the old one.
5. **Eased, not jarring:** the playback starts slow and accelerates smoothly to a constant top rate, slows to settle on
   the oldest moment reachable, and eases to a stop when let go.

Every number is a constant in `UnitySpaceApp::HoldRewindTuning` (`UnitySpaceApp.HoldRewind.inl`): `WindowS` 10,
`CooldownS` 10, `StartRate` 0.25, `RampS` 0.8, `TopRate` 3, `BrakeS` 0.6, `BrakeMinRate` 0.1, `ReleaseEaseS` 0.12,
`DeferBookings`, `ViewSystemBack`, `CapitalHz` 5, `CraftHz` 20, `PlayerEveryFrame`, `AnchorS` 0.5, `MaxSamples`
600,000, `MinHoldS` 0.15. The arithmetic (the ease, the brake, the oldest moment with its floor, a step) is pure, in
`Persistence/HoldRewindCurve.h`, unit-checked by `Tests/HoldRewind/HoldRewindCurveTests.cpp` (`tools/build-core.ps1`).
The key is the rebindable `KeyAction::HoldRewind` ("holdRewind"), Shift+R by default (Shift+F8 stays the keyframe
rewind; the two are separate keys because one is a tap and the other a hold).

### Resume: which of the candidates, and why

The research (`docs/research/rewind-short-track.md`, section 6) recommends restoring the keyframe at or before the
moment and replaying the logged frames to it. Here that cannot reach a moment inside a fight yet: the replay stops at
the first frame the player acted on (the input recorder does not exist), and keyframes are at least 20 wall-seconds
apart because a capture costs 110-490 ms. A dense full-world snapshot every second (candidate ii) costs the same
hitch every second. So the resume is **candidate (i), state restore, scoped to the fight in view and the hull flown,
and bounded by barriers**:

- **Resume points.** Every `AnchorS` (0.5 s) of lived real time, a read-only observer at the end of the frame copies
  *by value* the fight's whole state: the `Battle` itself (its lists of units, missiles, rounds, mines and decoys, its
  clock `Seconds`, its random generator, the recon aloft, the seats manned and flown), every `BattleUnit`, each one's
  `ShipCombat`, `Subsystem`s, reactor and `PowerDistribution`, `FighterState` and `BroadsideCommand`, every `Missile`,
  `KineticRound`, `Mine` and `Decoy`, and the helm's `PilotState` (the hull flown's motion). About 1 MB for a large
  fight, a few dozen copies kept.
- **Letting go** puts the newest resume point at or before the moment reached back over the live objects (copy
  assignment into the same objects, so every reference between them still holds), except the `Battle`'s hooks (its
  events and callbacks), which stay as the world set them. The fight's random generator goes back with it, so what
  follows is a consistent continuation; it is not the same future, and need not be.
- **Barriers.** Some changes are booked outside the fight the moment they happen, and copying the fight back cannot
  undo them: a hull lost (the world books the loss), fled, struck or taken, a unit joining (a craft launched, a hull
  arriving), a hangar or shuttle bay changing (a craft landed), a gun, craft or mech taken or let go, the fight ending
  or another coming into view, and the player changing seat or crossing into another body's frame. A census of these
  is compared every recorded frame; any change is a barrier, and no resume goes back past the newest one. The hold
  can still *draw* further back (a capital that jumped out plays back in), and the readout says how far back play can
  go on from ("plays on no earlier than -3.2 s: a hull was lost"); letting go resumes at the oldest resume point after
  the barrier and says so.
- **Not restored, by design for now:** the world's clock and everything outside the fight (ships in transit, the
  economy, other fights) go on from now: at real time that is 10 game seconds, invisible at system scale. What the
  law, the news or the galaxy booked in those seconds stays booked (barriers cover the losses). Inside a hull, the
  structure's knocked-loose sections (`ModularHull`, US-T91) and a boarding party's `Interior` are not copied; the
  hull's own fuel, cargo and crew records are not either.
- **The keyframe ring and its log.** No keyframe is taken during a hold (the frame runs no commands). After a resume
  the frame log is cleared, because it could not replay across a fight set back by hand; a seek then lands on the
  keyframes, which are whole worlds and still exact.
- **Bench and AI-only runs.** The recorder and the restore live in the game layer (`UnitySpaceApp.HoldRewind.cpp`),
  read only until the key is let go, and draw no random numbers; nothing in Core calls them, so the Bench never meets
  them and its digests cannot change.
- **Co-op.** Refused with a toast, and nothing is recorded while a co-op session runs. A co-op hold would have to be a
  host-ordered lockstep command (phase 4).

### Stage 1, as built

- **Recording** (single player, not in the tour, captures or `-norewind`; not while paused or between the stars): the
  fight in view (the hull flown's, else the one watched) and the hull flown. Each unit's pose (position, velocity,
  nose, whether dead, fled or docked) is kept at `CapitalHz` for capitals and platforms, `CraftHz` for craft, every
  frame for the hull held, and again on any change of those flags; missiles, rounds and decoys at `CraftHz`; the
  helm's `PilotState` every frame; the fight's clock every frame. Planets, stations and everything off the
  battlefield need no record (they are analytic in the date), and are not rewound. The window keeps `WindowS` plus one
  second, and projectiles stop being kept past `MaxSamples` poses.
- **The hold.** The press sets real time, captures the present as a resume point and starts the playback. While held,
  the moment drawn moves back at `PlaybackSpeed` real seconds per second, to at most `WindowS` back or the oldest
  pose kept. Each frame the recorded poses at that moment are written into the fight's units (and the lists of
  missiles, rounds and decoys in flight then), and the helm's state into the hull flown, smoothed between kept poses
  (cubic Hermite on position and velocity, the nose lerped and normalised). Hulls that left in the window (dead,
  fled) are drawn again; ones that arrived later are hidden before they came. The simulation does not run: the frame
  only lays out, moves the camera and draws.
- **Letting go** restores the resume point as above, drops what was kept after it, shifts the rest so it ends now,
  clears the frame log and starts the 60 s cooldown. A tap (under `MinHoldS`) puts the present back and costs
  nothing.
- **Readout.** "REWIND ready (hold Shift+R)", "REWIND in 42 s", and while held "<< REWIND -4.2 s", how far back play
  can go on from and why, and "let go to play on".

### US-T410: complete and seamless

- **The numbers:** a 10 s window and a 10 s cooldown (wall seconds, as before). **The floor:** letting go makes the
  resume point the floor (`FloorT`): the oldest moment a later hold reaches is `max(now - WindowS, first kept,
  floor)`, so a time already rewound through is never revisited, and what was kept before it is pruned.
- **The ease:** the playback rate is `StartRate` at the press, smoothstepped up over `RampS` to `TopRate` (three
  seconds of the past a second), constant after; within `BrakeS` of the oldest moment it smoothsteps down to
  `BrakeMinRate` so the hold settles on the edge; letting go eases the rate to zero over `ReleaseEaseS` (0.12 s, short
  so control never feels late) before play goes on. A tap still ends at once. The readout shows the rate (x0.3 ... x3.0).
- **No replayed or missed effects:** the restore keeps the live `Battle::Fx` list, `FxSeq` and the effect owner
  (`FxBy`, `FxByMount`): the views track the last sequence they drew, so setting it back hid the next effects and could
  show old ones again; now effects go on from now. The HUD's hit and after-action trackers stand aside while a hold
  plays (hulls die and come back on screen) and take stock on the first frame after a resume instead of reporting
  the difference (`HoldRewindActive`, `HoldRewindJustResumed`).
- **The bookings held back (barriers lifted):** the war's hooks on the fight in view (`UnitDestroyed`,
  `UnitCaptured`, `UnitSurrendered`) are wrapped while it is recorded: a hull lost, struck or taken is booked to the
  world (its `Ship` struck off, the wreck, the losses, the capture, the log) only once its moment is older than the
  window, so letting go before it brings the hull back whole and the booking never happens; a resume drops the
  bookings after it. Everything held back is booked when the fight ends (a new Core hook, `Battle::Ending`, raised by
  `War::Step` just before `War::Resolve`; null in every fight the rewind does not watch, so AI-only runs and the
  Bench never raise it), when the fight leaves the rewind's eye, before any save (`CaptureSave`), and on co-op. A
  keyframe (US-T99) waits while a fresh booking waits, at most the window and a second, so its save does not book it
  early. A booking made early (a save, a keyframe that could wait no longer) is a barrier at its moment. An AI hull
  fleeing is the fight's own flag (the resume puts it back), so it is no longer a barrier either.
- **The whole system drawn as it was:** while held, the world clock is set back to the moment reached (the marks keep
  the world's date beside the fight's clock) for the drawing only (planets, ships in orbit and on the lanes, gates),
  and put back to now on letting go (`ViewSystemBack`).
- **Still barriers, and why:** crossing into another body's frame, or moving to a seat on another body (a seat
  on a hull already in battle that the player holds nothing on cannot be taken back through the embody gate);
  another fight coming into view; the fight's end only when forced (the window out, or the fight leaving the
  rewind's eye; US-T419 holds it back otherwise); and a booking forced early (a save, a keyframe that waited the
  full window). No longer barriers: a craft launched or landed (US-T413); a unit joining mid-fight (US-T415); a
  deployed mech's state, stuck pods and the player's own guns and craft taken or let go (US-T416, US-T417); the
  player's own hull lost, taken or struck, and a surrender's terms (US-T417); a seat change on the same body
  (US-T415's census, US-T417's seat journal, verified end to end once US-T414 was fixed).
- **Tested:** `sg.HoldRewind selftest [quit]` (HoldRewindCommand.cpp) starts a fight off Uranus and, with no hand
  on the keys, marks a resume point and hashes the whole fight there (`HoldRewindFightDigest`: its clock and random
  draws to come, every unit's flags, pose, hull and mounts, every missile, round, decoy and mine), plays on 3 s, holds
  the rewind to the mark and lets go, then checks: the moment only went back while held; the resume is at the mark;
  the resumed fight hashes the same; hulls lost after the mark are back and none is struck off the world's books;
  effects go on from now; the resume point is the floor; a second hold is refused during the cooldown and free after;
  with the clock stopped for 2 s of the cooldown, the second hold stops at the floor, not the window; the second
  resume is no earlier than the first; the ease rises monotonically to the top rate. `./tools/play.ps1 -Offscreen
  -Capture '' -Seconds 300 -ExecCmds "sg.HoldRewind selftest quit"` runs it headless (no -capture: a capture run
  keeps no rewind); the log's last line is `HoldRewind selftest: PASS|FAIL (n checks, m failed)` and the game exits 0
  on a pass.

### US-T412: the whole system drawn as it was, checked

- **Bodies were not actually rewinding.** `ViewSystemBack` moved `Clock->Jd` back, but planets, moons, stations and
  anything body-relative (lane endpoints, a parked ship's helio position) are laid out from
  `Ephemeris()->Helio(i)` — a cache filled only by `Ephemeris::Update(jd)`, which normally runs inside
  `Galaxy->Update(Clock->Jd)` (`UnitySpaceApp.cpp`); that whole call is skipped for every frame a hold plays (the held
  branch returns before reaching it, `UnitySpaceApp.cpp` ~510-528). So while held, bodies stayed frozen at the moment
  the hold began, and anything keyed off one (`Fleet::BaseStateOf`'s `ephemeris->Helio(s->Orbit->Body)`,
  `Fleet.cpp` ~699) was drawn against a stale planet. Ships in free flight were fine regardless (`Fleet::StateOf`
  takes `jd` itself, `UpdateShips`, `UnitySpaceApp.Main2.cpp` ~1009), so the fight in view, free-flying traffic and
  the hull held always rewound correctly; only body-relative positions did not. Fixed: `PlayHoldRewind` now calls
  `Ephemeris()->Update(jd)` itself, gated the same as `Clock->SetJd(jd)`; it is a pure recompute of that cache (no
  RNG, no writes outside it), and the moment play resumes, the ordinary `Galaxy->Update(Clock->Jd)` call overwrites
  it again, correctly, before anything else reads it, so neither end of a hold needs its own restore.

### US-T413: craft launch and landing put back; seat change investigated

- **Craft launch and landing were a barrier only because their state lives outside the per-unit copies.** A
  fighter's own progress through a deck (`DeckVisit`: `Stage`, `Along`, `Offset`, `Service`, `Stowing`...) is mutated
  in place, not recreated, each time it taxis in or out; `FighterState::Deck` only holds a `Ref` to it, so copying
  `FighterState` by value (already done) carries the pointer, not what it points to. A carrier's own
  `FlightDeck`s (who is `Aboard`, who is `Final`, `NextLaunch`) live in `FlightDecks::Of`'s weak table
  (`FlightDecks.cpp:161`), entirely outside the fight's own object graph. `ShipCombat::Hangar`/`ShuttleBay`
  (`CombatState.h:85,98`) are a `Ref` to a shared `List`, so copying `ShipCombat` by value (already done) also only
  carries that pointer, not the list's contents. None of these three were captured or restored by anything, which is
  why the census (`HrWhyChanged`, `Docked`/`Hangars` counts) treated any change in them as unresumable.
- **Fixed.** `HrAnchor` gained three more by-value copies, keyed the same way as every other one (by the live
  object's own `Ref`): `Decks` (every carrier's `FlightDeck`s, from `FlightDecks::Of(u)` for each `Ship` unit in the
  fight), `Visits` (every flown fighter's own `DeckVisit`, from `u->Fighter->Deck`), and `Bays` (the hangar and
  shuttle bay lists' own contents, restored into the same live `List` object with `Clear()` then `Add()`, so a `Ref`
  held elsewhere still points at the right one). The census's `Docked` and `Hangars` fields are gone: nothing about
  a launch or a landing is left unaccounted for. (`b.Units != a.Units`, the one case that really does mean a new
  hull joined the fight, since fighters stay in `Battle::Units` the whole time launch or landing, is untouched and
  still a barrier, now worded as just that.)
- **Seat change, investigated, not changed.** The directive asked whether switching seats on the *same* hull inside
  the same recorded fight (pilot to a turret) is already covered. The simulated side is: nothing about it needs
  anything beyond what the per-unit snapshot already restores (no human-vs-AI flag lives in `ShipCombat`/`Subsystem`
  that the resume would need to touch). But the barrier census's `you changed seat` check compares `Ship` *and*
  `Piloting` (`a.Ship != b.Ship || a.Piloting != b.Piloting`), so moving from the helm to a turret on the identical
  hull flips `Piloting` and is flagged exactly like leaving the hull altogether, even though `Ship` never changed.
  Narrowing that check to `Ship` alone looks right on paper, but `putBack`'s own pilot-state reapply is gated on
  `IsPiloting()` being true *at the moment of letting go* (`UnitySpaceApp.HoldRewind.cpp`, the `putBack` lambda): if
  you are at a turret when you let go, the anchor's own `Pilot`/`pilotAim` are silently not reapplied at all, whether
  or not the barrier is there. Whether that is actually a problem (the turret's own state restores fine regardless;
  it is only the pilot's helm orientation that would be dropped, and arguably nothing visible depends on it while
  you are not piloting) is something this pass could not answer without running it. Left as a barrier rather than
  guessed at.

### US-T415: a unit joining mid-fight put back; seat change's census fixed and run for real

- **A unit joining mid-fight was a barrier for the same reason launch and landing were.** `Battle::AddShip`
  (`Combat/Battle.cpp:292`) sets `ship->Battle = shared_from_this()` on the `Ship` object itself, outside the
  per-unit snapshot, so a resume to before a reinforcement joined never cleared it back. **Fixed**: `HrRestoreFight`
  now clears `u->Ship->Battle` to null for any unit present now but not kept in the anchor being restored to — the
  same null `War::Join` gives a hull before it joins. The barrier census no longer keys `counted` on
  `u->PlayerControlled()` (which also flips on a same-hull seat change, below) but on `u->Ship == ship` (the hull the
  player is aboard): a non-player unit's mere presence — joining, dying, fleeing, taken, launching, landing — no
  longer enters the barrier hash or count at all. Not given its own dedicated scripted scenario (none of the fixed
  self-test's checks make a reinforcement join mid-recording), so this is verified by code reading plus the existing
  16 checks still passing unchanged, the same standard US-T412's Ephemeris fix was held to; it is not exercised by a
  check built specifically to make a reinforcement join.
- **Seat change's census is fixed.** `ship` for the census is now `HeldSeat()->Subject().Ship` (whichever seat, helm
  or turret) computed once and reused for both the barrier hash and `counted`; `HrWhyChanged` no longer looks at
  `Piloting` at all, only `Ship`. Every consumer of the recorded pilot track (`h.Pilot`/`h.PilotShip`) stays
  separately gated on `IsPiloting()`, so this cannot read stale pilot data while seated at a turret.
- **Run for real, and it surfaced a separate bug, not a rewind one.** The self-test (phases 8-12) takes a hull's
  helm, marks a moment, switches to one of its turrets, holds back to before the switch and lets go. Doing this
  found that `PickSeat`(a turret) on a hull you are piloting, mid-battle, is refused by the seat-taking command round
  trip (`SeatAuthority::Take`'s own `MayEmbody`/`HolderOf` check, reached through `SwitchSeat`'s
  `session->Submit`/`ApplyPending`) even when both front-line checks the player sees — `next->CanEmbody()` and
  `PassesEmbodyGate` — already return true; nothing is taken (`HeldSeat()` stays null). This reproduces with no hold
  or rewind involved at all (`EnterPiloting` a hull already in an active battle, then `PickSeat` one of its own
  turrets), so it is **filed separately as US-T414**, not fixed here. The self-test detects this exact known shape
  (`CanEmbody` and `PassesEmbodyGate` both true, yet `HeldSeat()` stays null after the pick) and logs a skip naming
  US-T414 instead of failing on it; if US-T414 is ever fixed, the pick starts succeeding and this phase falls
  through to the real rewind assertions (the resume reaches the pre-switch mark, the hull's pose stays finite, the
  player is still seated at the turret, not silently ejected) on its own, with no change needed here.

### US-T414, US-T416, US-T417: the seat, the mechs and your own hull

- **US-T414 (a bug in seat taking, not in rewind):** `TakeSeat` let go of the helm before the session weighed the
  take, so `EmbodyContext::Of` no longer saw the hull flown (it adds `World->PilotedShip`'s helm) and the gate's "in
  battle" rule refused a gun on the very hull being flown, leaving the player holding nothing. Single-player now
  takes the seat first and lets go of the helm after (`UnitySpaceApp.Embodiment.cpp`); co-op keeps its routed order.
- **US-T416:** a deployed mech (`Battle::Mechs`, a list of Refs to objects mutated in place) is captured and restored
  by value like a `FlightDeck`.
- **US-T417, the seat journal:** every resume point keeps the seat the player held besides the helm
  (`HrAnchor::Seat`, with `HasPilot`/`PilotShip` for the helm). Letting go puts that seat back first
  (`RestoreHoldRewindSeat`: the old seat taken while the current one still holds the body, so the gate lets it
  through, then the helm entered or left), then the fight over it. The census keys the player's hull by *body*
  (`HrBodyShip`: a craft off your own carrier is the carrier, as the embody gate counts it), so moving between seats
  on one body is never a barrier, and the census's `Seats` count is no longer one either.
- **US-T417, your own hull (`DeferOwnFate`):** a loss, capture or strike of the hull flown, and any surrender's
  terms, wait out the window like everyone else's booking. Meanwhile `HoldRewindOwnFateHeld` keeps the helm but dead
  in the hands (the view follows the wreck or the prize, the stick and triggers do nothing, the helm bar is hidden),
  and the readout shows "YOUR HULL IS LOST (or TAKEN) - hold Shift+R to undo it - final in N s" (or when the rewind
  is ready, inside its cooldown). A hold to before it brings the hull back alive, unbooked and still at the helm; left
  alone, the loss is booked once older than the window and the helm lets go exactly as before. A rewind in its
  cooldown cannot help, which is the cooldown's point.
- **Tested:** the self-test (32 checks, run headless twice, both passing) adds: a seat change from the helm to a gun
  on the same hull is undone strictly (the resume lands at or before the switch, the player is back at the helm, the
  gun is the crew's again); a killing impact (`Battle::ApplyImpact`, the real kill path) on the hull flown is held
  back (still on the world's books, the helm still held, the readout up), a hold to before it brings it back alive,
  unbooked and at the helm, and a second loss left alone in the cooldown is booked only once the window is out and
  then lets go of the helm. All game layer (no Core file changed), so the Bench is untouched.

### US-T418: ships that came and went in the window

- **Left the system:** `Fleet::LetGo` (`Fleet.cpp:2429`) does not take the hull out of `Fleet->Ships()`: it puts a
  `Ship::Tombstone()` in its slot (a copy flagged `Destroyed` + `GateDeparted`, keeping the motion it had), so the
  index, the per-frame arrays and `shipScene` stay put and `Fleet::StateOf` still places it at a past `jd` along the
  path it was flying to the gate. (The earlier note here said the hull was removed entirely; it is not.) The view
  loop hid every `GateDeparted` slot; now, while a hold plays back to before the moment the slot went, it is drawn as
  any live hull (`UnitySpaceApp.Main2.cpp`, the ship views), and its HUD mark comes back with it (`Hud.cpp`).
- **Arrived in the window:** the fleet only grows, so an index that first appears is a ship that came in. The hold
  notes the lived second each index first appears and each slot turns `GateDeparted` (`TrackHoldRewindShips`, from
  `RecordHoldRewind`; game layer, no Core timestamp, so no Bench question), and `HoldRewindShipShown` hides a ship
  that came after the moment reached.
- Drawing only, like `ViewSystemBack`: letting go does not bring a departed ship back into the system (it is on its
  passage), which matches the rest of the system outside the fight, drawn as it was and put back to now on release.
  Ships present when the recording started (a load, a new system) count as always there.
- **Tested:** the self-test checks the show/hide rule during a real hold (34 checks, all passing).

### US-T419: the fight's end

- `Battle::CheckOver` asks a new hook, `Battle::HoldEnd`, before it sets `Over`. The hold rewind sets it on the fight it
  watches (with the booking hooks, `HookHoldRewindBookings`): true while the moment the fight would first have ended is
  younger than the window. Meanwhile the fight runs on as a mop-up (`Mopping`, the state it already had with fighters
  aloft or a boarding on), `Winner` set and `EndHeld` true, and the readout says VICTORY / DEFEAT / BOTH SIDES BREAK OFF
  with the time left to undo it. Once the window is out it ends exactly as before: `Ending` books what waited, then
  `War::Resolve` settles it (loot, the ships let go, the report and the music's sting).
- Core change, but only behind the hook: null in every fight the rewind does not watch, so AI-only runs and the Bench
  never meet it (`tools/bench.ps1`: unchanged digests).
- Cost: after a win the fight stays a fight for up to the window (the ships in it are still in it, e.g. for an FTL jump);
  leaving the battle space frees the player at once as before. `HrTuning::DeferFightEnd = false` turns it off.
- The HUD follows a resume (`Hud.Hits.cpp`): the after-action timeline drops moments after the fight clock put back,
  a tally a loss closed is reopened (its report taken down) when the loss is undone, and the passing hit/kill lines go,
  with the kill claims of units alive again. A hull lost whose loss is still held back no longer closes the tally.
- **The real key (US-T420):** `sg.HoldRewind selftest` holds the rewind by pressing and letting go of Shift+R as key
  events into Slate (`FSlateApplication::ProcessKeyDownEvent`, the path the OS keyboard takes), with the app reading
  Unreal's own input for the test (a headless run otherwise feeds it scripted input): Slate, the game viewport, the
  player controller, `UnrealInputDevice::Feed`, the key bindings (`KeyAction::HoldRewind`, Shift+R) and the hold. The
  test's own "force down" no longer reaches the hold, and a check asserts the game read the key (44 checks, all passing;
  the run logs which path carried the keys: Slate).
- **Seen, not only checked:** the self-test saves the screen at four moments (`Cpp/Saved/Screenshots/holdrewind-*.png`:
  mid-hold, YOUR HULL IS LOST, VICTORY held, the fight on again after the resume). Looking at them found the stale
  after-action report and kill label above, both fixed and re-shot.
- **Tested:** the self-test strikes down every enemy with real killing impacts: the end is held (open, winner known, the
  rewind still on the fight), a hold to before the kills plays on with the enemies back and the fight on, and a second
  win left alone in the cooldown ends the fight only once the window is out.

### US-T421: effects played backward

- `BattleFx` draws every effect by its age (`now - Born`). While a hold plays the past back it ages them by
  `HoldRewindFxClock`, which runs backward with the hold (the press's clock minus how far back the hold has gone), so a
  laser, a hit flash or an explosion plays in reverse to its birth and is gone before it. Effects already over are kept
  for the window (a `history` list outside the effects budget, drawn only during a hold) so a hold also brings back
  the ones that had finished. On letting go (`HoldRewindFxEnded`), the effects born after the moment played on from
  never happened and go; the rest are shifted so they go on from that moment without a jump.
- Projectiles, mines and decoys were already played back from their tracks. Not reversed: the audio bed.
- **Tested:** the self-test checks the effects' clock sits at the moment reached during a hold (45 checks, all passing).

### US-T422: another body's seat, another body's frame, the hull left

- The census no longer counts the body held (`Ship`) or the helm's frame (`Frame`) as a barrier.
- **Seat:** `RestoreHoldRewindSeat` takes the helm of the resume point's seat body first when the player is not aboard
  it (the helm is ungated, as a load takes it), so the embody gate sees the player aboard and lets the seat back, then
  lets go of the helm if the resume point had none (US-T414's order). Letting go of the helm altogether (to the map)
  is undone the same way: the resume takes the helm back.
- **Frame:** each kept pilot pose carries its frame body; the hold plays it back in that frame and a resume puts the
  frame and the pose back together (`pilotFrameBody`, `helmFlight->FrameBody`), as a save loaded does.
- **Tested:** the self-test lets go of the helm mid-fight, holds back to before, and checks the player is back at the
  hull's helm in the fight's frame with no barrier (50 checks, all passing). The frame crossing itself is not driven
  by the test (a fight's helm never changes frame); it rides the same restore as a load.

### US-T423: co-op

The designer's call: in co-op the rewind plays **only while every player holds the key** (a vote). One player holding
alone does nothing but show who it waits for (`REWIND: P2 Client holds Shift+R - waiting for P1 Host to hold Shift+R`,
the readout's centre line on every peer, and a co-op log line whenever who holds changes).

- **The key is the frame's.** A client sends its key to the host when it changes (`hrkey 1|0`, as the clock and setting
  requests go). The host ANDs its own key with every roster player's latest (`CoopPeer::HostHoldAll`) and writes the
  result into the frame with the fight it keeps and who holds: `hr <all> <fight index> <seat mask>` (left out when
  nobody holds and no fight is kept). Every peer steps the hold on that value, never on its own key.
- **Hold frames are frames.** The hold's step runs before the clock step, so a hold frame used to open no record. Now
  the host's hold frame (and an Update that stopped at the pause menu after the hold's step) opens a record of its own
  (`HostOpenHoldFrame`: the dt, no queued commands, which wait for the next frame that steps) marked `ho`; a client
  replaying an `ho` frame runs the same hold step and the drawing only, and counts the frame consumed. A client frame
  replayed again after its pause menu stopped it does not step the hold twice (`coopHrStepFrame`).
- **Deterministic on every peer.** The cooldown runs on a clock of the frames' dt (`FrameClock`; the wall clock differs
  per machine), single-player too (it only stops while the pause menu is open; the self-test's phases keep their own
  wall timing). The recording, the hold and the resume run as shared steps (`coopShared`), so the helm is everyone's
  there; the fight kept is the host's (its frame names it by index: the helm's fight, else the one its camera
  watches); only the shared helm's seat is recorded and put back, and any player's seat or the helm changing is a
  barrier (`HrCensus::Coop`), so a resume never needs another player's seat back. During a hold no frame steps the
  clock, so a client's clock is never "corrected" against the host's set-back date. A machine's own saves (autosave,
  quicksave, keyframe) do not book the held-back fates in co-op (they wait out the window on every peer alike); a sync
  starts the recording afresh on every peer (a hold under way back to its press, the bookings made into the sync's
  save; `ResetHoldRewindForSync`). One machine's `-norewind` does not reach the party's rewind.
- **Proved** by the co-op smoke (`./tools/play.ps1 -Mode coop-smoke`): after its load the client starts a fight off
  Uranus in the frames (`x <seat> hrfight`, smoke only), holds Shift+R alone for 90 frames (no rewind), then the host
  holds too (`smokehold`, the headless host has no keyboard), both play it back from the same frame, let go, and play
  on from the same resume point with the same fight digest; every digest after it matches. A run: the client alone 90 frames, played on 0; both peers `every player holds
  Shift+R at frame 4095` and `plays the past back from frame 4095`; 5.33 s back after 120 frames; both `played on from
  5.55 s back at frame 4232; fight digest 5BA426BB37DC518A`; 67 frames with both digests, 67 identical, no DESYNC, every
  smoke check passing. The single-player self-test is unchanged at 50/50.

### A fight that began in the window (US-T424)

A fight's start is no longer a barrier. Whole-world snapshots were not needed: a battle's creation touches the world
in few places that last. Its ships are only marked as in it (`Join` and `AddShip` set `Ship::Battle` and place a
unit; their schedules run on underneath), and the war lists it, numbers it, counts its name and logs its start.

- **Entering a fight.** When the rewind starts watching a fight with none watched before (`UndoFightStart`), the
  helm's resume points from before it are kept (another fight replacing one still clears them, a barrier as before),
  and the fight entered is not a census change. It was **born** then if it had fought no more than `BornS` (1 s) of
  its own clock and the war holds it.
- **Playing back past the start.** Before a born fight began (`HoldRewindBeforeFight`) none of it is drawn: its
  ships blend back to where their schedules had them (the same 2 s blend they entered it with), its other units are
  hidden, and the hull flown follows the helm's track at once. The fight's clock stays at its first moment.
- **Playing on from before it.** A born fight is taken back: `War::Unstart` takes it off the war's list unresolved (no
  `BattleEnded`, nothing booked), clears every ship's `Battle` (a hull an ambush pulled out of a lane goes back into
  it), gives back its number and its name's count while nothing has come after it, and removes its start from the
  world's log. Its held-back bookings were all from after the resume point, so they are dropped, and its hooks go
  back to the world's. The helm plays on from the resume point, outside any fight. If the hull is still where the
  fight found it, the fight begins again, as the same battle by number and name.
- **Not born there.** A fight already under way when the hull joined it lets the hull go (out of it, as a hull that
  flew clear is), and one that only came into view goes on from now (its units, moved by the playback, are put back
  as they were at the press). Either way the helm plays on from back there.
- **What stays.** What the start set off elsewhere: the danger it raised at the body, and traders already turned away
  round it (`World::DivertAround`). Single-player only: in co-op entering a fight still clears the resume points
  (the barrier as before).
- **Proved** by `sg.HoldRewind selftest birth quit`. It takes the helm of a cruiser spawned off Uranus with no fight
  about, flies 3 s, marks a moment, and brings a Reach squadron alongside so the fight begins on the hull. After 2 s
  of fighting it holds back to the mark. The checks: the fight was seen born after the mark; the playback drew the
  moment before it (screenshot `holdrewind-5-before-the-fight-held.png`); the resume point is at the mark; the war no
  longer holds the fight; the hull is out of it and still at its helm; every ship that was in it is back on its
  schedule; the world's log no longer tells of it; the rewind watches no fight. Begun again, it is the same fight by
  number and name.

### No fight before it began, a fight left, damage played back (US-T426)

- **No fight before it began, anywhere.** While the hold plays back a moment before a born fight began, its ships are
  let out of it (`Ship::Battle` cleared, `HoldRewindState::Detached`). The simulation does not step during a hold, so
  this is only what everything reads: the HUD's fight panels, the helm's order bar and the status lines all show the
  hull "under your helm", as before the fight. They go back in as soon as the moment reached is after its start, and
  before anything else when the hold ends (`HoldRewindReattach`).
- **A fight left is kept** (`KeepFightLeft`). The rewind keeps a fight the hull flew clear of, or one that went out of
  view, while it goes on (only another fight replaces it), so leaving it is no barrier. A resume point from before the
  hull left puts the hull back in it (`HrRestoreFight`: a unit not fled at the resume point gets its ship's `Battle`
  back, the mirror of US-T414's joiners).
- **Damage played back.** As the hold passes each resume point, its hulls' combat state and systems are written over
  the live ones (`DamageShown`), so a hull mends as the hold goes back past its wounds (parts shot away come back,
  breaches close, the hull share rises). Every way a hold ends puts a whole resume point (or the press's) back over it.
- **Proved.** The birth check adds one check (12/12): held before the fight began, the hull is in no fight for
  anything reading it. The main self-test adds a check that the hull is whole again while still held before its loss,
  and phases 26-29: the helm is put well past the fight's edge, the hull is out of it with the fight still kept, and a
  hold to before puts it back in the fight at its helm (56/56).

### Heard backward (US-T427)

- **The bed.** Under a hold plays a synthesized loop: a tape running back, a breathy whirr with a falling warble
  (`CombatSounds`, `RewindBed`). Its pitch and loudness follow the playback's speed (the slow start, the top rate),
  and it fades out on letting go.
- **The fight, reversed.** Every fight sound heard is kept for the window with the moment it was heard on the
  effects' clock, the clock US-T421's reversed effects age by (`Persistence/HoldRewindEcho.h`). While a hold runs
  that clock back, each sound it passes plays again reversed, where it was heard: an explosion is sucked back into
  its flash, and a shot is drawn back into the gun. At most 12 play per frame, sped up a little with the playback.
  On letting go the sounds after the moment played on from never were, and the rest move on with the clock, as the
  effects do. Local to each machine, like all of the game's sound.
- **Proved.** `HoldRewindEcho` is unit-checked in `Tests/HoldRewind` (27/27 with the curve's checks): which sounds a
  hold passes and in what order, the cap, a resume dropping the undone and shifting the rest. A self-test run with a
  real audio device (`-GameAudio`, no `-nosound`) logs each hold. Every hold played the bed, and 5 to 64 sounds were
  played reversed per hold (`CombatSounds: hold rewind heard backward: bed played, 64 sounds played reversed`). The
  self-test passed 56/56 in that run.

### Crossing into another body's space, tested (US-T429)

`sg.HoldRewind selftest birth` now opens with a crossing check (phases 90-93). At the helm off Uranus, it marks a
moment, then puts the helm into a moon's space (Miranda), where the cruise frame's own rebase (`RebaseCruiseFrame`)
moves the helm into the moon's frame. A hold back to the mark puts the helm back in Uranus's frame, where it was
(0.000 km off): US-T422's frame and pose put back together. The check passed 16/16 with the birth checks.

### Not yet (later stages)

- A ship that changed phase (left an orbit, joined a lane) in the window is still drawn where its *current* phase
  puts it at the past `jd`, as a ship's analytic state is a snapshot of now, overwritten at every phase change.
- A fight born while another is watched (one replacing the other) is a barrier: the resume points of the first fight
  are cleared when the second takes its place.

### Co-op too (US-T428)

A fight born in the window is taken back, and a fight left is kept, in co-op as in single-player. Every peer sees the
same fight entered at the same frame (the host's frame names it, the war is the same everywhere), keeps the same one
when it is left, and takes it back at the same frame of the shared resume. Resume points are now kept every `AnchorS`
with no fight or helm too (empty ones), so a hold can play on from before a fight began when only watching. Each
peer's resume line says so ("played on from 4.33 s back: before Battle of Neptune began at frame 5142").
**Proved** by the co-op smoke. After its first hold, the client takes that fight off the war in the frames (`hrdrop`,
smoke only), and a fight is born off Neptune in the frames (`hrborn`, its clock at 0). Once the cooldown is out, both
players hold for 200 frames back past its start and let go. Both peers played on from the same moment before it began
at the same frame, the war holds it no more, and all 79 digests were identical.

## Parked: checkpoint loops with ghost replays (US-T169)

This is a design note only and nothing is built. It is parked until the US-T99 rewind lands, but the rewind build
must not rule it out.

**The idea.** The player marks a checkpoint, and from then on a short window (about 10 game-s) is recorded. At the end
of the window the world rewinds to the checkpoint and the player plays the same window again, this time embodying a
different unit. Every earlier pass plays back as a **ghost**: the unit it embodied repeats that pass's recorded
inputs. After N passes, N units in one battle are flown by the player's own past selves. The player can spend hours
choreographing ten seconds, then let it run on as one take.

**How it maps onto the rewind design.**

- *The checkpoint is a keyframe.* "Mark" forces a keyframe into the dense short-rewind ring and pins it so thinning
  cannot drop it while the loop is open. Each pass starts with a restore (0.18-0.46 s, measured above) and needs no
  replay, so starting a new pass costs about the same as a short rewind.
- *A ghost is a seat held by a recording.* Co-op already flies a hull from someone else's hands:
  `Coop::PilotFrame` (Cpp/Source/SpaceGod/Coop/CoopInput.h) is one frame of a helm holder's input, carried per tick
  in the lockstep stream and applied in place of the local poll, and `SeatAuthority` decides who holds which seat. A
  ghost is the same thing with the frames coming from a recorded track instead of a network peer, so playback reuses
  the co-op path rather than adding a second input route.
- *The recording is the intervention log, keyed per unit.* The log US-T99 needs (every input frame, order and clock
  change, stamped with its tick and its frame dt) has to record **which embodiable each input frame drove**
  (`EmbodiableKey`) and not only "the player's input". Then one pass's track is the log filtered to that unit inside
  the window, and a ghost set is several such tracks replayed together. Commands inside `EmbodimentInput` (power
  split, self-destruct, helm ability, sensor) are already commands that land the same when carried twice, so they
  replay safely.
- *Branches.* Each pass overwrites the window's future. With the single-timeline first version, a pass replaces the
  last take's future, and the pass tracks, not the timeline, are what persist. The keyframe tree kept
  branch-friendly (see Storage above) would let a later version keep takes side by side.

**Requirements this puts on the US-T99 build** (cheap now, costly to retrofit):

1. Log input per embodiable (`EmbodiableKey` + tick + dt + `PilotFrame`), not per player.
2. Allow a keyframe to be pinned against thinning, and keep a dense ring around a pinned one.
3. Let a seat be held by a track player as well as a local or remote human, going through `SeatAuthority`, so that
   the AI hand-back (`IEmbodiable::HandBack`) runs when a track ends.
4. Real-time combat has to re-simulate bit-exactly (the battle-warp0 row above, fixed by US-T111). A ghost loop is
   real-time combat by definition.

**Open questions / risks.**

- *Drift and causality.* A ghost replays inputs, not outcomes. When a later pass changes the battle (it kills the
  ship a ghost was chasing), the ghost flies its old stick movements into a new situation and stops matching what
  the player recorded. Options: accept it (inputs are the truth, so the result is emergent), freeze earlier ghosts
  as outcome tracks (positions forced, which breaks determinism and physics), or warn when a ghost's replayed state
  drifts from its recording by more than a threshold and offer to re-record that pass. Recommended first cut: accept
  drift and show the divergence. Determinism makes it reproducible, at least.
- *Pacing (the Phantom Brigade risk).* Ten seconds replayed dozens of times can turn a dogfight into a spreadsheet.
  Keep it opt-in and off the main loop: a cap on passes or a soft budget, a fast "commit the take" that hands every
  unit not yet flown back to the AI, and a scrub bar over the window so the player is never forced to sit through
  a full pass to fix one moment. The normal game should never need it.
- *Window length.* 10 s is the pitch. Seek cost does not matter here (every pass restores the pinned keyframe), so
  the limit is player patience and the log size of N tracks, which is kilobytes.
- *Co-op.* Several players could each record passes into the same loop. A rewind is already a lockstep command, so
  a pass restart is too. Who owns which ghost and whether passes interleave stay open.
- *Visuals.* Ghosts need to read as ghosts (a tint or trail on units flown by a track), distinct from the occluder
  ghost material (US-T185), which means something else.

Revisit once US-T99's short rewind (dense battle ring, per-frame input log) is built and measured.

## Parked: split timelines, a tree of futures (US-T104)

This is a design note only and nothing is built. The user's call on US-T99 (2026-11-05) stands: the first version has
one timeline, and changing the past replaces the old future. This note records what keeping the old future would take,
so the US-T99 build stays branch-friendly and does not need a rewrite to grow it.

**The idea.** When the player rewinds and acts, the future they left is not thrown away. It becomes a **branch**: a
sibling of the new line of play, still reachable. A timeline panel shows the session as a tree (like git): the trunk,
each fork point marked with the tick and the action that split it, and each branch's tip with its game date. The
player can switch to any branch tip (or any moment on it) and play on from there. "What if I had not declared that
war" stops being a one-way choice.

**The data model.** Everything US-T99 stores already has a time and a parent, so a tree adds a branch id and nothing
else structural.

- *Keyframes form a tree, not a list.* A keyframe is keyed by (branch, tick) with a parent link to the retained
  keyframe before it (Storage, above). The delta encoding already works against "the previous retained keyframe", so
  it needs no change: a branch's first keyframe is a delta against the keyframe at its fork point, which is on its
  parent branch. Shared history before a fork is stored once.
- *The intervention log forks too.* A branch is "the parent's log up to the fork tick, then its own entries". Store it
  as segments: each branch owns the entries from its fork tick to its tip and names its parent and fork tick. Replay
  of any moment walks up the parent chain to collect the entries. The log is kilobytes, so copying it would also be
  fine, but segments keep "what changed here" explicit.
- *A branch record* holds: id, parent id, fork tick, the action that forked it (the first log entry after the fork, for
  the panel's label), tip tick and game date, a player label, and last-visited wall time.
- *The current branch* is one id in the session. Rewinding to tick T on branch B and then doing nothing is still a seek;
  the fork happens only at the first intervention after the seek (the first log entry that differs from B's own entry
  at that tick). Seeking around without acting must not litter the tree with empty branches.

**Thinning across branches.** The budget is shared by the whole tree, not per branch, or a session with many forks
would multiply the 512 MB / 4 GB budget.

- The current branch (and its ancestors back to the root) is thinned exactly as the single-timeline schedule above.
- An inactive branch is thinned by the age since it was last visited, not by game age: it keeps its fork-point
  keyframe (which lives on the parent anyway), its tip keyframe, and keyframes at the coarse tiers (1 game-day, then
  1 game-week) in between. Seeking inside an old branch then costs one restore plus a replay of at most a tier
  spacing, which at warp 4 and above is under a second (Rewind latency, above).
- The tip keyframe of every kept branch is pinned (the same pin the checkpoint loop needs, requirement 2 of US-T169),
  so "return to that future" is always one restore and never a long replay.
- When the budget still overflows, drop whole branches, oldest-visited first, never the trunk the player is on and
  never a branch the player labelled or starred. Before dropping, the panel can offer to export the branch tip as a
  normal save (it is one keyframe, already the save format).
- Re-encoding after a drop works as now: a keyframe whose dictionary was dropped is re-encoded against its new
  predecessor. A branch's first keyframe depends on its fork point, so a fork-point keyframe is not dropped while any
  child branch still needs it as a dictionary (or the child's first keyframe is re-encoded whole first).

**Persistence.** A normal save stays what it is: one world, one `SaveData`, and the files in `SaveSlots` are unchanged.
The rewind store (keyframes and log under the save folder's spill directory) already lives beside the saves, not in
them. The tree is that store's own index (the branch records above). Loading a save starts a new tree rooted at that
save. Whether a save should carry its tree (so loading it restores the branches) is an open question: it would make
saves as large as the spill.

**Co-op.** A rewind is already a lockstep command that any player can issue (US-T99 note, 2026-11-05). Switching
branch is the same kind of command: the host orders it, and every client jumps through the existing resync
(`CoopPeer::RequestResync`). The tree is part of the host's session state, so every player sees the same tree. Two
players forking at once resolve in host order: the later command wins, and the earlier player's new future becomes a
branch like any other instead of being lost, which is gentler than the single-timeline rule.

**What it puts on the US-T99 build** (cheap now, costly to retrofit):

1. Key keyframes and log entries by (branch, tick), even while there is only branch 0.
2. Give every keyframe a parent link, and encode deltas against the parent, not against "the last one written".
3. Put the fork decision at the first intervention after a seek, not at the seek itself.
4. Make keyframe pinning a general mechanism (US-T169 needs it too) and keep the budget accounting global.
5. Keep the timeline bar's preview (analytic positions) a function of (branch, tick), so it can draw any branch.

**Open questions / risks.**

- *Is it fun, or bookkeeping?* A tree of futures is powerful for a god game and can also become a chore: the player
  manages branches instead of playing. Keep it out of the way: the first version shows the tree only in the rewind
  panel, keeps at most a few recent branches by default (setting: keep none / last 3 / all within budget), and drops
  old unlabelled ones quietly.
- *Comparing branches.* "Return to the old future" is the core. Showing two branches side by side (the same system's
  date in both) would need two worlds loaded, at 2x memory, and is out of scope.
- *Merging.* Branches never merge: two worlds that differ cannot be combined meaningfully. Carrying a single thing
  across (a design, a discovered blueprint) would be a separate feature.
- *Checkpoint loops (US-T169).* The loop's takes could each be a branch, which would let a player keep alternative
  choreographies. The pass tracks stay the persistent thing either way.
- *Determinism is the whole basis.* Every branch is rebuilt by replay, so it needs the same bit-exact restore as
  rewind (the Determinism table above, and the per-frame dt in the log).

Revisit after the single-timeline rewind (US-T99) is built and players have used it.

## Reproducing

```powershell
tools/bench.ps1 -Rewind                         # build Bench.Rewind.exe (Core lib too), probe all five scenarios
tools/bench.ps1 -Rewind -NoBuild -Scenario battle
Cpp/Intermediate/CoreStandalone/Release/Bench.Rewind.exe --scenario battle --rewind-probe --rewind-then-warp 0
python tools/rewind-project.py [--ram-mb 512] [--disk-mb 4096]
```

Probe options: `--rewind-samples N` (pairs per interval, default 4), `--rewind-verify N` (ticks after a restore,
default 60), `--rewind-then-warp N`, `--rewind-trace N` (live and replay digest hashes every N ticks after a
checkpoint, reporting the first divergence), `--rewind-dump-at T` (digest text and keyframe of the live run and of
each replay at tick T), `--graph-diff-files A B` (the saved entries in which two dumped keyframes differ),
`--rewind-graph-diff N` (lists the differing entries when a restored world re-captures differently) and
`--rewind-decks` (carrier deck state).
