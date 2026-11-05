# SpaceGod and Prose (US-T171)

SpaceGod is a consumer of [Prose](../../Prose/docs/CONSUMER_ONBOARDING.md), the MindAttic repository of universes. The
whole generated galaxy is catalogued into a Prose universe called `spacegod`, so Prose sessions can write lore, characters,
technologies and stories against it, and what Prose proposes comes back as data files a human reviews before anything
reaches the game.

The two sides are coupled only by a contract. Neither repo writes into the other's store:

```text
  SpaceGod Core ──(tools/universe-export)──> Data/Prose/spacegod.universe.json ──POST /api/universes/spacegod/import──> Prose Hub
  (simulation facts: read-only canon in Prose)          (the contract file)                (X-Prose-Key; never raw SQL)

  Prose session ──POST /api/outbox/spacegod──> Prose Outbox ──(tools/prose-outbox.ps1)──> Data/Lore/<category>/<id>.vN.json
  (proposals: never edits game data)                                                      (pending review; not loaded by the sim)
```

## Field ownership

- **The game owns the simulation facts**: orbits, positions, stats, populations, cargo, ownership, histories, pilots'
  life events. In Prose they are read-only canon. A change to them is a change to Core, re-exported.
- **Prose owns the narrative**: lore, voice, motives, rumours, technology descriptions, story templates, mystery hooks.
  It sends them back as proposals; it never edits game data.

## 1. Game to Prose: the export

`tools/universe-export.ps1` builds `tools/universe-export/UniverseExport.cpp`, a headless Core runner (no Unreal; it
links the standalone `SpaceGodCore.lib` from `tools/build-core.ps1`, as the Bench does) and runs it:

1. `Galaxy::CreateDefault(2461324.0, seed)`: the galaxy exactly as the game and the Bench build it, at the Bench's fixed
   start date so the clock never enters into it, with Sol in focus.
2. Runs it on for `-Days` (default 30) at coarse detail, so there is a little history: wrecks, derelicts, wars, grudges.
3. Meets the pilot of every ship (`Characters::Meet`, as a hail does), which writes their past from the simulation's
   own history, and tells their life story with the template teller.
4. Writes everything as one [Universe Interchange](../../Prose/docs/rfc/0007-universe-interchange.md) file,
   `Data/Prose/spacegod.universe.json`, and validates it against `Data/Prose/universe.schema.json` (Prose's schema,
   copied here) with `tools/prose_universe.py validate`.

Same seed and same build give the same file, byte for byte. The build hash (`git rev-parse HEAD`, `+dirty` when Core
has uncommitted edits), the seed, start date and days are recorded in the `spacegod-export-manifest` entity, with the
entity and edge counts, so it round-trips through Prose like any other entity.

**Why the file is gitignored.** It is derived: Core plus a seed reproduce it in about ten seconds, and with many agents
changing Core at once a committed copy would be stale after almost every commit (and is several megabytes). The
reviewable, committed side of the contract is the generator, the schema copy and the reviewed proposals in `Data/Lore`.

### Ids

Every id is a slug of the thing's name, prefixed by its type and qualified by its place, and contains only `a-z`, `0-9`
and single hyphens: exactly the form Prose's own slugifier leaves unchanged, so Prose can never rename one (the EVE
post-mortem's re-slugify bug). They never use pointers, list indices or counters, so they survive re-runs; two things
that slug alike get `-2`, `-3` in the order the seed finds them. Examples: `star-system-...` is `system-alpha-centauri`,
`planet-sol-earth`, `moon-sol-europa`, `settlement-sol-ceres-hub`, `ship-sol-sfs-resolute`, `pilot-tomas-brennan`,
`faction-sol-federation`, `void-station-12`, `module-railgun`, `good-helium-three`.

### What is in it

