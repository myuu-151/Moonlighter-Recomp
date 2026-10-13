#pragma once
// enemy_test's self-checks for the KamikazeGolem (game/KamikazeGolem.h, unity/Physics2DOverlap.h,
// game/EnemyJump.h), with the game's data and no recording:
//   - EnemyBehaviour.MakeJump on its own: the rise, the descent its OnComplete starts, x, the
//     callback, transform.DOKill() midway;
//   - Physics2D.OverlapCircleAll over a world of bodies and room colliders: the skin counts, the
//     layer mask (Ignore Raycast left out by default), triggers in or out, a body's collider seen
//     where its body is;
//   - the prefab made into a GolemKamikazeBehaviour with its fields, its AttackCollider's
//     Kinematic body of its own (the box off), HardEnemy on GroundEnemy;
//   - kamikazes in a walled box, entered as DungeonRoom.EnterRoom does it (EnteredInCombat while
//     they're switched off, OnRoomEnter, EnableEnemies half a second later):
//       - "wander": one far from the hero and not targeting him: MoveEnemy every 0.65 s from the
//         room's entry, each a Random.insideUnitCircle and a Range(60, 90) (three draws) to a
//         point up to 90 away, the agent sent there; the Animator walks (Movement, the
//         controller's default state; the Awake trigger came while it was off);
//       - "awake": EnteredInCombat on an active one: in Movement the trigger waits; asleep (Idle):
//         Awake, AllowEnemyMovement, then Movement;
//       - "jump": one 60 from him: once it targets him, JumpTowardsPlayer (one insideUnitCircle,
//         the end within 10 of him, invincible, FlyingEnemy, DOMoveY up then down and DOMoveX);
//         within 20 of him the dash and Explosion; its clip events in order, the sounds' draws
//         MasterAudio's, the jump's tweens killed by StopOnExplode, the attack box made on its
//         own body only while it's on, the hero hit once (15, "KamikazeGolem", pushed 50 away),
//         its own root touched too (HardEnemy: nothing), the explosion's OverlapCircleAll finding
//         itself (invincible: no damage), SelfDestroy (the room's EnemyDefeated, gone at the
//         frame's end, still alive: no drops, no dead sprite, MoveEnemy over);
//       - "neighbour": the same with another kamikaze 50 from the blast that isn't targeting:
//         the explosion kills it (15 against 5 health), its death (its drops when it goes 0.6 s
//         later) and its MoveEnemy still invoked while it's dead, until it's gone.
// Included by enemy_test.cpp (after enemy_replay.h).

#include <Box2D/Box2D.h>

#include "game/EnemyJump.h"
#include "game/KamikazeGolem.h"
#include "unity/Physics2DOverlap.h"

