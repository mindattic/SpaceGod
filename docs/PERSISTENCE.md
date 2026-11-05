# Persistence in the C++ Core (US-T20)

Unity.SpaceGod saves the game by serialising the whole simulation (`SaveData`: clock, galaxy, session) with
`ObjectGraph`, a reflection-driven object-graph writer (`Core/Persistence/ObjectGraph.cs`). C++ has no reflection,
so the port (`Cpp/Source/SpaceGodCore/Persistence/ObjectGraph.h/.cpp`) keeps ObjectGraph's **file format and
semantics exactly** and replaces reflection with an **explicit per-type field registry**: every serialisable class
declares its fields once, with `SG_SERIALIZE`. This document says what each ported class must declare so that the
integration ticket can add the registrations across Core.

Files: `Persistence/Serializable.h` (the one-line friend macro), `Persistence/ObjectGraph.h/.cpp` (registry, codecs,
writer, reader), `Persistence/SaveGame.h/.cpp` (save file, slots, DEFLATE), `Persistence/Profiles.h/.cpp`.
Worked examples: the registrations at the bottom of `Law/Law.cpp`, `Law/Outlaws.cpp` and `Persistence/SaveGame.cpp`.

## 1. The format (identical in both builds)

```
int32 Magic 0x4A42474F ("OGBJ")   int32 Version 1
types:   int32 n; per type: byte tag (0 plain: string FullName, string Assembly | 1 array: int32 rank, byte vector,
         int32 elementId | 2 generic: string DefinitionFullName, string Assembly, int32 argc, int32 argIds...)
         then per type: int32 fieldCount (-1: not a struct/class) and fieldCount x (string "DeclaringType:field", int32 typeId)
entries: int32 n; per entry: varint id, byte kind (0 Object, 1 String, 2 Array, 3 Collection, 4 Delegate, 5 External,
         6 EmptyArray) + kind data (type id; string; rank + lengths; comparer code; delegate calls; static key)
payload: int32 n; per object/array/collection, breadth first: varint id + contents (fields in type-table order;
         elements; varint count + items / key,value pairs)
int32 rootId
```

Strings are .NET `BinaryWriter` strings (7-bit length prefix, UTF-8). `varint` is ObjectGraph's zigzag 7-bit encoding.
Value types are inline (structs field by field, `Nullable` as bool + value, `DateTime` as `ToBinary()`, `TimeSpan`
as ticks); reference types (classes, strings, arrays, `List`/`Dictionary`/`HashSet`/`Queue`/`Stack`) are ids, so
shared references stay shared and cycles close. The C++ writer walks the graph in the same breadth-first order, Metas
types in the same order and numbers entries the same way, so with complete registrations **it produces the C# bytes**
(delegates aside, section 5). Writing compacts every dictionary and set it passes (cleared and refilled in its own
order), as the C# does, so a save never forks the timeline.

