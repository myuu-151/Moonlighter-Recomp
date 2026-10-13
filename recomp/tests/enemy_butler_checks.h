#pragma once
// enemy_test's self-checks for the GolemButler (game/GolemButler.h, game/EnemyProjectile.h,
// game/PoolManager.h), with the game's data and no recording: the prefab made into a
// GolemButlerBehaviour with its fields and its shot's, Init's pool of 2; Vector2.Angle and
// GetAngleFromVector; then a butler in a walled box (LevelBoundary all round), the hero standing
// where each case puts him, its Start run and its targeting 1.5 s later (its default state,
// ReactivationDown, holds it a second):
//   - far (200 to its right): it looks his way (WalkRight), shoots ("ShootAttack": the shot
//     state, its clip's ShootAttack and PlayShootSound), a shot every 1.6 s (RecoverShot), each
//     the pool's first; the shot flies at him by DOMove (550 over 2 s, linear: per component
//     start + change * t in doubles, rounded once, as Vector3Plugin's IL has it) and hits him (OnCollisionEnter2D: the damage, 10 from
//     "GolemButler", no push), crashes (the Crash state, its box a trigger) and is back in the
//     pool 0.3 s later; invincible while it faces him in its walk, not while it shoots (its shot
//     states face nowhere: he's behind it then);
//   - close in front (60 to its right): no shot; ReactivationDown's AllowEnemyMovement (0.8 s)
//     has its canMove back on, so it walks at him (MoveEnemy), not waiting, invincible;
//   - close behind (it looked left at him, then he stands 60 to its right): it waits (isWaiting,
//     StopEnemy), vulnerable (he's behind it), and 2.5 s later the Invoked LookToPosition turns it
//     (WalkRight): he's in sight, it stops waiting and is invincible again;
//   - far below it: it faces down, where "ShootAttack" takes no transition (WalkDown's wants
//     "Attacking"): no shot while he stays below; when he goes to its right, its next
//     TryShootAttack turns it and the trigger still set shoots.
// Included by enemy_test.cpp (after enemy_replay.h).

#include "game/EnemyProjectile.h"
#include "game/GolemButler.h"

