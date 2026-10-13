# Translating the game's code: tools/translate

The port's main road (the user's decision, 2026-10-13): instead of porting the game's
~160,000 counted lines of C# class by class and proving each with its own recording, a
tool translates the game's own code into C++ mechanically. The game logic is then right by
construction (it is the same code); what stays hand-written is the layer it calls: the .NET
runtime pieces (`recomp/src/cs`) and Unity (`recomp/src/unity`, through the facade
`recomp/src/bind`), plus the hand-ported services the Unity layer already has (PolyNav,
MasterAudio, ...). Oracle recordings then prove that layer and spot-check the whole.

Status (see the end for numbers): the pipeline runs on the whole game in 15 s; the cave
generation, translated, matches the game on every recorded floor (`cave_oracle_translated`:
3000 mirror layouts, 52 real-play layouts, 3000 floor-info resets, ALL MATCH); the BabySlime
and the GolemSoldier, their behaviours, Enemy, EnemyBehaviour, EnemyStats and PolyNav
translated and running on the Unity facade, match their recordings frame by frame with every
row the hand port compares (`slime_translated`, `enemy_golem_soldier_translated`: ALL MATCH,
row tables identical to `slime` and `enemy_golem_soldier`). 67.9% of the counted lines
translate today.

## The pipeline

```
Moonlighter_Data/Managed/Assembly-CSharp.dll           (the user's copy; never committed)
  -> ICSharpCode.Decompiler, in-process (tools/translate/Decompile.cs)
       lowered settings; cached in recomp/gen/decompiled/<dll hash>-<settings>/
  -> Roslyn: CSharpCompilation against the game's other Managed/*.dll (GameCompilation.cs)
       every symbol, type, overload and conversion resolved: the semantic model
  -> the translator, on the bound tree (IOperation) (Translator.cs, Body.cs, Names.cs)
       units (tools/translate/units.txt) and their closure
  -> recomp/gen/cpp/ (gitignored): game.h, one .cpp per C# file, sources.cmake, report.tsv
  -> built with the runtime (recomp/src/cs), the facade (recomp/src/bind) and the Unity layer
```

`mltranslate` (`dotnet run --project tools/translate -c Release -- <command>`):

