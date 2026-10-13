# The translated code on the GameCube: first build, first numbers

On 2026-10-13 the game's own code, translated by `tools/translate`, ran on the GameCube for the
first time: MLGC's build compiles it (`TRANSLATED=1`), and a test disc (`TTEST=1`) replays the
BabySlime's oracle recording against it in Dolphin, then the same recording against the hand port,
timing both with the Gekko's timebase. This page gives the method, the numbers, what costs the most
and the fixes, ranked by what they would save. It settles the guess in
`docs/translation_strategy.md` ("How much overhead is left").

## Result

**The translated BabySlime matches the game on the GameCube.** `slime.jsonl` (4 BabySlimes, 728
frames, room Golem_Chest_19) replayed against the translated code, with the collector at every
frame end (the ctest `slime_translated_gc`): ALL MATCH, every row and count identical to the PC's,
line for line: 2,794 fixed-step starts, 1,652 frame ends per row, the attack triggers, hits, pushes,
deaths, "collected at 728 frame ends: 4273 objects freed, 4479 left". The hand port's replay
(`slime_test`) on the same disc: ALL MATCH, identical to the PC's too.

The one difference the GameCube showed was in the C library: newlib's `cosf` missed 2 of the
recording's 16 `Random.insideUnitCircle` probes by an ulp. `unity/Random.cpp` now takes cos and sin
through the double functions where the compiler isn't MSVC (fixed; the PC is unchanged). No float,
endianness or 32-bit difference appeared in the enemies' rows.

## Method

