#pragma once
// enemy_test's self-checks of the FlyingKamikazeGolem (game/FlyingKamikazeGolem.h), with the
// game's data (prefabs.json with FlyingKamikazeGolemBehaviour's fields, animators/
// FlyingKamikazeGolem.json, enemy_stats.json, sound_groups.json):
//   - OutQuint as DOTween's IL computes it (0 at 0, 1 at the end, 1 - 0.5^5 halfway);
//   - the prefab: the behaviour and its fields, the root box off (no fixture), the attack child
//     inactive (no fixture), mass 12; its child's EventSounds enable sound
//     (golem_dungeon_flyingrepair_loop), played with its draws at Instantiate and SetActive(true);
//   - a golem in a walled box (the room's bounds inside the walls) with the hero standing:
//       - it targets him a second after, prepares (the Attack trigger) and drifts towards the
//         origin (bounds still empty) until the clip's MakeChargeAttack 0.333 s on;
//       - the charge: the bounds from the room, the root box and attack child on (fixtures made),
//         its draws (the sound's loops, Range(-20, 21)), the aim with the game's bug (computed
//         here from the hero and the golem where they were), the look;
//       - EndCharge 0.5 s after (the attack fixture gone), the return: the body's velocity
//         `start + change * OutQuint` rounded once each frame (DOTween.To on a Vector2) to zero
//         1.5 s on, then the root box off, EnableCharge Range(2, 6) s later, and the next charge;
//       - the charges hit him (the attack trigger's DealDamageToPlayer) only while the child is on;
//       - outside the bounds while charging: stopped, `angle` towards him, clamped 2 inside.

#include <cmath>
#include <string>
#include <vector>

#include "enemy_replay.h"
#include "game/FlyingKamikazeGolem.h"