| Type (`type`) | What |
|---|---|
| `star-system` | every simulated system (position, holder, biome, discovery) |
| `star`, `planet`, `dwarf-planet`, `moon`, `asteroid`, `barycentre`, `station` | every body of every simulated system, with its orbit |
| `star` (`charted` tag) | every charted or undiscovered real star out to 100 ly not yet simulated |
| `settlement`, `pirate-base` | colonies, stations, mining outposts; pirate havens |
| `void-station` | every void station of the neighbourhood, with its plan: `asteroid-field`/`ice-field`, `planetoid`, `sight`, `nebula`, `pirate-base`, `megastructure` |
| `nebula`, `ice-field`, `graveyard`, `derelict`, `derelict-station` | the space features of every place (US-T130/T131) |
| `jump-gate`, `megastructure`, `gas-harvester` | gates, belt-works megastructures, and gas harvesters once US-T146 lands |
| `ship` | every live ship in a system, in a gate or crossing deep space |
| `character` | the named pilot of each ship, with traits, career, grudges and life story (Comms/Pilots) |
| `missing-ship` | derelict hulls and distress calls, with `lastKnown` coordinates (galactic sector + offset, ly from Sol, system km) |
| `wreck`, `rogue-planet`, `ice-body`, `relay`, `listening-post` | the deep-space objects between the stars |
| `faction` | every polity: states, companies, free traders, pirate clans |
| `technology`, `weapon`, `equipment` | research tracks and megastructure designs; the outfitting catalogue's modules and the weapon battle profiles |
| `ship-class`, `ship-design` | every hull class and its designs |
| `good`, `biome`, `manifest` | the traded goods, the ten biomes, the export's own record |

Edges (`relations[].kind`): `orbits`, `located-in`, `near`, `owns`, `holds`, `capital-of`, `occupied-from`, `crew-of`,
`member-of`, `grudge-against` (pilots' grudges and factions' grievances), `rival-of`, `hostile-to`, `at-war-with`,
`friendly-to`, `pact-<kind>`, `gate-link`, `bridges` (void station to the systems it joins), `has-biome`, `is-class`,
`is-design`, `design-of`, `made-from`, `carries`, `stocks`, `yields`, `researched`, `built-to`, `last-seen-in`,
`lost-to`, `on-route-from`, `bound-for`. No relation points at an id outside the file (a dangling one would make Prose
invent a stub entity); the exporter drops any it cannot resolve and says how many.

## 2. Pushing it and proving the round trip

```powershell
tools/prose-sync.ps1 -Export        # export (seed 1977, 30 days), push, export back, diff, leak check
tools/prose-sync.ps1                # push the file already exported
tools/prose-sync.ps1 -Idempotency   # ...and import twice, requiring the second to change nothing
```

`tools/prose-sync.ps1` talks to the Prose Hub at `http://127.0.0.1:5900` only, with the `X-Prose-Key` header (the key
the Hub keeps in `%LOCALAPPDATA%\MindAttic\Prose\Settings.json`):

1. `POST /api/universes/spacegod/import` with the file (idempotent upsert by id; the universe is created on first push).
2. Exports it back with Prose's own exporter: the `export_universe_file` MCP tool, run by the Hub (`/api/mcp-invoke`),
   which is what `prose --universe-export spacegod <path>` runs, to `Data/Prose/spacegod.prose-export.json`.
3. `tools/prose_universe.py diff`: every entity, relation, tag and detail must match, and the counts by type and by
   edge kind must be equal.
4. `tools/prose_universe.py leak` against `GET /api/universes/spacegod/snapshot?scope=all`: Prose's graph view (the service
   the EVE post-mortem caught serving other universes' rows) must hold no node that is not one of the file's entities.

If the Hub is down (it stops itself after a few idle minutes) the script starts Prose's deployed
`C:\Apps\MindAttic\Prose\Hub.exe`, as Prose's own `.prose/hooks/start-prose-hub.ps1` does.

The CLI does the same by hand: `prose --universe-import D:\Projects\MindAttic\SpaceGod\Data\Prose\spacegod.universe.json`,
`prose --universe-export spacegod <path>`.

