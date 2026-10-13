#pragma once
// DungeonRoom's part in its enemies' lives that the enemies' replays need (DungeonRoom.cs):
//
// DisableEnemies (DungeonRoom.cs:951-966): run by ExitRoom (the hero going through a door,
// DungeonManager.ChangeRoomTo; or falling into the hole over a secret room,
// OnPlayerFallsOnHole -> SpecialChangeRoomTo: enemy_BrokenGolemTurret.jsonl, frame 208) and by
// RoomPlaced. For each enemy still in roomEnemies, in that list's order (a copy: nothing changes
// it meanwhile): its behaviour's OnPlayerLeaveRoom (SetVencible; the Broken turret's
// DestroyProjectiles), CancelPlayerTargetingAfterTime (the pending targeting Invoke), then
// gameObject.SetActive(false) (Enemy::SetActive: the agent out of allAgents, the body out of the
// world, its coroutines stopped; its Invokes stay, as Unity's do).
//
// (ExitRoom's other parts: Invoke("DisableRoom", 0.5) turns the room's object off, and with it
// the shots parented to the room that are still out then: not modelled, none was in the
// recording; OnRoomExit's subscribers: none among the ported enemies.)

#include <vector>

namespace game {

class Enemy;
class MasterAudio;

// `roomEnemies`: the room's enemies in its roomEnemies order (the destroyed ones and those
// EnemyDefeated took out are skipped, as they're not in the list).
void DisableEnemies(const std::vector<Enemy*>& roomEnemies);

// DungeonRoom.EnemyDefeated's end (DungeonRoom.cs:904-931), after the enemy left roomEnemies:
// AreAllEnemiesDefeated (no enemy left in roomEnemies but InvulnerableEnemies, none ported; one
// destroyed without EnemyDefeated is still listed; and the room not isLocked) runs
// OnAllEnemiesDefeated, whose handlers InitializeRoom subscribed: UnLockRoomChests,
// OpenAllDoors (the doors' Animators: game/RoomDoors.h), ResetHeroMerchantIsInCombat. Every
// EnemyDefeated with the list empty runs them again.
bool AreAllEnemiesDefeated(const std::vector<const Enemy*>& roomEnemies, bool locked);
// DungeonRoom.UnLockRoomChests: each of roomChests (Chest.chestType as ints, in that list's
// order) unlocked and its PlayUnlockChestSound (game/Chest.h): MasterAudio's draws (recq
// kamifix-3: the KamikazeGolem's SelfDestroy, the room's last enemy, draws the iron chest's
// "dungeon_chest_unlock").
void UnLockRoomChests(const std::vector<int>& chestTypes, MasterAudio* audio);

} // namespace game