namespace {

void CheckOverlapCircleAll(const Value& physics2d)
{
    unity::Physics2DSettings settings;
    if (!physics2d.IsNull()) settings.Load(physics2d);
    unity::PhysicsWorld world(settings);
    unity::Physics2D room;
    auto box = [&](const char* path, int layer, bool trigger, Vector2 at, Vector2 size) {
        unity::Collider2D c;
        c.path = path;
        c.layer = layer;
        c.trigger = trigger;
        c.position = at;
        c.size = size;
        room.Add(c);
    };
    // Around a circle of 10 at the origin: a wall whose face is 9.995 away (inside with its
    // skin of 0.01), one at 10.03 (out), one on Ignore Raycast, a trigger.
    box("Near", 11, false, {14.995f, 0.0f}, {10.0f, 10.0f});
    box("Far", 11, false, {-15.03f, 0.0f}, {10.0f, 10.0f});
    box("IgnoreRaycast", 2, false, {0.0f, 12.0f}, {10.0f, 10.0f});
    box("Trigger", 11, true, {0.0f, -12.0f}, {10.0f, 10.0f});
    for (const unity::Collider2D& c : room.Colliders()) world.AddCollider(c);
    unity::Rigidbody2DDef def;
    def.gravityScale = 0.0f;
    unity::Rigidbody2D& body = world.AddBody(def, {5.0f, 5.0f}, game::kLayerGroundEnemy, {0.0f, 0.0f}, {4.0f, 4.0f});
    unity::Rigidbody2D& away = world.AddBody(def, {50.0f, 50.0f}, game::kLayerGroundEnemy, {0.0f, 0.0f}, {4.0f, 4.0f});
    auto names = [&](const std::vector<const b2Fixture*>& found) {
        std::vector<std::string> out;
        for (const b2Fixture* f : found) {
            if (const unity::Collider2D* c = world.ColliderOf(f)) out.push_back(c->path);
            else out.push_back(f->GetBody() == body.Body() ? "body" : f->GetBody() == away.Body() ? "away" : "?");
        }
        std::sort(out.begin(), out.end());
        return out;
    };
    auto all = names(unity::OverlapCircleAll(world, {0.0f, 0.0f}, 10.0f));
    Check(all == std::vector<std::string>{"Near", "Trigger", "body"},
          "OverlapCircleAll(0, 10): the near wall (its skin), the trigger, the body; not the far wall, Ignore Raycast or the body away");
    auto noTriggers = names(unity::OverlapCircleAll(world, {0.0f, 0.0f}, 10.0f, unity::kDefaultRaycastLayers, false));
    Check(noTriggers == std::vector<std::string>{"Near", "body"}, "OverlapCircleAll without queriesHitTriggers: no trigger");
    auto everything = names(unity::OverlapCircleAll(world, {0.0f, 0.0f}, 10.0f, ~0));
    Check(everything.size() == 4, "OverlapCircleAll with every layer: Ignore Raycast too");
    // A body moved by its transform is seen where its body is once synced.
    world.SyncTransform(away, {0.0f, -5.0f});
    Check(names(unity::OverlapCircleAll(world, {0.0f, 0.0f}, 10.0f)).size() == 4, "a synced body is seen where it now is");
}

struct KamikazeBox {
    game::PolyNav2D nav;
    unity::Physics2DSettings settings;
    std::unique_ptr<unity::PhysicsWorld> world;
    std::unique_ptr<game::EnemyPhysics> physics;
    unity::Physics2D room;
    unity::Rigidbody2D* heroBody = nullptr;
    unity::DelayedCalls calls;
    unity::Tweens tweens;
    game::EnemyHero hero;
    game::EnemyScene scene;
    game::MasterAudio audio;
    bool haveAudio = false;
    std::vector<std::unique_ptr<game::Enemy>> enemies;
    int frameNow = 0;
    double time = 0.0, fixedTime = 0.0;
    // What happened.
    struct HeroHit {
        int frame;
        float strength;
        std::string source;
    };
    std::vector<HeroHit> heroHits;
    std::vector<Vector2> pushes;
    int strongVibrations = 0, defeated = 0, drops = 0, deadSprites = 0, deaths = 0;
    std::vector<std::pair<int, std::string>> events; // (enemy index, function)
    std::vector<int> eventDraws;
    unity::Random::State eventStart{};
    std::vector<std::pair<int, game::EnemyBehaviour::AttackOther>> attackHits; // (enemy index, other)

    int IndexOf(const game::Enemy& e) const
    {
        for (size_t i = 0; i < enemies.size(); i++)
            if (enemies[i].get() == &e) return static_cast<int>(i);
        return -1;
    }