## 3. Querying it from a session here

`.mcp.json` registers Prose's MCP server (`Prose.Mcp`, a Hub client, run from the sibling `../Prose` checkout's Release
build) with `PROSE_UNIVERSE=spacegod`. Use the RFC 0007 tools, which read the stored ids directly:
`get_universe_entity(slug: "spacegod", entitySlug: "pilot-tomas-brennan")`, `search_universe(slug: "spacegod", query: "Ceres")`,
`export_universe_file`. Prose's generic graph tools re-slugify names and are not reliable for interchange ids.

## 4. Prose to game: the outbox

Prose never edits game data. A Prose session proposes, by posting to its Outbox for consumer `spacegod`:

```json
POST /api/outbox/spacegod
{ "kind": "proposal.technology", "summary": "Graviton lance for the Centauri Accord",
  "data": "{\"id\":\"tech-graviton-lance\",\"type\":\"technology\",\"name\":\"Graviton lance\",\"summary\":\"...\",\"details\":{},\"relations\":[],\"tags\":[],\"status\":\"accepted\",\"rationale\":\"...\"}" }
```

`kind` is `proposal.<category>` (technology, character, story-template, mystery-hook, lore, ...); `data` is one
interchange-style entity plus `status` (`proposed`, `accepted`) and an optional `rationale`.

```powershell
tools/prose-outbox.ps1 -Peek   # see what is waiting, consuming nothing
tools/prose-outbox.ps1         # drain it
```

Draining journals every event (Prose's own import/export notices included) to the gitignored
`Data/Lore/.outbox/spacegod.jsonl`, then writes each **accepted** proposal to `Data/Lore/<category>/<id>.v<N>.json`
(`schema: spacegod-lore/1`, `status: pending-review`, the outbox event it came from, and the proposal itself), N one
past the newest version already there; an unchanged proposal is not written again. A proposal not yet accepted is only
journalled.

Nothing under `Data/Lore` is loaded by the simulation. It reaches the game only when someone reviews a file, commits it
and writes the Core loader that reads it deterministically (the US-T139 story pool, technology design). That keeps the
Bench digests and rewind deterministic: nothing Prose says changes a run until it is committed.

## 5. What the interchange format cannot say (proposed Prose changes)

Recorded on US-T171; none of these was changed in Prose.

1. **No delete on re-import.** `ImportAsync` upserts and never removes: an entity (or relation) that leaves the file stays
   in Prose forever, so a ship destroyed or a station renamed lingers as canon. Proposed: a `--prune` / `"mode": "replace"`
   import that invalidates the universe's entities and edges the file no longer holds (the round-trip diff here reports
   them as "only in theirs" until then).
2. **No provenance block.** The schema has nowhere for the build hash, seed and generator; they ride in a `manifest`
   entity. Proposed: an optional top-level `source` object (`generator`, `build`, `seed`, `generatedAt`) kept with the
   universe's `interchange.universe_source` setting.
3. **Untyped relations.** A relation is `{to, kind}` only: no weight, no sentiment, no dates. A rivalry's strength (an
   opinion of -70), a grudge's reason and date, a war's start live in `details` instead. Proposed: optional `weight`,
   `since`, `note` on a relation, stored on `Edge.Weight`/`Sentiment`.
4. **No coordinates or units.** Positions, orbits and last-known coordinates are free-form `details`. Proposed: an optional
   `position` with a declared frame and unit, so Prose (and other consumers) can place things without knowing SpaceGod.
5. **Import cost.** Every entity and every edge is its own query and `SaveChanges`, so a universe of ~10k entities and
   ~25k edges takes minutes. Proposed: batch the upsert per type and the edges per source.
6. **Edge provenance.** Edges carry `Source = "interchange"` for every consumer; with several game universes, a per-file
   source would let a replace-import touch only its own edges.
