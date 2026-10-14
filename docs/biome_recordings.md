# Enemy recordings beyond the Golem dungeon

The oracle's enemy recordings (`-oracle-enemy=<prefab>[:<room>]`) were all made in the Golem
dungeon. This lane makes them in the other three dungeons, so the translated enemies of the
Forest, the Desert and Tech can be checked as the Golem ones are. Paths below are under
`decomp/Assembly-CSharp/` unless they name a folder of this repository.

Status, 2026-10-14 01:25 (session ended): the Forest is recorded except a few hero-not-attacking variants and the wood mimic; the Desert has 3 of 12 enemies recorded; Tech has none. The next steps are at the end.

## How a run reaches each dungeon

Every dungeon is reached the same way, the way the Golem runs always went:

1. `Harvest.LoadSlot` (oracle): the main menu's Continue on the first used save slot. The
   news popup is answered "Later" (`InGameNewsPopup.Disable`), never "Read More".
2. `Harvest.InstallSafety` (oracle): from then on nothing is written. Every
   `IPersistenFilesController.WriteBytes` / `WriteBytesAsync`, `GameManager.SerializeGameSlot`
   and the Steam stats and achievements are skipped. This matters here because
   `GameManager.LoadDungeonScene` saves the slot on the way into a dungeon
   (`SaveSlotOnGoToDungeon(serialize: true)`, GameManager.cs:1752). The changes the run makes
   to the slot in memory (floors visited, the rival unlocked on Golem floor 2) are never saved.
3. `HeroMerchant.LoadDungeonScene(culture)` (HeroMerchant.cs:583) for floor 1, then
   `LoadNextLevelDungeonScene(culture)` for floors 2 and 3, as the next-floor door does.
   `-oracle-dungeon=<Golem|Forest|Desert|Tech|Unknown>` picks the culture (default Golem;
   `Tesla` is accepted for Tech; anything else fails the run with a `FAILED` marker).

The game's own debug paths do exactly this. `HUDDebug.cs:484-500` and
`MoonlighterConsoleDebugPanel.cs:601-613` call `HeroMerchant.Instance.LoadDungeonScene` with
each culture. Nothing below `HeroMerchant.LoadDungeonScene` checks progress. The gate lives
only in the town: `WorldDungeonDoor.UpdateByCulturesDefeated` (WorldDungeonDoor.cs:68-94) locks
a dungeon door past the first culture not yet in the slot's `culturesDefeated` bits. The oracle
never uses those doors, so it changes no progress and patches nothing of the game.

What the slot's progress still changes inside a dungeon:

- The Wanderer (`DungeonManager.SpawnWanderer`, DungeonManager.cs:408) only comes in a
  dungeon whose culture isn't defeated yet, after a long Invoke. A recording lasts about 25 s,
  so it never shows up.
- `DungeonBossRoom.isBossDefeated` (DungeonBossRoom.cs:91) affects the floor 3 boss room.
- The enemies' stats don't depend on it. `EnemyStats.Init` takes the level, the time of day,
  the difficulty, NG+, the DLC and the dungeon's culture, all of which are in the start line's
  `stats`.

The start line now records the slot's `progress` (`culturesDefeated`,
`maxDungeonLevelVisited`), so a recording says what the town's gates would have allowed.

The harvest (`-oracle-harvest`) has gone through all four dungeons this way since 2026-10-12:
`trace.jsonl`'s `harvestFloor` lines cover cultures 1 to 4, floors 1 to 3.

## Making a recording

Agents never run the game; they ask the recording queue (`tools/recq/README.md`):

```
powershell -NoProfile -ExecutionPolicy Bypass -File C:/Users/legen/Documents/moonlighter/tools/recq/request.ps1 \
    biome-forest-ForestSlime-1 -Worktree C:/Users/legen/Documents/ml-wt/biomes \
    -OracleArgs "-oracle-dungeon=Forest -oracle-enemy=ForestSlime" -Expects enemy_ForestSlime.jsonl -Timeout 1200
```