    KamikazeBox(const Value& physics2d, const Value& soundGroups)
    {
        nav.inflateRadius = 8.0f;
        nav.map = game::PolyNav2D::PolyMap({{{{-240.0f, 130.0f}, {-240.0f, -130.0f}, {240.0f, -130.0f}, {240.0f, 130.0f}}}}, {});
        nav.GenerateNodes();
        game::PolyNav2D::current = &nav;
        if (!physics2d.IsNull()) settings.Load(physics2d);
        world = std::make_unique<unity::PhysicsWorld>(settings);
        physics = std::make_unique<game::EnemyPhysics>(*world);
        auto wall = [&](const char* path, Vector2 at, Vector2 size) {
            unity::Collider2D c;
            c.path = path;
            c.layer = 11;
            c.tag = "LevelBoundary";
            c.position = at;
            c.size = size;
            room.Add(c);
        };
        wall("Room/Walls/Top", {0.0f, 140.0f}, {500.0f, 20.0f});
        wall("Room/Walls/Bottom", {0.0f, -140.0f}, {500.0f, 20.0f});
        wall("Room/Walls/Left", {-250.0f, 0.0f}, {20.0f, 300.0f});
        wall("Room/Walls/Right", {250.0f, 0.0f}, {20.0f, 300.0f});
        for (const unity::Collider2D& c : room.Colliders()) world->AddCollider(c);
        unity::Rigidbody2DDef heroDef;
        heroDef.mass = 1.0f;
        heroDef.linearDrag = 5.0f;
        heroDef.gravityScale = 0.0f;
        heroDef.constraints = unity::Rigidbody2DDef::FreezeRotation;
        heroDef.collisionDetection = unity::Rigidbody2DDef::Continuous;
        heroBody = &world->AddBody(heroDef, {0.0f, 0.0f}, 0, {1.0f, -3.5f}, {13.0f, 13.0f});
        physics->SetHero(heroBody);
        hero.position = {0.0f, 0.0f};
        hero.dealDamage = [this](float strength, const std::string& source, game::Enemy&) { heroHits.push_back({frameNow, strength, source}); };
        hero.addForce = [this](Vector2 force) { pushes.push_back(force); };
        hero.vibrate = [this](float intensity, float duration) { strongVibrations += intensity == 0.4f && duration == 0.1f; };
        scene.calls = &calls;
        scene.tweens = &tweens;
        scene.hero = &hero;
        haveAudio = !soundGroups.IsNull() && audio.Load(soundGroups, {"Sounds - GolemDungeon", "Sounds - GeneralDungeons"});
        if (haveAudio) scene.audio = &audio;
        scene.onEnemyDefeated = [this](game::Enemy&) { defeated++; };
        scene.dropItemsOnDeath = [this](game::Enemy&) { drops++; };
        scene.spawnEnemyDeadSprite = [this](const std::string&, Vector2) { deadSprites++; };
        scene.onAnimationEvent = [this](game::Enemy& e, const unity::AnimationEvent& ev, bool after) {
            if (!after) {
                events.emplace_back(IndexOf(e), ev.function);
                eventStart = unity::Random::GetState();
                return;
            }
            eventDraws.push_back(unity::Random::DrawsBetween(eventStart, unity::Random::GetState(), 64));
        };
    }
    ~KamikazeBox() { game::PolyNav2D::current = nullptr; }

    game::Enemy& Add(const game::Prefab& prefab, const unity::AnimatorController& controller, const Value& enemyStats, Vector2 at)
    {
        enemies.push_back(game::MakeEnemy(prefab, scene, &controller, {at, {0.0f, 0.0f}, {0.0f, 0.0f}}));
        game::Enemy& e = *enemies.back();
        game::EnemyStatModificator m;
        game::EnemyStatModificator::FromJson(enemyStats, "KamikazeGolem", m);
        e.Init("KamikazeGolem", m, game::EnemyStatsContext{});
        if (auto* k = dynamic_cast<game::GolemKamikazeBehaviour*>(e.enemyBehaviour.get())) k->registerDeath = [this] { deaths++; };
        e.SetActive(false); // (DisableEnemies: off until EnableEnemies)
        physics->Add(e);
        return e;
    }

    // DungeonRoom.EnterRoom: IsInCombat (EnteredInCombat), TriggerOnRoomEnter, and
    // Invoke("EnableEnemies", 0.5): each switched on, its targeting `targetDelay` later (< 0: none).
    void EnterRoom(const std::vector<float>& targetDelay)
    {
        hero.EnteredInCombat();
        for (auto& e : enemies) e->enemyBehaviour->EnemyRoom_OnRoomEnter();
        calls.Invoke(this, "EnableEnemies", 0.5f, [this, targetDelay] {
            for (size_t i = 0; i < enemies.size(); i++) {
                enemies[i]->SetActive(true);
                if (i < targetDelay.size() && targetDelay[i] >= 0.0f) enemies[i]->enemyBehaviour->StartTargetingPlayerAfterTime(targetDelay[i]);
            }
        });
    }

