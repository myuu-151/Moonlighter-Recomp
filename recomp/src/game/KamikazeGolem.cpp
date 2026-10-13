#include "game/KamikazeGolem.h"

#include <algorithm>

#include "game/Constants.h"
#include "game/MasterAudio.h"
#include "game/PolyNav.h"
#include "unity/Mathf.h"
#include "unity/Physics2DOverlap.h"
#include "unity/Random.h"
#include "unity/Tween.h"

namespace game {

using unity::Vector2;
using unity::mono::D;
using unity::mono::F;

GolemKamikazeBehaviour::~GolemKamikazeBehaviour()
{
    // (The game never unsubscribes: a destroyed one's handler finds its Animator gone.)
    if (EnemyHero* hero = enemy.scene.hero) {
        auto& subs = hero->enteredInCombat;
        subs.erase(std::remove_if(subs.begin(), subs.end(), [this](const auto& s) { return s.first == this; }), subs.end());
    }
}

void GolemKamikazeBehaviour::Init()
{
    EnemyBehaviour::Init();
    if (EnemyHero* hero = enemy.scene.hero) {
        // HeroMerchant.Instance.EnteredInCombat += HeroMerchantEnteredInCombat (once here: a
        // second Init would add it again, which only sets the trigger twice).
        if (!subscribedToCombat) hero->enteredInCombat.emplace_back(this, [this] { HeroMerchantEnteredInCombat(); });
        subscribedToCombat = true;
    }
    if (enemy.hasEnemyRoom) subscribedToRoomExit = true; // enemy.enemyRoom.OnRoomExit += OnExitRoom
}

void GolemKamikazeBehaviour::OnDeath()
{
    EnemyBehaviour::OnDeath();
    if (registerDeath) registerDeath();
    subscribedToRoomExit = false;
}

void GolemKamikazeBehaviour::EnemyRoom_OnRoomEnter()
{
    // (Subscribed to the room's OnRoomEnter while alive and not self-destroyed, as the base.)
    if (!subscribedToRoomEnter) return;
    EnemyBehaviour::EnemyRoom_OnRoomEnter();
    enemy.enemyStats.SetInvencible(false);
    enemy.enemyStats.SetCurrentHealth(enemy.enemyStats.maxHealth());
    canMove = true;
    enemyState = EnemyState::PursuingPlayer;
    SetLayer(kLayerGroundEnemy);
    if (unity::DelayedCalls* calls = enemy.scene.calls) calls->Invoke(this, "MoveEnemy", 0.65f, [this] { MoveEnemy(0.0f); }, 0.65f);
}

void GolemKamikazeBehaviour::OnExitRoom()
{
    if (!subscribedToRoomExit) return;
    if (unity::DelayedCalls* calls = enemy.scene.calls) calls->CancelInvoke(this, "MoveEnemy");
}

void GolemKamikazeBehaviour::HeroMerchantEnteredInCombat()
{
    // enemyAnimator != null (a destroyed object's is null); an inactive object's Animator
    // isn't playing: SetTrigger does nothing there.
    if (enemy.destroyed || !enemy.active) return;
    SetAnimatorTrigger("Awake", true);
}

void GolemKamikazeBehaviour::ChooseAndApplyAction(float)
{
    // Vector2.Distance(transform.position, heroMerchantTransform.position).
    const float num = unity::Distance(enemy.position(), HeroPosition());
    if (enemyState != EnemyState::Attacking && D(num) <= D(enemy.navAgent.stoppingDistance) + 20.0) {
        EnemyDashTowardsThePlayer(dashForce);
        AttackPlayer();
    }
    if (canMove) {
        if (num <= distanceToPlayerToStartChasing) JumpTowardsPlayer();
        else LookToPosition();
    }
}

void GolemKamikazeBehaviour::MoveEnemy(float)
{
    moveEnemyCalls++;
    if (onMoveEnemy) onMoveEnemy(*this, false);
    const float num = unity::Distance(enemy.position(), HeroPosition());
    if (canMove) {
        SetAnimatorTrigger("Attacking", false);
        if (num > distanceToPlayerToStartChasing) MoveRandomly();
    }
    if (onMoveEnemy) onMoveEnemy(*this, true);
}

void GolemKamikazeBehaviour::MoveRandomly()
{
    // transform.position + new Vector3(c.x, c.y, transform.position.z) * Random.Range(60, 90):
    // the circle first, then the range (an int, as a float); each component one product, one sum.
    const Vector2 c = unity::Random::insideUnitCircle();
    const Vector2 p = enemy.position();
    const float range = static_cast<float>(unity::Random::Range(60, 90));
    const Vector2 scaled{c.x * range, c.y * range};
    const Vector2 vector{p.x + scaled.x, p.y + scaled.y};
    randomMoves++;
    lastWander = vector;
    lastWanderValid = CheckIfDestinationPointIsReachable(vector);
    if (lastWanderValid) enemy.navAgent.SetDestination(vector);
}

void GolemKamikazeBehaviour::JumpTowardsPlayer()
{
    canMove = false;
    enemy.navAgent.Stop();
    const float time = 0.3f;
    const float num = 10.0f;
    const float height = 0.0f;
    // transform.position + (heroMerchantTransform.position - transform.position)
    //     + (Vector3)Random.insideUnitCircle * num (Vector3 operators: each component rounded).
    const Vector2 p = enemy.position(), h = HeroPosition();
    const Vector2 toHero{h.x - p.x, h.y - p.y};
    const Vector2 at{p.x + toHero.x, p.y + toHero.y};
    const Vector2 c = unity::Random::insideUnitCircle();
    const Vector2 scatter{c.x * num, c.y * num};
    const Vector2 endPosition{at.x + scatter.x, at.y + scatter.y};
    SetAnimatorTrigger("Jump", true);
    enemy.enemyStats.SetInvencible(true);
    SetLayer(kLayerFlyingEnemy);
    jumps++;
    MakeJump(endPosition, height, time, [this] { SetLayer(kLayerGroundEnemy); });
}

void GolemKamikazeBehaviour::MoveTowardsPlayer()
{
    // (Unused by the game.)
    enemy.navAgent.Stop();
    enemy.navAgent.SetDestination(HeroPosition());
}

bool GolemKamikazeBehaviour::CheckIfDestinationPointIsReachable(Vector2 point) const
{
    return PolyNav2D::current && PolyNav2D::current->PointIsValid(point);
}

void GolemKamikazeBehaviour::SetLayer(int layer)
{
    // gameObject.layer: the root's colliders refiltered (the AttackCollider child keeps its own).
    if (enemy.layer == layer) return;
    enemy.layer = layer;
    if (enemy.physics) enemy.physics->SetLayer(enemy, layer);
}

int GolemKamikazeBehaviour::MoveAxis(bool x, float end, float duration, unity::Ease ease)
{
    unity::Tweens* tweens = enemy.scene.tweens;
    if (!tweens) return 0;
    Enemy* e = &enemy;
    // DOMoveX / DOMoveY (Vector3Plugin's axis constraint): that component start + change *
    // ease (one expression, rounded once), the others as transform.position reads them then,
    // the whole position set; SetUpdate(false): the scaled clock.
    return tweens->To([e, x] {
                          const Vector2 p = e->position();
                          return Vector2{x ? p.x : p.y, 0.0f};
                      },
                      [e, x](Vector2 v) {
                          Vector2 p = e->position();
                          (x ? p.x : p.y) = v.x;
                          e->setPosition(p);
                      },
                      Vector2{end, 0.0f}, duration, ease, 0.0f, false);
}

void GolemKamikazeBehaviour::MakeJump(Vector2 endPosition, float height, float time, std::function<void()> callback)
{
    jumpTime = time;
    jumpEndPosition = endPosition;
    jumpCallback = std::move(callback);
    // DOMoveY((endPosition.y + transform.position.y) / 2f + height, time * 0.5f): the argument
    // one expression in double.
    const float middle = F((D(endPosition.y) + D(enemy.position().y)) / 2.0 + D(height));
    jumpTweenY = MoveAxis(false, middle, time * 0.5f, unity::Ease::OutCubic);
    if (jumpTweenY) enemy.scene.tweens->OnComplete(jumpTweenY, [this] { ContinueJump(); });
    jumpTweenX = MoveAxis(true, endPosition.x, time, unity::Ease::Linear);
}

void GolemKamikazeBehaviour::ContinueJump()
{
    jumpTweenY = MoveAxis(false, jumpEndPosition.y, jumpTime * 0.5f, unity::Ease::InCubic);
    if (jumpTweenY) enemy.scene.tweens->OnComplete(jumpTweenY, [this] { EndJump(); });
}

void GolemKamikazeBehaviour::EndJump()
{
    jumpTweenY = 0;
    if (jumpCallback) jumpCallback();
}

void GolemKamikazeBehaviour::StopOnExplode()
{
    // this.DOKill() (no tween targets the behaviour), transform.DOKill(): the jump's, not
    // completed (its end, back on GroundEnemy, never comes).
    if (unity::Tweens* tweens = enemy.scene.tweens) {
        tweens->Kill(jumpTweenY);
        tweens->Kill(jumpTweenX);
    }
    jumpTweenY = jumpTweenX = 0;
    if (enemy.body) enemy.body->SetVelocity(Vector2::zero());
    ForbidEnemyMovement();
    enemy.navAgent.Stop();
}

void GolemKamikazeBehaviour::AttackHit(bool otherIsPlayer)
{
    AttackOther other;
    other.player = otherIsPlayer;
    if (otherIsPlayer) other.tag = "Player";
    AttackHitOther(other);
}

void GolemKamikazeBehaviour::AttackHitOther(const AttackOther& other)
{
    EnemyHero* hero = enemy.scene.hero;
    if (other.player && hero && !(hero->isShieldProtected && hero->isShieldProtected(enemy))) {
        if (enemy.enemyStats.isAlive() && hero->dealDamage) hero->dealDamage(enemy.enemyStats.strength(), enemy.name, enemy);
        PushPlayerBackwards(attackPushBackwardsForce);
    }
    if (other.tag == "Enemy") {
        if (other.enemy) other.enemy->DealDamageToEnemy(enemy.enemyStats.strength());
    } else if (other.tag == "EnemyDamageReceiver") {
        if (other.receiver) other.receiver->DealDamageToEnemy(enemy.enemyStats.strength());
    }
}

void GolemKamikazeBehaviour::SelfDestroy()
{
    if (unity::DelayedCalls* calls = enemy.scene.calls) calls->CancelInvoke(this, "MoveEnemy");
    subscribedToRoomExit = false;
    // (OnDeath -= enemy.DropItemsOnDeath: never subscribed; it stays alive, so its Destroy
    // drops nothing either way.)
    subscribedToRoomEnter = false;
    enemy.enemyStats.SetInvencible(false);
    // EnemyStats.Die(): the achievements' counts, MarkEnemyAsDefeated (the room's EnemyDefeated).
    if (enemy.enemyStats.OnDie) enemy.enemyStats.OnDie();
    // Object.Destroy(gameObject): at the frame's end.
    Enemy* e = &enemy;
    if (unity::DelayedCalls* calls = enemy.scene.calls) calls->DestroyAtFrameEnd(e, [e] { e->Destroy(); });
    else e->Destroy();
}

void GolemKamikazeBehaviour::VibrateOnExplosion()
{
    EnemyHero* hero = enemy.scene.hero;
    if (hero && hero->vibrate)
        hero->vibrate(Constants::GetFloat("defaultStrongEnemyVibrationIntensity"), Constants::GetFloat("defaultStrongEnemyVibrationDuration"));
}

void GolemKamikazeBehaviour::CreateExplosionToDamageEnemies()
{
    explosions++;
    lastExplosionHits.clear();
    if (!enemy.physics) return;
    // Physics2D.OverlapCircleAll(transform.position, tileSize * 3.5f): the default layers
    // (all but Ignore Raycast), triggers too.
    const float radius = F(D(Constants::GetFloat("tileSize")) * 3.5);
    const std::vector<const b2Fixture*> found = unity::OverlapCircleAll(enemy.physics->World(), enemy.position(), radius);
    for (const b2Fixture* f : found) {
        // GetComponent<EnemyBehaviour>() on the collider's object: an enemy's root.
        Enemy* other = enemy.physics->RootOf(f);
        if (!other) {
            if (explosionOther) explosionOther(f);
            continue;
        }
        if (!other->enemyBehaviour) continue;
        lastExplosionHits.push_back(other);
        other->DealDamageToEnemy(enemy.enemyStats.strength());
    }
}

void GolemKamikazeBehaviour::PlaySound(const char* sound)
{
    if (enemy.scene.audio) enemy.scene.audio->PlaySound(sound);
    if (enemy.scene.playSound) enemy.scene.playSound(enemy, sound);
}

void GolemKamikazeBehaviour::PlayKamikazeJumpSound() { PlaySound("golem_dungeon_kamikaze_jump"); }

void GolemKamikazeBehaviour::PlayKamikazeExplosionSound() { PlaySound("golem_dungeon_kamikaze_explosion"); }

bool GolemKamikazeBehaviour::OnAnimationEvent(const unity::AnimationEvent& e)
{
    const std::string& f = e.function;
    if (f == "StopOnExplode") StopOnExplode();
    else if (f == "VibrateOnExplosion") VibrateOnExplosion();
    else if (f == "CreateExplosionToDamageEnemies") CreateExplosionToDamageEnemies();
    else if (f == "SelfDestroy") SelfDestroy();
    // (SoundsKamikaze, a component of its own on the object: SendMessage reaches it too.)
    else if (f == "PlayKamikazeJumpSound") PlayKamikazeJumpSound();
    else if (f == "PlayKamikazeExplosionSound") PlayKamikazeExplosionSound();
    else return EnemyBehaviour::OnAnimationEvent(e);
    return true;
}

void GolemKamikazeBehaviour::OnEnemyDestroyed()
{
    // DOTween's safe mode: a tween whose target is gone stops (it moves nothing after).
    if (unity::Tweens* tweens = enemy.scene.tweens) {
        tweens->Kill(jumpTweenY);
        tweens->Kill(jumpTweenX);
    }
    jumpTweenY = jumpTweenX = 0;
    EnemyBehaviour::OnEnemyDestroyed();
}

std::unique_ptr<GolemKamikazeBehaviour> MakeGolemKamikazeBehaviour(Enemy& enemy, const Behaviour& fields)
{
    auto kamikaze = std::make_unique<GolemKamikazeBehaviour>(enemy);
    kamikaze->stopCheckingHolesDelay = static_cast<float>(fields.Field("stopCheckingHolesDelay", 0.0));
    return kamikaze;
}

} // namespace game
