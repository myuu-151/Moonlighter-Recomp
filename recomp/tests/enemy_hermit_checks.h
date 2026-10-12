#pragma once
// enemy_test's self-checks for the HermitSlime (game/HermitSlime, game/EnemyColliders,
// game/HeroDamage), with the game's data and no recording: the prefab made into a
// HermitSlimeBehaviourNEW with its trigger root, its attack trigger and its "Collidere" hurtbox;
// ItemDrain's draws; then a hermit in an empty room: it patrols (Range(2, 6) and two int draws
// for a tile of the floor, the agent sent there, a new one when the lapse runs out), is
// invincible while it walks, slams when the hero comes within 90 (its clip events in order with
// their draws, the AttackCollider on only in its window, the hero hit once with ItemDrain), its
// root box grown by the curves and back (its fixture remade), and takes the weapon's hits: on its
// body while it's out of its shell (no push: a weight of 48), not through the hurtbox (which
// advises nobody, as in the game), and through a hurtbox that does advise it, with no push.
// Included by enemy_test.cpp (after enemy_replay.h).

#include "game/EnemyColliders.h"
#include "game/HeroDamage.h"
#include "game/HermitSlime.h"

namespace {

void CheckItemDrainDraws()
{
    unity::Random::InitState(77);
    unity::Random::State before = unity::Random::GetState();
    Check(game::DropRandomItemsDraws({}, game::kItemDrainDrops).empty() && unity::Random::DrawsBetween(before, unity::Random::GetState(), 4) == 0,
          "DropRandomItems on an empty bag: no drops, no draws");
    before = unity::Random::GetState();
    const auto drops = game::DropRandomItemsDraws({0, 2, 5, 7}, game::kItemDrainDrops);
    bool distinct = drops.size() == 3;
    for (size_t i = 0; distinct && i < drops.size(); i++)
        for (size_t k = 0; k < i; k++) distinct = distinct && drops[i].slot != drops[k].slot;
    bool angles = true;
    for (const auto& d : drops) angles = angles && d.angle >= 0 && d.angle < 360;
    Check(distinct && angles && unity::Random::DrawsBetween(before, unity::Random::GetState(), 16) == 6,
          "DropRandomItems(3) of four: three different slots, a slot and an angle drawn each (6 draws)");
    before = unity::Random::GetState();
    Check(game::DropRandomItemsDraws({4}, game::kItemDrainDrops).size() == 1 && unity::Random::DrawsBetween(before, unity::Random::GetState(), 16) == 2,
          "DropRandomItems(3) of one: one drop, two draws");
}

void CheckHermitSlime(const game::Prefab& prefab, const unity::AnimatorController& controller, const Value& enemyStats, const Value& physics2d,
                      const Value& soundGroups)
{
    CheckItemDrainDraws();

    // The room: 484 x 268 at the origin, its floor the map.
    game::PolyNav2D nav;
    nav.inflateRadius = 8.0f;
    nav.map = game::PolyNav2D::PolyMap({{{{-242.0f, 134.0f}, {-242.0f, -134.0f}, {242.0f, -134.0f}, {242.0f, 134.0f}}}}, {});
    nav.GenerateNodes();
    game::PolyNav2D::current = &nav;
    unity::Physics2DSettings settings;
    if (!physics2d.IsNull()) settings.Load(physics2d);
    unity::PhysicsWorld world(settings);
    game::EnemyPhysics physics(world);
    unity::Rigidbody2DDef heroDef;
    heroDef.mass = 1.0f;
    heroDef.linearDrag = 5.0f;
    heroDef.gravityScale = 0.0f;
    heroDef.constraints = unity::Rigidbody2DDef::FreezeRotation;
    heroDef.collisionDetection = unity::Rigidbody2DDef::Continuous;
    Vector2 heroAt{1000.0f, 1000.0f}; // far away while it patrols
    unity::Rigidbody2D& heroBody = world.AddBody(heroDef, heroAt, 0, {1.0f, -3.5f}, {13.0f, 13.0f});
    physics.SetHero(&heroBody);

    unity::DelayedCalls calls;
    unity::Tweens tweens;
    game::EnemyHero hero;
    hero.position = heroAt;
    int frameNow = 0;
    struct HeroHit {
        int frame;
        float strength;
        std::string source;
        game::HeroDamageType type;
    };
    std::vector<HeroHit> heroHits;
    hero.dealTypedDamage = [&](float strength, const std::string& source, game::HeroDamageType type, game::Enemy&) {
        heroHits.push_back({frameNow, strength, source, type});
    };
    int strongVibrations = 0;
    hero.vibrate = [&](float intensity, float duration) { strongVibrations += intensity == 0.4f && duration == 0.1f; };
    game::EnemyScene scene;
    scene.calls = &calls;
    scene.tweens = &tweens;
    scene.hero = &hero;
    scene.roomPosition = {0.0f, 0.0f};
    scene.roomFloorWidth = 484;
    scene.roomFloorHeight = 268;
    game::MasterAudio audio;
    const bool haveAudio = !soundGroups.IsNull() && audio.Load(soundGroups, {"Sounds - GolemDungeon", "Sounds - GeneralDungeons"});
    if (haveAudio) scene.audio = &audio;
    int crashSounds = 0, particles = 0;
    scene.playSound = [&](game::Enemy&, const std::string& sound) { crashSounds += sound == "golem_dungeon_golem_crash"; };
    std::vector<std::pair<int, std::string>> events;
    unity::Random::State eventStart{};
    bool eventDraws = true;
    std::string drawsSeen;
    scene.onAnimationEvent = [&](game::Enemy&, const unity::AnimationEvent& e, bool after) {
        if (!after) {
            events.emplace_back(frameNow, e.function);
            eventStart = unity::Random::GetState();
            return;
        }
        const int draws = unity::Random::DrawsBetween(eventStart, unity::Random::GetState(), 64);
        int want = 0;
        if (e.function == "DoScreenShakeFromEnemyAnimation") want = 15;
        // (Its one variation: the random pitch, the loops' Range(1, 6).)
        else if (e.function == "PlayCrashSound") want = haveAudio ? 2 : 0;
        if (draws != want) {
            eventDraws = false;
            drawsSeen += " " + e.function + ":" + std::to_string(draws);
        }
    };

    // The prefab.
    auto hermitEnemy = game::MakeEnemy(prefab, scene, &controller, {{120.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}});
    game::Enemy& e = *hermitEnemy;
    auto* hermit = dynamic_cast<game::HermitSlimeBehaviourNEW*>(e.enemyBehaviour.get());
    Check(hermit != nullptr, "the HermitSlime prefab makes a HermitSlimeBehaviourNEW");
    if (!hermit) return;
    hermit->playAttackParticles = [&](game::Enemy&) { particles++; };
    Check(e.colliderTrigger && Same(e.colliderOffset, {0.0f, -20.0f}) && Same(e.colliderSize, {15.0f, 15.0f}) && e.tag == "SoftEnemy" &&
              e.layer == game::kLayerGroundEnemy,
          "its root: a 15 x 15 trigger at (0, -20), SoftEnemy on GroundEnemy");
    Check(e.hasAttackCollider && !e.attackEnabled && Same(e.attackSize, {64.0f, 32.0f}) && Same(e.attackOffset, {0.0f, -6.0f}) &&
              e.attackLayer == game::kLayerEnemyAttack,
          "its AttackCollider: 64 x 32 at (0, -6) on EnemyAttack, off in the prefab");
    const game::EnemyDamageReceiver* receiver = e.damageReceivers.size() == 1 ? &e.damageReceivers[0] : nullptr;
    Check(receiver && receiver->path == "Collidere" && receiver->tag == "EnemyDamageReceiver" && receiver->layer == game::kLayerGroundEnemy &&
              receiver->trigger && Same(receiver->offset, {0.0f, -20.0f}) && Same(receiver->size, {15.0f, 15.0f}),
          "its hurtbox: Collidere, a 15 x 15 trigger at (0, -20) tagged EnemyDamageReceiver");
    bool adviseKnown = false;
    for (const game::PrefabNode& n : prefab.nodes)
        for (const game::Behaviour& b : n.behaviours)
            if (b.cls == "EnemyDamageReceiver") adviseKnown = b.raw.count("gameObjectToAdvise") > 0;
    if (adviseKnown) Check(receiver && receiver->advise == nullptr, "Collidere's gameObjectToAdvise is unset in the prefab: it advises nobody");
    else std::printf("(prefabs.json without EnemyDamageReceiver's fields: rerun tools/extract/prefabs.py)\n");
    game::EnemyStatModificator m;
    game::EnemyStatModificator::FromJson(enemyStats, "HermitSlime", m);
    e.Init("HermitSlime", m, game::EnemyStatsContext{});
    Check(e.navAgent.maxSpeed == 15.0f && e.rigidbody.mass == 48.0f && e.enemyStats.strength() == 15.0f && e.enemyStats.weight() == 48.0f,
          "Init: speed 15, mass 48, strength 15, weight 48");
    physics.Add(e);
    const b2Body* body = e.body->Body();
    Check(e.rootFixture && e.rootFixture->IsSensor() && body->GetMass() == 48.0f && body->GetLocalCenter().x == 0.0f && body->GetLocalCenter().y == 0.0f,
          "its body: the root a sensor, mass 48 at its origin");
    Check(receiver && receiver->fixture && receiver->fixture->IsSensor() && physics.ReceiverOf(receiver->fixture) == receiver,
          "the hurtbox: a sensor on the body, found by its fixture");
    Check(e.attackFixture == nullptr, "no attack fixture while the box is off");
    hermit->StartTargetingPlayerAfterTime(1.0f);

    const float dt = 1.0f / 60.0f;
    double time = 0.0, fixedTime = 0.0;
    // The patrol's draws: each Update's, and those of the Updates that chose a destination.
    int choices = 0;
    bool patrolDraws = true, patrolInRoom = true, patrolSent = true, lapseOk = true;
    bool walkInvincible = true, fixtureOnlyWhenOn = true;
    Vector2 maxRoot{0.0f, 0.0f};
    // The root fixture follows the box: its polygon's extent and centre as the box is.
    int rootChanges = 0;
    bool rootFollows = true;
    Vector2 lastSize = e.colliderSize;
    int attackFrame = -1, endAttackFrame = -1;
    int attackState = -1;
    bool vulnerableSeen = false;
    // The weapon's hits, once it's out of its shell.
    bool hitsDone = false;
    float hpBefore = 0.0f;
    game::WeaponHitResult viaReceiver, viaBody, viaAdvising;
    float hpAfterReceiver = 0.0f, hpAfterBody = 0.0f;
    Vector2 velocityAfterBody{1.0f, 1.0f};
    auto frame = [&](int f) {
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
            fixtureOnlyWhenOn = fixtureOnlyWhenOn && ((e.attackFixture != nullptr) == e.attackEnabled);
            calls.UpdateFixed(fixedTime);
            calls.inFixedStep = false;
        }
        calls.now = time;
        audio.now = time;
        audio.frameCount = f;
        tweens.Update(dt);
        const int before = hermit->destinations;
        const unity::Random::State r0 = unity::Random::GetState();
        hermit->Update(dt);
        if (hermit->destinations != before) {
            choices++;
            patrolDraws = patrolDraws && unity::Random::DrawsBetween(r0, unity::Random::GetState(), 8) == 3;
            const Vector2 d = hermit->destinyPosition;
            patrolInRoom = patrolInRoom && d.x >= -242.0f && d.x < 242.0f && d.y >= -134.0f && d.y < 134.0f && d.x == std::floor(d.x) &&
                           d.y == std::floor(d.y);
            patrolSent = patrolSent && Same(e.navAgent.primeGoal(), d);
            lapseOk = lapseOk && (hermit->patrolLapse == 2.0f || hermit->patrolLapse == 3.0f || hermit->patrolLapse == 4.0f || hermit->patrolLapse == 5.0f);
        } else if (hermit->enemyState != game::EnemyBehaviour::EnemyState::Attacking) {
            patrolDraws = patrolDraws && unity::Random::DrawsBetween(r0, unity::Random::GetState(), 8) == 0;
        }
        calls.Update(time);
        nav.ResumeCoroutines();
        e.UpdateAnimator(dt);
        nav.LateUpdate();
        e.navAgent.LateUpdate(dt);
        const int state = e.enemyAnimator->CurrentState();
        const std::string& stateName = controller.states[static_cast<size_t>(state)].name;
        const bool walking = stateName.compare(0, 7, "Walking") == 0;
        if (walking && hermit->targetPlayer && f > 5) walkInvincible = walkInvincible && e.enemyStats.Invencible();
        if (attackFrame < 0 && hermit->enemyState == game::EnemyBehaviour::EnemyState::Attacking) attackFrame = f;
        if (!walking && attackState < 0) attackState = state;
        if (attackFrame >= 0 && endAttackFrame < 0 && attackFrame != f && hermit->enemyState == game::EnemyBehaviour::EnemyState::Idle) endAttackFrame = f;
        if (std::getenv("ML_HERMIT_BOX") && !walking)
            std::printf("frame %d %s t %g root %s %s%s\n", f, stateName.c_str(), e.enemyAnimator->GetCurrentAnimatorStateInfo().normalizedTime,
                        Str(e.colliderOffset).c_str(), Str(e.colliderSize).c_str(), e.enemyStats.Invencible() ? " invincible" : "");
        if (e.colliderSize.x > maxRoot.x) maxRoot.x = e.colliderSize.x;
        if (e.colliderOffset.x > maxRoot.y) maxRoot.y = e.colliderOffset.x;
        if (!Same(e.colliderSize, lastSize)) {
            rootChanges++;
            lastSize = e.colliderSize;
        }
        {
            const auto* poly = static_cast<const b2PolygonShape*>(e.rootFixture->GetShape());
            float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
            for (int k = 0; k < poly->GetVertexCount(); k++) {
                const b2Vec2 v = poly->GetVertex(k);
                x0 = std::min(x0, v.x), x1 = std::max(x1, v.x), y0 = std::min(y0, v.y), y1 = std::max(y1, v.y);
            }
            rootFollows = rootFollows && e.rootFixture->IsSensor() && std::fabs((x1 - x0) - e.colliderSize.x) < 1e-3f &&
                          std::fabs((y1 - y0) - e.colliderSize.y) < 1e-3f && std::fabs((x0 + x1) / 2.0f - e.colliderOffset.x) < 1e-3f &&
                          std::fabs((y0 + y1) / 2.0f - e.colliderOffset.y) < 1e-3f;
        }
        if (!walking && !e.enemyStats.Invencible()) vulnerableSeen = true;
        // Out of its shell (and not flashing from a hit): the weapon's hits.
        if (!hitsDone && !walking && !e.enemyStats.Invencible() && !hermit->IsEnemyPlayingDamagedEffect() && attackFrame >= 0) {
            hitsDone = true;
            hpBefore = e.enemyStats.CurrentHealth();
            viaReceiver = game::WeaponMainAttackHit(*receiver, 10.0f);
            hpAfterReceiver = e.enemyStats.CurrentHealth();
            viaBody = game::WeaponMainAttackHit(e, 10.0f, heroAt, 100.0f);
            hpAfterBody = e.enemyStats.CurrentHealth();
            velocityAfterBody = e.velocity();
        }
    };
    int f = 0;
    for (; f < 600; f++) frame(f);
    Check(choices >= 2, "it patrols: " + std::to_string(choices) + " destinations in 10 s");
    Check(patrolDraws, "each destination: Range(2, 6) and two int draws (3), no draws in between");
    Check(patrolInRoom && patrolSent, "the destinations: whole tiles of the floor from its corner, the agent sent there");
    Check(lapseOk, "the patrol's lapse: 2 to 5 s");
    Check(unity::Distance(e.position(), {120.0f, 0.0f}) > 10.0f, "it walked (now at " + Str(e.position()) + ")");
    Check(walkInvincible, "invincible while it walks (SetInvencible at each walk loop's start)");
    Check(attackFrame < 0, "no attack with the hero far away");