| | |
|---|---|
| Build | MLGC `Makefile_GCN` with `TTEST=1` (implies `TRANSLATED=1`): the translator run against the Steam copy, `recomp/gen` + `recomp/src/bind` + `recomp/src/cs` and the rest compiled by devkitPPC GCC 15.2 (gekko-toolchain r49.2-2), `-O2 -ffp-contract=off -mcpu=750`, Octave-libogc v2.7; disc packed by Octave headless |
| Program | `MLGC/Source/TranslatedTest.cpp`: the replays are the PC's own programs (`tests/translated/enemy_translated_test.cpp`, `tests/slime_test.cpp`), their `main()` renamed; their files read through a `disc:` device on Octave's disc access (`SYS_AcquireFileData`); their output through OSReport to Dolphin's log |
| Data | `tools/make_test_data.py` (MLGC): the recording (`data/oracle/slime.jsonl`) and the replays' data from the Steam copy, the prefabs and sound groups cut to the BabySlime's (the PC replays give identical output on the cut data) |
| Emulator | Dolphin 2609-1 (DolphinWorks), its Accurate profile (one CPU thread, the CPU cache, FPRF, accurate NaNs, no fastmem, the DSP's LLE), Null video, unthrottled; on a hidden desktop (`tools/dolphin_test.ps1`, as `tools/oracle_launch.ps1` runs the game) |
| Time | per frame and phase (`tests/replay_profile.h`): only the game's work (physics steps, the scripts' Updates, Invoke and coroutines, Animators, LateUpdates, destroys, the collector), not the replay's reading and comparing |
| Where the time goes | `TTEST_PROFILE=1`: a tick task samples the main thread every 100 us (its PC, LR and the stack's back chain), inside the timed phases; `tools/ttest_profile.py` maps them to MLGC.elf's functions. 7,575 samples |
| Memory | newlib's `mallinfo` at every frame end and around each collection; a census of the collector's heap by class |

To run it (the MLGC repository; data made once with `tools/make_data.py`):

```
py tools/make_test_data.py --recomp <Moonlighter-Recomp>
set TTEST=1 & Octave.exe -headless -project <MLGC>/MLGC/MLGC.octp -build GameCube   (ML_GAME, ML_REPO set)
powershell -File tools\dolphin_test.ps1 -Out build\ttest.log
(TTEST_PROFILE=1 as well:) py tools/ttest_profile.py build\ttest.log
```

## Time per frame

Dolphin, the Gekko's timebase (40.5 MHz), mean over the 728 frames, in microseconds. A 60 fps frame
is 16,667 us.

| phase | translated | hand port | ratio |
|---|---:|---:|---:|
| physics (Box2D step, trigger messages) | 220.8 | 85.0 | 2.6 |
| fixed steps' game code (hits, pushes, Invokes) | 2.4 | 0.8 | 3.0 |
| DOTween | 1.1 | 0.4 | 2.8 |
| the enemies' Updates | 34.2 | 26.5 | 1.29 |
| Invoke, coroutines (the paths) | 12.4 | 9.5 | 1.31 |
| Animators | 4.4 | 2.7 | 1.6 |
| LateUpdates (PolyNav) | 147.7 | 101.9 | 1.45 |
| destroys | 0.1 | 0.2 | |
| **the game's work** | **423.1** | **226.9** | **1.86** |
| collector, at every frame end | 566.2 | (none) | |
| **all** | **989.3** | **226.9** | 4.4 |

The slowest frame (169: paths computed) took 3,173 us translated, 1,786 us hand-ported. One enemy
Update averages 16.6 us translated against 12.8 us. On the PC (MSVC, `ML_REPLAY_PROFILE=1`) the
same replay's game work is 11.3 us a frame translated against 7.0 us (1.6), the collector 46 us.

So the scripts themselves translate well: the enemies' Updates are 1.29 times the hand port's, the
pathfinding 1.3 to 1.45. What costs is around them: the scene facade's physics (2.6 times) and the
collector.

## Memory

| | |
|---|---:|
| in use when the replay starts (Octave) | 2,328 KB |
| in use at frame ends, after the collection | 7,106 to 7,311 KB |
| of which the replay's own copy of the recording (test harness only) | 3,719 KB |
| of which the game's data and scene (the rest, at its largest) | 1,263 KB |
| malloc's arena, its high-water mark | 15,706 KB |
| the collector's live objects after each collection | 4,433 to 4,486 (mean 4,468) |
| their own blocks / their strings' UTF-16 buffers (census at frame 364) | 149 KB / 165 KB |
| made in a frame (objects, net bytes): mean / max | 3.9, 1.1 KB / 43, 55 KB |
| freed per collection: mean / max (the first: the loading's garbage) | 5.9, 8.8 KB / 1,482, 6,296 KB |
| time per collection: mean / max | 566 us / 1,366 us |

The live heap is small and allocation per frame is light (4 objects, 1 KB). Two things stand out:

- **83% of the live objects are static data**: the constants' FullSerializer tree (1,562 `fsData`,
  936 boxed `Double`, 203 boxed `Int64`, most of the 1,063 strings) and 97 `EnemyStatModificator`.
  The scene is about 500 objects (80 GameObjects and Transforms, 59 colliders, the 4 enemies'
  components). Every collection marks all of it again.
- **The arena peaks at 15.7 MB because of loading garbage**: 6.2 MB of it is the game's own
  `FsJSONManager.RemoveComments`, which cuts each `//` comment out of the 34 KB constants text with
  `String.Remove`, copying the whole text (68 KB in UTF-16) about 90 times. Measured on the
  GameCube: RemoveComments 46 ms and 6,221 KB, `fsJsonParser.Parse` 12 ms and 237 KB; the hand
  port's reader of the same file: 6 ms. Mono collects this as it goes; here it waits for the first
  frame end.

## Size

| .dol | bytes |
|---|---:|
| the game (`make`) | 2,774,548 |
| the game with `TRANSLATED=1` (the translated code compiled; the game doesn't call it yet) | 2,792,500 |
| the test disc (`TTEST=1`: both replays, the translated proof's closure) | 4,490,220 |

In the test disc, from the link map (KB of 1,024 bytes): the translated code is 942 KB (704 KB code,
74 KB read-only data, 159 KB exception tables), the scene facade 146 KB, the .NET runtime 89 KB
(19 KB in the game), the replays and the driver 443 KB. Static data (`.data`, `.sdata`, `.bss`,
`.sbss`) is 280 KB against the game's 273 KB; the translated code's own is 4.4 KB.

**Like for like, the translated code is 4 times the hand port's.** The BabySlime's classes
(BabySlimeBehaviour, EnemyBehaviour, Enemy, EnemyStats and its manager, PolyNav2D, PolyNavAgent,
AStar...: 3,223 lines of decompiled C#) link to 374 KB translated; their hand port (3,028 lines of
C++) to 94 KB. Function for function the code is the same size (`SegmentsCross` 204 bytes both,
`EnemyBehaviour.Update` 1,052 both, `PolyNavAgent.LateUpdate` 2,812 against 3,096): the difference
is what the translation adds around the methods. Of the 608 KB of code naming translated types:

| what | share |
|---|---:|
| the game's methods | 31% |
| `cs::List<T>`, one instantiation per element type | 22% |
| thunks for interfaces (virtual bases) | 15% |
| `cs_Message` (SendMessage by name, per class) | 6% |
| `cs::Dictionary` / `HashSet` instantiations | 6% |
| `cs_trace` (the collector, per class) | 6% |
| `cs::Array<T>` instantiations | 4% |
| `cs_SetField` (prefab fields, per class) | 3% |

**The whole game won't fit as it is.** The proof's 12,051 generated lines link to 942 KB, about
80 bytes a line; at that rate the whole game's 270,000 lines would be some 20 MB of code, on a
machine with 24 MB for everything. Most of it is per-type machinery that can be shared (below),
but code size, not speed, is the GameCube's first problem for the translated game.

## Where the time goes (the profile)

The translated replay, 7,575 samples over 758 ms of the timed phases:

| phase | share | inside it |
|---|---:|---|
| collector | 58.6% | the mark and sweep loops 36%; the containers' and fsData's `cs_trace` 25%; a virtual `cs_traced()` per object in the sweep 12% |
| physics | 21.8% | `bind::PhysicsScene::Sync` 64%, its `Rescan` of each body's colliders 52%: `__dynamic_cast` 50%, of which `strcmp` 32% (GCC compares type names in a failed cast); Box2D's own step 24% |
| LateUpdates | 14.3% | PolyNav's geometry: `SegmentsCross` 37%, `PointInsidePolygon` 13%; newlib's software `sqrt` 26%; `Transform.get_position` (the parents walked, every call) 13% |
| Updates | 3.4% | `SetDestination`'s line-of-sight checks 44%; `sqrt` 13%; `Transform.get_position` 13%; `Animator.SetFloat` by name (the name converted from UTF-16, then looked up) 10% |
| Invoke, coroutines | 1.1% | the A* coroutine's line-of-sight checks |
| Animators | 0.4% | `Sprite.Named` looked up by name every frame |

## The biggest costs and their fixes, by gain

Per frame of this scene (4 BabySlimes), in Dolphin's microseconds, estimated from the profile.

**Speed**

1. **Collect less often** (−560 us a frame): the scene makes about 1 KB a frame; a collection
   when the heap has grown by some hundreds of KB (or every few seconds) costs the same 0.57 ms
   rarely instead of every frame. Then make each collection cheaper: keep static data out of the
   mark (the constants as a native table: the hand port's reader, an override; or a permanent
   generation), and mark which objects are traced in the header instead of asking `cs_traced()`.
2. **The physics facade's per-step Rescan** (−115 us): rescan a body's colliders only when one
   is added, removed, enabled or resized, and test their kind with a tag instead of
   `dynamic_cast`. That brings the physics to about the hand port's 85 us.
3. **A fast exact sqrt** (−35 us translated, as much in the hand port): the Gekko has no `fsqrt`
   and newlib computes it bit by bit. `frsqrte`, two Newton steps and a correction with `fmadd`'s
   exact residual give the correctly rounded result (what IEEE and the game's Mono demand) in a
   fraction of the time. A GameCube runtime fix, not a translation one.
4. **Cache world positions** (−25 us): `Transform.get_position` recomputes through the parents
   (in double, as Unity rounds) on every call; dirty flags make it a read.
5. **Cheaper casts** (11.5% of all samples are in `__dynamic_cast`): a class id per generated
   class for `cs::Is/As/Cast`, `dynamic_cast` only for interfaces.
6. **Names to ids** (−5 us): Animator parameters, `Sprite.Named`, `SendMessage`: hash the
   constant names once, as the hand port does.

**Size**

1. **Share the generic code for reference types**: `List<T*>`, `Dictionary`, `Array` over
   `cs::Object*` with a typed inline front, as .NET shares them (up to 193 KB of the 608 KB, and
   most of the thunks).
2. **Interfaces without virtual bases**: 15% is their thunks.
3. **Message dispatch by table** instead of a `cs_Message` per class comparing names (6%).
4. **Fewer exception tables**: 17% of the translated code's bytes; the throwing paths (`nc`, `at`)
   out of line, unwinding only where a `catch` or a `finally` can see it.
5. If the whole game is still too big: code by area, loaded from disc (relocatable modules, as
   GameCube games do).

**Memory**

1. **An override for `FsJSONManager.RemoveComments`**: one pass, the same result: −6.2 MB at the
   peak, −46 ms at load.
2. **The constants as a native table**: 6 ms instead of 58 to load, and 3,700 fewer objects for
   every collection to mark.
3. **Collect right after a scene loads**, so loading garbage never meets the next scene's.

## How far to trust Dolphin's timings

Dolphin's timebase counts its own estimate of the emulated cycles. Chains of one instruction,
timed on it (`CAL` lines in the log), show what it charges:

| instruction | Dolphin | the console (IBM's 750CL manual, about) |
|---|---:|---:|
| `addi` | 1 | 1 |
| `mullw` | 5 | 2 to 5 |
| `divw` | 40 | 19 |
| `fadd`, `fmul` (dependent) | 1 | 3 to 4 (latency) |
| `fdiv` | 31 | 31 to 33 |
| `lwz`, in the L1 cache | 1 | 2 |
| `lwz`, missing both caches (4 MB ring) | 1 | tens of cycles |

So Dolphin counts instructions, a few slow ones weighted, and nothing for latency or cache
misses. The numbers above are close to an instruction count and are a floor for the console: on
real hardware this code will run slower, by an amount these measurements can't give (perhaps 1.5
to 3 times; more for the collector's and the facade's pointer chasing, and for 942 KB of translated
code against a 32 KB instruction cache). The comparisons between the translated code and the hand
port, and the ranking of the costs, are what this measurement is good for. The console's numbers
need the same disc on hardware with the log on the USB Gecko (OSReport overwrites the console's RTC
counter, so `TranslatedTest.cpp` must print through `OctGeckoLog` there).

## What changed for this

- MLGC: `Makefile_GCN`'s `TRANSLATED=1` (the translator run before compiling, `gen_`/`bind_`
  objects), `TTEST=1`, `TTEST_PROFILE=1`, a variant stamp so switching them rebuilds what reads
  them; the builder's "Translated game code" option; `Source/TranslatedTest.cpp`,
  `Source/TranslatedLoad.cpp`; `tools/make_test_data.py`, `tools/dolphin_test.ps1`,
  `tools/ttest_profile.py`.
- Moonlighter-Recomp, the hand-written layers: `unity/Random.cpp` (cos and sin above);
  `cs/Object.h` (`Heap::ForEach`, for the census). Nothing in `recomp/src/bind` or `tools/translate`:
  GCC took the translated code, the facade and the runtime as they are (32-bit `size_t`, `int32_t`
  as `long`, big-endian: no change needed; Octave's `PI` macro: the translated code never shares a
  file with Octave's headers).
- The tests: the enemy replays read their frames as needed (`tests/replay_frames.h`: a recording
  parsed whole is tens of MB), close the recording once read, time their phases
  (`tests/replay_profile.h`) and take a renamed `main()` (`ML_TEST_MAIN`). Their PC output is
  unchanged (31/31, identical).
