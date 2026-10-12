![Moonlighter](docs/banner.png)

# Moonlighter recomp

Native C++ reimplementation of Moonlighter (Steam, v1.15.8, Unity 2019.2.7f2 / Mono),
aimed at a GameCube build on Octave-libogc. Game logic is translated from the decompiled
C# and checked against the real game by an injected oracle, rather than by eye.

It also builds for the GameCube:
[Moonlighter GC](https://github.com/myuu-151/Moonlighter-GC) compiles this code for the console.

## Layout

| Path | What | In git |
|---|---|---|
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
      snapshot: 14,589 objects, same names and float-exact positions, 370/370 rooms. Missing
      enemy prefabs are skipped like the game does (`PrefabCatalog`).
- [x] All game data made from the Steam copy (`tools/make_data.py`), none from oracle dumps
- [ ] Boss scripts' `Start()` draws: still the measured counts (Golem 132, Forest 72)
- [ ] `GenerateSounds` and the other frame-timed waits (draws there depend on frame timing;
      not generation)
