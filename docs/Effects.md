# World effects: exhaust clouds and the rest (US-T447)

The owner: "needs more special effects and cloud exhaust".

Every thrusting hull now leaves its exhaust as clouds: soft, lit puffs that billow out of each nozzle, glow in the drive's
colour while they are young, then cool into pale vapour, spread and thin out behind the hull. They are left in world
space, so a turning hull lays a curving trail and a moving one leaves its trail behind it. Around that, a set of effects
for flight, fights, drives and gates. All of it is drawing only: nothing in the simulation is read back or written, so
co-op peers stay in lockstep (each draws its own) and the bench (`tools/bench.ps1`) is untouched (SpaceGodCore is not
changed).

Every tunable is in one place: `Views::EffectsTune` (`Cpp/Source/SpaceGod/Views/EffectsTune.h`).

## What is drawn

| Effect | When | What it looks like |
| --- | --- | --- |
| **Exhaust clouds** | a main engine lit (the hull's own flames, `HullThrusters`, US-T432) | clouds out of every nozzle along its own axis, born hot in the drive's blue, cooling to pale vapour, growing several times their nozzle's width and thinning away. A fighter's are thin, quick and short-lived (0.9 s); a freighter's or a capital's denser, slower and long (3.0 s). Rate, size, speed and density scale with the throttle (the jet's level) and the hull's heft (its length, a freighter or a miner heavier than its length) |
| **Boost** | a hull's boost charge falling (a fighter or a gunship opening up past cruise) | the kick: a ring of hot puffs round the nozzles and a flash on them; while it burns, twice the clouds, bigger, hotter, faster |
| **RCS vapour** | any RCS jet or retro lit (US-T433) | a small cold puff of white vapour out of the jet, a little stream while it holds |
| **Venting** | a damaged hull's breaches burning (`ShipView`'s breach fires, by hull damage) | gas streaming out of each breach, now and then a puff with fire in it; a wreck smokes darker and longer |
| **Impact** | a hit on a hull (`FxKind::Impact`) | sparks spraying off the plating, a few specks of it tumbling clear, a puff of hot smoke |
| **Blast** | a hull lost (`UnitDestroyed`), a part shot out (`SubsystemDestroyed`, a third the size) | over BattleFx's fireball: a shockwave ring racing out and a slower one behind, sparks, glowing embers drifting off and cooling red, and a debris cloud that glows orange as it cools to dark smoke and hangs for about 8 s |
| **Breach jet** | a boarding pod's breach (`FxKind::Breach`) | a jet of air streaming out of the hole for 1.6 s |
| **FTL jump** | a hull jumping out of a fight (`FxKind::FtlJump`), the helm's FTL drive engaging or arriving | a flash, a ring, and a streak on along the heading (arriving: the streak it came in on) |
| **Cruise engage** | the helm's in-system cruise drive engaging | a shock ring off the stern, a flash and a burst of hot exhaust |
| **Gate transit** | a hull going out through a jump gate (its index's ship turns `GateDeparted`), or a hull new to the system's fleet beside a gate | going: space folds shut on it (a violet ring closing), then a white flash and a streak on through, ionised wisps left hanging; coming: the flash first, the fold opening out round it, the streak it came in on |

Shield hits already ripple across a hex lattice on the struck arc (BattleFx, U.SPC-T40); muzzle flashes, tracers and the
fireball are BattleFx's as before. Docking and berth lights are not part of this ticket.

## How it works

`Views::WorldFx` (`Views/WorldFx.*`) holds three pools and draws them as two dynamic meshes of camera-facing quads:

- **Clouds**, drawn with the new `M_Smoke` material (`Shared/Plugins/SpaceGodRender/Shaders/Smoke.ush`,
  `spacegod_render_materials.py`'s `build_smoke`; translucent, two-sided, sort priority 5 so the additive glows and fire
  draw over it), sorted back to front every frame. Each quad is a ball of cloud: a billowing edge from two octaves of
  value noise turned and shifted by the particle's seed, shaded as a sphere under it lit by the system's star (wrapped, so
  the night side still reads, with its folds darker), a bright fringe when it is backlit, and a hot core in the vertex
  colour that fades with the particle's heat. Per vertex: colour = tint and opacity; Unity uv0.zw (TexCoord1) = albedo
  level + seed, heat.
- **Sparks and embers**, and **flashes** (radial glows, rings, streaks), with `M_Fx`'s own shapes (no new material;
  `Fx.ush` gains a thin ring, shape -1, crisp at any radius, for the shockwaves, the gate's fold and the drives' rings).

Every particle moves on a closed form of its age (`P0 + V0 (1 - e^(-drag t)) / drag`; its size, opacity and heat are
functions of age too), so nothing is integrated frame by frame. That is what keeps the hold rewind working: the effects
run on `UnitySpaceApp::HoldRewindFxClock`, so while a hold plays the past back they run backward with it (a puff shrinks
back into its nozzle, a blast's cloud contracts to the blast); nothing new is emitted then; when the hold ends
(`HoldRewindFxEnded`) the effects born in the moments undone go and the rest carry on from the moment played on from,
exactly as BattleFx does.

The app wires it (`UnitySpaceApp.Effects.*`): `BeginWorldFx` sets the frame's clock in `UpdateViews` before BattleFx
runs, so BattleFx can hand over each new event (`BattleFx::HandToWorld`: impacts, blasts, breaches, jumps); at the end of
`UpdateViews`, `UpdateWorldFx` reads every fleet hull whose full model is on screen (`ShipView::Shown`): its jets
(`ShipView::JetsView`, `HullThrusters::Jets`: each nozzle's position, axis, width and level, in the hull's frame, turned
into scene coordinates through its view's own root), its burning breaches (`ShipView::Vents`) and its boost charge; then
every battle craft (`UnitKind::Fighter`, drawn by FighterViews) as a single nozzle astern; then the gate and drive edges;
then draws. A hull's velocity is read from its scene position frame to frame (smoothed), and each cloud keeps 12% of it:
the rest is left behind, which is the trail. Clouds are spread back over the frame along the hull's path (born where the
nozzle was then, that long ago), and enough of them are made to keep the trail whole however far the hull went.

## Performance and limits

- **Budgets** (`EffectsTune`): at most 5,000 clouds, 3,000 sparks and embers, 400 flashes alive; one hull at most 260
  clouds; past a budget the oldest go first.
- **LOD by apparent size**: a hull under 7 px long on screen throws no clouds; between 7 and 220 px its rate falls to 18%
  with clouds up to 1.9 times bigger, so a far hull's trail is a few big puffs rather than many tiny ones. Clouds under
  0.6 px are not drawn. A hull moving more than 60 of its lengths a second (high time warp) or jumping more than 6 of
  them in a frame leaves no clouds (they would be strung out, not a trail).
- The cost per frame is one pass over the live particles, a sort of the clouds (n log n, a few thousand at most) and
  two mesh uploads; the smoke shader is two 3-octave value noises per pixel. Big clouds near the camera are overdraw: a
  capital's exhaust seen from close behind is the heaviest case.
- Comfort: **Reduce flashes** (`PlayerOptions::FlashScale`) dims every flash (and BattleFx's white explosion flash,
  which it did not before); **reduced motion** (`UiMotion::ProfileReducesMotion`) drops the sweeping shockwave and drive
  rings and keeps 40% of the sparks and embers.
- Limits: clouds already gone are not brought back by a hold rewind (BattleFx keeps a history of finished effects; the
  clouds do not, to stay cheap); only those still alive run backward. A hull arriving through a gate is recognised as
  one new to the system's fleet within three gate diameters of a gate. The helm's cruise and FTL flashes are read from
  the drives' state changing (`SystemCruiseActive`, `InFtlCruise`); other hulls' cruise is not shown. Vents sit on the
  breach fires' spots in the hull's frame, so on a wreck whose halves drift apart they stay where the halves were joined.

## Checking it: `sg.Effects`

```
./tools/play.ps1 -Offscreen -Capture '' -Seconds 300 -ExecCmds "sg.Effects shot R:/Temp/effects quit"
```

`sg.Effects [shot <dir>] [quit]` (`EffectsCommand.cpp`) stages a battleship, a freighter and a fighter in open space with
their main engines lit (`sg.ThrustersLit 2`, new: the mains alone) and screenshots (`effects-*.png`): the capital's
exhaust from astern and abeam, the freighter's, the fighter's; the fighter's boost opening (its charge drained as a boost
drains it) and burning; the freighter with every jet lit (RCS vapour); the freighter's cruise drive engaging and the
capital's FTL jump (played on them directly); a destroyer spawned just inside a jump gate (the arrival read as in play)
and then sent out through it (`Destroyed` + `GateDeparted`, as a transit retires a hull); then a fleet action off Uranus:
the furball, a hit on a hull and its smoke, a hull blown apart (blast, shockwave, the debris cloud 3 s on), the most
damaged hull venting, a fighter in a dogfight watched from beside it (its curved trail) and a craft on its boost. Each
shot logs the live clouds (and how many were drawn), sparks and flashes.

Checked on 2026-11-21 (24 shots): the capital's and the freighter's exhaust billow out of their nozzles and spread and
thin into a trail several hull lengths long (about 300 to 450 clouds alive with three hulls lit); the fighter's is thin
and quick; the boost's kick is a hot white puff on the nozzles; the freighter with every jet lit stands in its own RCS
vapour; the FTL jump is a flash, a thin ring and a streak; the gate transit a violet fold and flash at the ring's scale;
in the fight, small craft lay curving cloud trails through their turns, a hit leaves sparks and a puff of smoke, a hull
lost throws two thin shock rings, sparks and embers and leaves an orange-lit cloud hanging; a damaged hull vents from
its breach. The fight adds about 100 to 300 sparks and embers and 100 to 200 clouds at its busiest.