The save file (`SaveGame`) is `string "USS-SAVE"`, `int32 Format 1`, the header (slot, label, `SavedUtc.ToBinary()`,
`SimJd`, active system, piloted ship, credits), then the object graph compressed with **raw DEFLATE** (RFC 1951:
.NET's `DeflateStream`, no zlib/gzip header). See section 7.

## 2. What every serialisable class declares

**Header** (only if the registration must name private members or a private default constructor), inside the class:

```cpp
#include "Persistence/Serializable.h"
class Law final { ... SG_SERIALIZABLE(Law); };
```

**.cpp**, at namespace scope inside `namespace SpaceGod::Core`, after including `Persistence/ObjectGraph.h`:

```cpp
SG_SERIALIZE(Law, "Unity.SpaceGod.Core.Law",
    SG_F(world), SG_F(Player), SG_F(Captains), SG_F(Recent), SG_F(notices), SG_P(Hail), SG_F(Committed),
    SG_F(PatrolHailKm), SG_F(PatrolHoldKm), SG_F(ScanSeconds), SG_F(HailSeconds), SG_F(ScanCooldownDays),
    SG_F(TrespassCooldownDays), SG_F(lastJd), SG_F(scannedUntil), SG_F(trespassUntil))
```

| Macro | For |
|---|---|
| `SG_SERIALIZE(T, "Full.Name", fields...)` | a class held by `cs::Ref` (needs a default constructor; it may be private) |
| `SG_SERIALIZE_ABSTRACT(T, "Full.Name", fields...)` | an abstract base class (fields only) |
| `SG_INTERFACE(T, "Full.Name")` | an interface (`IFoo`) that some field is declared as |
| `SG_STRUCT(T, "Full.Name", fields...)` | a C# struct held by value (`DVec3`, `LaneHop`, `PilotState`, ...) |
| `SG_ENUM(E, "Full.Name")` / `SG_ENUM_AS(E, "Full.Name", Byte)` | every enum a field holds (underlying Int32 unless the C# says `: byte` etc.) |
| `SG_STATIC("Full.Type", "field", CppType, expr)` | a static field holding objects (section 4) |
| `Graph::DeclareDelegateType("Full.Name")` | a `delegate` type declared in Core that a field holds |

Field items (any order: the registry sorts each class's own fields by ordinal .NET name, as `FieldsOf` does):

| Item | C# declaration | .NET field name written |
|---|---|---|
| `SG_F(m)` | field (any visibility, `readonly` too), field-like `event` | `m` (an event's backing field has the event's name) |
| `SG_P(m)` | auto-property: `{ get; }`, `{ get; set; }`, `{ get; private/internal set; }`, `{ get; init; }`, positional record parameter | `<m>k__BackingField` |
| `SG_ARRAY(m)` / `SG_PARRAY(m)` | a field / auto-property of array type `T[]` (held as `cs::List<T>` or `cs::Ref<cs::List<T>>`) | `m` / `<m>k__BackingField` |
| `SG_STR(m)` | a string field C# never leaves null (`= ""`), so empty is written as `""` not null | `m` |
| `SG_FN("name", m)` | a field the C++ member spells differently | `name` |
| `SG_GHOST("name", CppType)` | a C# field with no C++ member (writes the default, ignores the saved value) | `name` |
| `SG_BASE(B)` | the C# base class (register it too); its fields come first | |
| `SG_IMPLEMENTS(I)` | an interface the class implements, so a field declared `I` can hold it | |
| `SG_LOADED(fn)` | `fn(T&)` runs on each loaded instance once the whole graph is read (rewire delegates, rebuild C++-only caches) | |

Rules that decide whether the two builds read each other's saves:

1. **List every instance field the C# type declares**, private ones included, and nothing else. Not fields: `const`,
   `static` (see section 4), properties with a body (`=> expr`, `get { }`: C++ methods), indexers, methods. The C#
   reader **refuses** a save whose type lacks a field this build has ("The save is from an older build: X has fields
   it lacks (...)"), and the C++ reader does the same; a saved field this build lacks is skipped.
2. **Names**: the .NET FullName is the C# namespace + type (`Unity.SpaceGod.Core.Law`; nested `Outer+Inner`; Shared's
   types `Unity.Shared.Core...`). Each field key is `DeclaringType.Name + ":" + fieldName`, where Name is the simple
   name of the class that declares the field (`Inner` for a nested type, ``Foo`1`` for a generic). The registry
   builds keys from the FullName passed in, so pass the exact C# name.
3. **Order**: base class fields first (whole chain), then each class's own fields in **ordinal** order of the .NET
   name (`<Hail>k__BackingField` sorts before `HailSeconds` because `<` is 0x3C). The registry does this; just get
   the names right and declare `SG_BASE`.
4. **Shapes** follow docs/MIGRATION.md; the codec is picked from the C++ member's type:

| C# field type | C++ member | Written as |
|---|---|---|
| `bool int long double float byte ...` | same | primitive (TypeCode must match: a C# `long` must be `long long`, `float` stays `float`) |
| `string` | `cs::String` | string id; **empty is null** (use `SG_STR` for never-null strings) |
| enum | `enum class E : int` | underlying value (`SG_ENUM`) |
| `T?` | `cs::Nullable<T>` | `System.Nullable`1` |
| struct | by value | inline fields (`SG_STRUCT`) |
| class / interface / abstract | `cs::Ref<T>` (also `T*`, `std::weak_ptr<T>`) | object id; the object's own registered type is written (polymorphic) |
| `List<T>` / `Queue<T>` / `Stack<T>` / `HashSet<T>` / `Dictionary<K,V>` | by value, or `cs::Ref<C>` where C# shares it | collection id (a by-value member is identified by its address) |
| `T[]` | `cs::List<T>` + `SG_ARRAY` | array id; empty is `Array.Empty` (null loads as empty) |
| `(A, B)` | `std::tuple<A,B>` / `std::pair` | `System.ValueTuple`2` inline |
| `DateTime`, `TimeSpan` | `cs::DateTime`, `cs::TimeSpan` | int64 |
| `System.Random` | `cs::Random` (by value) or `cs::Ref<cs::Random>` | .NET 10's object layout (section 6) |
| `Action<..>` / `Func<..>` / `event` | `cs::Action`/`cs::Func`/`cs::Event`/`cs::Delegate` | null (section 5) |

   A `Dictionary`/`HashSet` built with `StringComparer.OrdinalIgnoreCase` must be
   `cs::Dictionary<cs::String, V, cs::OrdinalIgnoreCaseComparer>` (comparer code 2). The `NoCase` key wrapper in
   `Systems/CsGaps.h` is not yet a codec: give it `Graph::Codec<NoCase>` (type `System.String`, the comparer code 2
   on its containers) or switch those maps to `cs::OrdinalIgnoreCaseComparer`.
5. **Construction**: C# loads with `GetUninitializedObject` (no constructor, no initialisers) and then assigns every
   saved field. C++ makes each object with `new T()` and assigns every field. So every registered class needs a
   default constructor (private is fine: `SG_SERIALIZABLE` makes the registration a friend) that **has no side
   effects** (no RNG draws, no registering with a world, no `shared_from_this`). `readonly` C# fields must not be
   `const` in C++ (the loader assigns them; e.g. `Battle`'s `const int Body`, `const cs::String Name`,
   `const DVec3 Anchor` must lose `const`, as must `Profile::store`).
6. **C++-only members** (subscription tokens, caches, the World a raw pointer stands for) are not listed: they keep
   their default-constructed values; rebuild them in `SG_LOADED`.

## 3. Reading saves from the other build

- **C# save -> C++**: works once every type in the save is registered. Compiler-generated closure classes
  (`<>c__DisplayClass..`, `<>c`) are parsed as opaque objects and dropped; delegate entries are parsed and dropped
  (section 5). An unregistered enum, struct or delegate type throws "The save holds a type this build does not have".
- **C++ save -> C#**: the C# reader reads it (same format, same names), but every delegate field is null (section 5).
- To find what is missing: write a fresh `World` with the C# build (`ObjectGraph.ToBytes`), read it with the C++ build:
  the first missing registration throws with its name; a class missing fields throws listing them. Repeat. Diff the
  static keys with `Graph::StaticKeys()` against a C# dump of `Statics` (section 4).

## 4. Statics

The C# writer writes any object reachable from Core's static fields (ship class specs, weapon and module tables,
catalogues) as an **External** entry named by its path, e.g. `Unity.SpaceGod.Core.ShipClassSpec::<Specs>k__BackingField[3]`,
so a loaded hull shares its design. The path comes from a walk over **every static field of every Core type**,
types in ordinal FullName order, fields in ordinal name order, recursing through objects' fields (`.field`), arrays
and lists (`[i]`), dictionaries (`{ki}` keys, `{i}` values), up to depth 24; the first path that reaches an object
names it.

In C++ each static field that holds objects is declared with `SG_STATIC`:

```cpp
SG_STATIC("Unity.SpaceGod.Core.ShipClassSpec", "<Specs>k__BackingField", const cs::List<cs::Ref<ShipClassSpec>>, ShipClassSpec::Specs())
```

The C++ walk sorts the declared roots the same way and walks them with the same rules, so keys match **provided every
static that could reach a shared object first is declared** (a static of an earlier type that also holds the object
would otherwise name it in C# but not in C++). Static string and primitive fields need nothing. The reader throws if a
save names a static this build does not have.

## 5. Delegates

C# writes each delegate field as its invocation list (declaring type, method name, parameter types, target object),
keeping only methods in Core, and rebuilds the delegates on load. A `std::function` cannot be inspected, so the C++
port **writes every delegate as null and ignores saved delegates**. What the simulation wires with delegates (World's
event subscriptions such as `Law.Committed += Outlaws.OnCrime`, `Economy.DockAllowed`, `War.BattleEnded`, Fleet
hooks) must be wired again after a load by `SG_LOADED` hooks: the natural place is one `SG_LOADED` on `World` that
re-runs the wiring part of its constructor (split it out of `Construct()`), and on `Galaxy` for cross-system wiring.

Consequences: a C++ save loaded in C# has null delegates (the C# build has no rewiring step), and byte-identical output
holds only for graphs without delegates (C# also allocates ids for delegate targets). Closing that gap would need a
per-field "delegate description" registered beside the wiring (declaring type, method name, target), which the
format supports; not done here.

## 6. .NET runtime types

- `System.Random`: written in .NET 5+/10 layout (`Random._impl` -> `Random+Net5CompatSeedImpl._prng` ->
  `Random+CompatPrng {_inext, _inextp, _seedArray}`); read in that layout or Mono's (Unity: `Random {SeedArray, inext,
  inextp}`), so a Unity save loads in C++ and the C++ save loads on the CoreCLR C# track. Only seeded generators.
  `cs::Random` has no state accessor (a Cs.h gap), so ObjectGraph.cpp mirrors its layout under `static_assert`s.
- Not supported: `decimal` fields (parsed, not bound), multi-dimensional arrays, boxed values in `object`-typed
  fields, generic classes declared in Core, `KeyValuePair` fields (parsed only).
- Assembly names written: `Unity.SpaceGod.Core` for Core (Unity's asmdef calls it `UnitySpace.Core`; both readers fall
  back to any loaded assembly, so the name does not matter), `System.Private.CoreLib`, `System.Collections` for
  `Queue`1`/`Stack`1`.

## 7. Compression (DEFLATE)

`SaveGame` needs raw DEFLATE (`deflateInit2`/`inflateInit2` with windowBits -15, level 1 = `CompressionLevel.Fastest`).
Nothing is vendored: both builds use the engine's own zlib 1.3.

- **Unreal**: `SpaceGodCore.Build.cs` has `AddEngineThirdPartyPrivateStaticDependencies(Target, "zlib")` and
  `PrivateDefinitions.Add("SPACEGOD_WITH_ZLIB=1")`.
- **Standalone** (`tools/build-core.ps1`, Release): when UE 5.8 is installed (`tools/engine.ps1`), it compiles with
  `SPACEGOD_WITH_ZLIB` against `Engine/Source/ThirdParty/zlib/1.3/include` and merges the engine's prebuilt `/MD`
  `zlib/1.3/lib/Win64/Release/zlibstatic.lib` into `SpaceGodCore.lib`, so whatever links the lib (CsParity, Bench.Cpp)
  has it. Debug builds (`/MDd`) and machines without UE go without. `SPACEGOD_WITH_MINIZ` (single-file miniz, MIT) remains
  the way to a zlib-free standalone build if ever needed.

Without either, `SaveGame::Deflate/Inflate` throw `NotSupportedException` (ObjectGraph itself needs nothing).

## 8. Registration mechanics

Each `SG_*` macro defines a namespace-scope static whose initialiser registers the type, so registration happens when
the object file is linked in. Linking SpaceGodCore as a **static library** can drop object files nothing references:
link with `/WHOLEARCHIVE` (or `--whole-archive`), or reference the registrations from one place. UE modules and the
standalone build's direct object lists are unaffected. `Graph::Finalize()` (called by every write and read) resolves
base chains and orders fields once all registrations have run.

## 9. The registrations as built (US-T36)

All in `Persistence/Registrations/` (new files, so the folders' own `.cpp` files stay as ported):

- `<Folder>.cpp` (Combat, Economy, Embodiment, Events, Information, Infrastructure, Root, Sensors, Systems): every type a
  save reaches from `SaveData` (through every field's type, generic arguments, array elements and every implementer of
  an interface a field is declared as): 262 types (66 enums, 23 structs, 1 interface, 172 classes), each listing every C#
  instance field by .NET name. **Generated** from `Unity.SpaceGod.Core.dll`'s metadata (reflection over the C# build, the
  same `GetFields(Instance | Public | NonPublic | DeclaredOnly)` FieldsOf uses); Law, Outlaws and SaveGame keep their
  hand-written registrations in their own `.cpp`. Checked by a probe that resolves every registered field's key and
  declared .NET type in the C++ build and diffs them against the C# metadata: all 2,285 fields match.
- `Registrations.h`: `SG_TUPLE` for the C++ structs that stand for C# value tuples (`Appraisal::Reason`,
  `InterstellarTraffic::AtGateEntry`/`ExpeditionWanted`, `InterstellarWars::TakenGate`, `Statecraft::FactionPair`/`WarBegun`,
  `War::LanePass`: they save as `System.ValueTuple`N`), and helpers for fields declared as a type the C++ member does not
  name (`b.Typed(...)`: `IReadOnlyList<T>` held as a `cs::List`, delegates returning `IEnumerable<T>`).
- `Statics.cpp`: `SG_STATIC_OF` for the 96 static fields of Core types whose type holds a class a save reaches (ship
  class, combat, weapon and subsystem specs, facility layouts, deals, pact kinds, research tracks, megastructures, build
  plans, recipes, ...). Private tables are read through `Graph::Access<0>`, which `SG_SERIALIZABLE` befriends.
- `Wiring.cpp`: Galaxy's `SG_LOADED` hook, `RewireGalaxy`: every world's delegates again (`World::RewireAfterLoad`
  repeats `Construct`/`Wire`/`WireMining`/`WireColonisation`'s delegate statements verbatim; MarketShocks', War's
  escape-pod hooks, trade insurance and `War::Hook` per running battle through `Graph::Access<1>`), then the galaxy's
  (InterstellarTraffic's `Register` per system and its cruise hooks, the polities' transit hook, InterstellarColonisation).
  For every event with several subscribers the handler order is construction's. **A change to World's wiring must be
  repeated there.** Delegates held by static tables (Deal, PactKind, ResearchTrack, MegastructureSpec) need no rewiring:
  the save names those tables, so a load binds to the live ones.

New codecs for shapes the port uses: `std::array<T, N>` as a C# array (`SG_ARRAY`), `SG_ARRAYVALUES` for a
`Dictionary<K, V[]>` held with `cs::List` values, tuples of 8+ items (`ValueTuple`8` + `Rest`).

Limits that remain (US-T36): a C++ save loaded in C# had null delegates, the static walk declared only part of C#'s, and a
World saved without its Galaxy is not rewired (saves are SaveData). The first two are closed in section 11.

## 10. Integration checklist

1. Add `SG_SERIALIZABLE(T)` to every Core class with private fields; `SG_SERIALIZE`/`SG_STRUCT`/`SG_ENUM`/`SG_INTERFACE`
   for every type reachable from `SaveData` (World, Galaxy, Fleet, Ship, Faction, Economy, War, Battle, ..., and every
   enum and struct they hold), listing every C# instance field.
2. Give each a side-effect-free default constructor; drop `const` from fields C# declares `readonly`.
3. `SG_STATIC` for every static object table; diff `Graph::StaticKeys()` against the C# build's.
4. Rewire delegates in `SG_LOADED` (World, Galaxy, anything that subscribes).
5. Turn on zlib (UE) / miniz (standalone).
6. Seams found while porting Law/Outlaws: `EventDirector::Reputation` must be a `cs::Ref<cs::Dictionary<cs::Ref<Faction>, double>>`
   (World passes it to `Law`, which shares it as the player's standing); `Law.h` forward-declares
   `enum class ContactLevel : int` and `Outlaws.cpp`/`Law.cpp` include `Sensors/Sensors.h` (`Sensors::Resolve`,
   `Sensors::FindWitness(helio, jd, signature, exclude)`, `PilotBurning()`, `PilotSignature()`, `SignatureOf`,
   `Signatures::Battle/Of/Transponder`).

## 11. Cross-track saves (US-T41)

Proved by scratch probes over Bench's Simulation and digest (sol-idle at warp 4, saved at the first tick past 600 with a
battle running, then 1,200 ticks; and warp 8): **C# save -> C++ load -> step** and **C++ save -> C# load -> step** give the
same digest as the unsaved run in either Core and as the same save loaded and stepped in its own Core. A C# graph diff of
a C# save against the C++ re-save of it (loaded in C#) finds no field differing; with rewiring, every delegate slot is
filled as in the C# load but those of battles already over (`War.Ended`: never stepped again, not rewired in either track).

**Statics at parity.** C#'s walk (section 4) goes through *every* static of every Core type, and the first path that
reaches an object names it, so a table walked early can name a saved object first: the orbital station's and satellites'
designs are `HullFrames::All[23].Design` in C#, because `FieldInfo.GetValue` runs HullFrames' static constructor and fills
`All`; the C++ walk read `All` empty and named them `HullFrames::OrbitalStation`, so a C# save with stations did not load.
Now `Persistence/Registrations/StaticTables.cpp` (generated from the C# walk, like Statics.cpp) declares the other 94
statics C# names anything through, with walk registrations for the 24 classes it names there (only the fields it goes on
through: they are never saved), private tables reached by explicit instantiation (no ported header changes); VoidStations'
and RenderLod's file-local tables and SpinalMounts' SpinalPart are declared in their own .cpp; HullFrames' static
constructor runs before its tables are walked. Every name of C#'s walk is C++'s, in walk order (checked fresh, after
`Galaxy.CreateDefault` and after 3,600 ticks), except on purpose: settings (their names follow the order the process
registered them in; no save holds one), compiler closure caches (`+<>c::<>9`, bound to nothing), ObjectGraph's own tables
and locks, and LunarTheory's coefficient tables (constexpr in C++). C++ has ~20 names more: by-value copies of arrays C#
shares (`Array.Empty` bills, shared category and control-group arrays), which no save holds.

**C# null arrays.** The port holds a C# null array or list as the empty one. Fields C# leaves null until set (Ship.Plunder,
Route, Ports, Path, PathCumulative, Journey; Voyage.HomeRoute; MiningOperation.TugReadyJd; World.RaiderNames) are now
written as null when empty (`SG_NULLARRAY` / `SG_NULLLIST` in their registrations): written as `Array.Empty` they loaded
in C# as empty arrays where the C# tests for null (`War.UnloadPlunder` indexed an empty Plunder). Faction.Prefix is
written as a never-null string, Loadout.StockFacilities as the array C# holds there.

**Delegates on the C# track.** `CSharp/Core/SaveCompat/SaveWiring.cs` (`RewireIfNeeded(SaveData)`, called by the C# track's
`ApplyLoaded`; compiles into any net8+ project, so Bench/CSharp can include it) wires a save that carries no delegates as
construction did, mirroring `Registrations/Wiring.cpp` statement for statement: method groups bound to the same Core
methods (so a later C# save writes them as the original's would), the original's own pure wiring methods called as they
are (War.Hook, World.WireColonyHulls, InterstellarTraffic.Register, InterstellarColonisation.Wire), the rest the
original lambdas' bodies. It replaces every slot it wires (handlers from outside Core are kept, after Core's), so a C#
save of a rewired game (whose glue lambdas ObjectGraph does not save) is rewired again on load without doubling. **A
change to World's wiring must be repeated in Wiring.cpp and SaveWiring.cs.** C++ writes `System.Random` in .NET 10's layout,
so a C++ save loads on the C# track (.NET 10), not on Bench.CSharp's .NET 8.

**The original's weak-table bug (genuine; fixed in both tracks).** Two of the statics C#'s walk enumerates are
`ConditionalWeakTable` caches keyed by live simulation objects: `FlightDecks.decks` (carriers' battle units) and
`ArmourAngling.Tables` (combat specs). A ConditionalWeakTable is a generic IEnumerable, so the walk names everything they
reach: after `Galaxy.CreateDefault` ~400 live objects (ships, factions, worlds: `FlightDecks::decks[0].key.Faction`),
and combat specs under `ArmourAngling::Tables` instead of their own table. A save writes those as bare names, and any
process whose caches differ (a fresh one; one whose GC dropped entries; another co-op peer) cannot load it. Probe: the
unchanged original, saving 1,441 ticks into sol-idle, names 97 objects through `FlightDecks::decks` and 137 through
`ArmourAngling::Tables`; neither a fresh C# process nor C++ loads it ("a static object this build does not have:
...FlightDecks::decks[0].value[0].Aboard[0].Fighter.Lead.Target.Faction"). The fix: the walk does not look into those
caches. C++ never declared them. The C# track, whose Core stays unchanged, builds ObjectGraph's private table with the
original's own Walk minus those two (`CSharp/Core/SaveCompat/SaveStatics.cs`, `EnsureFixed`) at start (`InitSaves`, and the
co-op peers' start, US-T39's finding of the same cause), before any galaxy exists; the table then differs from one built
later only in settings and the lazily filled star lists, which no save holds, so every process (single-player across runs,
co-op peers, C++) names the same objects. Saves the unfixed original made once those caches held anything stay unloadable.

**Flight decks across a load (US-T111).** Not walking `FlightDecks.decks` leaves one seam: a craft's `DeckVisit.Deck` does
save the `FlightDeck` it visits (with its `Final`, `Aboard`, `NextLaunch`), but after a load `FlightDecks::Of(carrier)`
found no entry and built new decks, so the visit and the carrier ran two copies of one deck and a restored carrier fight
diverged the first time a craft used it. DeckVisit's `SG_LOADED` hook now calls `FlightDecks::Adopt`, which builds the
carrier's entry as `Of` would and puts the loaded deck in the slot of its own hangar (`Bay`). No format change: existing
saves load and are repaired the same way. Decks no visit holds are rebuilt fresh; the only state they could lose is
`NextLaunch`, which no craft can read before a fresh landing's turnaround (at least 5 s) outlasts it (3 s).

