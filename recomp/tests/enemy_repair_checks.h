#pragma once
// enemy_test's self-checks for the FlyingRepairGolem (game/FlyingRepairGolem, game/RoomDrops),
// with the game's data and no recording:
//   - the room's drop list on its own: OnDropListChanged's handlers in the order added (one
//     added twice is called twice; -= takes one away), GetRandom's one draw (none on an empty
//     list), RemoveDropItem and CleanDropList calling them only when something went;
//   - the prefab made into a FlyingRepairGolemBehaviour with its fields (a 10 x 10 HardEnemy box
//     on FlyingEnemy, its 20 x 20 AttackCollider, push 150, its item sprite set) and Init (speed
//     45, mass 12, strength 10);
//   - two golems in a walled box with the hero standing: a second after, they chase him at speed
//     x 1.6 (one frame's velocity checked against the formula, every operation rounded), their
//     AttackCollider on only in HelixFly's first 1/14 s each cycle, his hits each from a golem
//     whose box is on; an Iron Bar dropped in the room: both go after it (a draw each, in the
//     order they subscribed), ChaseItem's Translate bit for bit, the first to touch it picks it
//     (PickItem: PlayFlyingRepairPick, ForbidEnemyMovement, PickItemCollisionedAndRemoveItFromRoom,
//     AllowEnemyMovement; the clip's unnamed event calls nothing), the other loses its target
//     (RemoveDropItem's call, an empty list: no draw); the thief roams (Range(0.5, 1), then 3
//     draws a try, at most 7 tries, the point inside the map, at speed); the other, hit, stops
//     (velocity zero, canMove off) and, standing still when it moves again, throws away an
//     insideUnitCircle and flies off away from the hero; the thief killed drops what it carried
//     (ItemStack.Drop at its position, 30 around, not back in the list), leaves the room's
//     delegate and counts in the statistics.
// Included by enemy_test.cpp (after enemy_replay.h).

#include <cmath>

#include "game/FlyingRepairGolem.h"
#include "game/RoomDrops.h"