- `-oracle-enemy=<prefab>[:<room>]` works in any dungeon. Without a room it takes floor 1, then
  floors 2 and 3 through the next-floor door until a floor has the prefab. Floor 1 holds the
  floor's miniboss room (`DungeonGenerator.GetMinibossRoom`, level 0), floor 2 its corrupted
  version (level 1).
- `-oracle-tangle` takes `-oracle-dungeon` too. It records every TangleBehaviour (the Forest's
  are ForestTangles) with the Tangle oracle's LateUpdate weave rows, into `tangle.jsonl`, and
  its start line names the prefab. `tangle_test <file> ForestTangle` replays it.
- The BabySlimes of the other dungeons (ForestBabySlime, DesertBabySlime, TechBabySlime) run
  the ported BabySlimeBehaviour (game/EnemyFactory.cpp picks the culture from the prefab's
  name). `-oracle-enemy=ForestBabySlime` and `enemy_test <file> ForestBabySlime` replay them.
- The recordings stay in `moonlighter-recq/done/<id>/` (backed up in
  `moonlighter-oracle-backup/`) and in this worktree's `data/oracle/` (gitignored).

## What a recording holds now

Every row the Golem replays compare is still there: the generic mode writes the same lines
(`enemyStart`, `collider`, `navMap`, `rngProbe`, `enemy`, `frame`). The Golem replays still pass
25/25 with identical output, and they ignore the new keys. `-oracle-slime` (legacy names) is
unchanged.

The new rows (oracle `EnemyOracleBiome.cs`, `EnemyOracleQueries.cs`) cover what the other
dungeons' enemies do beyond a plain EnemyBehaviour. They are written for every `-oracle-enemy`
run, Golem ones included.

