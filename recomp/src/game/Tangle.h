#pragma once
// TangleBehaviour.cs: an EnemyBehaviour (game/Enemy.h) that weaves. Its Init flips the
// weave's two amplitudes (two Random.Range(-1, 2) draws, after the base Init: the draws
// SpawnDraws.h's EnemyInit stands in for at the floor's generation). Its LateUpdate, while
// it targets the hero and his feet (his transform plus his collider's offset) are more than
// a tile and a half away (diamond distance): the weave across the way to him (along y when
// he's further in x, else along x), amplitude * cos(2 * index), index running on with
// deltaTime; a Physics2D.Raycast of 30 from it along the weave against the walls
// (LevelBoundary, EnemyBoundary, RisenObstacle), and only if nothing is there the weave goes
// onto its PolyNavAgent's velocity (navAgent.velocity +=; the factor for a hit,
// distance / 30, is never used: the add only happens without one).
//
// Where its LateUpdate falls against its agent's (both on the default script order: the
// recording's "late" says which ran first) decides whether the agent's steering takes the
// weave in the same frame or the next: the driver calls the two in the game's order.
//
// Its Animator (animators/Tangle.json): a movement loop (TangleMovement, 0.5 s, four
// sprites), and four empty attack states that "Attacking" with DistanceX/Y take it to and
// that end back in the loop. Its death counts in HeroStatistics by culture.

#include <functional>
#include <memory>
#include <string>

#include "game/Enemy.h"
#include "game/Prefabs.h"
#include "unity/Physics2D.h"
#include "unity/Vector2.h"

namespace game {

class TangleBehaviour : public EnemyBehaviour {
public:
    explicit TangleBehaviour(Enemy& enemy) : EnemyBehaviour(enemy) {}

    // Serialized: CommonEnemyCulture (prefabs.json's TangleBehaviour tangleCulture).
    CommonEnemyCulture tangleCulture = CommonEnemyCulture::Golem;
    // HeroStatistics.Register<Culture>TangleDeath (Golem and Forest only).
    std::function<void(CommonEnemyCulture)> registerDeath;
    // HeroMerchant.Instance.heroMerchantController.GetColliderOffsset(): characterCollider.offset,
    // the hero prefab's (1, -3.5) (nothing changes it).
    unity::Vector2 heroColliderOffset{1.0f, -3.5f};

    // The weave (private in C#; public here for replays and checks).
    float originalAmplitudeX = 50.0f;
    float originalAmplitudeY = 50.0f;
    float amplitudeX = 50.0f;
    float amplitudeY = 50.0f;
    float omegaX = 2.0f;
    float omegaY = 2.0f;
    float index = 0.0f;

    // What the last LateUpdate did (for checks; not the game's).
    struct Weave {
        bool ran = false;           // the weave's branch was taken
        unity::Vector2 direction;   // new Vector2(x, y)
        unity::RaycastHit2D hit;    // the raycast's result
        unity::Vector2 added;       // what went onto the agent's velocity (zero if it hit)
    };
    Weave lastWeave;

    void Init() override;
    void PlayDeathSound() override {}
    // MonoBehaviour.LateUpdate (only while enabled and active, as Unity calls it).
    void LateUpdate(float deltaTime);

    // The raycast's layers: LevelBoundary | EnemyBoundary | RisenObstacle.
    static int WeaveLayerMask();

protected:
    void OnDeath() override;
};

// The TangleBehaviour of a prefab whose root has one (MakeEnemy's), with its serialized
// fields (`fields`: prefabs.json's TangleBehaviour row).
std::unique_ptr<TangleBehaviour> MakeTangleBehaviour(Enemy& enemy, const Behaviour& fields);

} // namespace game
