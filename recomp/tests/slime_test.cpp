// The BabySlime (game/Enemy, game/BabySlime, game/EnemyStats, unity/DelayedCalls):
//  1. self-checks that need no recording: UnityEngine.Random's xorshift128 against the
//     game's values for seed 0 (data/oracle/rng.jsonl's first draws), insideUnitCircle's
//     draws, the delayed calls' order, and with the game's data (ML_DATA, default
//     data/extracted) the stats from enemy_stats.json and a slime in a box: it waits its
//     second, chases a standing hero, touches him (his DealDamageToPlayer), takes a hit (the
//     red / white flash, invincible for 0.09 s, still), and dies (TransparentFX, the
//     Animator off, the dead sprite and the destroy 0.6 s later);
//  2. with the oracle's recording (data/oracle/slime.jsonl, `-oracle-slime`; skipped if
//     there's none): the room's slimes from their recorded start, every later frame replayed
//     from the recording's clocks (deltaTime, Time.time, each fixed step's Time.fixedTime, as
//     a double run on from the first: the floats are too coarse for what's due in which step;
//     DOTween's unscaled delta where recorded), the steps between EnableEnemies and the start
//     (towards the bodies' sleep), the hero's body as each fixed step found it (layer,
//     transform, velocity, awake), where he was when each slime's Update read him, the weapon's
//     hits (DealDamageToEnemy, PushEnemyBackwards) where they landed (and its hitbox's new
//     fixture the step before: PhysicsWorld::FixtureMadeElsewhere), the
//     Updates and the agents' LateUpdates in the game's order, and the Random state each
//     Update started from (the game's other scripts draw between them: counted, then
//     resynced). Compared per frame and slime, bit for bit: position (world and local), its
//     Rigidbody2D (position, velocity, awake), the agent (velocity, goal, path, pending,
//     acceleration), the Animator (state, normalized time, sprite, on), health, the
//     behaviour's fields, the material (flash, colour), alive and destroyed; the attack
//     trigger's hits on the hero; the Random draws of the slimes' own Updates; each fixed
//     step's start; insideUnitCircle against the game's ("rngProbe"). Two rows inform without
//     failing: the order of two hits in one step (Box2D's contact order, from proxy ids Unity's
//     whole history set), and the death fade bit for bit when the recording lacks DOTween's
//     clock (then it's held to 0.03, about a frame).
//
// The replay is tests/enemy_replay.h's (enemy_test's too).
//
// Usage: slime_test [slime.jsonl]   (ML_SLIME_DEBUG=1: every mismatch; ML_SLIME_CONTACTS=<frame>: the
//        slimes' Box2D contacts after each of that frame's steps)

#include "enemy_replay.h"

#include "game/BabySlime.h"