| Row | Where | What |
|---|---|---|
| `dungeon`, `progress`, `soundCreators`, `spawnPrefabs` | start line | The culture asked for; the slot's progress; MasterAudio's DynamicSoundGroupCreators in the order they made their groups (the first group of a name wins); what the enemies can make (the prefabs their scripts reference, two levels deep, and the pools named after the enemy) |
| `gen` / `gv`, `gc`, `act`, `ga`, `gs` | each enemy (setup, each frame end) | Every instance field of its game scripts, from the class down to MonoBehaviour (Enemy, PolyNavAgent and the sound and mesh scripts are left to the other rows). Also every Collider2D below it (enabled, trigger, layer, offset, shape, world position, rotation, scale), its objects' active flags, its other Animators (state, time) and its SpriteRenderers' sprites. The names come in the setup line, and again in any frame where its objects or scripts changed (an effect parented to it) |
| `spawn`, `despawn` | events | An object the enemies made (a shot, a puddle, an effect) seen active: by its scripts' Awake/OnEnable/Start, or a pool's object put into the room. `despawn` when it is destroyed or back in its pool |
| `olife`, `ohit`, `ocall` | events | Those objects' lifecycle messages, physics messages (the other's path, tag, layer, and its enemy id if it is one), and the methods their clips name, each with the Random state before and after |
| `o` | each fixed step | Each active made object with a body or collider: transform, body position, velocity, awake |
| `objs` | each frame end | Each made object: prefab (first row), active, world and local position, rotation, body, Animator, sprite, and its `gen` |
| `spawnEnemy` | events | An enemy the enemies made (a ForestSeeder's SeederCutting through `DungeonRoom.SpawnEnemy`, anything `Enemy.Init` runs on): it joins the recorded ones with the next id, its setup line in `line`; from then on it is in every row |
| `canim` | events | A clip event's method on a script other than the behaviour: the sound scripts' `Play...`, `Enemy.SpawnEnemyDeadSprite`, a child Animator's scripts (the FruitTree's roots) |
| `effect`, `heroEffect`, `heroForce` | events | `MoonlighterEffect.Create` (poison, venom, the Fertilizer's DoT: the effect, its target, who called), `HeroMerchant.AddEffect`, `HeroMerchantController.AddForce` |
| `src`, `by` | `dmgToPlayer` | Its damageSource (`DamageOverTime`, the enemy's name) and the script that called |
| `heroFx` | each frame end | speedFactor, isTrapped, canMove, canAttack, moved externally (the ForestSlime's trap), intangible, and his active Effects |
| `rt` | `updates`, frame | `Time.realtimeSinceStartup` at each recorded Update's start and at the frame's end (Slime2_0Behaviour's timers run on it) |
| `query` | events | Each Physics2D query (every Raycast/BoxCast/CircleCast/Linecast/Overlap* overload) a recorded enemy's or a made object's script made: its arguments and the game's answer (collider path, point, normal, distance, fraction) |

Costs, as measured. A Forest floor 1 recording runs at 60 fps like the Golem ones; ForestBabySlime
ran 1635 fixed steps in 968 frames. The first version scanned with FindObjectsOfType at every
fixed step and dropped floor 2 to about 25 fps (recq `biome-forest-ForestLandscaper-1`). It also
patched its hooks in the frame before the start, giving frame 0 a one-second delta. Both are
fixed in commit 5dfdd20. The recordings made before it are valid, since every delta is recorded,
but they ran slower than real play.

## What the replays read today

Both enemy replays read a recording of any dungeon:

- **Start line.** `stats.dungeonCulture` and `soundCreators` choose MasterAudio's groups
  (`recomp/tests/sound_creators.h`). The Golem recordings get exactly the lists they had; a
  Forest recording gets `Sounds - Will`, `Sounds - interface`, `Sounds - ForestDungeon`,
  (`Sounds - DesertDungeon` when the floor had a corrupted room of that culture),
  `Sounds - GeneralDungeons`.
- **Room.** The room's colliders and the PolyNav map come from the recording, as for the Golem
  rooms.
- **Prefabs.** `PrefabLibrary::Find(name, "enemies_golem")` already falls back to any bundle,
  so `enemies_forest/<prefab>` is found. The animator is `animators/<prefab>.json`.
- **Stats.** `enemy_stats.json` holds every dungeon's enemies.

`enemy_test <recording> <prefab>` on each Forest recording:

- It reads the start, builds the enemies and runs to the end.
- "stats from enemy_stats.json (Init)", "agent settings (prefab)" and "Rigidbody2D settings
  (prefab, stats)" match for every one.
- **ForestBabySlime: ALL MATCH.** The hand port's BabySlimeBehaviour, with the Forest culture
  from the prefab's name, plays the Forest recording frame for frame.
- **ForestTangle (`tangle_test <tangle.jsonl> ForestTangle`): ALL MATCH.** That includes the
  weave and its raycasts.
- The others run with a plain EnemyBehaviour, since game/EnemyFactory has no port of their
  classes and none is planned: the translator will run them. Their behaviour rows differ from the
  first frame the class acts, as expected.

The translated replay (`enemy_translated_test`, `-DML_TRANSLATED=ON`) shares the sound creators
and the prefab lookup. Its Golem tests still pass (5/5). A Forest enemy needs its classes in
`tools/translate/units.txt` and what they call bound in the facade; see the last section.

## The extracted data

What the replays need of the other dungeons, and what was already there:

- **Sound groups** (`tools/extract/sound_groups.py`): already every creator
  (`Sounds - ForestDungeon`, `- DesertDungeon`, `- TeslaDungeon`, `- UnknownDungeon`,
  `- Wanderer`, `- GeneralDungeons`, `- Will`, `- interface`). No change.
- **Rooms** (`rooms.json`, `roomdb_asset.json`): already every culture's rooms (171 Forest, 165
  Desert, 137 Tech), the corrupted lists and the miniboss rooms. No change.
- **Prefabs** (`tools/extract/prefabs.py`): the rows of `enemies_forest`, `enemies_desert` and
  `enemies_tesla` were there, but their scripts' fields were empty: only the classes in `FIELDS`
  get fields. Now every script of those three bundles that `FIELDS` doesn't name gets every
  serialized field (`raw_field`): raw values; references to the prefab's own objects as their
  paths; other assets as `{"ref", "type"}` (a projectile prefab: its name). The other bundles'
  rows are unchanged; checked by comparing the new `prefabs.json` against the old.
- **Animators** (`tools/extract/animators.py`): after the `TARGETS`, every prefab of those three
  bundles whose root plays an Animator, keyed by the prefab's name (`animators/<prefab>.json`,
  `sprites/enemies/<prefab>/`). That is 61 controllers: the enemies, their shots, puddles,
  trails and explosions. `--no-biomes` keeps it to the `TARGETS`.

The port's Animator can't play 10 of them; the extractor reports and skips each:

| Prefab | Why |
|---|---|
| Cutting, SeederCutting (Forest) | two layers (the head aims on its own layer) |
| FruitTree, FruitTree_Corrupted (Forest minibosses) | a clip with two sprite curves (the tree and its roots) |
| ForestMimicIronChest, DesertMimicIronChest, TechMimicIronChest | `BiteAttackDown` has two sprite curves |
| MagicianPuppet (Desert) | `BeginDash` is a blend tree |
| Oscillator, OscillatorCorrupted (Tech minibosses) | `Idle` has two sprite curves |

Four more extract with curves the extractor can't name:

- LavaCanonBall: a MonoBehaviour field, type 114.
- DeathGenerator, ElectricPost: ParticleSystem curves, type 198.
- ForestMimicWoodChest: a path CRC.

## What the current replays can't compare yet

The recordings carry these rows; no replay uses them yet. The translated replay is the one to
extend: the behaviours aren't hand-ported.

1. **The behaviours themselves.** The hand port has BabySlime and Tangle, so their Forest
   versions match. Every other Forest class is the translator's. As of `progress/class_deps.tsv`:
   - translate whole: WindTreeSproutBehaviour, FruitTurretBehaviour, VenomousTangleBehaviour,
     CuttingBehaviour, SeederCuttingBehaviour, DamageOverTimeEffect, BoostEffect.
   - partly: Slime2_0Behaviour 80%, ForestLandscaperBehaviour 75%, SporeBushBehaviour 76%,
     BladeTreeSproutBehaviour 73%, SpiningMushroomBehaviour 61%, FruitTreeBehaviour 98%,
     FruitTreeRoot 58%, AreaEffect 79%, and the shots 30-81%.
   - what blocks them: `Physics2D.Raycast` / `BoxCast` and `RaycastHit2D`, `Collision2D.contacts`,
     `Rect` members, `Collider2D.bounds`, DOTween's `Pause` / `OnUpdate`, `Transform.Rotate`,
     `MasterAudio.StopBus` / `StopAllOfSound`, ParticleSystem modules.
2. **What the enemies make.** There is no replay of shots, puddles, trails and effects:
   `spawn`, `despawn`, `o`, `objs`, `olife`, `ohit`, `ocall`. A replay would have to tie the
   objects the translated code makes (its Instantiate, its PoolManager) to the recording's ids.
   They come in creation order; a pool's objects carry the pool's name (`<enemy>_<prefab>`, the
   prefab in `spawnPrefabs`). Then it compares their bodies per step and their state per frame.
   The hand port's turret replay does this for one prefab (`turret_replay.h`); it needs to
   become generic.
3. **Enemies the enemies make** (`spawnEnemy`: the ForestSeeder's SeederCuttings, 15 in recq
   `biome-forest-ForestSeeder-1`). `enemyById` skips ids past the start's. The replay must adopt
   what the translated `DungeonRoom.SpawnEnemy` makes, in order, from its `line`.
4. **The generic state** (`gen`/`gv`/`gc`/`act`/`ga`/`gs`, `cb`): nothing compares it yet. It
   is what the per-class rows of the Golem replays (the hermit's patrol, the turret's look, the
   kamikaze's jump) compare by hand. A translated replay can compare it field by field, by name.
5. **Collisions.** The facade has no `OnCollisionEnter2D` yet (translator_plan.md). The Forest
   shots (FruitTurret, WindTreeSprout, SporeBush, Cutting, BladeTree leaf) hit by collision
   (`ohit` with `OnCollisionEnter2D`). The ForestSeeder's own `OnCollisionEnter2D` is dead code.
6. **Bodies and colliders the replay's world doesn't make.**
   - A static enemy body: SpiningMushroom's Rigidbody2D is Static, its root box a trigger.
   - A kinematic one: Cutting.
   - The VenomousTangle's tentacle PolygonCollider2D trigger.
   - The FruitTree's dynamic root body with two boxes.
   - The hero's ActionArea triggers. Queries hit triggers (`queriesHitTriggers`), and SporeBush
     and the BladeTree leaf look for the `ActionArea` tag in their line-of-sight raycasts.

   The `query` rows give the game's answers to compare against.
7. **Clocks.**
   - Slime2_0Behaviour's attack and expel timers read `Time.realtimeSinceStartup`. It is
     recorded at each Update's start and at the frame's end (`rt`), not at each read.
   - AreaEffect's `InvokeRepeating` (never cancelled, so it stacks when a pooled puddle comes
     back) is seen only through its effects.
8. **The hero's side.**
   - The ForestSlime's trap moves him with `MoveAutomaticallyToPoint`. The replays set his body
     from the recording each step, so this is fine.
   - DoT effects (`effect`, `heroFx.effects`, `dmgToPlayer` with `src` `DamageOverTime`): the
     replays' hero is a stand-in. The DoT ticks from AreaEffect have no enemy stats, so they
     deal nothing.
   - The Fertilizer's BoostEffect on an enemy: probably a NullReferenceException in the game
     (an uninitialized `_modificator`); the `gen` rows show what the game really did.
9. **Animators the port can't play** (the extractor's table above): the minibosses' clips with
   two sprite curves, the Cuttings' two layers, MagicianPuppet's blend tree. Also child
   Animators (the FruitTree's roots, in `ga`): the replays drive one Animator per enemy.

## The recordings

All of them are in `C:/Users/legen/Documents/moonlighter-recq/done/<id>/`, which also holds
`result.json`, the log and the oracle's build log. The ones marked ok are copied into this
worktree's `data/oracle/forest/` and `data/oracle/desert/`, which is gitignored.

- "oracle" is the commit of agent-biomes the queue built the oracle from.
- "joined" counts the enemies the enemies made.
- "objects made" counts the shots, puddles, effects and explosions tracked.

| id (`done/<id>/`) | oracle args | result | oracle | room | floor | frames | enemies | joined | objects made | hits / deaths |
|---|---|---|---|---|---|---|---|---|---|---|
| `biome-desert-ClothTangle-2` | `-oracle-dungeon=Desert -oracle-enemy=ClothTangle` | ok | 253f498 | Desert_HolesRocks_16 | 1 | 968 | 2 | 0 | 26 | 5 / 1 |
| `biome-desert-DesertBabySlime-2` | `-oracle-dungeon=Desert -oracle-enemy=DesertBabySlime` | ok | 253f498 | Desert_Holes_6 | 1 | 968 | 4 | 0 | 8 | 4 / 4 |
| `biome-desert-DesertSlime-2` | `-oracle-dungeon=Desert -oracle-enemy=DesertSlime` | ok | 253f498 | Desert_Tutorial_Desert_Slime | 3 | 968 | 1 | 0 | 0 | 0 / 0 |
| `biome-forest-BladeTreeSprout-1` | `-oracle-dungeon=Forest -oracle-enemy=BladeTreeSprout` | ok | 4b07cda | Forest_Chest_1 | 1 | 968 | 1 | 0 | 3 | 0 / 0 |
| `biome-forest-ForestBabySlime-1` | `-oracle-dungeon=Forest -oracle-enemy=ForestBabySlime` | ok | 5dfdd20 | Forest_Holes_8 | 1 | 968 | 2 | 0 | 4 | 2 / 2 |
| `biome-forest-ForestLandscaper-1` | `-oracle-dungeon=Forest -oracle-enemy=ForestLandscaper` | ok | 3bc7c43 | Forest_Update_1_8 | 2 | 968 | 1 | 0 | 4 | 2 / 1 |
| `biome-forest-ForestSeeder-1` | `-oracle-dungeon=Forest -oracle-enemy=ForestSeeder` | ok | 3bc7c43 | Forest_Update_Puddle_1_18 | 1 | 968 | 2 | 15 | 102 | 11 / 10 |
| `biome-forest-ForestSlime-1` | `-oracle-dungeon=Forest -oracle-enemy=ForestSlime` | ok | 3bc7c43 | Forest_Update_Puddle_1_18 | 1 | 968 | 1 | 0 | 1 | 1 / 1 |
| `biome-forest-ForestSlime-noatk-1` | `-oracle-dungeon=Forest -oracle-enemy=ForestSlime -oracle-hero-no-attacks` | ok | 4b07cda | Forest_Update_Puddle_1_22 | 2 | 968 | 1 | 0 | 2 | 0 / 0 |
| `biome-forest-ForestTangle-tangle-1` | `-oracle-dungeon=Forest -oracle-tangle` | ok | 5dfdd20 | Forest_Tutorial_Forest_Tangle | 1 | 728 | 1 | 0 | 0 | 2 / 1 |
| `biome-forest-FruitTree-3` | `-oracle-dungeon=Forest -oracle-enemy=FruitTree` | ok | 4b07cda | Forest_Miniboss | 1 | 968 | 1 | 0 | 1 | 7 / 1 |
| `biome-forest-FruitTree-4` | `-oracle-dungeon=Forest -oracle-enemy=FruitTree` | ok | 253f498 | Forest_Miniboss | 1 | 968 | 1 | 0 | 6 | 7 / 1 |
| `biome-forest-FruitTree_Corrupted-3` | `-oracle-dungeon=Forest -oracle-enemy=FruitTree_Corrupted` | ok | 253f498 | Corrupted_Forest_Miniboss | 2 | 968 | 1 | 0 | 10 | 8 / 0 |
| `biome-forest-FruitTurret-1` | `-oracle-dungeon=Forest -oracle-enemy=FruitTurret` | ok | 5dfdd20 | Forest_HolesRocks_3 | 1 | 968 | 1 | 0 | 6 | 2 / 1 |
| `biome-forest-SpiningMushroom-1` | `-oracle-dungeon=Forest -oracle-enemy=SpiningMushroom` | ok | 5dfdd20 | Forest_Tutorial_Forest_Spinning_Mushroom | 1 | 968 | 1 | 0 | 0 | 0 / 0 |
| `biome-forest-SporeBush-3` | `-oracle-dungeon=Forest -oracle-enemy=SporeBush` | ok | 253f498 | Forest_Chest_20 | 2 | 968 | 2 | 0 | 34 | 4 / 2 |
| `biome-forest-VenomousTangle-1` | `-oracle-dungeon=Forest -oracle-enemy=VenomousTangle` | failed (oracle-failed) | 5dfdd20 |  |  |  |  |  |  | 0 / 0 |
| `biome-forest-VenomousTangle-2` | `-oracle-dungeon=Forest -oracle-enemy=VenomousTangle` | ok | 4b07cda | Forest_Update_Puddle_1_24 | 1 | 968 | 1 | 0 | 3 | 3 / 1 |
| `biome-forest-WindTreeSprout-1` | `-oracle-dungeon=Forest -oracle-enemy=WindTreeSprout` | ok | f7f812a | Forest_Rocks_6_Vertical | 1 | 968 | 2 | 0 | 5 | 2 / 2 |
| `biome-forest-WindTreeSprout-2` | `-oracle-dungeon=Forest -oracle-enemy=WindTreeSprout` | ok | fc41ccb | Forest_T_1 | 1 | 968 | 2 | 0 | 12 | 3 / 2 |

Notes:

- **ForestBabySlime and DesertBabySlime** replay ALL MATCH in `enemy_test`.
- **ForestTangle** (`tangle.jsonl`) replays ALL MATCH in `tangle_test ... ForestTangle`.
- **Before 5dfdd20** (ForestLandscaper-1, ForestSeeder-1, ForestSlime-1, WindTreeSprout-1/-2),
  the recordings ran slower than real play: floor 2 at about 25 fps, and a second-long frame 0.
  They are valid, since every delta is recorded, but worth re-recording.
- **WindTreeSprout-1** has no `query` rows and no dungeon sound creators (fixed in fc41ccb).
- **FruitTree-3** lacks its fruits (fixed in 253f498): use FruitTree-4.
- **VenomousTangle-1 failed.** Its build let a breakable pot join the enemies (fixed in
  9a6c8e8), and the recording stopped after its start lines. The coordinator ended it after
  50 minutes. During its second attempt the agent wrote a `FAILED` note into its `out/` folder,
  hoping to end it early. The runner doesn't read that file during a run, so it changed nothing.
- **Enemies the hero kills at once.** With the save's Forest gear the hero kills most Forest
  enemies in one or two hits, so their own attacks are short. `-oracle-hero-no-attacks` shows
  their whole behaviour: in ForestSlime-noatk-1 the hero is trapped for 372 frames, with
  ReleasePlayer and its raycast.
- **BladeTreeSprout-1:** the first 100 frames ran slow (the machine was busy). Its leaf's three
  Update raycasts aren't among the queries; Mono probably inlined that overload.
- **The save** (slot 1487371392) has `culturesDefeated` 7: Golem and Forest beaten, the Desert
  open, Tech locked at the town's gate. `maxDungeonLevelVisited` is Golem 2, Forest 2, Desert 1,
  Tech -1. The Forest and Desert floors had "Tutorial" rooms (`Forest_Tutorial_Forest_Tangle`,
  `Desert_Tutorial_Desert_Slime`).

## Next steps

A fresh agent picks up with the oracle at agent-biomes HEAD; no oracle change is pending.

Ask the queue a few at a time, with `-Timeout 600 -Retries 1`. Look at each result
(`inspect_rec`-style: events, spawns, joined, queries; the log for "not joined" or exceptions)
before the next batch.

1. **Forest, still to record:**
   - Hero-not-attacking variants (`-oracle-hero-no-attacks`, ids `...-noatk-1`): ForestLandscaper
     (Acid and Fertilizer, the BoostEffect), ForestSeeder, WindTreeSprout, SporeBush (its shots:
     SporeBush-3 has jumps only), FruitTree (the root attack), FruitTurret.
   - ForestMimicWoodChest. Mimics are 7% of the chests, so the run may fail with "no room":
     that's an answer too.
   - Re-record ForestLandscaper, ForestSeeder, ForestSlime and WindTreeSprout with attacks on
     HEAD (better frame pacing).
2. **Desert, still to record** (`-oracle-dungeon=Desert -oracle-enemy=<prefab>`):
   - FirePuppet, LavaCatapultGolem, KatamariGolem, BardPuppet, MagicianPuppet, MotherGolem
     (MotherGolemElemental is what it makes), PatrolGolem.
   - The minibosses FireRhombus (floor 1) and FireRhombusCorrupted (floor 2).
   - DesertSlime-2 recorded nothing of its attacks (floor 3, the hero never reached it): try
     `-oracle-hero-no-attacks` or another room.
3. **Tech** (`-oracle-dungeon=Tech`): DeathGenerator, ElectricPost, ElectromagneticBall,
   GraafGenerator, Recharger, RepairDrone, TechBabySlime (replay it with `enemy_test`, as the
   Forest and Desert ones), TechSlime, TechTurret, TeslaTangle, TechMimicWoodChest (one room,
   `Tech_Update_1_5`, floors 2-3), the minibosses Oscillator and OscillatorCorrupted. The save
   has never been in Tech (`maxDungeonLevelVisited` -1): watch the first run for a panel or
   cutscene the floor-1 setup doesn't dismiss.
4. **Copy and check.** Copy each ok recording into `data/oracle/<dungeon>/` and run
   `enemy_test <file> <prefab>`: it must read the start, build the enemies and run to the end.
   The BabySlimes and the Tangles must stay ALL MATCH. Update the table above.
5. **Not done (the translator lane's):** the replays of what the enemies make (`spawn`, `objs`,
   `o`) and of joined enemies, the `gen` rows' comparison, and the Forest classes in
   `units.txt`. See "What the current replays can't compare yet".
