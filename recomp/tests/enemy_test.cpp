// The Golem dungeon's enemies past the BabySlime (slime_test has that one): GolemSoldier first.
//  1. self-checks that need no recording: the screen shake's Random draws (insideUnitSphere's
//     three, DOTween.Shake's), and with the game's data (ML_DATA, default data/extracted) the
//     GolemSoldier from its prefab (the factory's behaviour, its box off in the prefab) and a
//     soldier in a box with the hero standing: it waits its second, chases him, stops within 2
//     tiles + 1 (diamond) of his feet and swings, its clip events in order (ForbidEnemyMovement,
//     LockLookDirection, DoScreenShakeFromEnemyAnimation with its 15 draws, PlayHitOnGroundSound,
//     AllowEnemyMovement, UnLockLookDirection), its AttackCollider on only in the swing's
//     window, grown to 48 x 24 and moved by the curves, with a fixture only while it's on, and
//     each swing hitting him once (the box turned on again enters again; remade as it grows, it
//     doesn't), MasterAudio's draw for the sword's crash (sound_groups.json: its loops'
//     Range(1, 6));
//     The HermitSlime's (prefab HermitSlime): tests/enemy_hermit_checks.h (its patrol, its slam,
//     its root box, its hurtbox, ItemDrain's draws);
//  2. with the oracle's recording (data/oracle/enemy_<prefab>.jsonl, `-oracle-enemy=<prefab>`;
//     skipped if there's none): tests/enemy_replay.h's replay, with the attack trigger per fixed
//     step and frame, the look lock, and the clip events and their Random draws (and
//     tests/enemy_replay_colliders.h's rows: the root box, the hurtboxes, the patrol, ItemDrain).
//
// For the BrokenGolemTurret (enemy_BrokenGolemTurret.jsonl, prefab BrokenGolemTurret), also its
// shots and their pool (game/BrokenGolemTurret.h, game/EnemyProjectile.h, game/PoolManager.h):
//  1. self-checks with the game's data (prefabs.json with the turret's and its shot's fields,
//     animators/BrokenGolemTurret.json and GolemTurretProjectile.json): the prefab made into a
//     BrokenGolemTurretBehaviour with its fields, Init's pool of 6 (a second turret's Init: 12),
//     and three turrets in a walled box with the hero standing:
//       - in line with him: it looks his way (one raycast, free), its shot clip fires three
//         ShootProjectile a cycle, each takes the pool's first; a shot flies by DOMoveX (500 in
//         3 s, linear: start + change * ease rounded once), hits him (OnCollisionEnter2D: the
//         damage, 10 from "BrokenGolemTurret", the push of 50 away from the turret), crashes
//         (the Crash state, its box a trigger) and is back in the pool 0.3 s later (DestroyNow);
//       - a rock beside it on his side: the look's first raycast hits the rock, the second (up)
//         is free; it shoots up, the shots crash on the wall (the impact sound and its draws,
//         no damage, no push), the art turned 180;
//       - rocks on all four sides: canShoot off, the clip's ShootProjectile takes nothing;
//  2. the replay (tests/turret_replay.h): the common one plus the shots, the looks and the pool.
//
// Usage: enemy_test [enemy_GolemSoldier.jsonl] [prefab (GolemSoldier, HermitSlime, BrokenGolemTurret)]
//        (ML_HERMIT_BOX=1: the hermit's root box in its attack frames)
//        (ML_ENEMY_DEBUG=1: every mismatch; ML_ENEMY_CONTACTS=<frame>: the enemies' Box2D
//        contacts after each of that frame's steps; ML_ENEMY_BOX=1: the soldier in a box's
//        frames with its attack box on; ML_TURRET_BOX=1: the turrets in a box' first shot,
//        frame by frame)

#include "enemy_replay.h"
#include "enemy_hermit_checks.h" // (the HermitSlime's self-checks)
#include "turret_replay.h"

