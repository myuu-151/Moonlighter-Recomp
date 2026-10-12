# Floor exits, room transitions, chests, breakables

From a survey of the decompiled classes. Paths are under `decomp/Assembly-CSharp/`.

## Going down a floor

- **Where the door is.** Floors 0 and 1 end in a `NextLevelDoor` (floor 2 has the boss room).
  `AddNextLevelDoor` picks a random free side of the end room; that room gets `isEnd`.
- **Its prefab.** Tag `DungeonLevelDoor`, layer 25, a solid box at (0,17) of 32x30. Entering
  it is a push or a roll, as with any door.
- **Pushing.** `HeroMerchantController.cs:2379-2395` calls `OnPlayerEnter` only if
  `door.isOpened`, and **resets `timePushingDoors` only then**. Pushing a closed door keeps
  the timer running, so he goes through the frame it opens.
- **Rolling.** `DungeonDoor.OnCollisionEnter2D` enters at once while intangible.
- **`NextLevelDoor.OnPlayerEnter`.**
  - Needs the room cleared, or not in combat and not locked.
  - Plays `CloseSpecial`; his walk-in is a 0.5 s linear move.
  - An animation event (`OnDoorClosed`) then loads the next floor. That clip isn't extracted.
- **Loading** (`GameManager.LoadDungeonSceneCoroutine`).
  - A 0.5 s fade out, then the loading panel; floor drops are deleted.
  - The next floor is generated, and a 0.5 s fade in.
- **On the new floor.**
  - He appears 64 into the init room from its `DungeonLastLevelDoor`, facing in, and
    **rolls** (`UseAbility`).
  - HP is restored only on level 0. The bag, equipment, gold and HP carry over.
- **The boss door.** `DungeonBossRoomEnterDoor` behaves like a door. After the boss, its
  chest opens a portal to town.
- **`NextLevelPortal`.** The tag the controller treats like `Hole` belongs to unused content.

## Room transitions

`DungeonDoor.OnPlayerEnter` stops him, then `DungeonManager.ChangeRoomTo` runs:

1. The new room's door colliders go off.
2. The old room exits: its enemies are disabled, and `DisableRoom` follows 0.5 s later.
3. The new room enters: `Invoke("EnableEnemies", enemiesAppearDelay = 0.5)`, `IsInCombat`
   if it has enemies, `CloseAllDoors`, `OnRoomEnter`. Each enemy goes back to its spawn
   point with full health.
4. The camera's DOMove (0.5 s) runs, then the open doors' colliders come back and he can
   move again.

After that:

- **EnableEnemies:** `SetActive(true)` and `StartTargetingPlayerAfterTime(1)`. Enemies show
  up 0.5 s after entry and act at 1.5 s.
- **Doors lock only if** `isLocked || (isDeadEnd && enemies)`. `isDeadEnd` is any of: `isEnd`,
  chests, mimics, or exactly one connected door (and not init or special). A normal room
  with enemies does **not** lock.
- **When the last enemy dies,** `OpenAllDoors`, `UnLockRoomChests` and the end of combat
  follow `OnAllEnemiesDefeated`.

## Chests

- **Locking.** They spawn locked and unlock on `OnAllEnemiesDefeated` (at generation in a
  room without enemies).
- **Interacting.**
  - The facing ActionArea (20x20: Left (-20,4.5), Right (20,4.5), Back (0,30),
    Front (0,-20)) must overlap the chest's 24x48 trigger.
  - Unlocked and not in combat, Action1 **released** within 0.2 s opens it. A press already
    held when the chest came into range doesn't count.
  - While a chest is actionable, Action1 doesn't attack.
- **Opening.** `OpenChest` freezes him for 1.5 s, then the chest panel opens with
  timeScale 0. Items move between the chest and the bag (20 slots, `TryAddItem`: the first
  slot that fits). Nothing drops on the floor.
- **Floor items.**
  - `ItemStack.Drop` draws `Random.Range(0,360)` (int); its tweens run unscaled.
  - Items are picked up on contact (`PickOnWalk`) and attracted within 30, at 48 px/s.

## Breakables (pots: tag HardEnemy, maxHealth 1)

- **When the room first becomes active,** `BreakableObjectBehaviour.Start` draws the flip,
  then the skin.
- **Hits** are the weapon's like any enemy's, with the shake (RNG), the push, the damage and
  the hit-stop. They never count for clearing the room.
- **Breaking.**
  - The broken sprite replaces it, its colliders go and PolyNav regenerates.
  - Its drop: `Random.value < 0.14` (0.9 for a skeleton), then the breakable drop list's
    `GetRandom`, then `Drop`.
  - The list's sort comparator truncates (every weight is below 1), so it always returns 0.
    Forest's 18 items go through introsort and come out permuted.
