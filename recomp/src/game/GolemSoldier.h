#pragma once
// GolemSoldierBehaviour.cs: the Golem dungeon's sword soldier, an EnemyBehaviour that stops
// to swing. While targeting the hero, every frame (ChooseAndApplyAction):
//   - within the diamond distance (|dx| + |dy|) of 2 tiles + 1 of his feet (his transform
//     plus playerOffset), it stops (StopEnemy) and attacks (AttackPlayer: Attacking, the
//     Animator's trigger);
//   - else, if it can move, it chases him (MoveEnemy, EnemyBehaviour's);
//   - and, unless its look is locked, it faces him (LookToPosition: DistanceX / DistanceY).
//
// The swing is its Animator's (animators/GolemSoldier.json): from a Floating state the
// trigger takes it to the Attack state of its direction, whose clip (1.083 s) fires
// ForbidEnemyMovement at 0, LockLookDirection at 0.5, DoScreenShakeFromEnemyAnimation and
// PlayHitOnGroundSound at 0.667, AllowEnemyMovement and UnLockLookDirection at 1.0, and drives
// the "AttackCollider" child: its box comes on from 0.583 to 0.833 as it grows (to 48 x 24
// or 24 x 48) and moves to where the sword lands. Its end goes back to Floating, where the
// trigger (set every frame he's still in range) starts the next swing.
//
// Its death counts in HeroStatistics (RegisterGolemSoldierDeath: registerDeath).

#include <functional>

#include "game/Enemy.h"

namespace game {

class GolemSoldierBehaviour : public EnemyBehaviour {
public:
    explicit GolemSoldierBehaviour(Enemy& enemy) : EnemyBehaviour(enemy) {}

    bool iDirectionLocked = false; // serialized (the prefab's false)
    // HeroStatistics.RegisterGolemSoldierDeath.
    std::function<void()> registerDeath;

    void ChooseAndApplyAction(float deltaTime) override;
    void LockLookDirection() { iDirectionLocked = true; }
    void UnLockLookDirection() { iDirectionLocked = false; }
    // MasterAudio.PlaySound("golem_dungeon_soldier_sword_crash") and the pad's rumble
    // (defaultEnemyVibrationIntensity, defaultEnemyVibrationDuration).
    void PlayHitOnGroundSound();
    bool OnAnimationEvent(const unity::AnimationEvent& e) override;

protected:
    void OnDeath() override;
};

} // namespace game