    // The hero comes within 90, to its right.
    heroAt = e.position() + Vector2{40.0f, 0.0f};
    hero.position = heroAt;
    const int near = f;
    for (; f < near + 600; f++) frame(f);
    Check(attackFrame == near, "it slams the frame he's within 90 (frame " + std::to_string(attackFrame) + ", he came at " + std::to_string(near) + ")");
    Check(attackState >= 0 && controller.states[static_cast<size_t>(attackState)].name == "AttackRight",
          "the attack faces him: " + (attackState >= 0 ? controller.states[static_cast<size_t>(attackState)].name : std::string("none")));
    std::vector<std::string> slam;
    for (const auto& ev : events)
        if (ev.first >= near && ev.second != "SetInvencible") slam.push_back(ev.second);
    const char* const kOrder[6] = {"LockLookDirection", "SetVencible", "DoScreenShakeFromEnemyAnimation", "PlayCrashSound", "SpawnAttackParticles",
                                   "EndAttack"};
    bool order = slam.size() >= 6;
    for (size_t k = 0; order && k < 6; k++) order = slam[k] == kOrder[k];
    std::string got;
    for (size_t k = 0; k < slam.size() && k < 8; k++) got += " " + slam[k];
    Check(order, "the slam's clip events in order:" + got);
    Check(eventDraws, "DoScreenShakeFromEnemyAnimation draws 15, PlayCrashSound 2 (the sound's pitch and loops), the others none" + drawsSeen);
    Check(crashSounds >= 1 && particles >= 1 && strongVibrations >= 1, "the crash: its sound, its particles, the pad's strong rumble");
    Check(vulnerableSeen, "out of its shell after the slam (SetVencible)");
    Check(endAttackFrame > attackFrame && endAttackFrame - attackFrame >= 250 && endAttackFrame - attackFrame <= 262,
          "EndAttack 4.25 s in (frame " + std::to_string(endAttackFrame) + ")");
    Check(fixtureOnlyWhenOn, "the attack fixture exists exactly while the box is on");
    bool hitOnce = !heroHits.empty();
    int hitsFirstSlam = 0;
    for (const HeroHit& h : heroHits) {
        hitOnce = hitOnce && h.type == game::HeroDamageType::ItemDrain && h.strength == 15.0f && h.source == "HermitSlime";
        hitsFirstSlam += h.frame >= attackFrame && h.frame < endAttackFrame;
    }
    Check(hitOnce && hitsFirstSlam == 1,
          "the slam hits him once with ItemDrain (DealDamageToPlayer(15, \"HermitSlime\", ItemDrain)): " + std::to_string(heroHits.size()) + " hits");
    Check(maxRoot.x > 65.0f && maxRoot.y > 20.0f && rootChanges > 100,
          "its root box grows past 65 wide at x 20 (AttackRight's curves: " + Str(maxRoot) + "), changed in " + std::to_string(rootChanges) + " frames");
    Check(rootFollows, "the root's fixture follows the box every frame (a sensor)");
    // The weapon.
    Check(hitsDone, "the weapon's hits were made while it was out");
    Check(viaReceiver.enemy == nullptr && !viaReceiver.pushed && hpAfterReceiver == hpBefore,
          "a hit on Collidere: nobody's health (it advises nobody), no push");
    Check(viaBody.enemy == &e && viaBody.pushed && hpAfterBody == hpBefore - 9.0f && Same(velocityAfterBody, {0.0f, 0.0f}),
          "a hit on its body: 10 - 10% defense = 9 off its health, no push (weight 48), health " + std::to_string(hpAfterBody));
    // A hurtbox that advises it (as other enemies' are set up at run time): the hit reaches it,
    // without a push.
    game::EnemyDamageReceiver advising = *receiver;
    advising.advise = &e;
    for (; f < near + 1200 && (e.enemyStats.Invencible() || hermit->IsEnemyPlayingDamagedEffect()); f++) frame(f);
    const float hp0 = e.enemyStats.CurrentHealth();
    const Vector2 v0 = e.velocity();
    viaAdvising = game::WeaponMainAttackHit(advising, 10.0f);
    Check(viaAdvising.enemy == &e && !viaAdvising.pushed && e.enemyStats.CurrentHealth() == hp0 - 9.0f && Same(e.velocity(), v0),
          "a hit through a hurtbox advising it: 9 off its health, no push");
    // The hero gone again: back in its shell, walking, its box back to 15 x 15 at (0, -20); a hit
    // on its body then does nothing.
    heroAt = {1000.0f, 1000.0f};
    hero.position = heroAt;
    for (; f < near + 2400; f++) {
        frame(f);
        const std::string& n = controller.states[static_cast<size_t>(e.enemyAnimator->CurrentState())].name;
        if (n.compare(0, 7, "Walking") == 0 && e.enemyStats.Invencible() && !hermit->IsEnemyPlayingDamagedEffect()) break;
    }
    Check(Same(e.colliderSize, {15.0f, 15.0f}) && Same(e.colliderOffset, {0.0f, -20.0f}), "walking again, its root box back to 15 x 15 at (0, -20)");
    const float hpShell = e.enemyStats.CurrentHealth();
    game::WeaponMainAttackHit(e, 10.0f, heroAt, 100.0f);
    Check(e.enemyStats.CurrentHealth() == hpShell, "in its shell, a hit on its body leaves its health");
    // The type reaches a hero without dealTypedDamage as a plain hit.
    game::EnemyHero plain;
    int plainHits = 0;
    plain.dealDamage = [&](float, const std::string&, game::Enemy&) { plainHits++; };
    game::DealDamageToPlayer(plain, 15.0f, "HermitSlime", game::HeroDamageType::ItemDrain, e);
    Check(plainHits == 1, "DealDamageToPlayer without dealTypedDamage: dealDamage");
    game::PolyNav2D::current = nullptr;
}

} // namespace