namespace {

void CheckFlyingKamikazeGolem(const game::Prefab& prefab, const unity::AnimatorController& controller, const Value& enemyStats, const Value& physics2d,
                              const Value& soundGroups)
{
    using Golem = game::FlyingKamikazeGolemBehaviour;
    using unity::mono::D;
    using unity::mono::F;
    std::printf("-- a FlyingKamikazeGolem in a box\n");

    // OutQuint (DOTween's EaseManager).
    Check(unity::EaseEvaluate(unity::Ease::OutQuint, 0.0f, 1.5f) == 0.0f && unity::EaseEvaluate(unity::Ease::OutQuint, 1.5f, 1.5f) == 1.0f &&
              unity::EaseEvaluate(unity::Ease::OutQuint, 0.75f, 1.5f) == 0.96875f,
          "OutQuint: 0 at the start, 1 at the end, 1 - 0.5^5 halfway");
    {
        // An inexact quotient: the first factor the double, the other four the float.
        const float time = 0.1f, duration = 1.5f;
        const double q = D(time) / D(duration) - 1.0;
        const double tq = D(F(q));
        Check(unity::EaseEvaluate(unity::Ease::OutQuint, time, duration) == F(q * tq * tq * tq * tq + 1.0), "OutQuint: (t / d - 1) unrounded first, then four times the float");
    }

    game::PolyNav2D nav;
    nav.inflateRadius = 8.0f;
    nav.map = game::PolyNav2D::PolyMap({{{{-240.0f, 130.0f}, {-240.0f, -130.0f}, {240.0f, -130.0f}, {240.0f, 130.0f}}}}, {});
    nav.GenerateNodes();
    game::PolyNav2D::current = &nav;

    unity::Physics2DSettings settings;
    if (!physics2d.IsNull()) settings.Load(physics2d);
    unity::PhysicsWorld world(settings);
    game::EnemyPhysics physics(world);
    unity::Physics2D room;
    auto box = [&](const char* path, Vector2 at, Vector2 size) {
        unity::Collider2D c;
        c.path = path;
        c.layer = 11;
        c.tag = "LevelBoundary";
        c.position = at;
        c.size = size;
        room.Add(c);
    };
    box("Room/Walls/Top", {0.0f, 140.0f}, {500.0f, 20.0f});
    box("Room/Walls/Bottom", {0.0f, -140.0f}, {500.0f, 20.0f});
    box("Room/Walls/Left", {-250.0f, 0.0f}, {20.0f, 300.0f});
    box("Room/Walls/Right", {250.0f, 0.0f}, {20.0f, 300.0f});
    for (const unity::Collider2D& c : room.Colliders()) world.AddCollider(c);
    unity::Rigidbody2DDef heroDef;
    heroDef.mass = 1.0f;
    heroDef.linearDrag = 5.0f;
    heroDef.gravityScale = 0.0f;
    heroDef.constraints = unity::Rigidbody2DDef::FreezeRotation;
    heroDef.collisionDetection = unity::Rigidbody2DDef::Continuous;
    const Vector2 heroAt{60.0f, 40.0f};
    unity::Rigidbody2D& heroBody = world.AddBody(heroDef, heroAt, 0, {1.0f, -3.5f}, {13.0f, 13.0f});
    physics.SetHero(&heroBody);

    unity::DelayedCalls calls;
    unity::Tweens tweens;
    game::EnemyHero hero;
    hero.position = heroAt;
    int frameNow = 0;
    std::vector<int> hitFrames;
    bool hitOk = true;
    game::Enemy* golemEnemy = nullptr;
    hero.dealDamage = [&](float strength, const std::string& source, game::Enemy& e) {
        hitFrames.push_back(frameNow);
        hitOk = hitOk && source == "FlyingKamikazeGolem" && strength == e.enemyStats.strength() && e.attackFixture != nullptr;
    };
    game::EnemyScene scene;
    scene.calls = &calls;
    scene.tweens = &tweens;
    scene.hero = &hero;
    scene.roomPosition = {0.0f, 0.0f};
    scene.roomFloorWidth = 440;
    scene.roomFloorHeight = 220;
    scene.roomCenterYOffset = 0;
    game::MasterAudio audio;
    const bool haveAudio = !soundGroups.IsNull() && audio.Load(soundGroups, {"Sounds - GolemDungeon", "Sounds - GeneralDungeons"});
    if (haveAudio) scene.audio = &audio;
    int sounds = 0;
    scene.playSound = [&](game::Enemy&, const std::string& s) { sounds += s == "golem_dungeon_flyingkamikazegolem_attack"; };
    unity::Random::InitState(2024);

    const unity::Random::State beforeMake = unity::Random::GetState();
    auto golem = game::MakeEnemy(prefab, scene, &controller, {{-100.0f, 50.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}});
    const int makeDraws = unity::Random::DrawsBetween(beforeMake, unity::Random::GetState(), 64);
    golemEnemy = golem.get();
    auto* b = dynamic_cast<Golem*>(golem->enemyBehaviour.get());
    Check(b != nullptr, "the FlyingKamikazeGolem prefab makes a FlyingKamikazeGolemBehaviour");
    if (!b) return;
    Check(!golem->colliderEnabled && Same(golem->colliderSize, {10.0f, 10.0f}) && golem->layer == 13 && golem->tag == "HardEnemy",
          "its root box: 10 x 10, off in the prefab, HardEnemy on FlyingEnemy");
    Check(golem->hasAttackCollider && !golem->attackActive && golem->attackEnabled && Same(golem->attackSize, {15.0f, 15.0f}) &&
              golem->attackLayer == game::kLayerEnemyAttack,
          "its Attack child: a 15 x 15 trigger on EnemyAttack, inactive");
    bool hasFields = false;
    for (const game::Behaviour& f : prefab.nodes[0].behaviours)
        if (f.cls == "FlyingKamikazeGolemBehaviour") hasFields = f.Field("attackPushBackwardsForce", -1.0) >= 0.0;
    if (hasFields)
        Check(b->attackPushBackwardsForce == 100.0f && b->trails == 2 && b->enemyDirection == Golem::Direction::Left,
              "its fields (prefabs.py FIELDS): push 100, two trails, looking left");
    else
        std::printf("(prefabs.json without FlyingKamikazeGolemBehaviour's fields: rerun tools/extract/prefabs.py)\n");
    bool hasEventSounds = false;
    for (const game::PrefabNode& n : prefab.nodes)
        for (const game::Behaviour& f : n.behaviours) hasEventSounds = hasEventSounds || (f.cls == "EventSounds" && f.raw.count("enableSound"));
    if (hasEventSounds) {
        Check(golem->enableSounds == std::vector<std::string>{"golem_dungeon_flyingrepair_loop"},
              "its SoundsFlyingKamikaze child's EventSounds: golem_dungeon_flyingrepair_loop on enable");
        if (haveAudio) {
            Check(makeDraws > 0, "Instantiate (active): the enable sound's draws (" + std::to_string(makeDraws) + ")");
            golem->SetActive(false);
            const unity::Random::State beforeOn = unity::Random::GetState();
            golem->SetActive(true);
            const int onDraws = unity::Random::DrawsBetween(beforeOn, unity::Random::GetState(), 64);
            Check(onDraws > 0, "SetActive(true): the enable sound's draws again (" + std::to_string(onDraws) + ")");
        }
    } else {
        std::printf("(prefabs.json without EventSounds' fields: rerun tools/extract/prefabs.py)\n");
    }
    game::EnemyStatModificator m;
    game::EnemyStatModificator::FromJson(enemyStats, "FlyingKamikazeGolem", m);
    golem->Init("FlyingKamikazeGolem", m, game::EnemyStatsContext{});
    physics.Add(*golem);
    Check(golem->rootFixture == nullptr && golem->attackFixture == nullptr && golem->body && golem->body->Body()->GetMass() == 12.0f,
          "no fixtures while the box is off and the child inactive; the mass 12 (its weight)");
    Check(!b->isCharging && b->bounds.width == 0.0f, "Init: not charging, no bounds yet");
    golem->enemyBehaviour->StartTargetingPlayerAfterTime(1.0f);

    struct Mark {
        int frame;
        Golem::Event ev;
    };
    std::vector<Mark> marks;
    // Each charge: where the golem and the hero were, the draws, the expected aim.
    int chargesChecked = 0, chargesExact = 0, bugShows = 0, drawsOk = 0;
    Vector2 chargeFrom, chargeHero;
    unity::Random::State chargeRng{};
    bool fixturesOnInCharge = true, attackGoneAfterEnd = true, rootGoneAfterReturn = true, delaysOk = true;
    Vector2 driftBefore;
    bool drifted = false;
    b->onEvent = [&](Golem& g, Golem::Event ev, bool after) {
        if (!after) marks.push_back({frameNow, ev});
        if (ev == Golem::Event::Charge && !after) {
            chargeFrom = g.enemy.position();
            chargeHero = hero.position;
            chargeRng = unity::Random::GetState();
            if (!drifted) {
                driftBefore = g.desiredDirection;
                drifted = true;
            }
        }
        if (ev == Golem::Event::Charge && after) {
            chargesChecked++;
            fixturesOnInCharge = fixturesOnInCharge && g.enemy.rootFixture != nullptr && g.enemy.attackFixture != nullptr && g.enemy.colliderEnabled &&
                                 g.enemy.attackActive;
            drawsOk += unity::Random::DrawsBetween(chargeRng, unity::Random::GetState(), 16) == (haveAudio ? 2 : 1);
            // The aim, as the IL computes it.
            const unity::Rect r = g.bounds;
            const Vector2 n = unity::Normalized(chargeHero - chargeFrom);
            const float f = F(D(static_cast<float>(g.lastTurn)) * D(0.017453292f));
            const float c = F(std::cos(D(f))), s = F(std::sin(D(f)));
            const float x = F(D(n.x) * D(c) - D(n.y) * D(s));
            const float yBug = F(D(x) * D(s) + D(n.y) * D(c)), yRight = F(D(n.x) * D(s) + D(n.y) * D(c));
            Vector2 cp = chargeFrom + Vector2{x, yBug} * 150.0f;
            cp = {unity::Mathf::Clamp(cp.x, r.x, r.x + r.width), unity::Mathf::Clamp(cp.y, r.y, r.y + r.height)};
            const Vector2 dd = unity::Normalized(cp - chargeFrom) * 300.0f;
            chargesExact += Same(g.chargePosition, cp) && Same(g.desiredDirection, dd) && r.x == -220.0f && r.y == -110.0f && r.width == 440.0f &&
                            r.height == 220.0f;
            bugShows += g.lastTurn != 0 && yBug != yRight;
        }
        if (ev == Golem::Event::EndCharge && after) attackGoneAfterEnd = attackGoneAfterEnd && g.enemy.attackFixture == nullptr && !g.enemy.attackActive;
        if (ev == Golem::Event::ReturnDone && after) {
            rootGoneAfterReturn = rootGoneAfterReturn && g.enemy.rootFixture == nullptr && !g.enemy.colliderEnabled && g.desiredDirection.x == 0.0f &&
                                  !g.isReturning;
            delaysOk = delaysOk && g.lastEnableDelay >= 2.0f && g.lastEnableDelay <= 6.0f && calls.IsInvoking(&g, "EnableCharge");
        }
    };

    const float dt = 1.0f / 60.0f;
    double time = 0.0, fixedTime = 0.0;
    // The return: the velocity right after each tween update against start + change * OutQuint.
    bool returning = false, returnExact = true;
    int returnFrames = 0;
    float elapsed = 0.0f;
    Vector2 returnStart;
    for (int f = 0; f < 1200; f++) {
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
        const bool tweenStarts = b->returnTween != 0 && !returning;
        const Vector2 before = golem->velocity();
        tweens.Update(dt);
        if (tweenStarts) {
            returning = true;
            elapsed = 0.0f;
            returnStart = before;
        }
        if (returning) {
            elapsed += dt;
            if (elapsed > 1.5f) elapsed = 1.5f;
            const float e = unity::EaseEvaluate(unity::Ease::OutQuint, elapsed, 1.5f);
            const Vector2 want{F(D(returnStart.x) + D(0.0f - returnStart.x) * D(e)), F(D(returnStart.y) + D(0.0f - returnStart.y) * D(e))};
            returnExact = returnExact && Same(golem->velocity(), want);
            returnFrames++;
            if (b->returnTween == 0) returning = false;
        }
        golem->enemyBehaviour->Update(dt);
        calls.Update(time);
        nav.ResumeCoroutines();
        golem->UpdateAnimator(dt);
        nav.LateUpdate();
        golem->navAgent.LateUpdate(dt);
        if (std::getenv("ML_FLY_BOX"))
            std::printf("frame %d: %s v %s dd %s charging %d returning %d root %d attack %d state %d\n", f, Str(golem->position()).c_str(),
                        Str(golem->velocity()).c_str(), Str(b->desiredDirection).c_str(), b->isCharging, b->isReturning, golem->rootFixture != nullptr,
                        golem->attackFixture != nullptr, golem->enemyAnimator->CurrentState());
    }
    auto first = [&](Golem::Event ev, int after) {
        for (const Mark& mk : marks)
            if (mk.ev == ev && mk.frame >= after) return mk.frame;
        return -1;
    };
    const int prepare = first(Golem::Event::Prepare, 0), charge = first(Golem::Event::Charge, 0), end = first(Golem::Event::EndCharge, 0),
              done = first(Golem::Event::ReturnDone, 0), enable = first(Golem::Event::EnableCharge, 0), prepare2 = first(Golem::Event::Prepare, prepare + 1);
    Check(prepare >= 59 && prepare <= 61, "targets him a second after and prepares at once (frame " + std::to_string(prepare) + ")");
    Check(charge >= prepare + 19 && charge <= prepare + 21, "MakeChargeAttack from the clip 0.333 s on (frame " + std::to_string(charge) + ")");
    Check(drifted && driftBefore.x > 0.0f && driftBefore.y < 0.0f && std::fabs(driftBefore.y / driftBefore.x + 0.5f) < 0.01f,
          "before the first charge it drifts towards the origin (empty bounds), not him: " + Str(driftBefore));
    Check(end >= charge + 29 && end <= charge + 31, "EndCharge 0.5 s on (frame " + std::to_string(end) + ")");
    Check(done >= end + 89 && done <= end + 92, "the return's OnComplete 1.5 s on (frame " + std::to_string(done) + ")");
    Check(enable > done + 2 * 60 - 2 && enable < done + 6 * 60 + 2, "EnableCharge Range(2, 6) s on (frame " + std::to_string(enable) + ")");
    // (EnableCharge in a fixed step's pass: that frame's Update prepares; in the frame's pass: the next one's.)
    Check(prepare2 == enable || prepare2 == enable + 1, "the next charge prepared at the next Update (frame " + std::to_string(prepare2) + ")");
    Check(chargesChecked >= 2 && chargesExact == chargesChecked,
          "each charge's aim (bounds, the turn's bug, 150 clamped, 300 towards it): " + std::to_string(chargesExact) + " of " + std::to_string(chargesChecked));
    Check(bugShows > 0, "the bug shows: the turned y from the turned x differs from the rotation's");
    Check(drawsOk == chargesChecked, std::string("MakeChargeAttack's draws: ") + (haveAudio ? "the sound's loops and " : "") + "Range(-20, 21)");
    Check(sounds == chargesChecked, "the charge's sound each time");
    Check(fixturesOnInCharge, "the charge makes the root box's and the attack child's fixtures");
    Check(attackGoneAfterEnd, "EndCharge: the attack child inactive, its fixture gone");
    Check(rootGoneAfterReturn && delaysOk, "the return's end: the root box off (no fixture), EnableCharge invoked Range(2, 6) on");
    Check(returnFrames > 80 && returnExact, "the return: the velocity start + change * OutQuint, bit for bit (" + std::to_string(returnFrames) + " frames)");
    Check(!hitFrames.empty() && hitOk, "the charges hit him (" + std::to_string(hitFrames.size()) + " hits), only while the attack child is on");

    // Outside the bounds while charging: stopped, the look's angle, clamped 2 inside.
    b->isCharging = true;
    b->isReturning = false;
    golem->setPosition({300.0f, 50.0f});
    golem->enemyBehaviour->Update(dt);
    // From the right to (-240, -10): 180 + atan(10 / 240), counted the other way round.
    const double wantAngle = 180.0 + std::atan(10.0 / 240.0) * 180.0 / 3.14159265358979323846;
    Check(Same(golem->position(), {218.0f, 50.0f}) && b->desiredDirection.x == 0.0f && golem->velocity().x == 0.0f &&
              std::fabs(b->angle - wantAngle) < 0.01,
          "outside the bounds: stopped, clamped 2 inside (" + Str(golem->position()) + "), angle to him " + std::to_string(b->angle));
    (void)golemEnemy;
    game::PolyNav2D::current = nullptr;
}

} // namespace
