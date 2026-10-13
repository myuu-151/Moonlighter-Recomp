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
3000 mirror layouts, 52 real-play layouts, 3000 floor-info resets, ALL MATCH); every Golem
enemy the hand port replays (the BabySlime, the GolemSoldier, the HermitSlime, the KamikazeGolem,
the FlyingKamikazeGolem in two rooms, the BrokenGolemTurret with its shots and their pool, the
Tangle), its behaviour, Enemy, EnemyBehaviour, EnemyStats and PolyNav translated and running on
the Unity facade, matches its recordings frame by frame with every row the hand port compares
(ALL MATCH, row tables identical to the hand port's tests'), with Unity's objects collected once
destroyed and unreferenced. 70.3% of the counted lines translate today.

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
but a test doesn't compare: the hero's own code, the HUD, the save's statistics;
`recomp/tests/translated/scene/Standins.cpp`). `wrap <metadata name> <methods...>` translates a
game method as `<name>__game` and leaves `<name>` to the driver, which calls it around the game's
(a replay seeing where a dead enemy's sprite is stamped while the game's own code stamps it).
`stub-others`: what the seeds reach
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
  access), the interned strings and types, the Unity objects their native side holds (every
  `UnityEngine::Object` not destroyed: `Object::Live` with `rooted_`), the scene's root objects,
  running coroutines (and the ones a WaitForSeconds still holds), its pending Starts, destroys and
  Animators, the running tweens, and `cs::Root<T>` handles hand-written code holds. A destroyed
  Unity object is no root: it lives on (`== null`, as Unity's managed wrapper does) while
  something references it, and is collected with what only it references once nothing does. The
  facade's tables let go of what's destroyed (the scene's roots and children, its coroutines,
  delayed calls and Animators; the physics' bodies, fixtures and contacts, the attached fixtures'
  layers living in the physics' own slots); a renderer's own material instance is its renderer's
  (`rooted_` off: collected with it, as UnloadUnusedAssets would); a driver lets go of the objects
  the game destroyed (the replays release their dead enemies). Collecting only between frames means no C++
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
- checked by `cs_gc` (`tests/cs_gc_test.cpp`: each kind of field alone keeps its objects), by
  `bind_gc` (`tests/translated/bind_gc_test.cpp`: a destroyed GameObject tree with a body,
  colliders, a renderer and its material kept while a live list references it, `== null`, then
  collected whole once it doesn't, the physics stepping on with no dangling table; an object never
  destroyed kept), and by every translated replay's `_gc` variant: the replay with a collection at
  every frame end (`ML_GC_EVERY_FRAME=1`) and what it frees overwritten and never reused
  (`ML_GC_POISON=1`): ALL MATCH, 2,600 to 23,000 objects freed per replay, the destroyed ones
  counted (slime 42, slime_bis 63, tangle 105) and the dead enemies collected (slime 2 of 4,
  slime_bis 3 of 4, tangle 5 of 6). The ones still alive are referenced by the game's own state, as
  Mono keeps them: `Enemy.enemyPlayingSound` (a static), PolyNav2D's event a dead agent never
  left, HeroMerchant.EnteredInCombat (the KamikazeGolem's SelfDestroy doesn't leave it);
  `ML_GC_WHY=1` prints each one's chain from its root (`cs::Heap::WhyAlive`).

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
| `Scene.h` | the scene graph (GameObject, Transform, Component, Behaviour, MonoBehaviour), the messages in Unity's order (Awake, OnEnable, Start, Update, LateUpdate, OnDisable, OnDestroy), Invoke / CancelInvoke on `unity::DelayedCalls`, coroutines (WaitForSeconds, yield null, WaitForFixedUpdate; StartCoroutine / StopCoroutine by name through the generated `cs_Coroutine`), SendMessage, Destroy, a SetParent across an inactive parent switching the tree off or on; `bind::Scene` holds a scene's services, runs its passes and has drivers' hooks (each Invoke'd method, each tween OnComplete) |
| `Physics.h` | Rigidbody2D and the Collider2Ds on the Box2D world (`unity::PhysicsWorld`): the fixed step as `game::EnemyPhysics` and `game::EnemyProjectiles` run it (checked against the game), generalized to any body and the colliders below it: a body made when its object is first active and off and on with it; its own box as a trigger root, remade when an Animator or a script changes it, made again at once when isTrigger changes, only while on on a Kinematic body (the kamikaze's AttackCollider, synced with the root it hangs from); remade triggers' carries and rest restarts, one enter per collider; OnTriggerEnter2D, OnCollisionEnter2D / Stay / Exit to the colliders' objects' scripts (the order of several enters a driver's to set: `orderEnters`); Physics2D.OverlapBox and Raycast over the room's colliders (`unity::Physics2D`), OverlapCircleAll over the world (`unity/Physics2DOverlap.h`); drivers' hooks (each raycast, overlap, AddForce, collision) |
| `Components.h` | the Animator on the hand-written Mecanim (clip events to the scripts, the clips' writes applied to the objects as Unity binds them, write defaults from the values at OnEnable and written back when its object goes off, a fresh start when it comes back, root motion zeroing a Dynamic body's velocity and restarting its rest), renderers, materials, sprites (their textures' pixels presentation: none kept), the camera |
| `DOTween.h` | DOTween on `unity::Tweens` (To of floats and Vector2s, the shortcuts and their targets for DOKill, Pause, DOShakePosition's draws) |
| `Engine.h` | Instantiate (the copy's references into the copied tree remapped to the copies: the generated `cs_Remap`), FindObjectOfType, Application, Gizmos, layers |
| `Plugins.h` | MasterAudio's draws (`game::MasterAudio`), AntiCheat's obscured values |
| `Prefabs.h` | a prefab of `data/extracted/prefabs.json` made in the scene as Instantiate makes it: objects, engine components, the translated scripts by class name (`gen::cs_AddComponent`, or a driver's) with their serialized fields (a reference to another prefab by name: the driver's asset, `JsonSerialValue::assets`), then Awake/OnEnable |
| `Serial.h` | serialized values into fields: the generated `cs_SetField` per class (numbers, strings, vectors, colours, references to the prefab's own objects, lists, arrays, `[Serializable]` objects) and `cs_SerialInit` (the empty strings and lists a loaded object never has null) |

A driver (a replay, later the game's loop) owns a `bind::Scene` and calls its passes in Unity's
order: the fixed steps (`RunStarts` for what was enabled since, `PhysicsScene::Step`, the delayed
calls' fixed pass, WaitForFixedUpdate), DOTween, `RunStarts`, the Updates, the delayed calls, the
coroutines on yield null, `UpdateAnimators` (the Animators running as it starts: one switched on
by a clip event runs from the next pass), the LateUpdates, MasterAudio's LateUpdate,
`ProcessDestroys`. Where a driver needs to see a translated method run (a replay's rows), its
prefab's script can be a probe: the translated class with that virtual method wrapped
(`tests/translated/turret_translated.h`'s TurretProbe: the game's code unchanged).

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

### The enemies (enemy_translated_test, tangle_translated_test)

`-DML_TRANSLATED=ON` also builds `recomp/tests/translated/enemy_translated_test.cpp`: the enemy
replays of `tests/enemy_replay.h` (slime_test's and enemy_test's: the same recordings, the same
rows, the same pass criterion, with enemy_replay_colliders.h's rows and the newer probes read by
the hand port's own helpers: enable_clock.h, fixture_probe.h, polygon_probe.h,
enemy_replay_critters.h) with the game's own code in place of the hand port; what one enemy adds
is a Hooks of its own, as the hand port's ReplayHooks (`turret_translated.h`,
`kamikaze_translated.h`, `flykami_translated.h`; the HermitSlime's patrol and ItemDrain rows are
the common replay's). `tangle_translated_test.cpp` is tangle_test's replay the same way. Each
enemy is its prefab made in the scene (`bind::InstantiatePrefab`, its Animator from
animators/<prefab>.json), Init'ed by the game's code (EnemyStats.Init from enemy_stats.json
through the translated EnemyStatManager and Constants), its recorded state put back into the
translated fields; then every frame as the game ran it, the hero's doings (his body per step, his
weapon's hits through Enemy.DealDamageToEnemy or a hurtbox's EnemyDamageReceiver,
Weapon.PushEnemyBackwards, the room's DisableEnemies when he leaves) fed from the recording.
Translated: the behaviours (BabySlimeBehaviour, GolemSoldierBehaviour, HermitSlimeBehaviourNEW,
GolemKamikazeBehaviour and its SoundsKamikaze, FlyingKamikazeGolemBehaviour,
BrokenGolemTurretBehaviour, TangleBehaviour), BrokenGolemTurretProjectile, PoolManager,
EnemyBehaviour, Enemy, EnemyStats, EnemyStatManager, EnemyAttackCollisionDetection,
EnemyDamageReceiver, PolyNavAgent, PolyNav2D, AStar, Weapon.PushEnemyBackwards, DungeonRoom's
DisableEnemies / EnemyDefeated / AddDropItems / SpawnEnemyDeadSprite (wrapped), ItemDrop.DropItem,
CameraManager.Rumble, HeroMerchantController's CheckIfPlayerIsShieldProtected and AddForce, the
Wanderer Amulet's handler, with no gaps. Stood in for (units.txt `hand`): the hero's own code
(DealDamageToPlayer; his hole check after a push), the room's chests (UnLockRoomChests: their
sounds' draws) and the Wanderer's arrival, the item's object a drop makes (ItemDrop.SpawnItem: its
one draw made), the familiar's egg (the save decides it), the HUD and health bar, the save's
statistics, analytics, options and time of day, FullSerializer's reflection (EnemyStatManager.ReadFile).

| ctest (and `_gc`) | recording | result |
|---|---|---|
| `slime_translated` | slime.jsonl (4 BabySlimes, 728 frames) | ALL MATCH: 2794 fixed-step starts, 1652 frame ends per row, the attack triggers, hits, pushes, deaths |
| `slime_bis_translated` | slime_bis.jsonl (Golem_Enemies_15_Bis) | ALL MATCH |
| `enemy_golem_soldier_translated` | enemy_GolemSoldier.jsonl | ALL MATCH: the swing's clip events and their Random draws, the look lock, the enable-clock probe |
| `enemy_hermit_slime_translated` | enemy_HermitSlime.jsonl | ALL MATCH: the trigger root the clips drive, the hurtbox, the patrol from the game's draws, ItemDrain |
| `enemy_kamikazegolem_translated` | enemy_KamikazeGolem.jsonl (2 kamikazes) | ALL MATCH: MoveEnemy's InvokeRepeating timing and draws, the jump, the AttackCollider's own Kinematic body, the explosions' OverlapCircleAll (whom, in which order), SelfDestroy, the room's chests and the Wanderer Amulet's draw |
| `enemy_flyingkamikazegolem_translated`, `_bis_translated` | enemy_FlyingKamikazeGolem.jsonl, enemy_FlyingKamikazeGolem_bis.jsonl (main's turned door boundaries) | ALL MATCH: the charge (bounds, the turn's bug), the root box switched, EndCharge / EnableCharge's Invokes, the return's OutQuint tween and its OnComplete's draw, the enable sounds |
| `enemy_broken_turret_translated` | enemy_BrokenGolemTurret.jsonl (4 turrets, the root-motion probe) | ALL MATCH: the looks' raycasts, ShootAnimation's Invokes, the shots from the pool (Instantiate'd under its inactive parent, bodies at first activation), their flight, OnCollisionEnter2D (several in a step in the recording's order, as main's rule), crash (isTrigger remade), DestroyNow, the pool's order, GetContacts, the root motion |
| `tangle_translated` | tangle.jsonl (6 Tangles) | ALL MATCH: the weave's LateUpdates in the game's order, its raycast |
| `bind_gc` | (none) | the collector on the facade's objects (Memory, above) |

The row tables (every row, every count) are identical to the hand port's on the same data, but the
Random resyncs ("Random drawn by the game's other scripts (resynced)"), which are fewer or as many:
translated vs hand port, slime 34/41, slime_bis 20/24, GolemSoldier 33/34, HermitSlime 32/33,
KamikazeGolem 27/27, FlyingKamikazeGolem 28/28 and bis 17/18, BrokenGolemTurret 23/23, Tangle 30/31
(the translated code makes draws of the game's that the hand port leaves to the resync: the
drops'; what's left is the hero's and the room's other scripts', `ML_RESYNC_DEBUG=1` prints each).
The translated code and the hand port never disagreed; what the proofs found was in the facade
(each now as the hand port's rules have it): an attack trigger activated after its body was made
wasn't attached, the Animator took its write-defaults values at its first pass rather than its
OnEnable, a [Serializable] class whose constructor the closure stubbed left the prefab's list
null; a body's own box remade by an Animator, a trigger root, a collider disabled by a script, a
Kinematic body of a child's, the body made only once its object is active (a pool's), the shapes
made again when isTrigger changes, root motion's zero velocity, Start before the first step after
an object comes on, an Animator switched on in a pass running from the next one, Instantiate's
references into the copied tree, StartCoroutine by name, the turret shot's prefab named by its
turret. The recordings also showed the rooms' middle detail has no SpriteRenderer for a dead
enemy's stamp (no flip draw where one dies: slime.jsonl frames 310, 402, 448).

## What translates today

From `mltranslate survey` (progress/class_deps.tsv, progress/unity_api_gaps.tsv; 2026-10-14):

- of the 160,682 counted lines (1,173 classes), **112,956 lines (70.3%) translate today**
  (each class's lines times the share of its methods' lines that translate; class_deps.tsv's
  "translated" column to a tenth of a percent); 14,712 of 17,172 methods (85.7%); 646 classes
  translate whole (35,688 lines); of the whole assembly (third-party code compiled into it),
  18,115 of 21,660 methods. "Translates" means: every construct supported and every API the
  method calls bound (to a member the runtime or the facade has: `members` lines keep the
  convention-mapped types honest). The scene facade doubled it (34.1% before: GameObject,
  Transform, Component, the messages, coroutines, the 2D physics, the Animator, DOTween's core);
  the Golem enemies' work added Physics2D.Raycast / OverlapCircleAll, RaycastHit2D, collision
  messages, Rect, Camera, sprite textures, DOTween's Pause / DOKill / DOShakePosition / Vector2 To,
  StartCoroutine by name (67.9% before);
- the whole translated game (3,328 types, 21,660 methods, 270,000 lines of C++, the methods
  that don't translate stubbed) compiled with MSVC 2022 (PCH, unity build) and passed GCC 15's
  front end (`-std=gnu++17 -fsyntax-only`) when last built whole (2026-10-13, before this round's
  facade additions); the proofs' builds run under MSVC;
- the constructs are nearly done: 33 methods of the whole game hit an unsupported construct
  (22 of them calls to virtual generic methods);
- the rest is bindings: 961 APIs missing (`progress/unity_api_gaps.tsv`). By lines blocked:
  Unity UI first (Image, Text, Graphic.color, Selectable, EventSystem: 123 APIs), then
  RectTransform, CanvasGroup, CustomYieldInstruction, the scene manager, Color32, Input,
  AnimationCurve; DOTween's loops, SetTarget, sequences (75 APIs); the BCL's formatting
  (Int32.ToString(format)...: 157 APIs); MasterAudio's other calls (39).

What's left, in order:

1. the player and the dungeon's runtime (DungeonRoom, the doors, the floor's scene) translated,
   then the rooms' scenes built by the game's own GenerateDungeonCoroutine; with the hero
   translated, the stand-ins that are his (DealDamageToPlayer, his hole check, his body as the
   recording's) go;
2. the other stand-ins: FullSerializer's reflection (EnemyStatManager.ReadFile: a facade
   fsSerializer.TryDeserialize over the generated cs_SetField), the item's object a drop makes
   (ItemStack: Unity UI), the familiar's egg (the save: GameSlot, HeroStatistics' counts, the item
   database), the room's chests (Chest), MasterAudio's EventSounds as a facade component (the
   flying golem's enable sounds are read from the prefab today);
3. Unity UI and the town: the facade's biggest gaps (the table above);
4. the hand port's replays retired one by one as their classes run translated (the hand-written
   Unity layer they verified stays). bind/Physics and the hand port's game::EnemyPhysics /
   EnemyProjectiles keep the same physics rules twice until then (both checked by the recordings).

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
- the facade: Instantiate copies a script's fields as they are but its references to objects of
  the copied tree (remapped): a serialized list or [Serializable] object is shared with the
  original, where Unity copies it; a prefab's component order is the extraction's (renderer,
  colliders, body, Animator, scripts); trigger and collision messages go to the colliders' own
  objects (not also to an attached Rigidbody2D's object above them); the order of several
  OnCollisionEnter2D in one step is Box2D's here (Unity's follows proxy ids the whole session
  set: a replay sets the recording's, `orderEnters`); a texture's pixels aren't kept (GetPixel
  reads transparent) and a sprite's texture has no size;
- the facade's scene: the Updates, FixedUpdates and LateUpdates run in the order the driver
  calls them (the replays: the recorded order); Unity's own order (script execution order, then
  its behaviour lists) are not modelled yet; Start runs where the driver calls `Scene::RunStarts`
  (the replays: before each fixed step and before the Updates). `yield return new WaitForEndOfFrame()` resumes as
  after `yield return null` (the next frame, after the Updates), where Unity resumes it at the
  end of the same frame (DungeonGenerator and GameManager use it). Destroy(gameObject): OnDisable
  on the whole tree, then OnDestroy object by object, parents first: unverified;
- the facade's Transform with a rotation or a scale: its own TRS math (double trigonometry,
  float per component) in place of the engine's native float math; only translations (parents'
  local positions added in order) are checked against the game;
- UnityEngine.Object.GetInstanceID / GetHashCode: unique per object, not Unity's numbering (the
  game uses them as tween ids and in logs only);
- memory: a UnityEngine.Object not destroyed is a root, as its native side holds it, whether or
  not anything references it (Unity's: an asset or a never-destroyed object lives until unloaded);
  a renderer's own material instance is collected with its renderer (as UnloadUnusedAssets would
  collect it). Resources.Load makes a new TextAsset at each call (Unity returns the same asset);
- the enemy replays' stand-ins (units.txt `hand`) that are the game's logic rather than a scene
  service: HeroMerchant.DealDamageToPlayer (the hero's code: recorded), HeroMerchantController's
  CheckForHolesAfterPush (false: his fall is in the recording), ItemDrop.SpawnItem (the item's
  object not made: its Drop's direction draw made), DungeonRoom.UnLockRoomChests (the chests'
  unlock sounds' draws), FamiliarsManager.GetEggItem (false: the save decides it),
  EnemyStatManager.ReadFile (FullSerializer's reflection done by hand through cs_SetField),
  DungeonManager.SpawnWanderer (not reached), TimeManagerV2 / OptionsManager / GameManager (the
  recording's values); the HUD, health bars, statistics, analytics and achievements are no-ops.
  The draws they skip are what the replays resync ("Random drawn by the game's other scripts",
  with the hero's and the room's other scripts').
