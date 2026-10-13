![Moonlighter](docs/banner.png)

# Moonlighter recomp

Native C++ reimplementation of Moonlighter (Steam, v1.15.8, Unity 2019.2.7f2 / Mono),

It also builds for the GameCube:
[Moonlighter GC](https://github.com/myuu-151/Moonlighter-GC) compiles this code for the console.

<!-- progress:start -->
## Progress

![ported](https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/myuu-151/Moonlighter-Recomp/main/progress/badge.json)
![verified](https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/myuu-151/Moonlighter-Recomp/main/progress/badge_verified.json)

**7.3%** of the game's own code is ported (11,762 of 160,682 lines of C#, 41 of 1,173 classes whole); **6.5%** is verified against the real game by the oracle's recordings. Third-party libraries, platform code and editor tools aren't counted; partly ported classes count by an estimated fraction (`progress/ported.tsv`).

<img src="progress/bars/total.svg" alt="7.3% ported, 6.5% verified">

<sub>gold: ported · green: verified against the game</sub>

| Area | Lines | Ported | Verified | |
|---|---:|---:|---:|---|
| Dungeon generation | 10,310 | 50.2% | 50.2% | <img src="progress/bars/dungeon_generation.svg" alt="50.2%"> |
| Player | 9,277 | 22.0% | 20.9% | <img src="progress/bars/player.svg" alt="22.0%"> |
| Enemies | 27,342 | 12.2% | 9.2% | <img src="progress/bars/enemies.svg" alt="12.2%"> |
| Dungeon runtime | 5,487 | 5.7% | 3.4% | <img src="progress/bars/dungeon_runtime.svg" alt="5.7%"> |
| Bosses | 8,285 | 2.3% | 2.3% | <img src="progress/bars/bosses.svg" alt="2.3%"> |
| Items & effects | 13,388 | 0.6% | 0.0% | <img src="progress/bars/items_effects.svg" alt="0.6%"> |
| Town, shop & villagers | 12,097 | 0.0% | 0.0% | <img src="progress/bars/town_shop_villagers.svg" alt="0.0%"> |
| UI | 20,716 | 0.0% | 0.0% | <img src="progress/bars/ui.svg" alt="0.0%"> |
| Wanderer DLC | 9,474 | 0.9% | 0.9% | <img src="progress/bars/wanderer_dlc.svg" alt="0.9%"> |
| Core & other | 44,306 | 1.2% | 0.8% | <img src="progress/bars/core_other.svg" alt="1.2%"> |

_Updated by `tools/progress.py` on every commit._ On Windows, `py tools/tray_progress_startup.py` puts the same numbers next to the taskbar clock (`tools/tray_progress.ps1`), starting with Windows.
<!-- progress:end -->

## Layout

| Path | What | In git |
|---|---|---|
| `progress/` | The port's progress: the game's classes and line counts, what's ported, the badges (`tools/progress.py`, refreshed by the `tools/hooks/pre-commit` hook) | yes |
| `Moonlighter/` | Copy of the Steam install (+ BepInEx, `steam_appid.txt`) | no |
| `decomp/` | ILSpy output of `Assembly-CSharp*.dll` | no |
| `data/` | Oracle recordings, asset scans, downloads | no |
| `oracle/MoonlighterOracle/` | BepInEx plugin that records reference data inside the game | yes |
| `recomp/` | The C++ port and its tests | yes |
| `tools/make_data.py`, `tools/extract/` | Makes the data from your Steam copy (UnityPy) | yes |
| `tools/assets/` | Asset inventory / texture budget scripts (UnityPy) | yes |

## Data from the game

    python tools/make_data.py [--game <the game's folder>]   # needs: py -m pip install UnityPy

Finds the game (`--game`, `ML_GAME`, else Steam app 606150), checks some of its files are
the Steam copy's (v1.15.8), and extracts into `data/extracted/` (git-ignored), each checked
against what the oracle recorded in the running game:

| File | From | Check |
|---|---|---|
| `constants.json`, `rooms.json`, `specialRooms.json` | `Resources` text assets (`resources.assets`) | same text |
| `roomdb_asset.json` | `DungeonRoomDatabase` (`dungeon_common`) with `database` overwritten by `rooms`, as `LoadFromJSON` does | equal to the live object but for the prefab reference |
| `generation_data_counts.json` | each culture's `DungeonRoomGenerationData` | same list sizes |
| `details.json` | each culture/floor's `DetailsPrefabRegister` | same sizes and probability images, 12/12 |
| `bundle_assets.json` | the bundles' `.manifest` files | enemy lookups: scene check |
| `wanderer_specialRooms.json` | the DLC bundle inside `dlc/wanderer_dlc` | Wanderer floor check |

The tests read game data from there (`ML_DATA` overrides) and only the recordings from
`data/oracle/`. MLGC's builder runs the same extractors.

## Oracle

Build (copies the DLL into `Moonlighter/BepInEx/plugins`):

    dotnet build -c Release oracle/MoonlighterOracle

Record reference data (game boots, records, quits; Steam must be running):

    Moonlighter/Moonlighter.exe -screen-fullscreen 0 -screen-width 640 -screen-height 360 -oracle-run -oracle-quit

Writes to `data/oracle/`: `rng.jsonl`, `cave_mirror.jsonl` (3000 layouts from the game's
own code, seeds 1..1000 x floors 0..2), `constants.json`. Without `-oracle-run` the plugin
stays passive and appends every dungeon floor generated in real play to `cave_game.jsonl`,
and traces each floor room by room into `trace.jsonl` (every room pick with weights, every
fill stage with the RNG state before and after, spawned enemies/chests, and a snapshot of
the generated objects).

Harvest floors automatically (loads the first save slot like *Continue*, then plays floors
1-3 of each dungeon N times through the game's own transitions; saving and Steam
stats/achievements are blocked for the session, the main-menu news popup is answered "Later",
and the game throttles itself to Idle
priority on 2 cores):

    Moonlighter/Moonlighter.exe -screen-fullscreen 0 -screen-width 640 -screen-height 360 -oracle-harvest=3 -oracle-quit

Report (HTML page with test results, floor maps and room contents):

    python tools/report/build_report.py   # -> data/report/dungeon_report.html

## Port

    cmake -S recomp -B recomp/build -G "Visual Studio 18 2026" -A x64
    cmake --build recomp/build --config Release
    recomp/build/Release/cave_oracle_test.exe data/oracle

GameCube: the same sources compile with devkitPPC (`-mogc -mcpu=750 -meabi -mhard-float`).
**Always build game code with `-ffp-contract=off`**: GCC otherwise fuses multiply-adds
(it does in `Random::Range(float)`) and results drift from the original.

**Strategy:** since 2026-10-13 the game's own C# is translated to C++ by a tool (`tools/translate`)
instead of being ported class by class; the hand-written work is our replacement for the parts of
Unity the game calls. Why and how, the exactness rules, the GameCube concerns and the roadmap:
[`docs/translation_strategy.md`](docs/translation_strategy.md).

## Exactness notes (things the oracle caught or confirmed)

- `UnityEngine.Random` is xorshift128 seeded MT-style (`s[i+1] = s[i]*1812433253+1`).
  `value` = `(next & 0x7FFFFF) / 8388607f`; `Range(float a, float b)` = `t*a + (1-t)*b`
  (note the order); `Range(int)` = `min + next % (max-min)`. Verified bit-exact.
- The game seeds the global RNG once per save-slot load; everything draws from one stream.
- `List<T>.Sort` must be Mono's introsort (not stable): a stable sort changes 6/3000 layouts.
  Its depth limit comes from the backing array *capacity*, so `cs::List` tracks .NET growth.
- `BossRoomMiner.Init` aliases `generator.miners` and removes from it; copying the list
  breaks every floor-3 layout.
- Game bugs preserved on purpose: timed chests never spawn (`Range(0, 1)`), `ResetValues`
  checks `num2` where `num4` was meant, `GenerateNextLevelCaveRoom` treats (0,0) as "none".
- Room weights get x0.001 per pick and reset each floor through `LoadFromJSON`, which
  overwrites the database with the `rooms` text. It goes through a boxed copy of the
  struct, but the room objects are shared, so it does reach them. The asset's own
  serialized rooms are stale (weights already penalised, older zones), so the text is what
  counts: `tools/extract/room_database.py` merges it the same way.
- `AddLightShadow` aliases the culture's sprite list and the next room's `SetLights` clears
  it: only a floor's first room draws a light/shadow. Registers' `UnloadBundlesExcept` has an
  inverted test (unloads only the given culture), so other cultures' lists stay emptied for
  the session; corrupted rooms show it.
- Room names aren't unique (two `Golem_Enemies_13`); identify designs by database index.
- The room prefab's default `obstaclesGrid` is 20x11; `LoadGridFromInfo` makes 20 + 20x11.
- Hidden engine-side draws per prefab (enemy `Init`, `Chest.Awake` = fixed drops + capacity-1
  shuffle, decorations) are measured by `fill_oracle_test` and listed in the report.

## Status

- [x] RNG, dungeon layout (cave graph + special rooms): 3000/3000 layouts match
- [x] Layout check against real play captures (`cave_game.jsonl`)
- [x] Room selection (`GetRandomRoom`, contiguous-room exclusion, miniboss, weights)
- [x] Room fill, data stages: `LoadGridFromInfo`, champion pick, chest tier/mimic,
      `AddLightShadow`, hole rasteriser, slime/acid/lava/electric
- [x] `Chest.Awake` fixed drops + shuffle (47/47 chests)
- [x] Enemy `Init` (Tangle family), `SpawnRoomPrefabs` skeleton roll, `SpawnDetails`,
      middle-detail zones, floor-texture cache
- [x] Random chest contents (`game/ChestGeneration`): 40/40 calls, same items/quantities/effects
- [x] Whole-room sync (`room_sync_test`): every checkable room replays from its first draw to
      its last with no resyncs, random-content chests included (314/314; the rest are early
      traces missing inputs)
- [x] Whole-floor sync (`floor_sync_test`, `game/DungeonFloor`): layout -> room selection -> every
      room's fill -> secret rooms -> next-floor door -> boss room (save-dependent post-boss
      rolls) -> Desert hard-enemies shrine (2/2 same room and tile), 24/24 floors; resyncs only
      at chests from early traces without `chestGen` data
- [x] Wanderer DLC post-process (`DungeonFloor::WandererPostprocess`): random-comparison
      shuffle of holes rooms -> intermediate secret room -> advisor. It runs after frame-timed
      asset loading, so the test searches the start; 16/16 floors pin one start that picks the
      game's host and ends in the floor's final state. Room data:
      `data/extracted/wanderer_specialRooms.json` (from `Moonlighter/dlc/wanderer_dlc`, an
      asset bundle nested in a TextAsset)
- [x] Scene check (`floor_sync_test`, `game/RoomScene`): what generation places in each room
      (enemy spawns and enemies, chests and mimics, rocks, wall details, breakables, creatures,
      particles, and the floor/walls/light/shadow sprites) against the game's end-of-floor
      snapshot: 554/554 rooms in every category. Missing enemy prefabs are skipped like the
      game does (`PrefabCatalog`).
- [x] Whole rooms from the prefabs (`game/Prefabs`, `RoomSceneTree`): every object of the
      room's hierarchy, doors and special doors included, against the game's snapshot. 95.2% of
      the objects with a sprite match on path, active flag, local position and sprite; 82.1%
      of all objects. The rest are changed at runtime by code that isn't ported yet
- [x] Boss rooms' `Start()` draws ported (`game/BossRoomStart`), replacing the measured counts:
      36/36 floors in sync from layout to last room, boss rooms included
- [x] Runtime textures bit-exact (`game/Composite`, CRC against the game's): holes 86/86,
      LowerDetail 181, BaseDetail 109, MiddleDetail 89, slime 8, acid 6, lava 4, electric 9.
      Needs Unity's DXT decode (565 by bit replication, floor interpolation, three-colour blocks
      in DXT5 too)
- [x] All game data made from the Steam copy (`tools/make_data.py`), none from oracle dumps
- [x] Rooms drawn (`render/`): sprites, sorting layers and the painted textures, the same on the
      PC (`recomp/tools/preview`) and the GameCube
- [ ] `GenerateSounds` and the other frame-timed waits (draws there depend on frame timing;
      not generation)
- [x] Will (`game/HeroMerchantController`, `unity/Animator`): walking, the corner slide, doors,
      the roll, falling into holes and back, and the main attack with every weapon type's
      combo (the weapon's own Animator, hitbox and sprite: `game/Weapon`), replayed against the
      game frame by frame (`player_walk_test`: walk, roll, diagonals, fall, attack; positions,
      Animator states and times, sprites and hitboxes bit for bit). The Animator keeps
      normalized time as Unity does and samples the clips' curves exactly
- [x] Box2D 2.3.1 for his Rigidbody2D's contacts (`unity/PhysicsWorld`), the layer matrix
- [x] PolyNav (`game/PolyNav*`): the enemies' pathfinding map, A* and agents
- [ ] Enemies' AI (BabySlime first), combat damage both ways, town and shop, UI, saves, audio