| command | does |
|---|---|
| `decompile` | decompiles the DLL into the cache (10 s for the 1,905 files) |
| `translate [--units f] [--out d]` | the units and their closure into C++ (1-2 s for the proof's units, 15 s for the whole game) |
| `survey [--summary f.md]` | translates the whole game in memory; writes `progress/class_deps.tsv` and `progress/unity_api_gaps.tsv` |
| `diag` | Roslyn's errors on the decompiled code (a health check: 245 today, none in translated code) |

Options: `--managed <dir>` (the game's Managed folder; default `$ML_GAME/Moonlighter_Data/Managed`,
else the main checkout's `Moonlighter/`), `--cache`, `--repo`, `--bindings`.

### Decompiling lowered

The decompiler is asked to keep what the C# compiler lowered (`Decompile.Settings`), so the
translator sees what Mono runs and has fewer constructs to get right:

- iterators (coroutines) stay the compiler's `<Start>d__12` classes with their `MoveNext`
  state machines; lambdas stay the `<>c__DisplayClass` closures and the cached `<>c.<>9__0_0`
  delegates; local functions stay methods. Exact by construction: the same state machines,
  the same capture sharing, the same delegate instances (`-=` finds what `+=` added).
- no string interpolation, object/collection initializers, pattern matching, out variables,
  lifted nullable operators, query syntax, extension-method call syntax, optional or named
  arguments: each appears as the calls and temporaries the IL has.
- ILSpy's default inlining keeps every IL local (it inlines stack slots only, and a local
  only into a return, a condition or a switch), so a float stored to a local is rounded
  where Mono rounds it.
- invalid identifiers are escaped as ILSpy's project export does (`<>c` -> `_003C_003Ec`);
  file paths match `decomp/` and `progress/classes.tsv`.
- `beforefieldinit` comes from the metadata (`.beforefieldinit` in the cache), not from the
  decompiled C# (which shows a static constructor whenever it couldn't fold the initializers).

### Units and the closure

`tools/translate/units.txt` names the seeds: `file <path>` (every type declared in a C# file,
whole: all fields and members) or `type <metadata name> [members...]` (a type, only those
members; fields can be named too). `all` is every type. `hand <metadata name> <members...>`
declares game methods a driver defines by hand (a scene's services the translated code calls
but a test doesn't compare: the hero's own code, the camera's shake, the HUD, the save's
statistics; `recomp/tests/translated/scene/Standins.cpp`). `stub-others`: what the seeds reach
of a type no line names is declared, not translated (reaching it at run time is
`cs::NotTranslated`, naming it), so a proof translates only what it means to. The closure adds every game method a
translated body calls, the overridden declarations of translated overrides, the overrides of
translated virtual methods in the types in use, the implementations of interface members
(every member of a runtime interface a class declares: IEnumerator's MoveNext/Current/Reset,
IDisposable's Dispose), the static constructors of types whose static fields are used, and
the declarations of every type a field or signature names. A method that can't be translated
(an unsupported construct, a binding missing) gets a body that throws (`cs::NotTranslated`),
and `report.tsv` says why: the rest still compiles.

Partly translated types (DungeonGenerator for the proof) declare only the fields translated
code touches, which is invisible to the C# semantics.

## The C++ it writes

| C# | C++ |
|---|---|
| namespace `A.B`, type `C` | `::gen::A::B::C`; nested types flattened: `Outer_Inner`; generic: `Name_1<T>` (arity suffix) |
| class / interface reference | `T*`; `null` is `nullptr` |
| struct, enum | a value; `enum class E : <underlying>` |
| string / object | `::cs::String*` / `::cs::Object*` |
| `T[]` / `T[,]` | `::cs::Array<T>*` / `::cs::Array2<T>*` |
| delegate types | `::cs::Delegate<R(A...)>*`, by signature |
| `Nullable<T>` | `::cs::Nullable<T>` |
| other assemblies' types | `tools/translate/bindings.tsv` (below) |
| fields | the same names; an auto-property's backing field takes the property's name |
| properties, indexers | `get_X()` / `set_X(v)`, `get_Item(i)` / `set_Item(i, v)` |
| interface members | `<Interface>[_arity]_<member>` (`IEnumerator_MoveNext`), so explicit implementations can coexist; a class forwards to its implementing method |
| constructors | `void __ctor(args)` run on a zeroed object by `::cs::New<T>(args)` (`::cs::NewS<T>` for a struct) |
| static constructor | `__cctor()`, run once by `__si()` |
| locals | hoisted to the function's top and renamed apart (`l_name`), parameters `p_name` |

Everything is `public` (C# access isn't semantic at run time). Type and method-level
generics are templates; generic code goes to `game.h`, the rest to the `.cpp` of its C# file.
Types are emitted in dependency order (bases and value fields first; references only need a
forward declaration).

## Semantics, construct by construct

| C# construct | C++ output | why it is exact |
|---|---|---|
| float arithmetic `a * b + c` | `((double)a * (double)b + (double)c)`, rounded by `static_cast<float>` where a float is stored, passed, returned or cast | Mono (Unity 2019.2) evaluates float expressions in double and rounds at stores (recomp/src/unity/Mathf.h measured it on the PolyNav recording); comparisons and `(int)` conversions see the double |
| `(float)x` | `static_cast<float>(x)` even on a float expression | the compiler emits `conv.r4`, Mono rounds |
| float/double -> int | `cs::conv_i4(d)` (and i8, u4, i2...) | cvttsd2si: truncation, 0x80000000... for NaN and out of range |
| long -> float | `cs::conv_r4` (through double) | Mono converts through double |
| int `+ - *`, unary `-` | `cs::add/sub/mul/neg` on unsigned | wraps; no C++ undefined behaviour |
| int `/ %` | `cs::div/rem` | DivideByZeroException; `int.MinValue / -1` OverflowException |
| `<< >>` | `cs::shl/shr` | the count masked (& 31 / & 63) |
| `x++`, `x += y` | `cs::postinc(x)`, `x = x op y` with the lvalue's parts evaluated once | C# reads, computes, stores; a property's getter and setter are called once each |
| operand order | earlier operands with side effects go to temporaries: `(t1 = f(), t2 = g(), h(t1, t2))` | C# is left to right; C++ leaves argument and most operand order unspecified (and evaluates `a = b`'s right side first) |
| member access on a reference | `cs::nc(p)->m` | NullReferenceException; receivers of calls with side-effecting arguments are evaluated first and checked at the call, as `callvirt` does |
| `a[i]` | `cs::nc(a)->at(i)` | IndexOutOfRangeException |
| `(T)o`, `o as T`, `o is T` | `cs::Cast<T>` / `cs::As<T>` / `cs::Is<T>` (dynamic_cast) | InvalidCastException, null rules |
| boxing / unboxing | `cs::BoxAny(v)` / `cs::Unbox<T>(o)` | a `Box<T>` object with the value's Equals/GetHashCode/ToString; unboxing checks the type (an enum's box unboxes to its underlying type) |
| `==` on references | `cs::RefEq(a, b)` (class and interface pointers compared as objects) | reference equality |
| `==` with a user operator | the operator (UnityEngine.Object's: destroyed objects equal null) | |
| string `+` | `cs::String::Concat(...)` | `string.Concat(object, object)` semantics, null as "" |
| `switch` on strings | compare in order, `goto` the section | equality semantics of the compiler's hash switch |
| `goto`, labels, the iterators' `MoveNext` | C++ `goto` (locals hoisted, so no jump crosses an initialization) | the decompiled state machines as they are |
| `try`/`catch` | `catch (const cs::Thrown& ex)` and a type test per clause, else `throw;` | the first matching clause by the object's type; `throw;` rethrows |
| `finally`, `using`, `foreach`'s Dispose | a `cs::Finally` scope guard | runs on every exit (return, break, goto, exception) |
| `foreach` | the enumerator the compiler binds (struct enumerators by value), index loops for arrays and strings | the same MoveNext/Current calls, so List's version check throws on modification |
| `lock` | evaluates and null-checks its object | one thread |
| static fields | `(C::__si(), C::f)` when C has static initialization | `__si` runs the static constructor once (re-entry returns, as the CLR's does); beforefieldinit types at the first static field access, others also at static method entry and construction |
| constructors | `__ctor`: field initializers, then `Base::__ctor(...)`, then the body; `this(...)` skips the initializers | C#'s order; the object is already its final C++ type, so virtual calls from a base constructor reach overrides, as in C# |
| virtual / override / abstract / `new` | `virtual` / `override` / `= 0`; a hiding method gets a distinct name | |
| delegates | `Delegate<sig>::Bind<C, &C::M>(obj)`, `BindStatic<&F>()` | multicast lists; Combine/Remove as MulticastDelegate (Remove takes the last occurrence of the sublist); equality by target and method (a variable per method, never folded by a linker) |
| events | field-like: a delegate field plus `add_E`/`remove_E` doing Combine/Remove | |
| `typeof(T)`, `GetType()` | canonical `cs::Type` objects | `==` between types works |
| `default(T)`, `x == null` on a type parameter | `T{}`, `cs::IsNullT(x)` | |

Not supported yet (the whole game has 36 such methods): calls to virtual generic methods
(C++ has no virtual templates; 22 methods), tuples, `new T()` on a type parameter,
multi-dimensional array initializers, array covariance, `decimal`, ref locals, user-defined
`++`, exception filters, one decompiler artifact (a delegate built from a function pointer).
A type with no binding yet compiles as a placeholder (`cs::UnmappedRef<hash>`) in declarations;
the methods that use it are stubbed.

## The runtime (recomp/src/cs)

`Runtime.h` includes it all. Objects (`Object.h`): every reference type derives from
`cs::Object` (virtual ToString/Equals/GetHashCode, `cs_typename`, `cs_trace`); exceptions are
GC objects thrown inside `cs::Thrown`. Collections with mscorlib's (the reference source Mono
ships) exact behaviour, so enumeration order and growth match: `List` (capacity 4, doubling;
the introsort whose depth limit comes from the capacity, as the hand port verified),
`Dictionary` (buckets, entries in insertion order, a free list that the next insertion reuses
last-freed-first), `HashSet` (slots, free list, reset when emptied), their enumerators'
version checks, `Keys`/`Values`, `KeyValuePair`, `EqualityComparer<T>.Default` /
`Comparer<T>.Default`, the collection interfaces. `String` (UTF-16, interned literals,
ordinal operations exact; culture ones per en-US, which OptionsPanel.Load sets on every
thread: exact for ASCII letters and digits, see "Known approximations" for punctuation),
`StringBuilder`, number parsing with styles (the invariant
culture's symbols), `Array`, `Delegate`, `Box`/`Nullable`, `Type`, LINQ (eager operators so
far), `Math`.

The hand port uses the same `cs::List`/`cs::Dictionary` (both spellings of the API are there),
so translated and hand-ported code can share objects.

## Memory

No tracing collector of Mono's kind on the GameCube (24 MB), and no reference counting: the
game's object graph is full of cycles (a CaveRoom and its units point at each other, every
component and its GameObject, parents and children). The model:

- every object is allocated on a `cs::Heap` (linked into it: one pointer and two words of
  header);
- **a precise mark-sweep at frame boundaries** (`Heap::Collect`): the translator generates each
  class's `cs_trace` (its reference fields, and structs' through `cs_trace_value`); the roots
  are the static fields and static field-like events of translated classes (registered by the
  generated code; a generic type's per instantiation, by its `__si()` at its first static
  access), the interned strings and types, the Unity layer's live objects
  (`UnityEngine::Object::Live`), the scene's running coroutines (and the ones a WaitForSeconds
  still holds), its pending Starts, destroys and Animators, the running tweens, and
  `cs::Root<T>` handles hand-written code holds. Collecting only between frames means no C++
  stack is scanned: no translated code is running, every live object is reachable from those
  roots. A coroutine's locals live in its state machine object, so they are traced too,
  including the struct enumerators a `foreach` around a `yield` keeps there (List's,
  Dictionary's and its Keys'/Values', HashSet's: each traces its collection, which may be a copy
  nothing else holds: `foreach (x in new List<T>(...))`). Marks are epochs: nothing is reset
  after a collection.
