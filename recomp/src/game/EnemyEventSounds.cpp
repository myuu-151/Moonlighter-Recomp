// MasterAudio's EventSounds on an enemy's objects (game/Enemy.h: Enemy::enableSounds), the part
// that draws from UnityEngine.Random: its OnEnable, with useEnableSound on, runs
// PlaySounds(enableSound) -> PerformSingleAction for each of its events, and a PlaySound event
// (EventSoundFunctionType.PlaySound) is MasterAudio.PlaySound3DFollowTransform /
// PlaySound3DAtTransform / PlaySound by its soundSpawnMode: the same group, variation and
// SoundGroupVariation.Play as MasterAudio.PlaySound (game/MasterAudio.h's draws). The
// FlyingKamikazeGolem's "SoundsFlyingKamikaze" child plays golem_dungeon_flyingrepair_loop so
// each time the golem is switched on (Instantiate active, DungeonRoom.EnableEnemies'
// SetActive(true)). Its OnDisable (StopAllOfSound) draws nothing.

#include "game/Enemy.h"
#include "game/MasterAudio.h"

namespace game {

void Enemy::PlayEnableSounds()
{
    for (const std::string& s : enableSounds) {
        if (scene.audio) scene.audio->PlaySound(s);
        if (scene.playSound) scene.playSound(*this, s);
    }
}

} // namespace game