    void Frame()
    {
        const float dt = 1.0f / 60.0f;
        frameNow++;
        time += static_cast<double>(dt);
        nav.BeginFrame();
        while (fixedTime + 0.01 <= time) {
            fixedTime += 0.01;
            calls.fixedNow = fixedTime;
            calls.inFixedStep = true;
            world->SyncTransform(*heroBody, {0.0f, 0.0f});
            heroBody->SetVelocity({0.0f, 0.0f});
            physics->Step(0.01f);
            for (const game::EnemyPhysics::Hit& h : physics->LastHits()) attackHits.emplace_back(IndexOf(*h.enemy), physics->OtherOf(h));
            calls.UpdateFixed(fixedTime);
            calls.inFixedStep = false;
        }
        calls.now = time;
        audio.now = time;
        audio.frameCount = frameNow;
        tweens.Update(dt);
        for (auto& e : enemies)
            if (!e->destroyed && e->active) e->enemyBehaviour->Update(dt);
        calls.Update(time);
        nav.ResumeCoroutines();
        for (auto& e : enemies) e->UpdateAnimator(dt);
        nav.LateUpdate();
        for (auto& e : enemies)
            if (!e->destroyed && e->active) e->navAgent.LateUpdate(dt);
        calls.EndOfFrame();
    }
};

game::GolemKamikazeBehaviour* Kami(game::Enemy& e) { return dynamic_cast<game::GolemKamikazeBehaviour*>(e.enemyBehaviour.get()); }

void CheckKamikazePrefab(const game::Prefab& prefab, const unity::AnimatorController& controller, const Value& enemyStats, const Value& physics2d,
                         const Value& soundGroups)
{
    KamikazeBox box(physics2d, soundGroups);
    game::Enemy& k = box.Add(prefab, controller, enemyStats, {100.0f, 0.0f});
    game::GolemKamikazeBehaviour* b = Kami(k);
    Check(b != nullptr, "the KamikazeGolem prefab makes a GolemKamikazeBehaviour");
    if (!b) return;
    Check(k.tag == "HardEnemy" && k.layer == game::kLayerGroundEnemy && k.colliderSize.x == 10.0f && Same(k.colliderOffset, {0.0f, -6.0f}),
          "its body: a 10 x 10 box at (0, -6), HardEnemy on GroundEnemy");
    Check(k.hasAttackCollider && !k.attackEnabled && k.attackHasRigidbody && k.attackRigidbody.bodyType == unity::RigidbodyType2D::Kinematic &&
              k.attackBody && k.attackBody != k.body && k.attackBody->bodyType() == unity::RigidbodyType2D::Kinematic && !k.attackFixture,
          "its AttackCollider: off, on a Kinematic Rigidbody2D of its own");
    bool hasFields = false;
    for (const game::Behaviour& f : prefab.nodes[0].behaviours)
        if (f.cls == "GolemKamikazeBehaviour") hasFields = f.Field("attackPushBackwardsForce", -1.0) >= 0.0;
    if (hasFields) Check(b->attackPushBackwardsForce == 50.0f && b->canMove && b->fallInHoles, "its fields (prefabs.py FIELDS): push 50, can move, falls in holes");
    else std::printf("(prefabs.json without GolemKamikazeBehaviour's fields: rerun tools/extract/prefabs.py)\n");
    Check(k.enemyStats.maxHealth() == 5.0f && k.enemyStats.strength() == 15.0f && k.rigidbody.mass == 12.0f && b->subscribedToCombat &&
              b->subscribedToRoomExit,
          "Init: health 5, strength 15, mass 12, on EnteredInCombat and the room's exit");
}

void CheckKamikazeWander(const game::Prefab& prefab, const unity::AnimatorController& controller, const Value& enemyStats, const Value& physics2d,
                         const Value& soundGroups)
{
    KamikazeBox box(physics2d, soundGroups);
    game::Enemy& k = box.Add(prefab, controller, enemyStats, {150.0f, 60.0f});
    game::GolemKamikazeBehaviour* b = Kami(k);
    if (!b) return;
    unity::Random::InitState(1234);
    // The wanders: each from where it was, its draws.
    std::vector<int> moveFrames;
    bool wandersOk = true, drawsOk = true;
    unity::Random::State before{};
    Vector2 from;
    b->onMoveEnemy = [&](game::GolemKamikazeBehaviour& self, bool after) {
        if (!after) {
            before = unity::Random::GetState();
            from = self.enemy.position();
            moveFrames.push_back(box.frameNow);
            return;
        }
        drawsOk = drawsOk && unity::Random::DrawsBetween(before, unity::Random::GetState(), 16) == 3;
        const float d = unity::Distance(from, self.lastWander);
        wandersOk = wandersOk && d <= 90.01f && (!self.lastWanderValid || Same(self.enemy.navAgent.primeGoal(), self.lastWander));
    };
    box.EnterRoom({-1.0f}); // (not targeting him)
    const int enterFrame = box.frameNow;
    for (int f = 0; f < 200; f++) box.Frame();
    // 0.65 s apart from the room's entry: frames 39, 78, 117, 156, 195 (1/60 s each).
    std::string got;
    for (int m : moveFrames) got += " " + std::to_string(m);
    bool spaced = moveFrames.size() == 5;
    for (size_t i = 0; spaced && i < moveFrames.size(); i++) {
        const double due = 0.65 * static_cast<double>(i + 1);
        const int expect = enterFrame + static_cast<int>(std::ceil(due * 60.0 - 1e-6));
        spaced = moveFrames[i] == expect;
    }
    Check(spaced, "MoveEnemy every 0.65 s from the room's entry (InvokeRepeating), frames" + got);
    Check(b->randomMoves == 5 && drawsOk, "each MoveEnemy far from him wanders: insideUnitCircle and Range(60, 90), three draws");
    Check(wandersOk, "each wander picks a point up to 90 away (the circle's point times 60 to 89), the agent sent there if PolyNav takes it");
    Check(k.enemyAnimator->GetCurrentAnimatorStateInfo().IsName("Movement") && !k.enemyAnimator->GetBool("Awake") && b->jumps == 0,
          "walking all along (the controller's default state is Movement; EnteredInCombat came while it was off: no Awake), no jump");
    Check(k.position().x != 150.0f || k.position().y != 60.0f, "the agent moved it");
    b->OnExitRoom();
    const int calls = b->moveEnemyCalls;
    for (int f = 0; f < 60; f++) box.Frame();
    Check(b->moveEnemyCalls == calls, "OnExitRoom cancels MoveEnemy");
}

void CheckKamikazeAwake(const game::Prefab& prefab, const unity::AnimatorController& controller, const Value& enemyStats, const Value& physics2d,
                        const Value& soundGroups)
{
    KamikazeBox box(physics2d, soundGroups);
    game::Enemy& k = box.Add(prefab, controller, enemyStats, {150.0f, 60.0f});
    game::GolemKamikazeBehaviour* b = Kami(k);
    if (!b) return;
    box.EnterRoom({-1.0f});
    for (int f = 0; f < 40; f++) box.Frame(); // (switched on at 0.5 s)
    // In Movement (the default state) the trigger waits: only Idle goes to Awake.
    box.hero.EnteredInCombat();
    box.Frame();
    Check(k.enemyAnimator->GetBool("Awake") && k.enemyAnimator->GetCurrentAnimatorStateInfo().IsName("Movement"),
          "EnteredInCombat on an active one in Movement: the Awake trigger set, waiting");
    k.enemyAnimator->ResetTrigger("Awake");
    k.enemyAnimator->Play("Idle");
    b->canMove = false;
    box.hero.EnteredInCombat();
    bool awake = false, movement = false;
    for (int f = 0; f < 60; f++) {
        box.Frame();
        awake = awake || k.enemyAnimator->GetCurrentAnimatorStateInfo().IsName("Awake");
        movement = movement || (awake && k.enemyAnimator->GetCurrentAnimatorStateInfo().IsName("Movement"));
    }
    bool allowed = false;
    for (const auto& e : box.events) allowed = allowed || e.second == "AllowEnemyMovement";
    Check(awake && movement && allowed && b->canMove, "EnteredInCombat on an active one asleep (Idle): Awake (AllowEnemyMovement), then Movement");
}

// One 60 to the hero's left; `neighbour`: another, not targeting, 50 from where the first blows up.
void CheckKamikazeJump(const game::Prefab& prefab, const unity::AnimatorController& controller, const Value& enemyStats, const Value& physics2d,
                       const Value& soundGroups, bool neighbour)
{
    const std::string tag = neighbour ? " (with a neighbour)" : "";
    KamikazeBox box(physics2d, soundGroups);
    unity::Random::InitState(neighbour ? 99 : 7);
    game::Enemy& k = box.Add(prefab, controller, enemyStats, {-60.0f, 0.0f});
    game::GolemKamikazeBehaviour* b = Kami(k);
    game::Enemy* n = neighbour ? &box.Add(prefab, controller, enemyStats, {30.0f, 40.0f}) : nullptr;
    if (!b) return;
    box.EnterRoom({1.0f, -1.0f});
    int jumpFrame = -1, explosionFrame = -1, selfDestroyFrame = -1, destroyedFrame = -1, boxOnFrames = 0, nDeathFrame = -1, nGoneFrame = -1;
    bool flyingInJump = true, invincibleInJump = true, fixtureOnOwnBody = true, fixtureOnlyWhenOn = true;
    Vector2 jumpEnd;
    bool nInvokingDead = false, nInvokingGone = true;
    for (int f = 0; f < 240; f++) {
        box.Frame();
        if (k.destroyed) {
            if (destroyedFrame < 0) destroyedFrame = box.frameNow;
        } else {
            if (jumpFrame < 0 && b->jumps > 0) {
                jumpFrame = box.frameNow;
                jumpEnd = b->jumpEndPosition;
            }
            if (jumpFrame >= 0 && explosionFrame < 0) {
                flyingInJump = flyingInJump && k.layer == game::kLayerFlyingEnemy;
                invincibleInJump = invincibleInJump && k.enemyStats.Invencible();
            }
            if (explosionFrame < 0 && k.enemyAnimator->GetCurrentAnimatorStateInfo().IsName("Explosion")) explosionFrame = box.frameNow;
            fixtureOnlyWhenOn = fixtureOnlyWhenOn && ((k.attackFixture != nullptr) == k.attackEnabled);
            if (k.attackFixture) {
                boxOnFrames++;
                fixtureOnOwnBody = fixtureOnOwnBody && k.attackFixture->GetBody() == k.attackBody->Body();
            }
        }
        if (selfDestroyFrame < 0)
            for (const auto& e : box.events)
                if (e.first == 0 && e.second == "SelfDestroy") selfDestroyFrame = box.frameNow;
        if (n) {
            if (nDeathFrame < 0 && !n->enemyStats.isAlive()) {
                nDeathFrame = box.frameNow;
                nInvokingDead = box.calls.IsInvoking(Kami(*n), "MoveEnemy");
            }
            if (nGoneFrame < 0 && n->destroyed) {
                nGoneFrame = box.frameNow;
                nInvokingGone = box.calls.IsInvoking(Kami(*n), "MoveEnemy");
            }
        }
    }
    // The targeting a second after EnableEnemies (1.5 s: frame 90), the jump at once.
    Check(jumpFrame >= 90 && jumpFrame <= 91 && b->jumps == 1, "it jumps at him as soon as it targets him (frame " + std::to_string(jumpFrame) + ")" + tag);
    Check(unity::Distance(jumpEnd, {0.0f, 0.0f}) <= 10.0f, "the jump's end within 10 of him (" + Str(jumpEnd) + ")" + tag);
    Check(flyingInJump && invincibleInJump, "in the jump: on FlyingEnemy, invincible" + tag);
    Check(explosionFrame > jumpFrame && explosionFrame - jumpFrame <= 20, "within 20 of him: the dash and Explosion (frame " + std::to_string(explosionFrame) + ")" + tag);
    // The clip events in order: the jump's sound, then the explosion's.
    std::vector<std::string> mine;
    std::vector<int> draws;
    for (size_t i = 0; i < box.events.size(); i++)
        if (box.events[i].first == 0) {
            mine.push_back(box.events[i].second);
            draws.push_back(i < box.eventDraws.size() ? box.eventDraws[i] : -1);
        }
    const std::vector<std::string> expect = {"PlayKamikazeJumpSound",     "ForbidEnemyMovement", "PlayKamikazeExplosionSound", "StopOnExplode",
                                             "VibrateOnExplosion",        "CreateExplosionToDamageEnemies", "SelfDestroy"};
    std::string got;
    for (const std::string& m : mine) got += " " + m;
    Check(mine == expect, "its clip events in order:" + got + tag);
    bool drawsOk = draws.size() == expect.size();
    for (size_t i = 0; drawsOk && i < draws.size(); i++) {
        const bool sound = mine[i] == "PlayKamikazeJumpSound" || mine[i] == "PlayKamikazeExplosionSound";
        drawsOk = sound ? draws[i] >= 0 && (box.haveAudio || draws[i] == 0) : draws[i] == 0;
    }
    Check(drawsOk, "the sounds' draws are MasterAudio's, the other events draw nothing" + tag);
    Check(box.tweens.Empty(), "no tween left on it (the jump's done or killed by StopOnExplode)" + tag);
    Check(boxOnFrames > 0 && fixtureOnOwnBody && fixtureOnlyWhenOn, "the attack box: a fixture only while it's on, on its own body" + tag);
    bool selfTouched = false;
    int heroTouches = 0;
    for (const auto& h : box.attackHits) {
        if (h.first != 0) continue;
        selfTouched = selfTouched || (h.second.enemy == &k && h.second.tag == "HardEnemy");
        heroTouches += h.second.player;
    }
    Check(heroTouches == 1 && box.heroHits.size() == 1 && box.heroHits[0].strength == 15.0f && box.heroHits[0].source == "KamikazeGolem",
          "the box hits him once: DealDamageToPlayer(15, \"KamikazeGolem\")" + tag);
    Check(box.pushes.size() == 1 && std::fabs(unity::Magnitude(box.pushes[0]) - 50.0f) < 1e-3f, "and pushes him 50 away" + tag);
    Check(selfTouched, "its box touches its own root too (HardEnemy: nothing)" + tag);
    Check(box.strongVibrations == 1, "VibrateOnExplosion: the strong rumble" + tag);
    bool selfInBlast = false;
    for (game::Enemy* e : b->lastExplosionHits) selfInBlast = selfInBlast || e == &k;
    Check(b->explosions == 1 && selfInBlast && k.enemyStats.isAlive() && k.enemyStats.CurrentHealth() == 5.0f,
          "the explosion finds itself (invincible: no damage)" + tag);
    Check(selfDestroyFrame > explosionFrame && destroyedFrame == selfDestroyFrame,
          "SelfDestroy: gone at that frame's end (frames " + std::to_string(selfDestroyFrame) + ", " + std::to_string(destroyedFrame) + ")" + tag);
    Check(k.enemyStats.isAlive() && box.defeated >= 1 && b->subscribedToRoomEnter == false && b->subscribedToRoomExit == false,
          "SelfDestroy: Die (the room's EnemyDefeated), still alive, off the room's events" + tag);
    const int calls = b->moveEnemyCalls;
    for (int f = 0; f < 60; f++) box.Frame();
    Check(b->moveEnemyCalls == calls, "no MoveEnemy once it's gone" + tag);
    if (!n) {
        Check(box.drops == 0 && box.deadSprites == 0 && box.deaths == 0, "no drops, no dead sprite, no death counted");
        return;
    }
    bool nHit = false;
    for (game::Enemy* e : b->lastExplosionHits) nHit = nHit || e == n;
    Check(nHit && nDeathFrame >= explosionFrame && !n->enemyStats.isAlive() && n->layer == game::kLayerTransparentFX,
          "the blast kills the neighbour (15 against 5 health)" + tag);
    Check(nGoneFrame > nDeathFrame && box.drops == 1 && box.deadSprites == 0 && box.deaths == 1,
          "its death: gone 0.6 s later with its drops (no dead sprite: the prefab has none), counted" + tag);
    Check(nInvokingDead && !nInvokingGone, "its MoveEnemy still invoked while it's dead, not once it's gone" + tag);
}

// EnemyBehaviour.MakeJump (game/EnemyJump.h) on its own, at 60 fps from (0, 0) to (30, 10),
// height 20, 0.3 s: the rise (DOMoveY to (10 + 0) / 2 + 20 = 25 over 0.15 s, OutCubic) ends in
// the update where its time reaches 0.15 (the 9th), its OnComplete's descent (DOMoveY to 10
// over jumpTime / 2, InCubic, the fields as they are then) starts in the next update and ends,
// with DOMoveX (Linear, 0.3 s), in the 18th, where the callback runs once; each value FloatPlugin's
// start + change * ease. transform.DOKill() midway: nothing moves after, no descent, no callback.
void CheckMakeJump(const game::Prefab& prefab, const unity::AnimatorController& controller, const Value& enemyStats, const Value& physics2d,
                   const Value& soundGroups)
{
    KamikazeBox box(physics2d, soundGroups);
    game::Enemy& k = box.Add(prefab, controller, enemyStats, {0.0f, 0.0f});
    k.SetActive(true);
    const float dt = 1.0f / 60.0f;
    for (int run = 0; run < 2; run++) {
        const bool kill = run == 1;
        k.setPosition({0.0f, 0.0f});
        int calls = 0, callFrame = -1;
        game::MakeJump(k, {30.0f, 10.0f}, 20.0f, 0.3f, [&] { calls++; }, unity::Ease::Linear);
        const bool fields = Same(k.enemyBehaviour->jumpEndPosition, {30.0f, 10.0f}) && k.enemyBehaviour->jumpTime == 0.3f;
        bool path = true, peak = false;
        float rise = 0.0f, fall = 0.0f, x = 0.0f;
        bool falling = false;
        Vector2 killedAt;
        for (int f = 1; f <= 24; f++) {
            if (kill && f == 5) {
                box.tweens.KillTarget(&k);
                killedAt = k.position();
            }
            box.tweens.Update(dt);
            if (calls && callFrame < 0) callFrame = f;
            if (kill && f >= 5) {
                path = path && Same(k.position(), killedAt);
                continue;
            }
            // The expected values: each tween's time on by dt per update, clamped at its end.
            x += dt;
            const float xt = x >= 0.3f ? 0.3f : x;
            float ex = static_cast<float>(0.0 + 30.0 * static_cast<double>(unity::EaseEvaluate(unity::Ease::Linear, xt, 0.3f)));
            float ey;
            if (!falling) {
                rise += dt;
                const float rt = rise >= 0.15f ? 0.15f : rise;
                ey = static_cast<float>(0.0 + 25.0 * static_cast<double>(unity::EaseEvaluate(unity::Ease::OutCubic, rt, 0.15f)));
                if (rise >= 0.15f) {
                    falling = true; // (the descent's first update is the next one)
                    peak = ey == 25.0f && f == 9;
                }
            } else {
                fall += dt;
                const float ft = fall >= 0.15f ? 0.15f : fall;
                ey = static_cast<float>(25.0 + -15.0 * static_cast<double>(unity::EaseEvaluate(unity::Ease::InCubic, ft, 0.15f)));
            }
            if (f > 18) { // (both done: no tween left to move it)
                ex = 30.0f;
                ey = 10.0f;
            }
            path = path && Same(k.position(), {ex, ey});
        }
        if (!kill) {
            Check(fields && path && peak && Same(k.position(), {30.0f, 10.0f}),
                  "MakeJump: its fields, the rise to 25 (OutCubic) ending in the 9th update, the descent (InCubic) from the next, x Linear, at (30, 10)");
            Check(calls == 1 && callFrame == 18 && box.tweens.Empty(), "MakeJump: the callback once, in the 18th update (the descent's end), no tween left");
        } else {
            Check(path && calls == 0 && box.tweens.Empty(), "MakeJump: transform.DOKill() in the rise: it stays, no descent, no callback");
        }
    }
}

void CheckKamikazeGolem(const game::Prefab& prefab, const unity::AnimatorController& controller, const Value& enemyStats, const Value& physics2d,
                        const Value& soundGroups)
{
    CheckOverlapCircleAll(physics2d);
    CheckMakeJump(prefab, controller, enemyStats, physics2d, soundGroups);
    CheckKamikazePrefab(prefab, controller, enemyStats, physics2d, soundGroups);
    CheckKamikazeWander(prefab, controller, enemyStats, physics2d, soundGroups);
    CheckKamikazeAwake(prefab, controller, enemyStats, physics2d, soundGroups);
    CheckKamikazeJump(prefab, controller, enemyStats, physics2d, soundGroups, false);
    CheckKamikazeJump(prefab, controller, enemyStats, physics2d, soundGroups, true);
}

} // namespace
