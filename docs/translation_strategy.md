# Translating the game instead of hand-porting it

Since 2026-10-13 the port's main road is a **translator**: a tool that turns the game's own C# into
C++ mechanically. This document explains why, how the pieces fit together, what "exact" means for
translated code, what changes for the GameCube, and how the translator's output can be brought to
hand-port quality. The translator's technical design is in `docs/translator_plan.md`, written by the
lane that builds it.

## Why

Until now every class was ported by hand and proven against the real game with its own oracle
recording, frame by frame, bit for bit. That works. The dungeon generator, the hero's movement and
combat, and several enemies match the game exactly. It is also slow:

- 160,682 lines of the game's own C# count toward the port. About 11,800 (7.3%) are ported after
  the first stretch of work, so about 150,000 are left.
- Each class takes reading, porting, a recording and, almost always, a mismatch hunt: a body asleep
  one frame early, a different wall picked at a corner. The hunts are where most of the time goes,
  and they don't parallelise.
- At that pace the game logic alone is months away.

A translator converts all of it at once. The game logic is then right **by construction**: it is the
same code, so there's no need for a recording per class to prove that someone retyped it correctly.

## The pieces of the finished port

Nothing in the finished port uses Unity or Mono. It is a native program built from four parts:

| Part | What it is | Where | Who writes it |
|---|---|---|---|
| Game code | Moonlighter's own logic: enemies, items, shop, town, UI scripts, saves | generated into a gitignored folder (`recomp/gen/`) | the translator, from your copy of the game |
| `.NET` runtime | the pieces of the C# standard library the game uses: strings, `List`, `Dictionary`, delegates, LINQ | `recomp/src/cs/` | hand-written |
| Unity replacement | our own C++ versions of the Unity functions the game calls: `Animator`, `Rigidbody2D` and 2D physics, `Random`, `Invoke` and coroutines, DOTween, UI, scenes | `recomp/src/unity/` (and services in `recomp/src/game/`) | hand-written |
| Platform layer | drawing, sound, input, files | GameCube: the [MLGC](https://github.com/myuu-151/Moonlighter-GC) repo (Octave, GX); PC: still to write | hand-written |

The assets (sprites, animations, rooms, sounds) are extracted from your own Steam copy by
`tools/extract` and the MLGC builder, as they are today.

"The Unity layer" in our notes means the third row: **our replacement for the parts of Unity the game
uses**, not Unity itself. The game calls `animator.SetTrigger("Attack")` or `Random.Range(0, 5)`;
those calls land in our C++, which behaves exactly like Unity's. It is the same idea as Wine
reimplementing the Windows API, or the N64 decompilation projects reimplementing Nintendo's
libraries.

## How the translator works

```
Moonlighter_Data/Managed/Assembly-CSharp.dll   (your copy of the game)
        |  ICSharpCode.Decompiler (the engine of ILSpy, which made decomp/)
        v
C# syntax trees  +  Roslyn semantic model (every symbol resolved, with the game's other DLLs as references)
        |  tools/translate (C#, .NET 9)
        v
C++17 in recomp/gen/  ->  compiled with recomp/src/cs (the .NET pieces) and recomp/src/unity (the Unity replacement)
```

- **Input is the game's DLL**, decompiled in-process, so the build needs only your copy of the game,
  like the data does.
- **The analysis is semantic, not textual.** Roslyn knows each expression's type, every overload,
  every implicit conversion, which is what exact translation needs.
- **The output compiles with MSVC (PC) and GCC (devkitPPC, GameCube).**
- **Generated code is never committed.** The repos hold only the tool, the hand-written libraries
  and the hand-ported classes, never game code or anything decompiled.

## What "exact" means for translated code

Being "the same code" is only true if every C# rule is reproduced exactly. These are the rules that
change results, and how the translation keeps them:

| C# / Mono behaviour | Why it matters | Translation |
|---|---|---|
| Mono evaluates `float` expressions in `double` and rounds to `float` at stores, casts, calls and returns | damage, speeds, positions: one rounding difference spreads | emit the double-then-round form the hand port already uses (`unity/Mathf.h`'s `D`/`F` helpers) |
| `int` overflow wraps | hashes, random seeds, counters | wrapping arithmetic (C++ signed overflow is undefined) |
| `/` and `%` on ints; `float` to `int` conversion | grid maths, damage rounding | C#'s rules, including `int.MinValue / -1` and out-of-range conversions as Mono x86-64 gives them |
| `Dictionary` / `HashSet` enumeration order | the game iterates dictionaries and the order decides what happens first | a faithful copy of .NET's layout (buckets, entries, free list: a removal then an insertion reuses the slot) |
| `string`: UTF-16, ordinal vs culture rules, `float.ToString` / `Parse` | save files, item names, sorting | .NET's rules under the culture the game runs in |
| delegates and multicast events | the order handlers run in | an invocation list with .NET's `+=` / `-=` semantics |
| `yield` / `IEnumerator` | coroutines and iterators | generated state machines, as the C# compiler makes them |
| LINQ | lazy evaluation can be observable | lazy, as in .NET (lowered to loops when provably equivalent, see below) |
| `struct` vs `class`, boxing | copies vs shared objects | value types stay values, reference types stay references |
| static constructors | initialisation order | .NET's rules (`beforefieldinit`, first access) |
| exceptions | the game catches some | `try` / `catch` / `finally` kept |
| Unity's `== null` on destroyed objects | a destroyed object compares equal to `null` | the Unity replacement implements that check |

The hand-ported, oracle-verified classes are the test of these rules. Translate a class we have
already proven, run the same recording, and get the same bits. That is the translator's
acceptance test (below).

## Verification

The byte-exact bar doesn't change; where it's checked does:

- **The translator:** translated versions of already-verified classes must reproduce their oracle
  results exactly. First the dungeon generator (cave, room, fill and floor tests), then BabySlime
  (`slime`, `slime_bis`), then GolemSoldier. The build gets an opt-in switch (`-DML_TRANSLATED=ON`)
  to swap translated classes in for hand-ported ones.
- **The Unity replacement:** this is hand-written, so this is where real differences can come from.
  It keeps being proven by recordings: fewer, richer ones that exercise each Unity feature rather
  than one per game class. The recording queue (`tools/recq`, `docs/recording_queue_plan.md`) runs
  them unattended, on a hidden desktop (`tools/oracle_launch.ps1`).
- **Spot checks of the whole:** recordings of real play (a dungeon floor, a shop day) replayed
  against the translated game.

When translated code and a hand port disagree, the recording decides which one is wrong.

## The GameCube: what changes

The Unity replacement and the platform layer are the same for hand-ported and translated code. The
game code itself differs in three ways on the GameCube (24 MB of main RAM, a 485 MHz Gekko):

1. **Size.** A naive translation emits every class and some helper code per construct. 150,000 lines
   of C# could compile to several MB.
2. **Memory management.** C# frees memory automatically and the game relies on it everywhere. The
   GameCube has no garbage collector, so the translation needs a scheme. Reference counting,
   per-scene pools, a small collector or a mix all cost something; done badly they leak or fragment,
   and on 24 MB that means crashes. This is the biggest GameCube-specific risk.
3. **Speed.** Generic code carries overhead a hand port avoids: per-object bookkeeping, temporary
   strings, LINQ chains, extra checks. Most of the game won't notice; per-frame hot spots might.

Two issues apply to both approaches: the GameCube is **big-endian** (anything reading raw bytes, like
saves, needs care), and GCC may **fuse multiply-adds** on PowerPC, which changes float results
(exactness needs `-ffp-contract=off`).

## Bringing the output to hand-port quality

A translator doesn't have to emit naive code. It can do what a person porting by hand does,
automatically:

1. **Translate only what's used.** Start from what the game loads (scenes, prefabs, the scripts on
   them, reflection and `Invoke` targets) and follow every call. Editor tools, other platforms' code
   and dead features are never emitted.
2. **Work out ownership.** Most objects have an obvious owner: a component belongs to its
   game object, a temporary list lives in one function. Where the tool can prove it, it emits plain
   C++ ownership (on the stack, or held by a parent). Reference counting remains only for genuinely
   shared objects, which removes most of the memory overhead.
3. **Rewrite expensive patterns the way a person would, with the same results:**
   - LINQ chains become loops when they're provably equivalent;
   - constant strings used as keys (`SetTrigger("Attack")`, `GetComponent<T>()`, sound names)
     become precomputed hashes or direct references, as the hand port already does;
   - virtual calls with one possible target become direct calls;
   - `foreach` over a `List` becomes an indexed loop with the same version check where it matters.
4. **Free exactness.** The rounding and wrapping rules compile to a cast or to nothing, the same as
   hand-written code.
5. **Hand-written overrides.** The tool keeps a map of "use this hand-written class instead". The
   classes already hand-ported and verified go there first (the hero, the enemies, PolyNav, the
   dungeon generator); then anything profiling on the GameCube flags as hot.

With these the output comes close to a hand port: somewhat bigger, slower in a few places, the same
structure. None of the five changes results, so they can come after the translator is correct
without losing anything. The order is correctness first, then these, guided by measuring a real
GameCube build.

### How much overhead is left

Not zero, but small, and mostly in places that don't matter:

- **Objects whose ownership can't be proven.** Objects passed around and stored in several places
  keep reference counting: a few instructions per copy. Most objects aren't like that.
- **Exactness costs, which a hand port pays too.** Copying .NET's exact `Dictionary` layout, UTF-16
  strings (twice the memory of plain text) and C#'s rounding rules cost a little memory and speed,
  but a hand port needs most of them anyway to match the game. That is the price of exactness, not
  of translation.
- **Conservatism.** When the game calls something by name (`Invoke("Explode")`, `SendMessage`), the
  tool can't always tell what's reachable, so it keeps a bit more code than a person would. That
  costs size, not speed.
- **Safety checks.** C# checks array bounds and nulls. The tool drops the checks it can prove
  unnecessary; the rest stay, and they're cheap.

**Expected** (the guess before a GameCube build): within roughly 10–30% of a hand port in size
and speed overall, concentrated in a few spots. Those spots get the hand-written overrides, so they
end up exactly as fast as a hand port. Most of the game (menus, shop logic, dialogue, item data)
runs rarely enough that the difference will never show on screen.

**Measured** (2026-10-13, the first GameCube build of translated code: the BabySlime's oracle
replay, translated and hand-ported, in Dolphin's Accurate profile, timed on the Gekko's timebase;
`docs/gamecube_translated.md` has the method and the profile). The translated BabySlime matches the
game on the GameCube, every row as on the PC. Then:

| | translated | hand port | |
|---|---:|---:|---|
| speed: the enemies' Updates (4 slimes, per frame) | 34.2 us | 26.5 us | 1.29 times: the guess held for the scripts |
| speed: all the game's work per frame | 423 us | 227 us | 1.86 times: the scene facade's physics is 2.6 times the hand port's |
| speed: the collector at every frame end | 566 us | (none) | 83% of what it marks is static data (the constants' tree) |
| size: the same classes, linked | 374 KB | 94 KB | 4 times: the methods compile to the same size; generics per type, interface thunks and per-class dispatch make up two thirds |
| memory: live objects / made per frame | 4,468 / 4 | | small; but 6.2 MB of garbage at load, from the game's own RemoveComments |

So: speed is within the guess where it is the game's code, and outside it in the layers around it
(the facade's physics, the collector run every frame), which are hand-written and fixable. Size is
far outside it: at this density the whole game is about 20 MB of code, more than the GameCube can
hold with its art, so sharing the generic code and slimming the per-class machinery come before any
speed work. Dolphin's timebase counts instructions, not cache misses or latencies: on the console
everything is slower, the pointer-heavy translated code and the collector likely more so. The fixes,
ranked by what they save, are in `docs/gamecube_translated.md`.

### Fitting in 24 MB of RAM and 16 MB of ARAM

The game fits; the 20 MB above is what today's unoptimised translator emits, not what the game
needs:

- **20 MB is the unoptimised translator's output, not the game's real size.** The same code
  hand-ported is about 4 times smaller, and the extra is duplicated machinery the translator can
  share: one body for each generic over reference types (as .NET does), interfaces without virtual
  bases, MonoBehaviour messages from a table, tracing from per-type field maps, exception tables
  only where the game catches.
- **Not all code has to be in RAM at once.** Town, each dungeon and the UI screens can load as
  separate code modules (RELs, as commercial GameCube games do), so only the current area's code
  sits next to what every area shares.
- **Data streams.** Sprites and sounds come from the disc per area, with ARAM as the cache and the
  sound bank, as a previous GameCube port on the same engine did.

The rough target budget:

| | RAM (24 MB) | ARAM (16 MB) |
|---|---|---|
| Resident code | ~3-5 MB | - |
| Current area's code module | ~1 MB | - |
| Engine (Octave) | ~2.3 MB | - |
| Game heap (objects, scene) | ~3-4 MB | - |
| Textures in use | ~6-8 MB | spill / cache |
| UI (open screens) | ~2-3 MB | closed screens |
| Audio | small mixing buffers | SFX bank ~8-10 MB |
| Streaming buffers (music, loads) | ~1 MB | music stream buffers |

That's a target, not a measurement yet: the next step is a whole-game GameCube build of the
translated code, shrunk as above, whose measured numbers replace this table
(`docs/gamecube_translated.md`).

### The GameCube optimisation pass

Like the Castle Crashers GameCube port, the GameCube build gets hand-optimised beyond what the
original game does. MLGC is built on the same Octave-libogc base, so the memory and streaming work
done there (texture formats, budgets, the audio RAM as a cache, streaming from disc) carries over.
Two kinds of optimisation, with one rule between them:

1. **Doing the same thing faster: always fine.** A hot class can be rewritten completely: different
   data structures, precomputed tables, no allocations, batching for GX. It must give the same
   results, and here that is checked: the hand-optimised version has to pass the same oracle
   recordings as the original code. That's what the translator's override list is for.
2. **Doing less, on the GameCube only: fine, with care.** Fewer particles, halved soft light and
   shadow layers, simpler effects, cheaper UI transitions. Presentation-only cuts are free as long as
   they don't touch game logic. The trap: some effects draw from Unity's shared `Random` when they
   spawn (the screen shake, sound variations). Cutting one of those draws shifts every random number
   after it and changes dungeons, drops and enemy behaviour. Cut the visual, keep the draw. The
   recordings catch any slip.

The data comes from real hardware: a USB Gecko streams frame times per system (enemy updates,
physics, pathfinding, drawing) and memory high-water marks per area from the console. (The first
numbers came from Dolphin: MLGC's `TTEST=1` disc times the translated and hand-ported replays phase
by phase, `TTEST_PROFILE=1` samples where the time goes; `docs/gamecube_translated.md`.) That decides
which code gets an override and how much art each area can keep in memory. (In DolphinWorks:
install the FTDI driver, and enable Gecko log output for hardware builds.)

## The hand-ported code

The ~11,800 hand-ported lines aren't wasted:

- They are the **acceptance tests** for the translator, since they're proven against the game.
- They are the first **overrides**: hand-tuned versions of hot code for the GameCube.
- Their engine-side work (the Animator, Box2D with Unity's settings, DOTween's eases, `Invoke` in
  fixed steps, MasterAudio's random draws) **is** the Unity replacement the translated code runs on.

Hand-port lanes for new game classes stopped on 2026-10-13. The unfinished ones (GolemHead, Slime,
the chest mimics, GolemWarrior) are kept as branches (`agent-golemhead`, `agent-slime`,
`agent-mimics`). KamikazeGolem had already finished and is merged.

## What's left, after the translator

The translator turns "port 150,000 lines" into a finite checklist: **every Unity API the game calls
that the replacement doesn't have yet**. The translator lane writes that list to
`progress/unity_api_gaps.tsv`, with the lines of game code each gap blocks. The big items, in
roughly the order the game needs them:

- **UI:** Unity's canvas, layout, text rendering and the widgets the menus use. Probably the largest
  single piece.
- **Scenes:** loading and switching between the dungeon, the town and the shop; game objects
  created from prefabs at runtime.
- **Saves:** file input/output (big-endian on the GameCube).
- **Audio:** MasterAudio's playback, beyond the random draws already matched.
- **Input:** the game's input layer, mapped to a keyboard or pad on PC and the GameCube pad.
- **Rendering on PC:** a small renderer (SDL or Octave's Windows target); the GameCube already
  draws through GX.
- **GameCube memory:** streaming each area's art from disc, and the memory model above.

## Roadmap

| Step | What | Status (2026-10-13) |
|---|---|---|
| 1 | The translator, correct: tool, `.NET` runtime library, Unity binding, the survey (`class_deps.tsv`, `unity_api_gaps.tsv`) | building (wave 2) |
| 2 | Acceptance: translated dungeon generator and BabySlime match their recordings exactly | part of step 1 |
| 3 | The recording queue, so lanes get recordings without waiting on a person | building (wave 2) |
| 4 | Fix the Unity replacement's open mismatches: sleeping bodies, `Invoke` timing, MasterAudio draws, contacts | building (wave 2: two lanes) |
| 5 | Translate the whole game; work through `unity_api_gaps.tsv` | next |
| 6 | A playable PC build: renderer, input, audio, saves | next |
| 7 | The optimisations above, measured on the GameCube; memory streaming | after 6 |

**Timing.** With the translator, the game logic goes from months to days once the tool works. The
remaining weeks go into the Unity replacement (UI first) and the platforms. A playable PC build is a
matter of weeks; the GameCube follows, mostly limited by memory work. This estimate will firm up once
the translator's first survey shows how much converts cleanly.

## Decisions on record

- **2026-10-12:** everything byte-matched against the real game, UI and town included.
- **2026-10-13:** translate the game's code instead of hand-porting it class by class. Exactness stays
  the bar, for the translation's semantics and for the Unity replacement.
- **Proposed, not adopted:** small tolerances for physics positions, and behaviour-level (not
  frame-level) checks for UI. They would remove most mismatch hunts, but would give up the claim that
  every frame matches. They stay open in case they're needed later.