namespace {

struct ButlerData {
    const game::Prefab* prefab = nullptr;
    const unity::AnimatorController* controller = nullptr;
    const game::Prefab* projectilePrefab = nullptr;
    const unity::AnimatorController* projectileController = nullptr;
    Value enemyStats, physics2d, soundGroups;
};

// What one case's run did.
struct ButlerRun {
    std::vector<std::pair<int, std::string>> events;      // (frame, clip event)
    std::vector<std::pair<int, game::GolemButlerProjectile*>> shots;
    std::vector<std::pair<int, std::string>> collisions;  // (frame, what a shot hit)
    std::vector<int> damageFrames, destroyFrames;
    std::vector<Vector2> pushes;
    bool damageOk = true;
    std::vector<std::string> states;                      // per frame: the Animator's state
    std::vector<bool> invincible, waiting;
    std::vector<float> butlerX;                           // per frame: its transform's x
    int targetFrame = -1;
    // The first shot (while the run lasts): its flight bit for bit, its crash, its return; its
    // pool instance and PixelRotation angle (after).
    game::GolemButlerProjectile* first = nullptr;
    int firstInstance = -1, firstAngle = -1;
    int firstFrame = -1, flightFrames = 0, crashFrame = -1, crashStateFrame = -1, backInPoolFrame = -1;
    bool flightExact = true, triggerAfterCrash = false;
    Vector2 firstStart, firstEnd;
    size_t pooled0 = 0, pooledEnd = 0;
    int shootSounds = 0, impactSounds = 0;
};

// A butler at `at` in a walled box, the hero where `heroAt(frame)` says, for `frames` frames.
ButlerRun RunButler(const ButlerData& data, Vector2 at, const std::function<Vector2(int)>& heroAt, int frames)
{
    ButlerRun run;
    game::PolyNav2D nav;
    nav.inflateRadius = 8.0f;
    nav.map = game::PolyNav2D::PolyMap({{{{-240.0f, 130.0f}, {-240.0f, -130.0f}, {240.0f, -130.0f}, {240.0f, 130.0f}}}}, {});
    nav.GenerateNodes();
    game::PolyNav2D::current = &nav;

    unity::Physics2DSettings settings;
    if (!data.physics2d.IsNull()) settings.Load(data.physics2d);
    unity::PhysicsWorld world(settings);
    game::EnemyPhysics physics(world);
    unity::Physics2D room;
    auto box = [&](const char* path, Vector2 c, Vector2 size) {
        unity::Collider2D col;
        col.path = path;
        col.layer = 11;
        col.tag = "LevelBoundary";
        col.position = c;
        col.size = size;
        room.Add(col);
    };
    box("Room/Walls/Top", {0.0f, 140.0f}, {500.0f, 20.0f});
    box("Room/Walls/Bottom", {0.0f, -140.0f}, {500.0f, 20.0f});
    box("Room/Walls/Left", {-250.0f, 0.0f}, {20.0f, 300.0f});
    box("Room/Walls/Right", {250.0f, 0.0f}, {20.0f, 300.0f});
    for (const unity::Collider2D& c : room.Colliders()) world.AddCollider(c, unity::RigidbodyType2D::Static, false);
    unity::Rigidbody2DDef heroDef;
    heroDef.mass = 1.0f;
    heroDef.linearDrag = 5.0f;
    heroDef.gravityScale = 0.0f;
    heroDef.constraints = unity::Rigidbody2DDef::FreezeRotation;
    heroDef.collisionDetection = unity::Rigidbody2DDef::Continuous;
    unity::Rigidbody2D& heroBody = world.AddBody(heroDef, heroAt(0), 0, {1.0f, -3.5f}, {13.0f, 13.0f});
    physics.SetHero(&heroBody);

    unity::DelayedCalls calls;
    unity::Tweens tweens;
    game::EnemyHero hero;
    hero.position = heroAt(0);
    int frameNow = 0;
    hero.dealDamage = [&](float strength, const std::string& source, game::Enemy&) {
        run.damageFrames.push_back(frameNow);
        run.damageOk = run.damageOk && source == "GolemButler" && strength == 10.0f;
    };
    hero.addForce = [&](Vector2 f) { run.pushes.push_back(f); };
    game::EnemyScene scene;
    scene.calls = &calls;
    scene.tweens = &tweens;
    scene.hero = &hero;
    scene.queries = &room;
    game::MasterAudio audio;
    if (!data.soundGroups.IsNull() && audio.Load(data.soundGroups, {"Sounds - GolemDungeon", "Sounds - GeneralDungeons"})) scene.audio = &audio;
    scene.playSound = [&](game::Enemy&, const std::string& sound) {
        run.shootSounds += sound == "golem_dungeon_golembutler_shot";
        run.impactSounds += sound == "golem_dungeon_golembutler_shot_impact";
    };
    game::EnemyProjectiles projectiles(scene);
    projectiles.Register(data.projectilePrefab, data.projectileController);
    projectiles.SetPhysics(world, physics);
    projectiles.SetHero(&heroBody);
    projectiles.heroFollowsBody = false; // (the hero stands where heroAt puts him)
    projectiles.roomParents = {{0.0f, 0.0f}};
    scene.projectiles = &projectiles;
    projectiles.onCollisionEnter = [&](game::EnemyProjectile&, const game::Collision2D& c, bool after) {
        if (!after) run.collisions.emplace_back(frameNow, c.tag);
    };
    scene.onAnimationEvent = [&](game::Enemy&, const unity::AnimationEvent& e, bool after) {
        if (!after) run.events.emplace_back(frameNow, e.function);
    };
    unity::Random::InitState(4321);

    auto butler = game::MakeEnemy(*data.prefab, scene, data.controller, {at, {0.0f, 0.0f}, {0.0f, 0.0f}});
    auto* behaviour = dynamic_cast<game::GolemButlerBehaviour*>(butler->enemyBehaviour.get());
    if (!behaviour) return run;
    game::EnemyStatModificator m;
    game::EnemyStatModificator::FromJson(data.enemyStats, "GolemButler", m);
    butler->Init("GolemButler", m, game::EnemyStatsContext{});
    physics.Add(*butler);
    const game::PoolManager::Pool* pool = projectiles.poolManager.Find(behaviour->ProjectilePooledName());
    run.pooled0 = pool ? pool->pooledObjects.size() : 0;
    behaviour->onShot = [&](game::GolemButlerBehaviour&, game::GolemButlerProjectile* p) { run.shots.emplace_back(frameNow, p); };
    for (game::EnemyProjectile* p : projectiles.All())
        if (auto* s = dynamic_cast<game::GolemButlerProjectile*>(p)) s->onDestroyNow = [&](game::GolemButlerProjectile&) { run.destroyFrames.push_back(frameNow); };

    const float dt = 1.0f / 60.0f;
    double time = 0.0, fixedTime = 0.0;
    float elapsed = 0.0f;
    for (int f = 0; f < frames; f++) {
        frameNow = f;
        hero.position = heroAt(f);
        time += static_cast<double>(dt);
        nav.BeginFrame();
        while (fixedTime + 0.01 <= time) {
            fixedTime += 0.01;
            calls.fixedNow = fixedTime;
            calls.inFixedStep = true;
            world.SyncTransform(heroBody, hero.position);
            heroBody.SetVelocity({0.0f, 0.0f});
            physics.Step(0.01f);
            calls.UpdateFixed(fixedTime);
            calls.inFixedStep = false;
        }
        calls.now = time;
        audio.now = time;
        audio.frameCount = f;
        tweens.Update(dt);
        // The first shot's DOMove: per component start + change * (t / 2) (change = end - start,
        // rounded), one expression in doubles rounded once (Vector3Plugin's `startValue.x +=
        // changeValue.x * easeVal`).
        if (run.first && run.crashFrame < 0 && run.first->active && !run.first->crashed && f > run.firstFrame) {
            elapsed += dt;
            const float e = unity::EaseEvaluate(unity::Ease::Linear, elapsed, 2.0f);
            const Vector2 change = run.firstEnd - run.firstStart;
            const Vector2 want{static_cast<float>(static_cast<double>(run.firstStart.x) + static_cast<double>(change.x) * static_cast<double>(e)),
                               static_cast<float>(static_cast<double>(run.firstStart.y) + static_cast<double>(change.y) * static_cast<double>(e))};
            if (std::getenv("ML_BUTLER_BOX") && !Same(run.first->position(), want))
                std::printf("frame %d: the shot at %s, DOMove's %s\n", f, Str(run.first->position()).c_str(), Str(want).c_str());
            run.flightExact = run.flightExact && Same(run.first->position(), want);
            run.flightFrames++;
        }
        if (f == 0) behaviour->Start(); // (its first frame: Start before the Update)
        // (Its targeting 1.5 s on: past its default state, ReactivationDown, 1 s to its walk.)
        if (f == 0) behaviour->StartTargetingPlayerAfterTime(1.5f);
        behaviour->Update(dt);
        calls.Update(time);
        nav.ResumeCoroutines();
        projectiles.UpdateAnimators(dt);
        butler->UpdateAnimator(dt);
        nav.LateUpdate();
        butler->navAgent.LateUpdate(dt);
        if (run.targetFrame < 0 && behaviour->targetPlayer) run.targetFrame = f;
        if (!run.first && !run.shots.empty()) {
            run.first = run.shots[0].second;
            run.firstFrame = run.shots[0].first;
            run.firstStart = run.first->position();
            run.firstEnd = run.firstStart + unity::Normalized(run.first->projectileDirection) * 550.0f;
            run.firstInstance = run.first->instance;
            run.firstAngle = run.first->rotationAngle;
        }
        if (run.first && run.crashFrame < 0 && run.first->crashed) {
            run.crashFrame = f;
            run.triggerAfterCrash = run.first->isTrigger;
        }
        if (run.first && run.crashFrame >= 0 && run.crashStateFrame < 0 && run.first->active && run.first->animator &&
            run.first->animator->CurrentState() == run.first->animator->Controller().State("CollisionBladeLeaf"))
            run.crashStateFrame = f;
        if (run.first && run.crashFrame >= 0 && run.backInPoolFrame < 0 && !run.first->active) run.backInPoolFrame = f;
        const int state = butler->enemyAnimator->CurrentState();
        run.states.push_back(state >= 0 ? data.controller->states[static_cast<size_t>(state)].name : "");
        run.invincible.push_back(butler->enemyStats.Invencible());
        run.waiting.push_back(behaviour->isWaiting);
        run.butlerX.push_back(butler->position().x);
        if (std::getenv("ML_BUTLER_BOX"))
            std::printf("frame %d: %s t %.4f inv %d waiting %d shotEnabled %d dx %g dy %g shot %s\n", f, run.states.back().c_str(),
                        butler->enemyAnimator->GetCurrentAnimatorStateInfo().normalizedTime, butler->enemyStats.Invencible(), behaviour->isWaiting,
                        behaviour->shotEnabled, behaviour->distanceX, behaviour->distanceY, run.first ? Str(run.first->position()).c_str() : "-");
    }
    run.pooledEnd = pool ? pool->pooledObjects.size() : 0;
    run.first = nullptr; // (the projectiles go with the run)
    for (auto& shot : run.shots) shot.second = nullptr;
    game::PolyNav2D::current = nullptr;
    return run;
}

int CountEvents(const ButlerRun& run, const std::string& fn, int from = 0, int to = 1 << 30)
{
    int n = 0;
    for (const auto& e : run.events) n += e.second == fn && e.first >= from && e.first < to;
    return n;
}

void CheckGolemButler(const ButlerData& data)
{
    // The angles (Vector2.Angle's float steps; GetAngleFromVector's clockwise turn).
    Check(game::GolemButlerAngle({1.0f, 0.0f}, {0.0f, 1.0f}) == 90.0f && game::GolemButlerAngle({0.0f, 0.0f}, {3.0f, 4.0f}) == 0.0f &&
              game::GolemButlerAngle({-1.0f, -0.0f}, {5.0f, 0.0f}) == 180.0f,
          "Vector2.Angle: 90 for a right angle, 0 against a zero vector, 180 opposite");
    Check(game::GolemButlerAngleFromVector({1.0f, 0.0f}, {0.0f, 1.0f}) == 90.0f && game::GolemButlerAngleFromVector({1.0f, 0.0f}, {0.0f, -1.0f}) == 270.0f,
          "GetAngleFromVector(right, up) 90, (right, down) 270");

    if (!data.prefab || !data.controller || !data.projectilePrefab || !data.projectileController) {
        std::printf("(no GolemButler / GolemButlerProjectile prefab or animators/GolemButler.json, GolemTurretProjectile.json: the butler in a box skipped)\n");
        return;
    }
    // The prefab, its fields, its pool.
    {
        game::EnemyScene scene;
        unity::DelayedCalls calls;
        scene.calls = &calls;
        game::EnemyProjectiles projectiles(scene);
        projectiles.Register(data.projectilePrefab, data.projectileController);
        scene.projectiles = &projectiles;
        auto butler = game::MakeEnemy(*data.prefab, scene, data.controller, {{0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}});
        auto* behaviour = dynamic_cast<game::GolemButlerBehaviour*>(butler->enemyBehaviour.get());
        Check(behaviour != nullptr, "the GolemButler prefab makes a GolemButlerBehaviour");
        if (!behaviour) return;
        Check(behaviour->projectilePrefab == "GolemButlerProjectile" && behaviour->spawnProyectilePositions.empty() &&
                  behaviour->attackPushBackwardsForce == 200.0f && behaviour->canMove,
              "its fields (prefabs.py FIELDS): GolemButlerProjectile, no spawn positions, push 200, canMove");
        Check(butler->hasAttackCollider && butler->attackEnabled && Same(butler->attackSize, {25.0f, 25.0f}) && Same(butler->attackOffset, {0.0f, -10.0f}) &&
                  butler->tag == "HardEnemy" && Same(butler->colliderSize, {15.0f, 15.0f}),
              "its body (15 x 15, HardEnemy) and its AttackCollider (25 x 25 at (0, -10), on)");
        game::EnemyStatModificator m;
        game::EnemyStatModificator::FromJson(data.enemyStats, "GolemButler", m);
        butler->Init("GolemButler", m, game::EnemyStatsContext{});
        const game::PoolManager::Pool* pool = projectiles.poolManager.Find("GolemButler_GolemButlerProjectile");
        Check(pool && pool->pooledObjects.size() == 2 && behaviour->previousEnemyHealth == 70.0f && butler->enemyStats.strength() == 10.0f,
              "Init: the pool GolemButler_GolemButlerProjectile of 2, previousEnemyHealth 70, strength 10");
        const auto* shot = pool && !pool->pooledObjects.empty() ? dynamic_cast<const game::GolemButlerProjectile*>(pool->pooledObjects[0]) : nullptr;
        Check(shot && shot->timeToDestroyAfterCrash == 0.3f && shot->layer == 15 && shot->tag == "EnemyProjectile" && shot->boxSize.x == 8.0f &&
                  shot->boxSize.y == 7.0f && !shot->active,
              "its shot from the prefab: a GolemButlerProjectile, 8 x 7 on EnemyProjectile, crash wait 0.3, pooled");
        behaviour->Start();
        Check(!behaviour->canMove, "Start: ForbidEnemyMovement");
    }

    // Far: it shoots at him.
    {
        std::printf("-- a butler in a box: the hero far to its right\n");
        const Vector2 at{-100.0f, 0.0f}, heroAt{100.0f, 0.0f};
        const ButlerRun run = RunButler(data, at, [&](int) { return heroAt; }, 420);
        Check(run.targetFrame >= 89 && run.targetFrame <= 91, "targets the hero 1.5 s after (frame " + std::to_string(run.targetFrame) + ")");
        int rightFrame = -1;
        for (size_t f = 0; f < run.states.size() && rightFrame < 0; f++)
            if (run.states[f] == "WalkRight") rightFrame = static_cast<int>(f);
        Check(rightFrame >= 59 && rightFrame <= 62 && run.states[58] == "ReactivationDown",
              "its default state, ReactivationDown, for its second (its exit time); then Start's look turns it right (WalkRight, frame " +
                  std::to_string(rightFrame) + ")");
        const int shots = CountEvents(run, "ShootAttack");
        Check(shots >= 3 && CountEvents(run, "PlayShootSound") >= 3 && run.shootSounds == CountEvents(run, "PlayShootSound"),
              "it shoots (ShootAttack, PlayShootSound: " + std::to_string(shots) + " shots)");
        // A shot every 1.6 s (RecoverShot), the event 0.25 s into the shot state.
        bool spacing = run.shots.size() >= 3;
        for (size_t k = 1; spacing && k < run.shots.size(); k++) {
            const int gap = run.shots[k].first - run.shots[k - 1].first;
            spacing = gap >= 95 && gap <= 98;
        }
        Check(spacing && static_cast<int>(run.shots.size()) == shots, "a shot each ShootAttack, 1.6 s apart (RecoverShot)");
        bool shootState = false;
        for (const std::string& s : run.states) shootState = shootState || s == "ShootAttackRight";
        Check(shootState, "the shot state: ShootAttackRight");
        Check(run.firstInstance == 0 && Same(run.firstStart, at) && run.firstAngle == 90,
              "the first shot: the pool's first, at the butler, its PixelRotation 90 (0 + 90: aimed right)");
        Check(run.flightFrames > 10 && run.flightExact, "its flight: DOMove's start + (end - start) * (t / 2) rounded once, bit for bit (" + std::to_string(run.flightFrames) + " frames)");
        Check(run.crashFrame > run.firstFrame && run.triggerAfterCrash, "it crashes on him (OnCollisionEnter2D): its box a trigger");
        Check(run.crashStateFrame == run.crashFrame, "the Crash trigger: the crash state in the frame's animation pass");
        Check(run.backInPoolFrame >= run.crashFrame + 17 && run.backInPoolFrame <= run.crashFrame + 19, "DestroyNow 0.3 s after the crash: back in the pool");
        bool hitsHim = !run.collisions.empty();
        for (const auto& c : run.collisions) hitsHim = hitsHim && c.second == "Player";
        Check(hitsHim && run.damageOk && run.damageFrames.size() == run.collisions.size() && run.pushes.empty() && run.impactSounds == 0,
              "each shot hits him: DealDamageToPlayer(10, \"GolemButler\"), no push, no impact sound (" + std::to_string(run.collisions.size()) + " hits)");
        Check(run.pooledEnd + run.shots.size() - run.destroyFrames.size() == run.pooled0, "the pool: every shot taken from its front, every DestroyNow back at its end");
        // Invincible facing him in its walk, vulnerable in its shot state (it faces nowhere): as its
        // Update found the state (the last frame's animation pass left it).
        bool invOk = true;
        for (size_t f = static_cast<size_t>(run.targetFrame) + 1; f < run.states.size(); f++) {
            if (run.states[f - 1] == "WalkRight") invOk = invOk && run.invincible[f];
            if (run.states[f - 1] == "ShootAttackRight") invOk = invOk && !run.invincible[f];
        }
        Check(invOk, "invincible in WalkRight facing him, not in ShootAttackRight (he's 'behind' a state that faces nowhere)");
    }

    // Close, in front.
    {
        std::printf("-- a butler in a box: the hero close in front\n");
        const ButlerRun run = RunButler(data, {-100.0f, 0.0f}, [](int) { return Vector2{-40.0f, 0.0f}; }, 240);
        bool waited = false, inv = true;
        for (size_t f = static_cast<size_t>(run.targetFrame) + 1; f < run.states.size() && f < static_cast<size_t>(run.targetFrame) + 30; f++) {
            waited = waited || run.waiting[f];
            inv = inv && run.invincible[f];
        }
        Check(run.shots.empty() && CountEvents(run, "ShootAttack") == 0, "no shot within 150");
        // ReactivationDown's AllowEnemyMovement (0.8 s) undid Start's ForbidEnemyMovement: it walks at him.
        Check(CountEvents(run, "AllowEnemyMovement", 40, 60) == 1, "ReactivationDown's AllowEnemyMovement at 0.8 s (canMove back on)");
        Check(!waited && inv && run.targetFrame > 0 && run.butlerX.size() > static_cast<size_t>(run.targetFrame) + 30 &&
                  run.butlerX[static_cast<size_t>(run.targetFrame) + 30] > -100.0f,
              "he's in sight: not waiting, invincible, it walks at him (MoveEnemy)");
    }

    // Close, behind: it waits, then turns.
    {
        std::printf("-- a butler in a box: the hero close behind\n");
        const ButlerRun run = RunButler(data, {0.0f, 0.0f}, [](int f) { return f < 30 ? Vector2{-60.0f, 0.0f} : Vector2{60.0f, 0.0f}; }, 360);
        int waitFrame = -1, turnFrame = -1;
        for (size_t f = 0; f < run.states.size(); f++) {
            if (waitFrame < 0 && run.waiting[f]) waitFrame = static_cast<int>(f);
            if (waitFrame >= 0 && turnFrame < 0 && run.states[f] == "WalkRight") turnFrame = static_cast<int>(f);
        }
        bool left = false;
        for (size_t f = 0; f < run.states.size() && f < 90; f++) left = left || run.states[f] == "WalkLeft";
        Check(left, "Start's look turns it left at him (WalkLeft)");
        Check(waitFrame >= run.targetFrame && waitFrame <= run.targetFrame + 1 && !run.invincible[static_cast<size_t>(waitFrame)],
              "he's behind it: it waits from its first targeted frame, vulnerable (frame " + std::to_string(waitFrame) + ")");
        // LookToPosition Invoked 2.5 s on: it turns in the frame's animation pass after the pass that ran it.
        Check(turnFrame >= waitFrame + 149 && turnFrame <= waitFrame + 151, "2.5 s later the Invoked LookToPosition turns it right (frame " + std::to_string(turnFrame) + ")");
        const size_t after = static_cast<size_t>(turnFrame) + 3;
        Check(after < run.states.size() && !run.waiting[after] && run.invincible[after], "turned to him: not waiting, invincible again");
        Check(run.shots.empty(), "no shot within 150");
    }

    // Far below: facing down, the trigger waits for a turn.
    {
        std::printf("-- a butler in a box: the hero far below, then to its right\n");
        const ButlerRun run = RunButler(data, {0.0f, 90.0f}, [](int f) { return f < 240 ? Vector2{0.0f, -90.0f} : Vector2{200.0f, 90.0f}; }, 480);
        bool down = run.states.size() > 200 && run.states[200] == "WalkDown";
        Check(down && CountEvents(run, "ShootAttack", 0, 240) == 0,
              "facing down: \"ShootAttack\" takes WalkDown nowhere (its transition wants \"Attacking\"): no shot while he's below");
        Check(!run.shots.empty() && run.shots[0].first >= 240 && run.shots[0].first <= 240 + 96 + 20 && run.firstAngle == 90,
              "he goes to its right: its next TryShootAttack turns it, and the trigger still set shoots, right (frame " +
                  std::to_string(run.shots.empty() ? -1 : run.shots[0].first) + ")");
    }
}

} // namespace