namespace {

struct Data {
    std::string dir;
    Value enemyStats, physics2d, soundGroups;
    std::unique_ptr<unity::AnimatorController> controller;
    const game::Prefab* prefab = nullptr;
    bool ok = false;
    // The turret's shot (BrokenGolemTurret only): its prefab and its Animator's controller.
    std::unique_ptr<unity::AnimatorController> projectileController;
    const game::Prefab* projectilePrefab = nullptr;
};

Data LoadData(const std::string& name)
{
    Data d;
    d.dir = std::getenv("ML_DATA") ? std::getenv("ML_DATA") : "data/extracted";
    const std::string constants = ReadFile(d.dir + "/constants.json"), stats = ReadFile(d.dir + "/enemy_stats.json"),
                      prefabs = ReadFile(d.dir + "/prefabs.json"), anim = ReadFile(d.dir + "/animators/" + name + ".json"),
                      physics = ReadFile(d.dir + "/physics2d.json");
    if (constants.empty() || stats.empty() || prefabs.empty() || anim.empty()) return d;
    game::Constants::LoadFromText(constants);
    d.enemyStats = json::Parse(stats);
    if (!physics.empty()) d.physics2d = json::Parse(physics);
    const std::string sounds = ReadFile(d.dir + "/sound_groups.json");
    if (!sounds.empty()) d.soundGroups = json::Parse(sounds);
    game::PrefabLibrary::Load(json::Parse(prefabs));
    d.prefab = game::PrefabLibrary::Find(name, "enemies_golem");
    d.controller = std::make_unique<unity::AnimatorController>(unity::AnimatorController::FromJson(json::Parse(anim)));
    d.ok = d.prefab != nullptr;
    if (name == "BrokenGolemTurret") {
        const std::string shot = ReadFile(d.dir + "/animators/GolemTurretProjectile.json");
        if (!shot.empty())
            d.projectileController = std::make_unique<unity::AnimatorController>(unity::AnimatorController::FromJson(json::Parse(shot)));
        d.projectilePrefab = game::PrefabLibrary::Find("BrokenGolemTurretProjectile", "enemies_golem");
    }
    return d;
}

// ---- 1. Self-checks ---------------------------------------------------------------------

void CheckShakeDraws()
{
    unity::Random::InitState(4242);
    unity::Random::State before = unity::Random::GetState();
    const unity::Random::Vector3 v = unity::Random::insideUnitSphere();
    Check(unity::Random::DrawsBetween(before, unity::Random::GetState(), 8) == 3 && v.x * v.x + v.y * v.y + v.z * v.z <= 1.0001f,
          "Random.insideUnitSphere: three draws, inside the unit sphere");
    // The enemies' rumble: 0.15 s at vibration 50: (int)(50 * 0.15) = 7 waypoints: the first
    // angle, 5 turns, 6 rotations about y.
    before = unity::Random::GetState();
    const int n = unity::ShakeDraws(0.15f, 50, 0.0f, true, false);
    Check(n == 12 && unity::Random::DrawsBetween(before, unity::Random::GetState(), 32) == 12, "DOTween.Shake (vectorBased, 7 waypoints): 12 draws");
    Check(unity::ShakeDraws(0.01f, 10, 90.0f, false, true) == 1, "DOTween.Shake: at least 2 waypoints (the first angle only, z ignored)");
}

// A GolemSoldier in an empty box with the hero standing at the origin.
void CheckSoldierInABox(const Data& data)
{
    game::PolyNav2D nav;
    nav.inflateRadius = 8.0f;
    nav.map = game::PolyNav2D::PolyMap({{{{-240.0f, 130.0f}, {-240.0f, -130.0f}, {240.0f, -130.0f}, {240.0f, 130.0f}}}}, {});
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
    int frameNow = 0;
    std::vector<int> hitFrames;
    hero.dealDamage = [&](float strength, const std::string& source, game::Enemy&) {
        hitFrames.push_back(frameNow);
        Check(source == "GolemSoldier" && strength == 15.0f, "the hit: DealDamageToPlayer(15, \"GolemSoldier\")");
    };
    int vibrations = 0;
    hero.vibrate = [&](float intensity, float duration) { vibrations += intensity == 0.2f && duration == 0.1f; };
    game::EnemyScene scene;
    scene.calls = &calls;
    scene.tweens = &tweens;
    scene.hero = &hero;
    int sounds = 0, rumbles = 0;
    game::MasterAudio audio;
    const bool haveAudio = !data.soundGroups.IsNull() && audio.Load(data.soundGroups, {"Sounds - GolemDungeon", "Sounds - GeneralDungeons"});
    if (haveAudio) scene.audio = &audio;
    else std::printf("(no sound_groups.json (tools/extract/sound_groups.py): the sound's draw not checked)\n");
    scene.playSound = [&](game::Enemy&, const std::string& sound) { sounds += sound == "golem_dungeon_soldier_sword_crash"; };
    scene.rumble = [&](float duration, float strength, Vector2, int vibration) { rumbles += duration == 0.15f && strength == 2.0f && vibration == 50; };
    // The clip events as they come, with the Random draws each made.
    std::vector<std::pair<int, std::string>> events;
    unity::Random::State eventStart{};
    bool shakeDraws = true;
    scene.onAnimationEvent = [&](game::Enemy&, const unity::AnimationEvent& e, bool after) {
        if (!after) {
            events.emplace_back(frameNow, e.function);
            eventStart = unity::Random::GetState();
            return;
        }
        const int draws = unity::Random::DrawsBetween(eventStart, unity::Random::GetState(), 64);
        if (e.function == "DoScreenShakeFromEnemyAnimation") shakeDraws = shakeDraws && draws == 15;
        else if (e.function == "PlayHitOnGroundSound") shakeDraws = shakeDraws && draws == (haveAudio ? 1 : 0);
        else shakeDraws = shakeDraws && draws == 0;
    };

    auto soldier = game::MakeEnemy(*data.prefab, scene, data.controller.get(), {{120.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}});
    auto* behaviour = dynamic_cast<game::GolemSoldierBehaviour*>(soldier->enemyBehaviour.get());
    Check(behaviour != nullptr, "the GolemSoldier prefab makes a GolemSoldierBehaviour");
    if (!behaviour) return;
    Check(soldier->hasAttackCollider && !soldier->attackEnabled && soldier->attackActive && Same(soldier->attackSize, {10.0f, 10.0f}) &&
              Same(soldier->attackOffset, {0.0f, 10.0f}) && soldier->attackLayer == game::kLayerEnemyAttack,
          "its AttackCollider: a 10 x 10 box at (0, 10) on EnemyAttack, off in the prefab");
    Check(soldier->colliderSize.x == 15.0f && soldier->tag == "HardEnemy" && soldier->layer == game::kLayerGroundEnemy,
          "its body: a 15 x 15 box, HardEnemy on GroundEnemy");
    bool hasFields = false;
    for (const game::Behaviour& b : data.prefab->nodes[0].behaviours)
        if (b.cls == "GolemSoldierBehaviour") hasFields = b.Field("attackPushBackwardsForce", -1.0) >= 0.0;
    if (hasFields) Check(behaviour->attackPushBackwardsForce == 100.0f && !behaviour->iDirectionLocked, "its fields (prefabs.py FIELDS): push 100, not locked");
    else std::printf("(prefabs.json without GolemSoldierBehaviour's fields: rerun tools/extract/prefabs.py)\n");
    game::EnemyStatModificator m;
    game::EnemyStatModificator::FromJson(data.enemyStats, "GolemSoldier", m);
    soldier->Init("GolemSoldier", m, game::EnemyStatsContext{});
    Check(soldier->navAgent.maxSpeed == 30.0f && soldier->rigidbody.mass == 12.0f && soldier->enemyStats.strength() == 15.0f,
          "Init: speed 30, mass 12, strength 15");
    physics.Add(*soldier);
    Check(soldier->attackFixture == nullptr, "no attack fixture while the box is off");
    soldier->enemyBehaviour->StartTargetingPlayerAfterTime(1.0f);

    const float dt = 1.0f / 60.0f;
    double time = 0.0, fixedTime = 0.0;
    int targetFrame = -1, attackFrame = -1;
    float attackDistance = 0.0f;
    bool fixtureOnlyWhenOn = true, grew = false, wasOn = false, lockedSeen = false;
    int swingsOn = 0; // times the box came on
    std::vector<int> onFrames;
    auto frame = [&](int f) {
        frameNow = f;
        time += static_cast<double>(dt);
        nav.BeginFrame();
        while (fixedTime + 0.01 <= time) {
            fixedTime += 0.01;
            calls.fixedNow = fixedTime;
            calls.inFixedStep = true;
            world.SyncTransform(heroBody, {0.0f, 0.0f});
            heroBody.SetVelocity({0.0f, 0.0f});
            physics.Step(0.01f);
            // The fixture follows the box (made at the step's sync).
            fixtureOnlyWhenOn = fixtureOnlyWhenOn && ((soldier->attackFixture != nullptr) == soldier->attackEnabled);
            calls.UpdateFixed(fixedTime);
            calls.inFixedStep = false;
        }
        calls.now = time;
        audio.now = time;
        audio.frameCount = f;
        tweens.Update(dt);
        soldier->enemyBehaviour->Update(dt);
        calls.Update(time);
        nav.ResumeCoroutines();
        soldier->UpdateAnimator(dt);
        nav.LateUpdate();
        soldier->navAgent.LateUpdate(dt);
        if (targetFrame < 0 && soldier->enemyBehaviour->targetPlayer) targetFrame = f;
        if (attackFrame < 0 && soldier->enemyBehaviour->enemyState == game::EnemyBehaviour::EnemyState::Attacking) {
            attackFrame = f;
            const Vector2 p = soldier->position();
            attackDistance = std::fabs(p.x - 0.0f) + std::fabs(p.y - -3.5f);
        }
        if (soldier->attackEnabled && !wasOn) {
            swingsOn++;
            onFrames.push_back(f);
        }
        wasOn = soldier->attackEnabled;
        if (std::getenv("ML_ENEMY_BOX") && soldier->attackEnabled)
            std::printf("frame %d: soldier %s state %d box %s fixture %d hits %zu\n", f, Str(soldier->position()).c_str(),
                        soldier->enemyAnimator->CurrentState(), AttackStr(*soldier).c_str(), soldier->attackFixture != nullptr, hitFrames.size());
        grew = grew || (soldier->attackSize.x == 48.0f && soldier->attackSize.y == 24.0f);
        lockedSeen = lockedSeen || behaviour->iDirectionLocked;
    };
    int f = 0;
    for (; f < 600; f++) frame(f);
    Check(targetFrame >= 59 && targetFrame <= 61, "targets the hero a second after (frame " + std::to_string(targetFrame) + ")");
    Check(attackFrame > targetFrame && attackDistance <= 49.0f,
          "chases him and stops to swing within 2 tiles + 1 of his feet (frame " + std::to_string(attackFrame) + ", diamond " +
              std::to_string(attackDistance) + ")");
    Check(soldier->position().x > 30.0f, "it stopped short of him (x " + std::to_string(soldier->position().x) + ")");

    // The swings after the attack began: their events in the clip's order.
    std::vector<std::string> swing;
    for (const auto& e : events)
        if (e.first >= attackFrame) swing.push_back(e.second);
    const char* const kOrder[6] = {"ForbidEnemyMovement", "LockLookDirection", "DoScreenShakeFromEnemyAnimation", "PlayHitOnGroundSound",
                                   "AllowEnemyMovement", "UnLockLookDirection"};
    bool order = swing.size() >= 12;
    for (size_t k = 0; order && k < 12; k++) order = swing[k] == kOrder[k % 6];
    std::string got;
    for (size_t k = 0; k < swing.size() && k < 12; k++) got += " " + swing[k];
    Check(order, "two swings' clip events in order:" + got);
    Check(shakeDraws, "DoScreenShakeFromEnemyAnimation draws 15 (insideUnitSphere 3, the shake 12), PlayHitOnGroundSound 1 (the "
                      "sound's loops), the other events none");
    Check(sounds >= 2 && rumbles >= 2 && vibrations >= 2, "the sword's crash: the sound, the camera's rumble, the pad's");
    Check(fixtureOnlyWhenOn, "the attack fixture exists exactly while the box is on");
    Check(grew, "the box grows to 48 x 24 (AttackLeft's curves)");
    Check(swingsOn >= 3, "the box comes on once per swing (" + std::to_string(swingsOn) + " times)");
    // Each swing's box hits him anew (OnTriggerEnter2D again): a hit within each window after
    // the attack began.
    int swingsHit = 0, swingsAfter = 0, hitsInSwings = 0;
    for (int on : onFrames) {
        if (on < attackFrame) continue;
        swingsAfter++;
        int hits = 0;
        for (int h : hitFrames) hits += h >= on - 1 && h <= on + 16;
        swingsHit += hits > 0;
        hitsInSwings += hits;
    }
    Check(swingsAfter >= 2 && swingsHit == swingsAfter,
          "every swing at him hits him (" + std::to_string(swingsHit) + " of " + std::to_string(swingsAfter) + ", " +
              std::to_string(hitFrames.size()) + " hits in all)");
    // The box is remade as it grows: it enters him once a swing.
    Check(hitsInSwings == swingsAfter, "one hit a swing: " + std::to_string(hitsInSwings) + " in " + std::to_string(swingsAfter));

    // The look is locked from 0.5 s into a swing to 1.0 s: a hero who moves then isn't followed.
    Check(lockedSeen, "LockLookDirection locks the look in a swing");
    game::PolyNav2D::current = nullptr;
}

// ---- The Broken turret ----------------------------------------------------------------

// A Broken turret in a walled box (LevelBoundary all round), the hero standing to its right.
// variant 0: in line with him; 1: a rock beside it on his side; 2: rocks on all four sides.
void CheckTurretInABox(const Data& data, int variant)
{
    const char* const names[3] = {"in line", "a rock on his side", "boxed in by rocks"};
    std::printf("-- a Broken turret in a box (%s)\n", names[variant]);
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
    auto box = [&](const char* path, int layer, const char* tag, Vector2 at, Vector2 size) {
        unity::Collider2D c;
        c.path = path;
        c.layer = layer;
        c.tag = tag;
        c.position = at;
        c.size = size;
        room.Add(c);
    };
    box("Room/Walls/Top", 11, "LevelBoundary", {0.0f, 140.0f}, {500.0f, 20.0f});
    box("Room/Walls/Bottom", 11, "LevelBoundary", {0.0f, -140.0f}, {500.0f, 20.0f});
    box("Room/Walls/Left", 11, "LevelBoundary", {-250.0f, 0.0f}, {20.0f, 300.0f});
    box("Room/Walls/Right", 11, "LevelBoundary", {250.0f, 0.0f}, {20.0f, 300.0f});
    const Vector2 turretAt{-100.0f, 0.0f};
    if (variant >= 1) box("Room/Rocks/East", 8, "Rock", {-75.0f, 0.0f}, {10.0f, 40.0f});
    if (variant == 2) {
        box("Room/Rocks/North", 8, "Rock", {-100.0f, 25.0f}, {40.0f, 10.0f});
        box("Room/Rocks/West", 8, "Rock", {-125.0f, 0.0f}, {10.0f, 40.0f});
        box("Room/Rocks/South", 8, "Rock", {-100.0f, -25.0f}, {40.0f, 10.0f});
    }
    for (const unity::Collider2D& c : room.Colliders())
        world.AddCollider(c, c.layer == 8 ? unity::RigidbodyType2D::Kinematic : unity::RigidbodyType2D::Static, c.layer == 8);
    unity::Rigidbody2DDef heroDef;
    heroDef.mass = 1.0f;
    heroDef.linearDrag = 5.0f;
    heroDef.gravityScale = 0.0f;
    heroDef.constraints = unity::Rigidbody2DDef::FreezeRotation;
    heroDef.collisionDetection = unity::Rigidbody2DDef::Continuous;
    const Vector2 heroAt{60.0f, 0.0f};
    unity::Rigidbody2D& heroBody = world.AddBody(heroDef, heroAt, 0, {1.0f, -3.5f}, {13.0f, 13.0f});
    physics.SetHero(&heroBody);

    unity::DelayedCalls calls;
    unity::Tweens tweens;
    game::EnemyHero hero;
    hero.position = heroAt;
    int frameNow = 0;
    std::vector<int> damageFrames;
    bool damageOk = true;
    hero.dealDamage = [&](float strength, const std::string& source, game::Enemy&) {
        damageFrames.push_back(frameNow);
        damageOk = damageOk && source == "BrokenGolemTurret" && strength == 10.0f;
    };
    std::vector<Vector2> pushes;
    hero.addForce = [&](Vector2 f) { pushes.push_back(f); };
    game::EnemyScene scene;
    scene.calls = &calls;
    scene.tweens = &tweens;
    scene.hero = &hero;
    scene.queries = &room;
    game::MasterAudio audio;
    const bool haveAudio = !data.soundGroups.IsNull() && audio.Load(data.soundGroups, {"Sounds - GolemDungeon", "Sounds - GeneralDungeons"});
    if (haveAudio) scene.audio = &audio;
    int shotSounds = 0, impactSounds = 0, impactDraws = 0;
    scene.playSound = [&](game::Enemy&, const std::string& sound) {
        shotSounds += sound == "golem_dungeon_turret_shot";
        impactSounds += sound == "golem_dungeon_turret_shot_impact";
    };
    game::EnemyProjectiles projectiles(scene);
    projectiles.Register(data.projectilePrefab, data.projectileController.get());
    projectiles.SetPhysics(world, physics);
    projectiles.SetHero(&heroBody);
    projectiles.roomParents = {{0.0f, 0.0f}};
    scene.projectiles = &projectiles;
    unity::Random::InitState(1234 + variant);
    // Each OnCollisionEnter2D: on what, and its draws (the impact sound's).
    std::vector<std::pair<int, std::string>> collisions;
    unity::Random::State before{};
    projectiles.onCollisionEnter = [&](game::EnemyProjectile&, const game::Collision2D& c, bool after) {
        if (!after) {
            collisions.emplace_back(frameNow, c.tag);
            before = unity::Random::GetState();
        } else if (c.tag == "LevelBoundary" || c.tag == "Rock") {
            impactDraws += unity::Random::DrawsBetween(before, unity::Random::GetState(), 64);
        }
    };

    auto turret = game::MakeEnemy(*data.prefab, scene, data.controller.get(), {turretAt, {0.0f, 0.0f}, {0.0f, 0.0f}});
    auto* behaviour = dynamic_cast<game::BrokenGolemTurretBehaviour*>(turret->enemyBehaviour.get());
    Check(behaviour != nullptr, "the BrokenGolemTurret prefab makes a BrokenGolemTurretBehaviour");
    if (!behaviour) return;
    game::EnemyStatModificator m;
    game::EnemyStatModificator::FromJson(data.enemyStats, "BrokenGolemTurret", m);
    turret->Init("BrokenGolemTurret", m, game::EnemyStatsContext{});
    const std::string poolName = "BrokenGolemTurret_BrokenGolemTurretProjectile";
    const game::PoolManager::Pool* pool = projectiles.poolManager.Find(poolName);
    std::unique_ptr<game::Enemy> second;
    if (variant == 0) {
        Check(behaviour->projectilePrefab == "BrokenGolemTurretProjectile" && behaviour->shootDelay == 0.0f &&
                  behaviour->spawnProyectilePositions.empty() && behaviour->enemyDirection == game::HeroDirection::Left,
              "its fields (prefabs.py FIELDS): BrokenGolemTurretProjectile, shootDelay 0, no spawn positions, looking left");
        Check(behaviour->attackPushBackwardsForce == 50.0f && turret->rigidbody.mass == 48.0f && turret->enemyStats.strength() == 10.0f,
              "Init: push 50, mass 48 (its weight), strength 10");
        Check(pool && pool->pooledObjects.size() == 6, "Init fills the pool " + poolName + " with 6");
        // Another turret's Init fills the same pool with 6 more (the floor's turrets share it);
        // it never targets, so its shots stay pooled.
        second = game::MakeEnemy(*data.prefab, scene, data.controller.get(), {{0.0f, 100.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}});
        second->Init("BrokenGolemTurret", m, game::EnemyStatsContext{});
        second->navAgent.OnDisable();
        Check(pool && pool->pooledObjects.size() == 12 && projectiles.All().size() == 12, "a second turret's Init: 12 in the one pool");
        const auto* shot = dynamic_cast<const game::BrokenGolemTurretProjectile*>(projectiles.All()[0]);
        Check(shot && shot->timeToDestroyAfterCrash == 0.3f && shot->layer == 15 && shot->tag == "EnemyProjectile" && shot->boxSize.x == 8.0f &&
                  shot->boxSize.y == 7.0f && shot->rigidbody.gravityScale == 0.0f && !shot->active && shot->body == nullptr &&
                  shot->sprite == "golemturret_projectile_01",
              "its shot from the prefab: 8 x 7 on EnemyProjectile, no gravity, crash wait 0.3, pooled (inactive, no body yet)");
    }
    physics.Add(*turret);
    turret->enemyBehaviour->StartTargetingPlayerAfterTime(1.0f);
    const size_t pooled0 = pool ? pool->pooledObjects.size() : 0;

    const float dt = 1.0f / 60.0f;
    double time = 0.0, fixedTime = 0.0;
    std::vector<std::pair<int, std::string>> events;
    scene.onAnimationEvent = [&](game::Enemy&, const unity::AnimationEvent& e, bool after) {
        if (!after) events.emplace_back(frameNow, e.function);
    };
    std::vector<std::pair<int, game::BrokenGolemTurretProjectile*>> shots;
    behaviour->onShot = [&](game::BrokenGolemTurretBehaviour&, game::BrokenGolemTurretProjectile* p) { shots.emplace_back(frameNow, p); };
    std::vector<int> destroyFrames;
    for (game::EnemyProjectile* p : projectiles.All())
        if (auto* s = dynamic_cast<game::BrokenGolemTurretProjectile*>(p))
            s->onDestroyNow = [&](game::BrokenGolemTurretProjectile&) { destroyFrames.push_back(frameNow); };
    // The first shot's flight, frame by frame (its transform right after each tween update).
    game::BrokenGolemTurretProjectile* first = nullptr;
    int firstFrame = -1, flightFrames = 0;
    bool flightExact = true;
    float elapsed = 0.0f;
    Vector2 firstStart;
    int crashFrame = -1, crashStateFrame = -1, backInPoolFrame = -1;
    bool triggerAfterCrash = false;
    for (int f = 0; f < 420; f++) {
        frameNow = f;
        time += static_cast<double>(dt);
        nav.BeginFrame();
        while (fixedTime + 0.01 <= time) {
            fixedTime += 0.01;
            calls.fixedNow = fixedTime;
            calls.inFixedStep = true;
            world.SyncTransform(heroBody, heroAt);
            heroBody.SetVelocity({0.0f, 0.0f});
            physics.Step(0.01f);
            calls.UpdateFixed(fixedTime);
            calls.inFixedStep = false;
        }
        calls.now = time;
        audio.now = time;
        audio.frameCount = f;
        tweens.Update(dt);
        // The first shot's DOMoveX: start.x + change.x * (elapsed / 3), one rounding.
        if (variant == 0 && first && first->active && !first->crashed && f > firstFrame) {
            elapsed += dt;
            const float e = elapsed / 3.0f;
            const float x = static_cast<float>(static_cast<double>(firstStart.x) + static_cast<double>(500.0f) * static_cast<double>(e));
            flightExact = flightExact && Same(first->position().x, x);
            flightFrames++;
        }
        turret->enemyBehaviour->Update(dt);
        calls.Update(time);
        nav.ResumeCoroutines();
        projectiles.UpdateAnimators(dt);
        turret->UpdateAnimator(dt);
        nav.LateUpdate();
        turret->navAgent.LateUpdate(dt);
        if (!first && !shots.empty()) {
            first = shots[0].second;
            firstFrame = shots[0].first;
            firstStart = first->position();
        }
        if (first && crashFrame < 0 && first->crashed) {
            crashFrame = f;
            triggerAfterCrash = first->isTrigger;
        }
        if (first && crashFrame >= 0 && crashStateFrame < 0 && first->active && first->animator &&
            first->animator->CurrentState() == first->animator->Controller().State("CollisionBladeLeaf"))
            crashStateFrame = f;
        if (first && crashFrame >= 0 && backInPoolFrame < 0 && !first->active) backInPoolFrame = f;
        if (std::getenv("ML_TURRET_BOX") && first && first->active)
            std::printf("frame %d: shot %s body %s v %s trigger %d state %d sprite %s\n", f, Str(first->position()).c_str(),
                        first->body ? Str(first->body->position()).c_str() : "-", first->body ? Str(first->body->velocity()).c_str() : "-", first->isTrigger,
                        first->animator ? first->animator->CurrentState() : -1, first->sprite.c_str());
    }
    // The look and the clip's events.
    int shootEvents = 0, quits = 0;
    for (const auto& e : events) {
        shootEvents += e.second == "ShootProjectile";
        quits += e.second == "QuitAttackState";
    }
    const std::string lookName = variant == 0 ? "right, free" : variant == 1 ? "right on the rock, then up" : "all four on rocks";
    const game::HeroDirection wantDir = variant == 1 ? game::HeroDirection::Back : game::HeroDirection::Right;
    Check(behaviour->enemyDirection == wantDir && behaviour->canShoot == (variant != 2), "the look: " + lookName);
    const auto& rays = behaviour->rays;
    bool raysOk = !rays.empty();
    // tileSize * 2.4f in double, a float argument: 57.600002 (one ulp over 57.6f).
    const float reach = static_cast<float>(24.0 * static_cast<double>(2.4f));
    if (variant == 0) raysOk = raysOk && rays[0].direction.x == 1.0f && !rays[0].hit && Same(rays[0].distance, reach) && reach != 57.6f;
    if (variant == 1)
        raysOk = raysOk && rays.size() >= 2 && rays[0].hit && rays[0].hit.collider->tag == "Rock" && rays[1].direction.y == 1.0f && !rays[1].hit;
    if (variant == 2) raysOk = raysOk && rays.size() >= 4 && rays[0].hit && rays[1].hit && rays[2].hit && rays[3].hit;
    Check(raysOk, "its raycasts (2.4 tiles, RisenObstacle and LevelBoundary): " + lookName);
    Check(shootEvents >= 6 && quits >= 2, "the shot clip: ShootProjectile three a cycle (" + std::to_string(shootEvents) + " in all)");
    if (variant == 2) {
        Check(shots.empty() && pool && pool->pooledObjects.size() == pooled0, "boxed in: no shot taken, the pool untouched");
        game::PolyNav2D::current = nullptr;
        return;
    }
    Check(static_cast<int>(shots.size()) == shootEvents, "each ShootProjectile takes a shot (" + std::to_string(shots.size()) + ")");
    Check(first && first->instance == 0 && first->artRotation == (variant == 0 ? 90.0f : 180.0f) && Same(firstStart, turretAt),
          "the first shot: the pool's first, at the turret, its art turned " + std::string(variant == 0 ? "90" : "180"));
    if (variant == 0)
        Check(flightFrames > 10 && flightExact, "its flight: DOMoveX's start.x + 500 * (t / 3), bit for bit (" + std::to_string(flightFrames) + " frames)");
    else
        Check(first && first->position().x == turretAt.x, "its flight is along y (DOMoveY): x stays");
    Check(crashFrame > firstFrame && triggerAfterCrash, "it crashes (OnCollisionEnter2D): its box a trigger (frame " + std::to_string(crashFrame) + ")");
    Check(crashStateFrame == crashFrame, "the Crash trigger: the crash state in the frame's animation pass after the step");
    // 0.3 s on the fixed clock from the step it crashed in: 18 frames on, give or take one.
    Check(backInPoolFrame >= crashFrame + 17 && backInPoolFrame <= crashFrame + 19,
          "DestroyNow 0.3 s after the crash: back in the pool (frame " + std::to_string(backInPoolFrame) + ")");
    Check(pool && pool->pooledObjects.size() + shots.size() - destroyFrames.size() == pooled0,
          "the pool: every shot taken from its front, every DestroyNow back at its end");
    bool hitsOk = true;
    for (const auto& c : collisions) hitsOk = hitsOk && c.second == (variant == 0 ? "Player" : "LevelBoundary");
    Check(!collisions.empty() && hitsOk,
          "the shots hit " + std::string(variant == 0 ? "him" : "the top wall") + " (" + std::to_string(collisions.size()) + " hits)");
    Check(shotSounds == shootEvents, "PlayShootSound with every shot");
    if (variant == 0) {
        Check(damageOk && !damageFrames.empty() && damageFrames.size() == collisions.size(), "each hit on him: DealDamageToPlayer(10, \"BrokenGolemTurret\")");
        bool pushOk = pushes.size() == collisions.size();
        for (const Vector2& p : pushes) pushOk = pushOk && p.x == 50.0f && p.y == 0.0f;
        Check(pushOk, "each hit pushes him 50 away from the turret (PushPlayerBackwards)");
        Check(impactSounds == 0, "no impact sound on him");
    } else {
        Check(damageFrames.empty() && pushes.empty(), "a wall: no damage, no push");
        Check(impactSounds == static_cast<int>(collisions.size()) && (!haveAudio || impactDraws >= impactSounds),
              "a wall: the impact sound each hit (" + std::to_string(impactDraws) + " draws for " + std::to_string(impactSounds) + ")");
    }
    game::PolyNav2D::current = nullptr;
}

} // namespace

