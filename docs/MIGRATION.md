# Unity.SpaceGod → Unreal.SpaceGod migration

Goal (US-T4): a **perfect replica of Unity.SpaceGod in Unreal Engine 5.8, built twice** — once in native C++,
once in C# — both **co-op**, then raced against each other to see whether either has a real advantage.

| Track | Folder | Packaged exe | Core simulation | Engine side |
|---|---|---|---|---|
| C++ | `Cpp/` | `Unreal.SpaceGod.Cpp.exe` | `Cpp/Source/SpaceGodCore/`: Unity.SpaceGod's Core translated file-for-file to C++20 against `Cs/Cs.h` | `Cpp/Source/UnrealSpaceGod/`: UE C++ (rendering, HUD, input, camera, co-op) |
| C# | `CSharp/` | `Unreal.SpaceGod.CSharp.exe` | Unity.SpaceGod's own `Assets/UnitySpace/Core/**/*.cs`, compiled unchanged (as its `Tests/UnitySpace.Core` csproj does) | UnrealSharp (C# in UE5): the Runtime layer rewritten for Unreal in C# |
| Bench | `Bench/` | `Bench.Cpp.exe`, `Bench.CSharp.exe` | Headless runners over the two Cores | Same scenarios, same seeds; compare state hashes and timings |

The source of truth is **Unity.SpaceGod** (`D:\Projects\MindAttic\Unity.SpaceGod`, cloned next to this repo),
plus its dependency **Unity.Shared** (`Runtime/Core`: JSON and player profiles). Faithful-port rule: reproduce
the original's behaviour; fix only genuine bugs (note each one on the ticket); never change or "improve"
gameplay. Co-op is the one addition, and it goes through the original's own Session layer (`Core/Session`:
`ISession`, `Commands`, `CommandCodec`, `SeatAuthority`), with the host running the authoritative simulation.

## The C++ Core: translation rules

The C++ Core must run **the same simulation bit for bit** as the C# Core, so the Bench can compare their
state after N ticks. That is why the translation is mechanical and why `Cs/Cs.h` reproduces .NET exactly.

**Layout.** `Assets/UnitySpace/Core/<Dir>/<Name>.cs` → `Cpp/Source/SpaceGodCore/<Dir>/<Name>.h` + `.cpp`.
Partial classes (`World.cs`, `World.Mining.cs`, …) share one header (`World.h`) and keep one `.cpp` per C# file.
Includes are rooted at the module: `#include "Combat/Battle.h"`, `#include "Cs/Cs.h"`.
Everything lives in `namespace SpaceGod::Core` (`Unity.Shared.Core` → `namespace SpaceGod::Shared`).
No Unreal headers anywhere in SpaceGodCore except `SpaceGodCoreModule.cpp`. No `using namespace` in headers.

**Names are kept exactly**, so a file ported by one agent compiles against a file ported by another without
either seeing the other's work. Decide every name *from the C# declaration alone*:

