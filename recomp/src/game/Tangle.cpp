#include "game/Tangle.h"

#include <cmath>

#include "game/Constants.h"
#include "unity/Mathf.h"
#include "unity/Random.h"

namespace game {

using unity::Vector2;
namespace Mathf = unity::Mathf;
using unity::mono::D;
using unity::mono::F;

namespace {

// LayerMask.NameToLayer (the project's TagManager; game/Enemy.cpp has the same).
constexpr int kLayerRisenObstacle = 8, kLayerLevelBoundary = 11, kLayerEnemyBoundary = 21;
constexpr float kRayDistance = 30.0f;

// Mathf.Cos(f): (float)Math.Cos(f), the argument a float.
float Cos(float f) { return F(std::cos(D(f))); }

} // namespace

int TangleBehaviour::WeaveLayerMask() { return (1 << kLayerLevelBoundary) | (1 << kLayerEnemyBoundary) | (1 << kLayerRisenObstacle); }

void TangleBehaviour::Init()
{
    EnemyBehaviour::Init();
    originalAmplitudeX *= unity::Random::Range(-1, 2) >= 0 ? 1.0f : -1.0f;
    originalAmplitudeY *= unity::Random::Range(-1, 2) >= 0 ? 1.0f : -1.0f;
}

void TangleBehaviour::LateUpdate(float deltaTime)
{
    lastWeave = Weave{};
    if (!enabled || !enemy.active || enemy.destroyed) return;
    const EnemyHero* hero = enemy.scene.hero;
    if (!targetPlayer || !hero) return;
    const Vector2 p = enemy.position();
    // ((Vector3)((Vector2)heroMerchantTransform.position + GetColliderOffsset())).DiamondDistance(position):
    // the sum stored (a Vector2), each difference a float argument of Mathf.Abs.
    const Vector2 feet = hero->position + heroColliderOffset;
    const float diamond = Mathf::Abs(feet.x - p.x) + Mathf::Abs(feet.y - p.y);
    if (!(D(diamond) > D(Constants::GetFloat("tileSize")) * 1.5)) return;
    const Vector2 vector = hero->position - p; // heroMerchantTransform.position - transform.position
    if (Mathf::Abs(vector.x) > Mathf::Abs(vector.y)) {
        amplitudeY = originalAmplitudeY;
        amplitudeX = 0.0f;
    } else {
        amplitudeY = 0.0f;
        amplitudeX = originalAmplitudeX;
    }
    index += deltaTime;
    const float x = amplitudeX * Cos(omegaX * index);
    const float y = amplitudeY * Cos(omegaY * index);
    lastWeave.ran = true;
    lastWeave.direction = {x, y};
    if (enemy.scene.queries) lastWeave.hit = enemy.scene.queries->Raycast(p, {x, y}, kRayDistance, WeaveLayerMask());
    if (lastWeave.hit.collider == nullptr) {
        // velocity += new Vector2(x, y) * 1f (the hit's factor isn't taken without a hit).
        lastWeave.added = Vector2{x, y} * 1.0f;
        enemy.navAgent.velocity = enemy.navAgent.velocity + lastWeave.added;
    }
}

void TangleBehaviour::OnDeath()
{
    EnemyBehaviour::OnDeath();
    if (registerDeath && (tangleCulture == CommonEnemyCulture::Golem || tangleCulture == CommonEnemyCulture::Forest)) registerDeath(tangleCulture);
}

std::unique_ptr<TangleBehaviour> MakeTangleBehaviour(Enemy& enemy, const Behaviour& fields)
{
    auto tangle = std::make_unique<TangleBehaviour>(enemy);
    tangle->tangleCulture = static_cast<EnemyBehaviour::CommonEnemyCulture>(static_cast<int>(fields.Field("tangleCulture", 0.0)));
    return tangle;
}

} // namespace game
