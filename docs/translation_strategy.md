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
