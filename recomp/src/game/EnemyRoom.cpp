#include "game/EnemyRoom.h"

#include "game/Enemy.h"

namespace game {

void DisableEnemies(const std::vector<Enemy*>& roomEnemies)
{
    const std::vector<Enemy*> list = roomEnemies; // new List<Enemy>(roomEnemies)
    for (Enemy* item : list) {
        if (!item || item->destroyed || !item->inRoomEnemies) continue;
        if (EnemyBehaviour* b = item->enemyBehaviour.get()) {
            b->OnPlayerLeaveRoom();
            b->CancelPlayerTargetingAfterTime();
        }
        item->SetActive(false);
    }
}

} // namespace game
