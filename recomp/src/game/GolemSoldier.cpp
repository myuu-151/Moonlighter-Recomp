#include "game/GolemSoldier.h"

#include "game/Constants.h"
#include "game/MasterAudio.h"
#include "unity/Mathf.h"

namespace game {

using unity::Vector2;
namespace Mathf = unity::Mathf;

void GolemSoldierBehaviour::ChooseAndApplyAction(float deltaTime)
{
    // transform.position.DiamondDistance(heroMerchantTransform.position + playerOffset) (Vector3s:
    // the sum stored, each difference rounded as Mathf.Abs's argument, their sum a float), against
    // tileSize * 2 + 1 (in double, as Mono has it; the comparison widens the distance).
    const Vector2 p = enemy.position(), h = HeroPosition();
    const Vector2 feet{h.x + playerOffset.x, h.y + playerOffset.y};
    const float distance = Mathf::Abs(p.x - feet.x) + Mathf::Abs(p.y - feet.y);
    const double reach = static_cast<double>(Constants::GetFloat("tileSize")) * 2.0 + 1.0;
    if (static_cast<double>(distance) <= reach) {
        StopEnemy();
        AttackPlayer();
    } else if (canMove) {
        MoveEnemy(deltaTime);
    }
    if (!iDirectionLocked) LookToPosition();
}

void GolemSoldierBehaviour::PlayHitOnGroundSound()
{
    static const char* const kSound = "golem_dungeon_soldier_sword_crash";
    if (enemy.scene.audio) enemy.scene.audio->PlaySound(kSound);
    if (enemy.scene.playSound) enemy.scene.playSound(enemy, kSound);
    EnemyHero* hero = enemy.scene.hero;
    if (hero && hero->vibrate)
        hero->vibrate(Constants::GetFloat("defaultEnemyVibrationIntensity"), Constants::GetFloat("defaultEnemyVibrationDuration"));
}

bool GolemSoldierBehaviour::OnAnimationEvent(const unity::AnimationEvent& e)
{
    const std::string& f = e.function;
    if (f == "LockLookDirection") LockLookDirection();
    else if (f == "UnLockLookDirection") UnLockLookDirection();
    else if (f == "PlayHitOnGroundSound") PlayHitOnGroundSound();
    else return EnemyBehaviour::OnAnimationEvent(e);
    return true;
}

void GolemSoldierBehaviour::OnDeath()
{
    EnemyBehaviour::OnDeath();
    if (registerDeath) registerDeath();
}

} // namespace game