| C# declaration | C++ |
|---|---|
| class / sealed class | `class Name` (`final` if sealed); held as `cs::Ref<Name>` (shared_ptr). `new Foo(a)` → `cs::New<Foo>(a)` |
| abstract class / interface `IFoo` | abstract class with pure virtuals, same name (`IFoo`); implementers inherit publicly |
| struct / readonly struct | `struct Name` held by value; every field default-initialised (`double X = 0;`) as C# zeroes it |
| record (class) | `class` with value equality: `bool operator==` + `size_t HashCode() const`; held as `cs::Ref` |
| record struct | `struct` with `operator==` + `HashCode()` |
| enum | `enum class Name : int` with the same members and values, plus `const char* ToString(Name)` returning the member name (C#'s enum ToString). `[Flags]`: also `CS_FLAGS(Name)` (bitwise operators) |
| static class | `namespace`-scoped class with only `static` members (keep it a class: callers write `Foo::Bar()`) |
| field (any visibility) | field, same name |
| auto-property `{ get; }` `{ get; set; }` `{ get; private set; }` `{ get; init; }` | **field**, same name |
| property with a body (`=> expr`, `get { … }`) | **method** `Name()` (const); a bodied setter → `SetName(value)` |
| expression-bodied / normal method | method, same name |
| indexer `this[int i]` | `operator[](int)` |
| `event Action<A, B> Name` | `cs::Event<A, B> Name;` — `Name?.Invoke(a, b)` → `Name.Invoke(a, b)`; `Name += h` → `Name += h` |
| `const` / `static readonly` of a simple type | `static constexpr` / `static inline const` |
| `static readonly` of a class/collection | a static function returning a function-local static (`static const List<X>& Xs()`), called as `Xs()` — C# initialises statics lazily per type; this avoids C++'s cross-file init order |
| nested type `Outer.Inner` | nested `Outer::Inner` |
| `out T x` / `ref T x` / `in T x` | `T& x` / `T& x` / `const T& x` |
| `T?` (value type) | `cs::Nullable<T>`; `x ?? y` → `x.value_or(y)`; `x.HasValue` → `x.has_value()`; `x.Value` → `*x` |
| class reference null | `nullptr`; `a?.B` → `(a ? a->B : default)`; `a ??= b` → `if (!a) a = b;` |
| `is T t` / `as T` | `if (auto t = cs::As<T>(x))` |
| `List<T>`, `T[]`, `IReadOnlyList<T>`, `IList<T>` | `cs::List<T>` (by value for a member; `const cs::List<T>&` for an `IReadOnlyList` parameter/return of a member) — note a C# list is a reference: where C# code **shares** one list between objects, hold `cs::Ref<cs::List<T>>` |
| `Dictionary<K,V>`, `IReadOnlyDictionary` | `cs::Dictionary<K,V>` (enumerates in .NET order — required for parity) |
| `HashSet<T>`, `Queue<T>`, `Stack<T>` | `cs::HashSet<T>`, `cs::Queue<T>`, `cs::Stack<T>` |
| `IEnumerable<T>` + `yield return` | return a `cs::List<T>` built eagerly — **unless** the C# relies on laziness (stops early with `First`/`Any`, infinite, or side effects interleaved with the caller); then write a callback form `void ForEachX(Func<T,bool> visit)` and note it |
| `Func<…>` / `Action<…>` | `cs::Func<R, A…>` / `cs::Action<A…>` (std::function) |
| `new Random(seed)` | `cs::Random(seed)` (bit-exact .NET sequence) |
| `Math.X` / `double.X` / `int.MaxValue` | `cs::Math::X` / `cs::Double::X` / `cs::Int::MaxValue` — **always** these, never `std::` directly: Math.Round is banker's rounding and Math.Max propagates NaN |
| `(int)someDouble` | `cs::ToInt(someDouble)` (C++'s cast is undefined for NaN/out of range) |
| `unchecked(a * b)` on ints | `cs::WrapMul(a, b)` etc. Unsigned `uint`/`ulong` arithmetic wraps natively: use `uint32_t`/`uint64_t` |
| `x.ToString()` / `"a" + x` / `$"{x}"` | `cs::Str(x)` / `cs::Cat("a", x)` |
| `x.ToString("0.##", Invariant)` / `$"{x:0.0}"` | `cs::Fmt(x, "0.##")` |
| string methods | `cs::Strings::Join/Substring/StartsWith/...`; `s.Length` → `static_cast<int>(s.size())`; `==` is ordinal |
| `throw new ArgumentOutOfRangeException(...)` | `throw cs::ArgumentOutOfRangeException(cs::Cat(...))` |
| `list.Sort(cmp)` | `list.Sort(cmp)` (port of .NET's unstable introsort); LINQ `OrderBy` → `cs::OrderBy` (stable) |
| LINQ elsewhere (`Where/Select/Any/All/Max/Sum/Count(pred)`) | an explicit loop with the same semantics and the same order |
| `foreach` over a collection that the body mutates | C# would throw; the original doesn't do it — keep the same iteration |
| `lock`, `Interlocked`, `Thread.Yield`, `Task` | `std::mutex`, `std::atomic`, `std::this_thread::yield`, `std::thread`/`std::async` |
| `System.IO` | `std::filesystem` + `std::fstream`; `GZipStream` (one use) → see Persistence ticket |
| reflection (`ObjectGraph`) | hand-written: see Persistence ticket |

**Comments.** Keep every XML doc comment and inline comment, as `///` or `//`, so the port stays reviewable
side by side with the original.

**Headers.** Forward-declare classes you hold by `cs::Ref`; include a header only for by-value members, base
classes and inline code. Constructors keep C#'s parameters and default arguments. Field initialisers move to
default member initialisers in the same order (C# runs them before the constructor body, in textual order).

**Conventions settled during wave 1** (apply them too):
- C# tuples `(A a, B b)` → `std::tuple<A, B>` (or a small named struct when the C# names the elements and callers use the names); `T?` of a tuple → `cs::Nullable<std::tuple<…>>`.
- A `string` that C# compares with `null` → `cs::String`, with empty standing for null (note it where null and "" mean different things).
- A partial class spread over several C# files (e.g. `Battle` extended by `BoardingShuttles.cs`): the file that adds members writes their bodies in its own `.cpp` and lists the exact declarations in a comment at the top of its header; the integration ticket adds them to the main class header.
- A class that passes `this` as a C# reference (`Foo(this)`) → inherit `std::enable_shared_from_this<T>` and pass `shared_from_this()`.
- `[SettingsSource]` and other attribute-reflection discovery → explicit registration; list what the integration ticket must call.

**Evaluation order (parity-critical).** C# evaluates operands and call arguments strictly left to right;
C++ does not (argument order is unspecified, `a() + b()` may run `b` first). Wherever two or more operands or
arguments of one expression have side effects — above all `rng.Next…()` draws, but also counters, queue
pops, list adds — hoist them into locals in C#'s left-to-right order first:
`double x = rng.NextDouble(); double y = rng.NextDouble(); DVec3(x, y, …)`.

**Don't compile in the porting waves** (memory is shared by many agents): write the files faithfully, then
the integration ticket compiles all of SpaceGodCore and fixes the seams. If a referenced C# member's shape is
ambiguous, read its declaration in Unity.SpaceGod and apply the table above; if still unclear, note it on
your ticket.

## C# track

**Decision (US-T6, 2026-11-03): UnrealSharp**, pinned as a git submodule at `CSharp/Plugins/UnrealSharp`
(upstream `main` @ `115e6753`, 2026-10-26). Reasons:

- It is the maintained way to write a UE game in C# today: `main` declares UE 5.6–5.8, 5.8-specific fixes have
  landed (issue #705, PR #794), it was pushed a week ago, and it is MIT. A C# class is a real `UCLASS`
  (`[UClass] public partial class ASpaceGameMode : AGameModeBase`), so the engine side is written in C# against the
  ordinary Unreal API, not through a scripting sandbox.
- **CoreCLR, .NET 10**: Unity.SpaceGod's Core (netstandard2.1, C# 9) is referenced as a plain library and runs
  *unchanged*, which is the point of this track. Hot reload: a watcher rebuilds `Script/` on save and swaps the
  collectible AssemblyLoadContext (editor only; off in commandlets and `-unattended`).
- **Packaging works** (Win64/Mac, Development and Shipping): the managed code is published self-contained, the
  .NET runtime ships next to the exe. Today that is a second step after UE's own package
  (`RunUAT PackageProject -ScriptDir=<plugin>\Build\Scripts ...`; open PR #795 folds it into cook/stage). Native
  AOT is not supported.
- **Replication from C#**: `[UProperty(PropertyFlags.Replicated)]`, `ReplicatedUsing` OnReps, lifetime conditions
  and `RunOnServer` / `RunOnClient` / `Multicast` RPCs, so co-op can stay in C#.
- Alternatives looked at: UnrealCSharp (crazytuzi, active, 5.0–5.8, also .NET 10; a Mono/CoreCLR binding layer
  with a mostly Chinese doc set, smaller English community) — the fallback if UnrealSharp stalls. UnrealCLR (last
  push 2023), USharp (archived 2020) and MonoUE are dead. A self-written host (hostfxr + a C ABI bridge) would
  work for the Core alone but means hand-writing every engine binding UnrealSharp generates.

Submodule rather than vendoring: the plugin is ~1,700 files we never edit, a pin records the exact upstream
commit for the benchmark write-up, and an update is a one-line diff. Its build outputs (`Binaries/`,
`Intermediate/`, `Generated/`, `*.props`) are ignored by its own `.gitignore`, so the submodule stays clean.

**Layout.**

| Path | What |
|---|---|
| `CSharp/UnrealSpaceGodCSharp.uproject` | UE 5.8 project, plugin UnrealSharp. Config mirrors `Cpp/` (`/Engine/Maps/Entry`, game mode by class path) |
| `CSharp/Source/UnrealSpaceGodCSharp/` | the empty C++ primary game module every UE game target needs; no game code |
| `CSharp/Core/SpaceGod.Core/` | csproj compiling `../Unity.SpaceGod/Assets/UnitySpace/Core/**/*.cs` **in place** (assembly `Unity.SpaceGod.Core`, netstandard2.1, C# 9), as Unity.SpaceGod's `Tests/UnitySpace.Core` does. `UnitySpaceGodDir` overrides the checkout |
| `CSharp/Core/Unity.Shared.Core/` | the same for `../Unity.Shared/unity/com.mindattic.unity.shared/Runtime/Core` (`UnitySharedDir`) |
| `CSharp/Script/ManagedUnrealSpaceGodCSharp/` | the UnrealSharp user project (`Managed<ProjectName>`): the engine side in C#. Class path `/Script/ManagedUnrealSpaceGodCSharp.<Name>` |

The Core projects live outside `Script/` on purpose: UnrealSharp treats every csproj under `Script/` as a user
module (glue, load order, hot reload); the Core is a plain dependency that goes into the publish output.

**Build** (`tools/build.ps1 -Track CSharp`): fetches the submodule if missing; UBT builds the plugin's C++ and its
UBT plugin generates the C# glue and builds UnrealSharp's managed bindings; then `tools/build-managed.ps1` runs the
plugin's own `BuildUserSolution` UAT command (what the editor runs on launch) to publish `Script/` + Core into
`CSharp/Binaries/Managed/net10.0` with its `UserCode.LoadOrder.json`. Two workarounds, both in our scripts, none in
the submodule:

1. On an *installed* (launcher) engine UnrealSharp's `UnrealSharp.Automation.props` cannot see `UE_5_8_OR_LATER`
   (it reads a rules csproj only source builds have), keeps `net8.0`, and UBT skips its glue plugin
   ("Skipping net8.0 project"; upstream PR #717 was closed unmerged). `build.ps1` writes the plugin's optional,
   gitignored `*.csproj.props` hooks defining the symbol.
2. UnrealSharp needs the .NET SDK **10.0.3xx or later** (Roslyn 5.6, since 2026-09-16) and takes the
   highest-*named* `sdk\10.*` folder without checking it is complete. When the machine's dotnet fails either check
   (this workstation: newest complete SDK 10.0.202, plus two empty `10.0.300/400-preview` leftovers),
   `tools/dotnet-sdk.ps1` installs SDK 10.0.403 privately into `CSharp/Intermediate/DotNet` (Microsoft's
   `dotnet-install.ps1`, no admin) and puts it on PATH for the build. Installing SDK ≥ 10.0.300 machine-wide (and
   deleting the empty preview folders) makes this a no-op.

## Order of work

1. **Foundation** — `Cs/` runtime implementation (`Cs.cpp`, `CsSort.inl`), standalone build of SpaceGodCore
   (`tools/build-core.ps1`), and the `Unity.Shared.Core` port (`Shared/`).
2. **Core porting waves** (parallel, by folder) — see the tickets.
3. **Integration** — compile all of SpaceGodCore standalone and inside UE, fix seams, then port Unity.SpaceGod's
   Core tests that pin numbers to a parity check in the Bench (tests run only when asked).
4. **C# track** — UnrealSharp on 5.8, the `CSharp/` project, Core linked in unchanged.
5. **Engine side, both tracks** — Runtime (Views, HUD/UI, Rendering + the 26 shaders as UE materials, Camera,
   Input), wired to each track's Core.
6. **Co-op, both tracks** — host-authoritative simulation through the Session layer.
7. **Bench & race** — identical scenarios on both, parity hashes, sim-tick timings, frame times, memory,
   load times, build times and binary size; write up the result.
