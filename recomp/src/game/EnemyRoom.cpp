#include "game/EnemyRoom.h"

#include "game/Chest.h"
#include "game/Enemy.h"
#include "game/MasterAudio.h"

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

bool AreAllEnemiesDefeated(const std::vector<const Enemy*>& roomEnemies, bool locked)
{
    for (const Enemy* e : roomEnemies)
        if (e && e->inRoomEnemies) return false;
    return !locked;
}

void UnLockRoomChests(const std::vector<int>& chestTypes, MasterAudio* audio)
{
    for (int type : chestTypes) {
        // roomChest.isLocked = false (the chests aren't this port's), PlayUnlockChestSound.
        const char* sound = UnlockChestSound(type);
        if (sound && audio) audio->PlaySound(sound);
    }
}

} // namespace game
