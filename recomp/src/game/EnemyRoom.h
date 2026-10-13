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

// `roomEnemies`: the room's enemies in its roomEnemies order (the destroyed ones and those
// EnemyDefeated took out are skipped, as they're not in the list).
void DisableEnemies(const std::vector<Enemy*>& roomEnemies);

} // namespace game