namespace {

// ---- 1. Self-checks ---------------------------------------------------------------------

void CheckRandom()
{
    // InitState(0) and its first value()s, as the game drew them (rng.jsonl: seed 0, "value").
    unity::Random::InitState(0);
    const unity::Random::State s = unity::Random::GetState();
    Check(s.s0 == 0 && s.s1 == 1 && s.s2 == 1812433254u && s.s3 == 1900727103u, "Random.InitState(0)");
    const uint32_t want[4] = {1058376237u, 1058375277u, 1059877249u, 1061435854u};
    bool ok = true;
    for (uint32_t w : want) ok &= U(unity::Random::value()) == w;
    Check(ok, "Random.value after InitState(0): the game's first four");

    // insideUnitCircle: two draws, inside the circle.
    unity::Random::InitState(12345);
    bool inside = true;
    for (int i = 0; i < 1000; i++) {
        const unity::Random::State before = unity::Random::GetState();
        const Vector2 p = unity::Random::insideUnitCircle();
        inside &= p.x * p.x + p.y * p.y <= 1.0001f;
        inside &= unity::Random::DrawsBetween(before, unity::Random::GetState(), 4) == 2;
    }
    Check(inside, "Random.insideUnitCircle: two draws, inside the unit circle");
}

void CheckDelayedCalls()
{
    unity::DelayedCalls calls;
    std::string log;
    int owner = 0, other = 0;
    calls.now = 1.0;
    calls.fixedNow = 1.0; // (Invoke counts from Time.fixedTime: unity/DelayedCalls.h)
    calls.Invoke(&owner, "B", 0.5f, [&] { log += "B"; });
    calls.Invoke(&owner, "A", 0.25f, [&] { log += "A"; });
    calls.Invoke(&other, "B", 0.5f, [&] { log += "b"; });
    calls.Wait(&owner, 0.5f, [&] { log += "w"; });
    calls.Invoke(&owner, "C", 0.1f, [&] {
        log += "C";
        // Made during the pass and due at once: waits for the next pass.
        calls.Invoke(&owner, "D", 0.0f, [&] { log += "D"; });
    });
    calls.CancelInvoke(&owner, "B"); // only owner's B; the wait stays
    calls.Update(1.2);
    Check(log == "C", "delayed calls: only what's due (" + log + ")");
    calls.Update(1.5);
    Check(log == "CDAbw", "delayed calls: earliest first, ties in order, a call made in a pass waits, CancelInvoke by owner and name (" +
                              log + ")");
    calls.now = 2.0;
    calls.fixedNow = 2.0;
    calls.Invoke(&owner, "R", 0.1f, [&] { log += "r"; }, 0.1f);
    calls.Update(2.35);
    Check(log == "CDAbwr", "InvokeRepeating: once per pass (" + log + ")");
    calls.Update(2.36);
    Check(log == "CDAbwrr", "InvokeRepeating: the next period");
    calls.Forget(&owner);
    calls.Update(10.0);
    Check(calls.Empty() && log == "CDAbwrr", "Forget: the owner's calls go");
}

struct Data {
    std::string dir;
    Value enemyStats, physics2d, soundGroups;
    std::unique_ptr<unity::AnimatorController> controller;
    const game::Prefab* prefab = nullptr;
    bool ok = false;
};

Data LoadData()
{
    Data d;
    d.dir = std::getenv("ML_DATA") ? std::getenv("ML_DATA") : "data/extracted";
    const std::string constants = ReadFile(d.dir + "/constants.json"), stats = ReadFile(d.dir + "/enemy_stats.json"),
                      prefabs = ReadFile(d.dir + "/prefabs.json"), anim = ReadFile(d.dir + "/animators/BabySlime.json"),
                      physics = ReadFile(d.dir + "/physics2d.json");
    if (constants.empty() || stats.empty() || prefabs.empty() || anim.empty()) return d;
    game::Constants::LoadFromText(constants);
    d.enemyStats = json::Parse(stats);
    if (!physics.empty()) d.physics2d = json::Parse(physics);
    const std::string sounds = ReadFile(d.dir + "/sound_groups.json");
    if (!sounds.empty()) d.soundGroups = json::Parse(sounds);
    game::PrefabLibrary::Load(json::Parse(prefabs));
    d.prefab = game::PrefabLibrary::Find("BabySlime", "enemies_golem");
    d.controller = std::make_unique<unity::AnimatorController>(unity::AnimatorController::FromJson(json::Parse(anim)));
    d.ok = d.prefab != nullptr;
    return d;
}

void CheckStats(const Data& data)
{
    game::EnemyStatModificator m;
    Check(game::EnemyStatModificator::FromJson(data.enemyStats, "BabySlime", m), "enemy_stats.json has BabySlime");
    Check(m.isModifiedByFloor && m.maxHealth == 5.0f && m.strength == 10.0f && m.speed == 50.0f && m.weight == 6.0f && m.culture == 1,
          "BabySlime's base stats (5 health, 10 strength, 50 speed, 6 weight, Golem)");
    game::EnemyStats s;
    game::EnemyStatsContext ctx; // the first floor of the Golem dungeon, no difficulty option
    s.Init(m, ctx);
    Check(s.maxHealth() == 5.0f && s.strength() == 10.0f && s.CurrentHealth() == 5.0f && s.isAlive(), "Init on floor 1: as the file has them");
    ctx.difficulty = 1; // difficultyMod[1] = 1.25
    s.Init(m, ctx);
    Check(s.maxHealth() == 6.25f && s.strength() == 12.5f, "Init with the difficulty option 1: x1.25");
    // Floor 2 (level 1): health + 0.7 * health; strength + 0.7 * max(0, 1 - 0.5) * strength.
    ctx = {};
    ctx.level = 1;
    s.Init(m, ctx);
    Check(s.maxHealth() == 5.0f + 0.7f * 5.0f && s.strength() == 10.0f + (0.7f * 0.5f) * 10.0f, "Init on floor 2: the leveling");
    // A Golem enemy in the Forest dungeon: the autoscale tables (2.85, 2.5).
    ctx = {};
    ctx.dungeonCulture = 2;
    s.Init(m, ctx);
    Check(s.maxHealth() == 5.0f * 2.85f && s.strength() == 10.0f * 2.5f, "Init in the Forest: the autoscale tables");
    // The health's setter: invincible, decreased, death.
    s.Init(m, game::EnemyStatsContext{});
    int decreased = 0, died = 0;
    s.OnHealthDecreased = [&](float) { decreased++; };
    s.OnDeath = [&] { died++; };
    s.SetInvencible(true);
    s.SetCurrentHealth(1.0f);
    Check(s.CurrentHealth() == 5.0f && decreased == 0, "invincible: the setter does nothing");
    s.SetInvencible(false);
    s.SetCurrentHealth(-3.0f);
    Check(s.CurrentHealth() == 0.0f && !s.isAlive() && decreased == 1 && died == 1, "below zero: 0, dead, once");
    s.SetCurrentHealth(2.0f);
    Check(s.CurrentHealth() == 0.0f, "dead: the setter does nothing");
}

// A slime in an empty box with the hero standing at the origin.
void CheckSlimeInABox(const Data& data)
{
    game::PolyNav2D nav;
    nav.inflateRadius = 8.0f;
    nav.map = game::PolyNav2D::PolyMap({{{{-200.0f, 120.0f}, {-200.0f, -120.0f}, {200.0f, -120.0f}, {200.0f, 120.0f}}}}, {});
    nav.GenerateNodes();
    game::PolyNav2D::current = &nav;

    unity::Physics2DSettings settings;
    if (!data.physics2d.IsNull()) settings.Load(data.physics2d);
    unity::PhysicsWorld world(settings);
    game::EnemyPhysics physics(world);
    unity::Rigidbody2DDef heroDef;
    heroDef.mass = 1.0f;
    heroDef.linearDrag = 5.0f;
    heroDef.gravityScale = 0.0f;
    heroDef.constraints = unity::Rigidbody2DDef::FreezeRotation;
    heroDef.collisionDetection = unity::Rigidbody2DDef::Continuous;
    unity::Rigidbody2D& heroBody = world.AddBody(heroDef, {0.0f, 0.0f}, 0, {1.0f, -3.5f}, {13.0f, 13.0f});
    physics.SetHero(&heroBody);

    unity::DelayedCalls calls;
    unity::Tweens tweens;
    game::EnemyHero hero;
    int hits = 0;
    float hitStrength = 0.0f;
    hero.dealDamage = [&](float strength, const std::string& source, game::Enemy&) {
        hits++;
        hitStrength = strength;
        Check(source == "BabySlime", "the damage's source is the enemy's name");
    };
    game::EnemyScene scene;
    scene.calls = &calls;
    scene.tweens = &tweens;
    scene.hero = &hero;
    int deadSprites = 0;
    scene.spawnEnemyDeadSprite = [&](const std::string& sprite, Vector2) { deadSprites += sprite == "babyslime_dead"; };

    auto slime = game::MakeEnemy(*data.prefab, scene, data.controller.get(), {{80.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}});
    auto* behaviour = dynamic_cast<game::BabySlimeBehaviour*>(slime->enemyBehaviour.get());
    Check(behaviour != nullptr, "the BabySlime prefab makes a BabySlimeBehaviour");
    Check(slime->hasAttackCollider && slime->attackSize.x == 8.0f && slime->colliderSize.x == 6.0f && slime->attackLayer == game::kLayerEnemyAttack &&
              slime->layer == game::kLayerGroundEnemy,
          "its colliders: a 6x6 box on GroundEnemy, an 8x8 trigger on EnemyAttack");
    game::EnemyStatModificator m;
    game::EnemyStatModificator::FromJson(data.enemyStats, "BabySlime", m);
    slime->enemyDeadSprite = "babyslime_dead";
    slime->Init("BabySlime", m, game::EnemyStatsContext{});
    Check(!slime->enemyBehaviour->fallInHoles && slime->navAgent.maxSpeed == 50.0f && slime->rigidbody.mass == 6.0f,
          "Init: no holes for it, the agent's speed and the body's mass from the stats");
    Check(U(slime->enemyBehaviour->playerOffset.x) == 0x80000000u && slime->enemyBehaviour->playerOffset.y == -3.5f, "playerOffset (-0, -3.5)");
    physics.Add(*slime);
    slime->enemyBehaviour->StartTargetingPlayerAfterTime(1.0f);

    const float dt = 1.0f / 60.0f;
    double time = 0.0, fixedTime = 0.0;
    int targetFrame = -1, firstHitFrame = -1;
    float closest = 1e9f;
    auto frame = [&](int f) {
        time += static_cast<double>(dt);
        nav.BeginFrame();
        while (fixedTime + 0.01 <= time) {
            fixedTime += 0.01;
            calls.fixedNow = fixedTime;
            calls.inFixedStep = true;
            world.SyncTransform(heroBody, {0.0f, 0.0f});
            heroBody.SetVelocity({0.0f, 0.0f});
            physics.Step(0.01f);
            calls.UpdateFixed(fixedTime);
            calls.inFixedStep = false;
        }
        calls.now = time;
        tweens.Update(dt);
        slime->enemyBehaviour->Update(dt);
        calls.Update(time);
        nav.ResumeCoroutines();
        slime->UpdateAnimator(dt);
        nav.LateUpdate();
        if (!slime->destroyed && slime->active) slime->navAgent.LateUpdate(dt);
        if (targetFrame < 0 && slime->enemyBehaviour->targetPlayer) targetFrame = f;
        if (firstHitFrame < 0 && hits > 0) firstHitFrame = f;
        closest = std::min(closest, unity::Distance(slime->position(), {0.0f, 0.0f}));
    };
    int f = 0;
    for (; f < 300; f++) frame(f);
    Check(targetFrame >= 59 && targetFrame <= 61, "targets the hero a second after (frame " + std::to_string(targetFrame) + ")");
    Check(firstHitFrame > targetFrame && hits >= 1 && hitStrength == 10.0f,
          "chases and touches him: DealDamageToPlayer(10) (first at frame " + std::to_string(firstHitFrame) + ", " + std::to_string(hits) + " in all)");
    Check(closest < 14.0f, "it got to him (" + std::to_string(closest) + ")");
    Check(slime->sprite.rfind("babyslime_walk_", 0) == 0, "walks in place: " + slime->sprite);

    // A hit of 3: 2 left, red, then white at 0.02 s, red at 0.07, the default at 0.09; invincible
    // and still meanwhile.
    float dealt = slime->DealDamageToEnemy(3.0f);
    Check(dealt == 3.0f && slime->enemyStats.CurrentHealth() == 2.0f, "a hit of 3 (no defense): 2 left");
    Check(slime->flashAmount == 1.0f && slime->color[1] == 0.0f && slime->enemyStats.Invencible() && !slime->enemyBehaviour->canMove &&
              slime->enemyBehaviour->IsEnemyPlayingDamagedEffect() && !slime->navAgent.hasPath(),
          "hit: flash on, red, invincible, stopped");
    Check(slime->DealDamageToEnemy(3.0f) == 3.0f && slime->enemyStats.CurrentHealth() == 2.0f, "a second hit while invincible: nothing");
    frame(f++);
    frame(f++);
    Check(slime->color[1] == 1.0f && slime->flashAmount == 1.0f, "0.033 s later: white");
    frame(f++);
    frame(f++);
    frame(f++);
    Check(slime->color[1] == 0.0f, "0.083 s: red again");
    frame(f++);
    Check(slime->color[1] == 1.0f && slime->flashAmount == 0.0f && !slime->enemyStats.Invencible() && slime->enemyBehaviour->canMove,
          "0.1 s: the default, vulnerable, moving again");

    // The killing hit.
    const int before = hits;
    slime->DealDamageToEnemy(10.0f);
    Check(!slime->enemyStats.isAlive() && slime->layer == game::kLayerTransparentFX && !slime->animatorEnabled && !slime->enemyBehaviour->enabled,
          "dead: TransparentFX, the Animator off, the behaviour off");
    Check(!slime->inRoomEnemies && slime->navAgent.parents.size() == 2, "EnemyDefeated: out of its spawn point");
    for (int k = 0; k < 34; k++) frame(f++);
    Check(!slime->destroyed && slime->flashAmount > 0.0f && slime->color[0] < 1.0f, "0.57 s: fading");
    for (int k = 0; k < 4; k++) frame(f++);
    Check(slime->destroyed && deadSprites == 1, "0.6 s: destroyed, its dead sprite spawned");
    Check(hits == before, "no more damage once dead");
    game::PolyNav2D::current = nullptr;
}

} // namespace

int main(int argc, char** argv)
{
    const std::string file = argc > 1 ? argv[1] : "data/oracle/slime.jsonl";
    CheckRandom();
    CheckDelayedCalls();
    Data data = LoadData();
    if (data.ok) {
        CheckStats(data);
        CheckSlimeInABox(data);
    } else {
        std::printf("(no game data in %s (ML_DATA): skipping the stats and the slime in a box)\n", data.dir.c_str());
    }
    std::printf("self-checks: %d passed, %d failed\n", gPassed, gFailed);
    ReplayData replay;
    replay.enemyStats = data.enemyStats;
    replay.physics2d = data.physics2d;
    replay.prefab = data.prefab;
    replay.controller = data.controller.get();
    replay.soundGroups = data.soundGroups;
    const int replayed = ReplayRecording(file, replay);
    return gFailed || replayed ? 1 : 0;
}