namespace {

void CheckRoomDrops()
{
    game::RoomDrops drops;
    int a = 0, b = 0;
    std::vector<char> order;
    const int keyA = 0, keyB = 0;
    drops.Subscribe(&keyA, [&](game::RoomDrops::List&) {
        a++;
        order.push_back('a');
    });
    drops.Subscribe(&keyB, [&](game::RoomDrops::List&) {
        b++;
        order.push_back('b');
    });
    drops.Subscribe(&keyA, [&](game::RoomDrops::List&) {
        a++;
        order.push_back('A');
    });
    game::DroppedItem i1, i2;
    drops.AddDropItems({&i1, &i2});
    Check(a == 2 && b == 1 && order.size() == 3 && order[0] == 'a' && order[1] == 'b' && order[2] == 'A' && drops.GetDropList().size() == 2,
          "OnDropListChanged: its handlers in the order added, one added twice called twice");
    drops.Unsubscribe(&keyA);
    order.clear();
    drops.RemoveDropItem(&i1);
    Check(order.size() == 2 && order[0] == 'a' && order[1] == 'b' && drops.GetDropList().size() == 1 && drops.GetDropList()[0] == &i2,
          "-= takes the last one added away; RemoveDropItem takes the item out and calls them");
    order.clear();
    drops.RemoveDropItem(&i1);
    drops.CleanDropList();
    Check(order.empty(), "RemoveDropItem of an item not in the list, CleanDropList with nothing destroyed: no call");
    i2.destroyed = true;
    drops.CleanDropList();
    Check(order.size() == 2 && drops.GetDropList().empty(), "CleanDropList takes a destroyed item out and calls them");
}

// A walled box (LevelBoundary all round), the hero standing at (100, 0); two golems.
void CheckFlyingRepairGolem(const game::Prefab& prefab, const unity::AnimatorController& controller, const Value& enemyStats, const Value& physics2d,
                            const Value& soundGroups)
{
    CheckRoomDrops();

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
    const Vector2 heroAt{100.0f, 0.0f};
    unity::Rigidbody2D& heroBody = world.AddBody(heroDef, heroAt, 0, {1.0f, -3.5f}, {13.0f, 13.0f});
    physics.SetHero(&heroBody);

    unity::DelayedCalls calls;
    unity::Tweens tweens;
    game::EnemyHero hero;
    hero.position = heroAt;
    int frameNow = 0;
    struct HeroHit {
        int frame;
        game::Enemy* enemy;
        bool boxOn;
    };
    std::vector<HeroHit> heroHits;
    bool hitsOk = true;
    hero.dealDamage = [&](float strength, const std::string& source, game::Enemy& e) {
        heroHits.push_back({frameNow, &e, e.attackActive && e.attackEnabled});
        hitsOk = hitsOk && strength == 10.0f && source == "FlyingRepairGolem";
    };
    game::EnemyScene scene;
    scene.calls = &calls;
    scene.tweens = &tweens;
    scene.hero = &hero;
    scene.queries = &room;
    game::RoomDrops drops;
    scene.drops = &drops;
    struct Drop {
        game::DroppedItem* item;
        Vector2 at;
        float radius;
        bool inRoom;
    };
    std::vector<Drop> dropped;
    drops.drop = [&](game::DroppedItem& item, Vector2 at, float radius, bool inRoom) { dropped.push_back({&item, at, radius, inRoom}); };
    game::MasterAudio audio;
    const bool haveAudio = !soundGroups.IsNull() && audio.Load(soundGroups, {"Sounds - GolemDungeon", "Sounds - GeneralDungeons"});
    if (haveAudio) scene.audio = &audio;
    int pickSounds = 0;
    scene.playSound = [&](game::Enemy&, const std::string& sound) { pickSounds += sound == "golem_dungeon_flyingrepair_pick"; };
    std::vector<std::pair<game::Enemy*, std::string>> events;
    unity::Random::State eventStart{};
    bool eventDraws = true;
    scene.onAnimationEvent = [&](game::Enemy& e, const unity::AnimationEvent& ev, bool after) {
        if (!after) {
            events.emplace_back(&e, ev.function);
            eventStart = unity::Random::GetState();
            return;
        }
        const int draws = unity::Random::DrawsBetween(eventStart, unity::Random::GetState(), 64);
        // (PlayFlyingRepairPick: MasterAudio's draws for the pick's group; the others none but
        // the pick's RemoveDropItem: the handlers' GetRandom on what's left, here nothing.)
        if (ev.function != "PlayFlyingRepairPick" && draws != 0) eventDraws = false;
    };

    // The prefab.
    auto makeGolem = [&](Vector2 at) {
        return game::MakeEnemy(prefab, scene, &controller, {at, {0.0f, 0.0f}, {0.0f, 0.0f}});
    };
    auto golemA = makeGolem({-150.0f, 60.0f});
    auto golemB = makeGolem({-150.0f, -60.0f});
    auto* a = dynamic_cast<game::FlyingRepairGolemBehaviour*>(golemA->enemyBehaviour.get());
    auto* b = dynamic_cast<game::FlyingRepairGolemBehaviour*>(golemB->enemyBehaviour.get());
    Check(a != nullptr && b != nullptr, "the FlyingRepairGolem prefab makes a FlyingRepairGolemBehaviour");
    if (!a || !b) return;
    game::Enemy& ea = *golemA;
    game::Enemy& eb = *golemB;
    Check(!ea.colliderTrigger && Same(ea.colliderSize, {10.0f, 10.0f}) && Same(ea.colliderOffset, {0.0f, 0.0f}) && ea.tag == "HardEnemy" && ea.layer == 13,
          "its root: a 10 x 10 box (not a trigger), HardEnemy on FlyingEnemy");
    Check(ea.hasAttackCollider && ea.attackEnabled && ea.attackActive && Same(ea.attackSize, {20.0f, 20.0f}) && ea.attackLayer == game::kLayerEnemyAttack,
          "its AttackCollider: a 20 x 20 box on EnemyAttack, on in the prefab");
    bool hasFields = false;
    for (const game::Behaviour& beh : prefab.nodes[0].behaviours)
        if (beh.cls == "FlyingRepairGolemBehaviour") hasFields = beh.raw.count("itemSprite") > 0;
    if (hasFields) Check(a->attackPushBackwardsForce == 150.0f && a->hasItemSprite && a->canMove, "its fields (prefabs.py FIELDS): push 150, its item sprite set");
    else std::printf("(prefabs.json without FlyingRepairGolemBehaviour's fields: rerun tools/extract/prefabs.py)\n");
    Check(controller.Param("PickItem") >= 0 && controller.State("HelixFly") == 0 && controller.State("PickingItem") >= 0,
          "its Animator: HelixFly first, PickingItem on the PickItem trigger");
    game::EnemyStatModificator m;
    game::EnemyStatModificator::FromJson(enemyStats, "FlyingRepairGolem", m);
    ea.Init("FlyingRepairGolem", m, game::EnemyStatsContext{});
    eb.Init("FlyingRepairGolem", m, game::EnemyStatsContext{});
    Check(ea.enemyStats.speed() == 45.0f && ea.rigidbody.mass == 12.0f && ea.enemyStats.strength() == 10.0f, "Init: speed 45, mass 12, strength 10");
    int deaths = 0;
    a->registerDeath = [&] { deaths++; };
    b->registerDeath = [&] { deaths++; };
    physics.Add(ea);
    physics.Add(eb);
    a->StartTargetingPlayerAfterTime(1.0f);
    b->StartTargetingPlayerAfterTime(1.0f);
    Check(drops.Subscriptions(a) == 1 && drops.Subscriptions(b) == 1, "StartTargetingPlayerAfterTime: each on the room's OnDropListChanged");
    unity::Random::InitState(9001);

    const float dt = 1.0f / 60.0f;
    double time = 0.0, fixedTime = 0.0;
    std::vector<game::Enemy*> golems{&ea, &eb};
    // Per Update, its draws (for the phases below).
    std::map<game::Enemy*, int> lastDraws;
    std::function<void(game::Enemy&)> beforeUpdate, afterUpdate;
    // The item the test drops, and its trigger (the golem's root box touching its 8 x 8 box).
    game::DroppedItem bar;
    bar.id = 1;
    bar.item = "Iron Bar";
    bar.sprite = "IronBar";
    bool barDropped = false;
    std::map<game::Enemy*, bool> touching;
    bool attackWindowOk = true;
    int attackCycles = 0;
    std::map<game::Enemy*, bool> wasActive;
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
            // The item's trigger: OnTriggerEnter2D as a golem's box starts touching it.
            for (game::Enemy* e : golems) {
                if (e->destroyed || !e->active) continue;
                const Vector2 p = e->position();
                const bool now = barDropped && bar.isActiveAndEnabled() && std::fabs(p.x - bar.position.x) < 9.0f && std::fabs(p.y - bar.position.y) < 9.0f;
                if (now && !touching[e]) static_cast<game::FlyingRepairGolemBehaviour*>(e->enemyBehaviour.get())->OnTriggerEnter2D("Item", &bar);
                touching[e] = now;
            }
            calls.UpdateFixed(fixedTime);
            calls.inFixedStep = false;
        }
        calls.now = time;
        audio.now = time;
        audio.frameCount = f;
        tweens.Update(dt);
        for (game::Enemy* e : golems) {
            if (beforeUpdate) beforeUpdate(*e);
            const unity::Random::State before = unity::Random::GetState();
            e->enemyBehaviour->Update(dt);
            lastDraws[e] = unity::Random::DrawsBetween(before, unity::Random::GetState(), 64);
            if (afterUpdate) afterUpdate(*e);
        }
        calls.Update(time);
        nav.ResumeCoroutines();
        drops.CleanDropList(); // (the room's coroutine)
        for (game::Enemy* e : golems) e->UpdateAnimator(dt);
        nav.LateUpdate();
        for (game::Enemy* e : golems) e->navAgent.LateUpdate(dt);
        // The attack box: on only in HelixFly's first 1/14 s (PickingItem: the default, on).
        for (game::Enemy* e : golems) {
            if (e->destroyed || !e->enemyAnimator || !e->animatorEnabled) continue;
            const unity::Animator& an = *e->enemyAnimator;
            if (an.CurrentState() == controller.State("HelixFly")) {
                const float t = an.CurrentTime();
                const float cycle = 0.6428571939468384f;
                const float in = std::fmod(t, cycle);
                // (Within a frame of the edges: the curve's step is at 1/14.)
                if (in > 0.0714285746216774f + 0.0001f && in < cycle - 0.02f) attackWindowOk = attackWindowOk && !e->attackActive;
                if (in > 0.0005f && in < 0.0714285746216774f - 0.0001f) attackWindowOk = attackWindowOk && e->attackActive;
            }
            if (e->attackActive && !wasActive[e]) attackCycles++;
            wasActive[e] = e->attackActive;
        }
    };

    // 1. The chase.
    int f = 0;
    bool chaseFormula = false, chaseChecked = false;
    for (; f < 420; f++) {
        if (f == 90) {
            // One frame's ChasePlayer against the formula, every operation rounded.
            beforeUpdate = [&](game::Enemy& e) {
                if (&e != &ea || chaseChecked) return;
                const Vector2 v0 = e.body->velocity(), p = e.position();
                const Vector2 h = hero.position;
                auto norm = [](Vector2 v) {
                    const float mag = static_cast<float>(std::sqrt(static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y));
                    return mag > 1e-05f ? Vector2{v.x / mag, v.y / mag} : Vector2{0.0f, 0.0f};
                };
                const Vector2 md{h.x - p.x, h.y - p.y};
                const Vector2 n0 = norm(v0);
                const Vector2 x{n0.x * 0.95f, n0.y * 0.95f};
                const float k = 1.0f - 0.95f;
                const Vector2 nm = norm(md);
                const Vector2 y{nm.x * k, nm.y * k};
                const Vector2 s{x.x + y.x, x.y + y.y};
                const Vector2 ns = norm(s);
                const Vector2 want{(ns.x * 45.0f) * 1.6f, (ns.y * 45.0f) * 1.6f};
                afterUpdate = [&, want](game::Enemy& e2) {
                    if (&e2 != &ea || chaseChecked) return;
                    chaseChecked = true;
                    chaseFormula = Same(e2.body->velocity(), want);
                };
            };
        }
        frame(f);
    }
    beforeUpdate = nullptr;
    afterUpdate = nullptr;
    Check(a->targetPlayer && b->targetPlayer && a->enemyState == game::EnemyBehaviour::EnemyState::PursuingPlayer, "a second after, they target the hero");
    Check(chaseChecked && chaseFormula, "ChasePlayer's velocity: (v.normalized * 0.95 + 0.05 * dir.normalized).normalized * 45 * 1.6, bit for bit");
    const float speedA = unity::Magnitude(ea.body->velocity());
    Check(std::fabs(speedA - 72.0f) < 0.01f, "chasing at 72 (" + std::to_string(speedA) + ")");
    Check(attackWindowOk && attackCycles >= 4, "the AttackCollider on only in HelixFly's first 1/14 s (" + std::to_string(attackCycles) + " cycles)");
    bool hitWindows = !heroHits.empty();
    for (const HeroHit& h : heroHits) hitWindows = hitWindows && h.boxOn;
    Check(hitsOk && hitWindows, "its hits on the hero: DealDamageToPlayer(10, \"FlyingRepairGolem\"), each with its box on (" +
                                    std::to_string(heroHits.size()) + " hits)");

    // 2. An Iron Bar dropped in the room, far from him: both go for it.
    bar.position = {-200.0f, 100.0f};
    bool handlerDraws = true;
    std::vector<const void*> handled;
    unity::Random::State handlerStart{};
    drops.onHandler = [&](const void* key, bool after) {
        unity::Random::State& s = handlerStart;
        if (!after) {
            s = unity::Random::GetState();
            handled.push_back(key);
            return;
        }
        const int n = unity::Random::DrawsBetween(s, unity::Random::GetState(), 8);
        handlerDraws = handlerDraws && n == (drops.GetDropList().empty() ? 0 : 1);
    };
    barDropped = true;
    drops.AddDropItems({&bar});
    Check(handled.size() == 2 && handled[0] == a && handled[1] == b && handlerDraws && a->targetItem == &bar && b->targetItem == &bar &&
              a->itemsDroppedInRoom == &drops.GetDropList(),
          "AddDropItems: each golem's OnItemDroppedInRoom in turn, GetRandom's one draw, the bar their target");
    // ChaseItem: transform.Translate(dir.normalized * 45 * deltaTime), bit for bit.
    bool translateOk = true, translated = false;
    beforeUpdate = [&](game::Enemy& e) {
        auto* g = static_cast<game::FlyingRepairGolemBehaviour*>(e.enemyBehaviour.get());
        if (!g->canMove || !g->stolenItems.empty() || g->targetItem != &bar || e.destroyed) return;
        const Vector2 p = e.position();
        const Vector2 d{bar.position.x - p.x, bar.position.y - p.y};
        const float mag = static_cast<float>(std::sqrt(static_cast<double>(d.x) * d.x + static_cast<double>(d.y) * d.y));
        const Vector2 n{d.x / mag, d.y / mag};
        const Vector2 t{(n.x * 45.0f) * dt, (n.y * 45.0f) * dt};
        // transform.position = position + translation: the setter's local position under its
        // parents (from the root down).
        Vector2 want{p.x + t.x, p.y + t.y};
        for (auto it = e.navAgent.parents.rbegin(); it != e.navAgent.parents.rend(); ++it) want = want - *it;
        afterUpdate = [&, want](game::Enemy& e2) {
            if (&e2 != &e) return;
            translated = true;
            translateOk = translateOk && Same(e2.navAgent.localPosition, want);
            afterUpdate = nullptr;
        };
    };
    game::Enemy* thief = nullptr;
    int pickFrame = -1;
    const size_t eventsBefore = events.size();
    for (; f < 900 && !thief; f++) {
        frame(f);
        if (!a->stolenItems.empty()) thief = &ea;
        else if (!b->stolenItems.empty()) thief = &eb;
        if (thief) pickFrame = f;
    }
    beforeUpdate = nullptr;
    afterUpdate = nullptr;
    Check(translated && translateOk, "ChaseItem: Translate(dir.normalized * 45 * deltaTime), bit for bit");
    Check(thief != nullptr, "a golem reaches the bar and steals it (frame " + std::to_string(pickFrame) + ")");
    if (!thief) {
        game::PolyNav2D::current = nullptr;
        return;
    }
    auto* t = static_cast<game::FlyingRepairGolemBehaviour*>(thief->enemyBehaviour.get());
    game::Enemy& other = thief == &ea ? eb : ea;
    auto* o = static_cast<game::FlyingRepairGolemBehaviour*>(other.enemyBehaviour.get());
    Check(t->stolenItems.size() == 1 && t->stolenItems[0] == &bar && !bar.active && bar.parent == thief && t->itemSprite == "IronBar" &&
              t->targetItem == nullptr && drops.GetDropList().empty(),
          "the bar stolen: carried, off, under the golem, shown, out of the room's list");
    Check(o->targetItem == nullptr && handled.size() == 4 && handlerDraws, "RemoveDropItem: both handlers again, the list empty: no draw, no target");
    Check(eventDraws && (!haveAudio || pickSounds >= 1), "the pick's sound (MasterAudio's draws); the other events draw nothing");

    // 3. The thief roams; the other is hit.
    bool roamOk = true;
    int roams = 0;
    afterUpdate = [&](game::Enemy& e) {
        if (&e != thief || !t->canMove || e.destroyed) return;
        if (lastDraws[&e] > 0) {
            roams++;
            const int tries = static_cast<int>(t->lastPoints.size());
            bool valid = true;
            if (tries < 7) valid = nav.PointIsValid({t->lastPoints.back().x, t->lastPoints.back().y});
            for (int k = 0; k + 1 < tries; k++) valid = valid && !nav.PointIsValid({t->lastPoints[static_cast<size_t>(k)].x, t->lastPoints[static_cast<size_t>(k)].y});
            const Vector2 p = e.position();
            roamOk = roamOk && tries >= 1 && tries <= 7 && valid && lastDraws[&e] == 1 + 3 * tries && t->patrolLapse >= 0.5f && t->patrolLapse < 1.0f &&
                     Same({t->moveDirection.x, t->moveDirection.y}, {t->lastPoints.back().x - p.x, t->lastPoints.back().y - p.y}) &&
                     t->enemyState == game::EnemyBehaviour::EnemyState::RunningAway;
        } else {
            roamOk = roamOk && std::fabs(unity::Magnitude(e.body->velocity()) - 45.0f) < 0.01f;
        }
    };
    for (int k = 0; k < 120; k++, f++) frame(f);
    // The pick's clip, played once: its events in order (the unnamed one at 0.25 calls nothing).
    std::vector<std::string> pick;
    for (size_t k = eventsBefore; k < events.size(); k++)
        if (events[k].first == thief) pick.push_back(events[k].second);
    const char* const kPick[4] = {"PlayFlyingRepairPick", "ForbidEnemyMovement", "PickItemCollisionedAndRemoveItFromRoom", "AllowEnemyMovement"};
    bool pickOrder = pick.size() == 4;
    for (size_t k = 0; pickOrder && k < 4; k++) pickOrder = pick[k] == kPick[k];
    std::string got;
    for (const std::string& s : pick) got += " " + (s.empty() ? std::string("\"\"") : s);
    Check(pickOrder, "PickingItem's events in order, the unnamed one calling nothing:" + got);
    Check(thief->enemyAnimator->CurrentState() == controller.State("HelixFly"), "back in HelixFly after the pick");
    Check(roamOk && roams >= 2,
          "the thief roams: Range(0.5, 1) then 3 draws a try (at most 7, the last inside the map), at speed 45 (" + std::to_string(roams) + " points)");
    afterUpdate = nullptr;
    // The other, hit: stopped, can't move; moving again from standing still, a circle thrown away.
    other.DealDamageToEnemy(10.0f);
    Check(o->wasHit && !o->canMove && Same(other.body->velocity(), {0.0f, 0.0f}), "hit: wasHit, the body stopped, canMove off");
    bool resumed = false, resumedOk = false, resumedStill = false;
    beforeUpdate = [&](game::Enemy& e) {
        if (&e != &other || resumed || !o->canMove) return;
        resumed = true;
        const bool still = unity::Magnitude(e.body->velocity()) == 0.0f;
        resumedStill = still;
        const Vector2 p = e.position();
        afterUpdate = [&, still, p](game::Enemy& e2) {
            if (&e2 != &other) return;
            const Vector2 v = e2.body->velocity();
            const float away = (v.x * (hero.position.x - p.x) + v.y * (hero.position.y - p.y));
            resumedOk = still ? (lastDraws[&e2] == 2 && !o->wasHit && away < 0.0f && std::fabs(unity::Magnitude(v) - 72.0f) < 0.01f)
                              : (lastDraws[&e2] == 0 && o->wasHit);
            afterUpdate = nullptr;
        };
    };
    for (int k = 0; k < 120; k++, f++) frame(f);
    beforeUpdate = nullptr;
    afterUpdate = nullptr;
    Check(resumed && resumedOk, "moving again, standing still: an insideUnitCircle thrown away (2 draws), off away from the hero at 72");

    // 4. The thief killed: what it carried dropped where it is, out of the delegate.
    const Vector2 at = thief->position();
    thief->DealDamageToEnemy(1000.0f);
    Check(!thief->enemyStats.isAlive() && dropped.size() == 1 && dropped[0].item == &bar && Same(dropped[0].at, at) && dropped[0].radius == 30.0f &&
              dropped[0].inRoom && bar.active && t->stolenItems.empty() && t->itemSprite.empty(),
          "its death: the bar back on, ItemStack.Drop'ped at it (30 around, into the room); nothing carried, no sprite");
    Check(drops.Subscriptions(t) == 0 && drops.Subscriptions(o) == 1 && drops.GetDropList().empty() && deaths == 1,
          "its death: off OnDropListChanged (the other still on), the bar not back in the list, counted");
    // The dead one's handler is gone: an add calls only the other.
    handled.clear();
    game::DroppedItem second;
    drops.AddDropItems({&second});
    Check(handled.size() == 1 && handled[0] == o && o->targetItem == &second, "the next drop: only the live one's handler");
    // Cancel: off the delegate; twice on: called twice.
    o->CancelPlayerTargetingAfterTime();
    Check(drops.Subscriptions(o) == 0, "CancelPlayerTargetingAfterTime: off OnDropListChanged");
    o->StartTargetingPlayerAfterTime(1.0f);
    o->StartTargetingPlayerAfterTime(1.0f);
    handled.clear();
    const unity::Random::State before = unity::Random::GetState();
    drops.AddDropItems({});
    Check(handled.size() == 2 && unity::Random::DrawsBetween(before, unity::Random::GetState(), 8) == 2,
          "subscribed twice: called twice, a draw each");
    // OnCollisionEnter2D: the layer index against 1 << LevelBoundary: a wall does nothing.
    const Vector2 v = other.body->velocity();
    const game::GolemVector3 md = o->moveDirection;
    o->OnCollisionEnter2D(11, true, {0.0f, -1.0f});
    Check(Same(other.body->velocity(), v) && md.x == o->moveDirection.x && md.y == o->moveDirection.y,
          "OnCollisionEnter2D with a LevelBoundary (layer 11, not 2048): nothing");
    std::printf("golems in a box: %zu hits on the hero, %d attack cycles, golem %s stole the bar (frame %d), %d roaming points, the hit one "
                "%s\n",
                heroHits.size(), attackCycles, thief == &ea ? "A" : "B", pickFrame, roams,
                resumedStill ? "stood still (its circle thrown away)" : "was moving (no draw)");
    game::PolyNav2D::current = nullptr;
}

} // namespace
