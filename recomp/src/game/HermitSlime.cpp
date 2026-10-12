#include "game/HermitSlime.h"

#include "game/Constants.h"
#include "game/HeroDamage.h"
#include "game/MasterAudio.h"
#include "unity/Mathf.h"
#include "unity/Random.h"

namespace game {

using unity::Vector2;
namespace Mathf = unity::Mathf;

void HermitSlimeBehaviourNEW::EnemyRoom_OnRoomEnter()
{
    // (Subscribed to the room's OnRoomEnter while alive, as the base.)
    if (!subscribedToRoomEnter) return;
    enemyState = EnemyState::Idle;
    iDirectionLocked = false;
    canMove = true;
    EnemyBehaviour::EnemyRoom_OnRoomEnter();
}

void HermitSlimeBehaviourNEW::OnDeath()
{
    EnemyBehaviour::OnDeath();
    if (registerDeath) registerDeath();
}

void HermitSlimeBehaviourNEW::ChooseAndApplyAction(float deltaTime)
{
    if (enemyState == EnemyState::Attacking) return;
    // Vector2.Distance(transform.position, heroMerchantTransform.position): the difference
    // stored, its magnitude.
    if (unity::Distance(enemy.position(), HeroPosition()) <= attackDistance) {
        LookToPosition();
        StopEnemy();
        patrolLapse = 0.0f;
        AttackPlayer();
    } else if (canMove) {
        MoveEnemy(deltaTime);
        LookToDestination();
    }
}

void HermitSlimeBehaviourNEW::MoveEnemy(float deltaTime)
{
    if (patrolLapse > 0.0f) {
        patrolLapse -= deltaTime;
        return;
    }
    patrolLapse = static_cast<float>(unity::Random::Range(2, 6));
    ChooseNewDestination();
}

void HermitSlimeBehaviourNEW::StopEnemy()
{
    canMove = false;
    EnemyBehaviour::StopEnemy();
}

void HermitSlimeBehaviourNEW::LockLookDirection()
{
    SetAnimatorTrigger("Attacking", false);
    iDirectionLocked = true;
}

void HermitSlimeBehaviourNEW::EndAttack()
{
    enemyState = EnemyState::Idle;
    UnLockLookDirection();
    canMove = true;
    // enemyAnimator.SetBool("DoAttack", false): the controller has no such parameter (Unity
    // logs it and carries on).
}

void HermitSlimeBehaviourNEW::ChooseNewDestination()
{
    const EnemyScene& room = enemy.scene;
    if (roomGridSize.x == 0.0f) {
        // (Vector2)enemyRoom.transform.position + new Vector2(0, roomCenterYOffset)
        //     - new Vector2(roomFloorWidth, roomFloorHeight) * 0.5f: each Vector2 operator's
        // result stored.
        const Vector2 floor{static_cast<float>(room.roomFloorWidth), static_cast<float>(room.roomFloorHeight)};
        roomGridOrigin = (room.roomPosition + Vector2{0.0f, static_cast<float>(room.roomCenterYOffset)}) - floor * 0.5f;
        roomGridSize = floor;
    }
    // new Vector2(Random.Range(0, (int)x), Random.Range(0, (int)y)) + roomGridOrigin (x drawn first).
    const int x = unity::Random::Range(0, static_cast<int>(roomGridSize.x));
    const int y = unity::Random::Range(0, static_cast<int>(roomGridSize.y));
    destinyPosition = Vector2{static_cast<float>(x), static_cast<float>(y)} + roomGridOrigin;
    destinations++;
    if (enemy.hasNavAgent) enemy.navAgent.SetDestination(destinyPosition);
}

void HermitSlimeBehaviourNEW::LookToDestination()
{
    const Vector2 p = enemy.position();
    const float num = destinyPosition.x - p.x;
    const float num2 = destinyPosition.y - p.y;
    if (Mathf::Abs(num) >= Mathf::Abs(num2)) {
        SetAnimatorFloat("DistanceX", num);
        SetAnimatorFloat("DistanceY", 0.0f);
    } else {
        SetAnimatorFloat("DistanceY", num2);
        SetAnimatorFloat("DistanceX", 0.0f);
    }
}

void HermitSlimeBehaviourNEW::AttackHit(bool otherIsPlayer)
{
    EnemyHero* hero = enemy.scene.hero;
    if (!otherIsPlayer || !hero) return;
    // (Shield-protected, the hero isn't "Shield": nothing.)
    if (hero->isShieldProtected && hero->isShieldProtected(enemy)) return;
    if (hero->isDead) return;
    if (enemy.enemyStats.isAlive()) DealDamageToPlayer(*hero, enemy.enemyStats.strength(), enemy.name, HeroDamageType::ItemDrain, enemy);
    // (effectOnAttack: none on the prefab.)
}

void HermitSlimeBehaviourNEW::AttackHitShield() { PushPlayerBackwards(attackPushBackwardsForce); }

void HermitSlimeBehaviourNEW::SpawnAttackParticles()
{
    if (hasAttackParticles && playAttackParticles) playAttackParticles(enemy);
}

void HermitSlimeBehaviourNEW::PlayCrashSound()
{
    static const char* const kSound = "golem_dungeon_golem_crash";
    if (enemy.scene.audio) enemy.scene.audio->PlaySound(kSound);
    if (enemy.scene.playSound) enemy.scene.playSound(enemy, kSound);
    EnemyHero* hero = enemy.scene.hero;
    if (hero && hero->vibrate)
        hero->vibrate(Constants::GetFloat("defaultStrongEnemyVibrationIntensity"), Constants::GetFloat("defaultStrongEnemyVibrationDuration"));
}

bool HermitSlimeBehaviourNEW::OnAnimationEvent(const unity::AnimationEvent& e)
{
    const std::string& f = e.function;
    if (f == "LockLookDirection") LockLookDirection();
    else if (f == "UnLockLookDirection") UnLockLookDirection();
    else if (f == "EndAttack") EndAttack();
    else if (f == "SpawnAttackParticles") SpawnAttackParticles();
    else if (f == "PlayCrashSound") PlayCrashSound();
    else return EnemyBehaviour::OnAnimationEvent(e);
    return true;
}

std::unique_ptr<HermitSlimeBehaviourNEW> MakeHermitSlimeBehaviour(Enemy& enemy, const Behaviour& fields)
{
    auto hermit = std::make_unique<HermitSlimeBehaviourNEW>(enemy);
    hermit->iDirectionLocked = fields.Field("iDirectionLocked", 0.0) != 0.0;
    hermit->hasAttackParticles = fields.Field("attackParticles", 1.0) != 0.0;
    return hermit;
}

} // namespace game
