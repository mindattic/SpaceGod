# Rewind: measurements for the time-reversal budget (US-T99, phase 1)

US-T99 proposes rewinding the session to any earlier moment from **deterministic keyframes plus an intervention log**:
keyframes are denser during battles, older ones are thinned under a fixed budget, and the session start is always
kept. The first version has one timeline, stored so that it can branch later. Before a horizon is picked ("how far
back can you go"), this phase measures what keyframes cost, how fast the simulation re-simulates, and whether a
restored keyframe re-simulates bit-exactly. Nothing here builds the rewind feature.

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
