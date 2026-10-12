# Golem dungeon enemies: porting plan

From a survey of the decompiled classes, the extracted data and the `enemies_golem` bundle.
Paths are under `decomp/Assembly-CSharp/`.

## What spawns (floors 1-3)

Every room has one enemy pack; names resolve by path substring `Enemies/Golem/{name}.`
(`DungeonRoom.cs:789-880`). Weighted chance that a drawn room has the enemy (F1 / F2 / F3):

| Enemy | F1 | F2 | F3 |
|---|---|---|---|
| BabySlime (ported) | 14% | 12% | 12% |
| GolemSoldier | 41% | 34% | 25% |
| BrokenGolemTurret | 45% | 22% | 9% |
| FlyingRepairGolem | 29% | 17% | 15% |
| Tangle | 25% | 13% | 6% |
| GolemTurret | 1% | 19% | 27% |
| HermitSlime | 3% | 16% | 25% |
| Slime | 5% | 6% | 8% |
| GolemHead | 1% | 9% | 13% |
| GolemButler | - | 17% | 27% |
| KamikazeGolem | - | 7% | 16% |
| FlyingKamikazeGolem | - | 7% | 11% |

- `ChampionGolemTurret` and `GolemBossGolemHead` have no prefab: nothing spawns. If the
  champion pick lands on one, the room has no champion.
- Champions are `Enemy.TransformToChampion` on any enemy (championProb 0.7).
- The miniboss is `GolemWarrior` (`Golem_Miniboss`).
- Mimics replace 7% of chests (wood on F1, iron on F2, gold on F3), never in timed rooms.

## Watch out

- **GolemTurret's water beams draw `UnityEngine.Random` every frame** from room entry, and
  how many depends on `Time.realtimeSinceStartup` (`WaterMesh.cs:100-128`). This is because
  `DynamicWaterBeam.EndBeamAttack`'s OnComplete re-enables the mesh. Every room with a
  GolemTurret has a non-deterministic RNG stream.
- **Hidden RNG draws:**
  - `DoScreenShakeFromEnemyAnimation` (`Random.insideUnitSphere`, plus `DOShakePosition`).
  - MasterAudio's pitch, volume and variation (`MasterAudio.cs:2206,2432`,
    `SoundGroupVariation.cs:747`).
- **Enemy Animators never get "Damaged":** the Damaged* states are unreachable.

## Shared machinery, in build order

1. **Behaviour factory and fields:** a behaviour factory in `MakeEnemy`, plus each class's
   serialized fields in `tools/extract/prefabs.py` FIELDS.
2. **Animators:**
   - All controllers extracted `--exact`.
   - Animator events routed to the behaviour.
   - Animated `AttackCollider` enabled/size/offset/active/position applied to its fixture.
     Re-enabling a collider fires a new OnTriggerEnter.
   - AnimatePhysics update mode (GolemHead).
3. **Hits:**
   - `AttackHit` takes the other object's tag.
   - EnemyDamageReceiver child hurtboxes, with no push and no hit-stop (`Weapon.cs:327`).
   - DamageType ItemDrain, and MoonlighterEffects on the hero (`ConfusedEffect`).
4. **Physics:**
   - Physics2D `Raycast` and `OverlapCircleAll` that see moving colliders.
   - `OnCollisionEnter2D` with the contact normal.
   - Mass changes, collider toggles, child kinematic bodies.
5. **DOTween:**
   - OutCubic, OutQuint, InExpo, OutExpo and OutSine eases.
   - DOMoveX/Y, `To<Vector2>`, Kill/DOKill.
   - `EnemyBehaviour.MakeJump` (`EnemyBehaviour.cs:272-294`).
6. **RNG side effects:** the screen shake's and MasterAudio's draws (probe them).
7. **Projectiles:**
   - `PoolManager`, keyed `enemy.name + "_" + prefab`.
   - One projectile class for the Broken turret, the Butler and the Gold mimic (a linear
     DOMove, collisions, a crash animator).
8. **Room services:**
   - One-shot `PolyNav2D.FindPath` callbacks.
   - GetRoomBounds, tile and position conversions, obstaclesGrid, OnRoomExit,
     OnDropListChanged.
9. **Mimics:** `Mimic.Start`'s drop lists, taken from the current room's culture.
10. **Items on the floor:** ItemStack drops (for FlyingRepairGolem).
11. **Water beams**, last.

## Order

| # | Enemy | Effort | Notes |
|---|---|---|---|
| 1 | GolemSoldier | S | 52 lines; the attack is clip curves on AttackCollider; no Random of its own |
| 2 | Tangle | S | LateUpdate sine weave (Raycast, then `navAgent.velocity +=`) |
| 3 | HermitSlime | S-M | Patrol draws; a trigger root with a child hurtbox; ItemDrain |
| 4 | FlyingKamikazeGolem | S-M | Body velocity; charge-angle draw with a rotation bug; `Invoke(Range(2,6))` |
| 5 | BrokenGolemTurret | M | Projectiles and the pool of 6; four-direction raycasts |
| 6 | GolemButler | M | Field of view from `IsName`; invincible toggled per frame; pool of 2 |
| 7 | KamikazeGolem | M | MakeJump; Ground/Flying layers; OverlapCircleAll damages enemies |
| 8 | GolemHead | M-L | AnimatePhysics; FindPath; contact normals; stun |
| 9 | Wood, Iron, Gold mimics | M each | Jumps, FindPath, Gold's six projectiles |
| 10 | FlyingRepairGolem | M-L | Steals dropped items |
| 11 | Slime | L | Traps the hero (mash to escape), `realtimeSinceStartup` cooldowns |
| 12 | GolemWarrior | L | `unscaledDeltaTime`; a 122-point annulus PolygonCollider2D rebuilt per tween update |
| 13 | GolemTurret | XL | BFS on the grid; water beams (see above) |

Each enemy's oracle: a generic `-oracle-enemy=<name>` built on `SlimeOracle.cs`. It picks the
room with the most of that enemy and switches the others off.