int main(int argc, char** argv)
{
    const std::string file = argc > 1 ? argv[1] : "data/oracle/enemy_GolemSoldier.jsonl";
    const std::string name = argc > 2 ? argv[2] : "GolemSoldier";
    CheckShakeDraws();
    Data data = LoadData(name);
    if (data.ok) {
        if (name == "GolemSoldier") {
            CheckSoldierInABox(data);
        }
        if (name == "HermitSlime") CheckHermitSlime(*data.prefab, *data.controller, data.enemyStats, data.physics2d, data.soundGroups);
        if (name == "BrokenGolemTurret") {
            if (data.projectilePrefab && data.projectileController && data.controller->Param("Shoot") >= 0)
                for (int variant = 0; variant < 3; variant++) CheckTurretInABox(data, variant);
            else
                std::printf("(no BrokenGolemTurretProjectile prefab or animators/GolemTurretProjectile.json: the turret in a box skipped)\n");
        }
    } else {
        std::printf("(no %s data in %s (ML_DATA; animators/%s.json from tools/extract/animators.py): skipping the soldier in a box)\n",
                    name.c_str(), data.dir.c_str(), name.c_str());
    }
    std::printf("self-checks: %d passed, %d failed\n", gPassed, gFailed);
    ReplayData replay;
    replay.enemyStats = data.enemyStats;
    replay.physics2d = data.physics2d;
    replay.prefab = data.prefab;
    replay.controller = data.controller.get();
    replay.soundGroups = data.soundGroups;
    replay.recordWith = "-oracle-enemy=" + name;
    replay.debugEnv = "ML_ENEMY_DEBUG";
    replay.contactsEnv = "ML_ENEMY_CONTACTS";
    TurretReplay turret;
    if (name == "BrokenGolemTurret") {
        turret.projectilePrefab = data.projectilePrefab;
        turret.projectileController = data.projectileController.get();
        replay.hooks = &turret;
    }
    const int replayed = ReplayRecording(file, replay);
    turret.Summary();
    return gFailed || replayed ? 1 : 0;
}