- `Heap::Clear` (a test's or a scene's heap) frees what the roots don't reach and moves what
  they do (a static list made during a test, a cached delegate) to the program's own heap;
  static constructors allocate there directly (`cs::StaticScope`).
- objects without a generated `cs_trace` (the hand port's) are never collected, only cleared
  with their heap; what they reference is kept only if something traced reaches it too.
- checked by `cs_gc` (`tests/cs_gc_test.cpp`: each kind of field alone keeps its objects) and by
  `slime_translated_gc` / `enemy_golem_soldier_translated_gc`: the enemy replays with a
  collection at every frame end (`ML_GC_EVERY_FRAME=1`) and what it frees overwritten and never
  reused (`ML_GC_POISON=1`): ALL MATCH, about 3,000 objects freed per replay.

Why: it is exact (no finalizers in the game's code to observe a collection; no weak
references in the counted classes), deterministic (a collection runs at a fixed point, never
in the middle of game code), cheap per allocation (`new` plus a link), and precise without
compiler support. The frame budget sees one mark pass over live objects per collection,
which can run every N frames or when the heap passes a threshold. What it needs from the rest
of the port: hand-written code that keeps a translated object across frames holds it in a
`cs::Root` or a registered root source.

## The Unity facade (recomp/src/bind)

What translated scripts run on: Unity's API with Unity 2019.2's semantics, on the hand-written
Unity layer (`recomp/src/unity`) and the hand port's checked models. `bind/UnityEngine.h` is the
umbrella:

| header | what |
|---|---|
| `Core.h`, `Values.h` | UnityEngine.Object (`== null` of a destroyed object), the value types with Mono's float semantics, Resources, Debug, Time |
| `Scene.h` | the scene graph (GameObject, Transform, Component, Behaviour, MonoBehaviour), the messages in Unity's order (Awake, OnEnable, Start, Update, LateUpdate, OnDisable, OnDestroy), Invoke / CancelInvoke on `unity::DelayedCalls`, coroutines (WaitForSeconds, yield null, WaitForFixedUpdate), SendMessage, Destroy; `bind::Scene` holds a scene's services and runs its passes |
| `Physics.h` | Rigidbody2D and the Collider2Ds on the Box2D world (`unity::PhysicsWorld`): the fixed step as `game::EnemyPhysics` runs it (checked against the game), generalized to any body and the colliders below it, with OnTriggerEnter2D to both objects' scripts |
| `Components.h` | the Animator on the hand-written Mecanim (clip events to the scripts, the clips' writes applied to the objects as Unity binds them, write defaults from the values at OnEnable), renderers, materials, sprites |
| `DOTween.h` | DOTween on `unity::Tweens` |
| `Engine.h` | Instantiate, FindObjectOfType, Application, Gizmos, layers |
| `Plugins.h` | MasterAudio's draws (`game::MasterAudio`), AntiCheat's obscured values |
| `Prefabs.h` | a prefab of `data/extracted/prefabs.json` made in the scene as Instantiate makes it: objects, engine components, the translated scripts by class name (`gen::cs_AddComponent`) with their serialized fields, then Awake/OnEnable |
| `Serial.h` | serialized values into fields: the generated `cs_SetField` per class (numbers, strings, vectors, colours, references to the prefab's own objects, lists, arrays, `[Serializable]` objects) and `cs_SerialInit` (the empty strings and lists a loaded object never has null) |

A driver (a replay, later the game's loop) owns a `bind::Scene` and calls its passes in Unity's
order: the fixed steps (`PhysicsScene::Step`, the delayed calls' fixed pass, WaitForFixedUpdate),
DOTween, the Updates, the delayed calls, the coroutines on yield null, `UpdateAnimators`, the
LateUpdates, `ProcessDestroys`.

## Binding other assemblies (tools/translate/bindings.tsv)

Everything the game calls outside its own assembly maps through one table: a type to its C++
type (`type`), a member to a template (`get`, `set`, `call`, `new`, `op`, `field`, `add`,
`remove`), or a type mapped by convention (`conv`: methods by name, properties to
`get_X`/`set_X`, constructors to `__ctor`), optionally limited to the members a `members` line
lists. mscorlib maps to `recomp/src/cs`; UnityEngine to `recomp/src/bind/UnityEngine.h`, which
puts Unity's API on the hand-written Unity layer with Unity's exact semantics (Vector2's `==`
in double, UnityEngine.Object's `== null` for destroyed objects, Mathf on `unity/Mathf.h`,
Resources.Load of the extracted text assets, ...). Whatever isn't bound blocks the methods that
use it; `progress/unity_api_gaps.tsv` lists every such API with the methods and lines it blocks
(the plan of the hand-written work, biggest first).

## Build integration

- **recomp's CMake**: `-DML_TRANSLATED=ON` (`recomp/translated.cmake`) runs the translator at
  configure time (`dotnet run --project tools/translate -c Release -- translate`, from the
  game copy found as above or `-DML_GAME=<folder>`), builds `mlgen` (the generated sources,
  the facade) and the oracle tests against the translated classes. Changing `units.txt`,
  `bindings.tsv` or the tool's sources re-runs it on the next build. Needs the .NET 9 SDK.
- `-DML_TRANSLATED_DEBUG=ON` (MSVC): PDBs for the translated code and its tests, and
  `ML_CS_STACK=1` prints the C++ stack (function, file, line) at every C# exception thrown:
  where translated code met a null, an index out of range, a stub.
- **the MLGC builder** (the GameCube build) runs the same command against the user's copy
  before compiling, and compiles `recomp/gen/cpp/*.cpp` (listed in `sources.cmake`) with the
  runtime and the facade; devkitPPC's GCC needs `-std=gnu++17 -fexceptions -frtti
  -ffp-contract=off` (exceptions: C# exceptions are C++ exceptions; RTTI: casts and catch
  clauses). Nothing generated is committed, in either repository. Wired in since 2026-10-13:
  MLGC's `Makefile_GCN` with `TRANSLATED=1` runs the translator into its own Intermediate/gen
  and compiles it and the facade (objects `gen_*.o`, `bind_*.o`: the translation's file names
  are the hand port's too); the builder's "Translated game code" option sets it. devkitPPC GCC
  15.2 takes the generated code, the facade and the runtime unchanged. `TTEST=1` builds a disc
  that replays the BabySlime against the translated code and the hand port on the GameCube:
  ALL MATCH in Dolphin, with the time and memory numbers in `docs/gamecube_translated.md`.

## The proof

`-DML_TRANSLATED=ON` builds `cave_oracle_translated`: `recomp/tests/cave_oracle_test.cpp`
itself, compiled with `recomp/tests/translated/` first on its include path, where `game/Cave.h`,
`game/DungeonLayout.h` and `game/Constants.h` name the translated classes instead of the hand
port's (aliases; `Adapters.cpp` copies values in and out). Translated: CaveGenerator,
CaveRoom, SingleCaveRoom, BossCaveRoom, CaveRoomUnit, Miner, CommonRoomMiner, BossRoomMiner,
FixedStepsMiner, DungeonGenerator's layout methods and FloorGenerationSaveInfo, and
Constants with the game's own JSON reader (FullSerializer's fsJsonParser, fsData) reading the
`constants` TextAsset. The one hand-written piece is `DungeonLayout::Generate`, which runs the
layout part of `GenerateDungeonCoroutine` statement for statement on the translated
DungeonGenerator (the coroutine around it builds the dungeon's scene, which needs the Unity
layer's GameObjects). Result: rng 130/130, floor info (ResetValues) 3000/3000, layouts
(mirror) 3000/3000, layouts (real play) 52/52: ALL MATCH, identical to the hand port's run.

### The enemies (enemy_translated_test)

`-DML_TRANSLATED=ON` also builds `recomp/tests/translated/enemy_translated_test.cpp`: the enemy
replays of `tests/enemy_replay.h` (slime_test's and enemy_test's: the same recordings, the same
rows, the same pass criterion, with enemy_replay_colliders.h's rows and the newer probes read by
the hand port's own helpers: enable_clock.h, fixture_probe.h, polygon_probe.h,
enemy_replay_critters.h) with the game's own code in place of the hand port. Each enemy is its
prefab made in the scene (`bind::InstantiatePrefab`: BabySlime or GolemSoldier from
prefabs.json, its Animator from animators/<prefab>.json), Init'ed by the game's code
(EnemyStats.Init from enemy_stats.json through the translated EnemyStatManager and Constants),
its recorded state put back into the translated fields; then every frame as the game ran it,
the hero's doings (his body per step, his weapon's hits through Enemy.DealDamageToEnemy or a
hurtbox's EnemyDamageReceiver, Weapon.PushEnemyBackwards, the room's DisableEnemies when he
leaves) fed from the recording. Translated: BabySlimeBehaviour, GolemSoldierBehaviour,
EnemyBehaviour, Enemy, EnemyStats, EnemyStatManager, EnemyAttackCollisionDetection,
EnemyDamageReceiver, PolyNavAgent, PolyNav2D, AStar, Weapon.PushEnemyBackwards,
DungeonRoom.DisableEnemies/EnemyDefeated, with no gaps. Stood in for (units.txt `hand`): the
hero's own code, the camera's shake (its Random draws made), the HUD and health bar, the save's
statistics and analytics, the item drops (their draws resynced by the replay, as the hand port's).

| ctest | recording | result |
|---|---|---|
| `slime_translated` | slime.jsonl (4 BabySlimes, 728 frames) | ALL MATCH: 2542 fixed-step starts, 1416 frame ends per row, the attack triggers, hits, pushes, deaths |
| `slime_bis_translated` | slime_bis.jsonl | ALL MATCH; on main's real recording of the second room (Golem_Enemies_15_Bis, 2026-10-13 17:10; until then a copy of slime.jsonl) too, row table identical to `slime_bis`'s |
| `enemy_golem_soldier_translated` | enemy_GolemSoldier.jsonl (re-recorded 2026-10-13 16:54) | ALL MATCH: 1778 fixed-step starts, 968 frame ends, the swing's clip events and their Random draws, the look lock, the enable-clock probe |

The row tables (every row, every count) are identical to the hand port's on the same data.
(One line differs, for the better: "Random drawn by the game's other scripts (resynced)", 21 times
against the hand port's 23 on slime.jsonl, 33 against 34 on enemy_GolemSoldier.jsonl: the
translated code makes draws of the game's that the hand port leaves to the resync, e.g. in
frames 300 and 394 of slime.jsonl.) The translated code and the hand port never disagreed; what the proof found was in the facade:
an attack trigger activated after its body was made wasn't attached (bind/Physics now rescans
a body's colliders at each sync), the Animator took its write-defaults values at its first pass,
after the replay had restored a mid-swing attack box (it now binds at OnEnable, as Unity does),
and a [Serializable] class whose constructor the closure stubbed left the prefab's list null
(Enemy.itemDrops: ItemDrop's constructor is now a unit).

## What translates today

From `mltranslate survey` (progress/class_deps.tsv, progress/unity_api_gaps.tsv):

- of the 160,682 counted lines (1,173 classes), **109,045 lines (67.9%) translate today**
  (each class's lines times the share of its methods' lines that translate); 14,534 of 17,172
  methods (84.6%); 601 classes translate whole (29,754 lines). "Translates" means: every
  construct supported and every API the method calls bound (to a member the runtime or the
  facade has: `members` lines keep the convention-mapped types honest). The scene facade
  doubled it (34.1% before: GameObject, Transform, Component, the messages, coroutines, the 2D
  physics, the Animator, DOTween's core);
- the whole translated game (3,328 types, 21,660 methods, 270,000 lines of C++, the methods
  that don't translate stubbed) compiles with MSVC 2022 (PCH, unity build) and passes GCC 15's
  front end (`-std=gnu++17 -fsyntax-only`); the proof's build also runs under GCC: ALL MATCH;
- the constructs are nearly done: 36 methods of the whole game hit an unsupported construct
  (22 of them calls to virtual generic methods);
- the rest is bindings: 994 APIs missing (`progress/unity_api_gaps.tsv`). By lines blocked:
  Unity UI first (Image, Text, Graphic.color, Selectable, EventSystem: 123 APIs), then
  RectTransform, RaycastHit2D and Physics2D.Raycast, CanvasGroup, Camera, the scene manager,
  Color32, Input, AnimationCurve; DOTween's loops, Pause, SetTarget, sequences (77 APIs); the
  BCL's formatting (Int32.ToString(format)...: 157 APIs); MasterAudio's other calls (39).

What's left, in order:

1. the other Golem enemies on the translated code, each against its recording as the
   BabySlime and the GolemSoldier are (HermitSlime: its patrol and drain rows; BrokenGolemTurret:
   its shots' pool; the Kamikaze golems; Tangle), finding what the facade still lacks (the body's
   own box remade when a clip drives it, OnCollisionEnter2D, Physics2D.Raycast);
2. the player and the dungeon's runtime (DungeonRoom, the doors, the floor's scene) translated,
   then the rooms' scenes built by the game's own GenerateDungeonCoroutine;
3. Unity UI and the town: the facade's biggest gaps (the table above);
4. the hand port's replays retired one by one as their classes run translated (the hand-written
   Unity layer they verified stays).

## Known approximations (to verify or finish)

- culture-sensitive string comparison (String.Compare, CompareTo, Sort() of strings,
  StartsWith/EndsWith/IndexOf(string)): lowercase code-unit order, ties broken lowercase first;
  exact for ASCII letters and digits, unverified where punctuation decides (Mono's collator has
  its own order for symbols, and word sort ignores the hyphen and the apostrophe at the first
  level: "a-c" against "ab") and past ASCII. The game's culture-sensitive calls seen so far
  compare plain names and prefixes such as "//", "[", "data:image", "Register_";
- Math.Round(double): to even (nearbyint). Mono's icall (unless its JIT inlines a round-to-even
  instruction) rounds through floor(x + 0.5) with a fix for halves: the same for every float
  argument and every product of two floats, different only for a double whose x + 0.5 rounds
  (Mathf.Round / RoundToInt take floats: exact);
- float.ToString / double.ToString: 7 / 15 significant digits from the first 17 (Mono's
  NumberFormatter); `String.Format` without format strings yet;
- string.GetHashCode is Mono's legacy algorithm, unverified (no counted class reads it);
- System.Math's transcendental functions are the C library's (as Mono's are its platform's):
  exact on the PC build, the GameCube's newlib may differ in the last bit;
- beforefieldinit types initialize at their first static field access (Mono may do it when it
  compiles the method that makes it: earlier, never later);
- an exception thrown from a `finally` while another unwinds ends the program (C++);
- the evaluation order of a receiver's null check against side-effecting arguments follows
  `callvirt` for calls, not yet for every field access through a temporary;
- the facade: a body's own box (its object's first BoxCollider2D) isn't remade when changed
  after the body was made (an Animator driving the root box: the HermitSlime's); no
  OnCollisionEnter2D/Exit/Stay messages yet; Instantiate copies every field as is (Unity remaps
  references into the copied tree); a prefab's component order is the extraction's (renderer,
  colliders, body, Animator, scripts);
- the facade's scene: the Updates, FixedUpdates and LateUpdates run in the order the driver
  calls them (the replays: the recorded order); Unity's own order (script execution order, then
  its behaviour lists) and Start's moment for an object made mid-frame (`Scene::RunStarts`, when
  the driver calls it) are not modelled yet. `yield return new WaitForEndOfFrame()` resumes as
  after `yield return null` (the next frame, after the Updates), where Unity resumes it at the
  end of the same frame (DungeonGenerator and GameManager use it). Destroy(gameObject): OnDisable
  on the whole tree, then OnDestroy object by object, parents first: unverified;
- the facade's Transform with a rotation or a scale: its own TRS math (double trigonometry,
  float per component) in place of the engine's native float math; only translations (parents'
  local positions added in order) are checked against the game;
- UnityEngine.Object.GetInstanceID / GetHashCode: unique per object, not Unity's numbering (the
  game uses them as tween ids and in logs only);
- memory: every UnityEngine.Object is a root for as long as it exists (`Object::Live`),
  destroyed ones too, so none is ever collected, nor what it references: a leak to close before
  the full game (to do: a destroyed object stops being a root once the facade's tables, the
  physics' and the drivers' drop it);
  Resources.Load makes a new TextAsset at each call (Unity returns the same asset);
- the enemy replays' stand-ins (units.txt `hand`) that are the game's logic rather than a scene
  service: HeroMerchant.DealDamageToPlayer (recorded), HeroMerchantController's
  CheckIfPlayerIsShieldProtected (false), AddForce (recorded) and CheckForHolesAfterPush (not
  reached), CameraManager.Rumble (DOShakePosition's draws only), DungeonRoom.SpawnEnemyDeadSprite
  (recorded; its Random.Range(0, 1) left to the resync), DungeonRoom.AddDropItems,
  ItemDrop.DropItem (its Random.value / Range left to the resync), FamiliarsManager.GetEggItem
  (false), EnemyStatManager.ReadFile (FullSerializer's reflection done by hand through
  cs_SetField), TimeManagerV2 / OptionsManager / GameManager (the recording's values); the HUD,
  health bars, statistics, analytics and achievements are no-ops. The draws they skip are what
  the replays resync ("Random drawn by the game's other scripts");
- `data/extracted/prefabs.json` has Enemy.itemDrops' entries as "min"/"max" until it is
  extracted again (tools/extract/prefabs.py now writes minDropRate/maxDropRate, the fields'
  names): harmless while ItemDrop.DropItem is stood in.
