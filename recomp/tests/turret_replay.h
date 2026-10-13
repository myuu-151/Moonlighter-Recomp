#pragma once
// The Broken turret's part of enemy_test's replay (tests/enemy_replay.h's ReplayHooks), for
// enemy_BrokenGolemTurret.jsonl (`-oracle-enemy=BrokenGolemTurret`; TurretOracle.cs writes the
// fields read here):
//   - the scene gets the room's projectiles (game/EnemyProjectile.h: the pool manager, the shots'
//     bodies in the replay's world, the room's transform chain "roomChain") and the room's
//     colliders for the turrets' raycasts (and the holes' check); the shots' pool is made as the
//     recording has it ("pools": its ids in order, the port's objects given them);
//   - each turret from its recorded state ("turret": patrolLapse, canShoot, enemyDirection, the
//     Animator's Direction* bools and "Shoot");
//   - each fixed step's start ("psteps"): every active shot by id (transform, body position and
//     velocity, awake, isTrigger); how many points its Rigidbody2D.GetContacts hands back
//     (EnemyProjectiles::GetContactsCount: the last simulation's touching contacts' manifold
//     points, none for a trigger);
//   - each shot's OnCollisionEnter2D ("hit", in order within its step): which shot, what it hit
//     (tag, name), from the game's Random state before it (resynced) and the hero's transform
//     then, its draws compared after (the impact sound's);
//   - per frame, in order: the looks' raycasts ("look": origin, direction, distance, mask, the
//     collider hit; point, fraction and distance inform), ShootAnimation's runs ("shootAnim":
//     which turret, which step: the Invokes' timing), the shots taken ("shot": turret, step,
//     projectile id, where, direction), the pushes on the hero ("push": step, force),
//     DestroyNow ("destroyNow": projectile, step);
//   - each frame's end: every active shot ("proj": transform world and local, body, awake,
//     isTrigger, Animator state and normalized time, sprite, the art's rotation), the pools'
//     order ("pooled"), each turret's fields; its transform touched after its Update ("tc", the
//     Animator's root motion) informs;
//   - after the last frame, "rootMotionProbe" (TurretRootMotionProbe.cs) where the recording has
//     it: the root motion's effect on a moving body against the port's model (AfterFrames).

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "enemy_replay.h"
#include "game/BrokenGolemTurret.h"
#include "game/EnemyProjectile.h"

namespace {

struct TurretReplay : ReplayHooks {
    const game::Prefab* projectilePrefab = nullptr;
    const unity::AnimatorController* projectileController = nullptr;

    std::unique_ptr<game::EnemyProjectiles> sys;
    game::EnemyHero* hero = nullptr;
    std::map<int, game::Enemy*> enemies;
    std::map<const game::EnemyProjectile*, int> ids;
    int nextId = 0;
    int step = -1; // the fixed step running (-1: the frame's part)
    const Value* frameLine = nullptr;
    size_t nextHit = 0; // the frame's game "hit" events matched so far
    size_t heroHits = 0; // DealDamageToPlayer calls in this step (or the frame's part) so far
    // The frame's "tev" (set before its first step and before its Updates), the looks per turret so far.
    const Value* frameEvents = nullptr;
    std::map<int, size_t> looks;

    // The port's log of the frame.
    struct Shot {
        int s, step, p, dir;
        Vector2 pos;
    };
    std::vector<std::pair<int, int>> shootAnims; // (turret, step)
    std::vector<Shot> shots;
    std::vector<std::pair<int, Vector2>> pushes;   // (step, force)
    std::vector<std::pair<int, int>> destroys;     // (projectile, step)
    std::vector<std::string> hits;                 // "p/step/tag"
    int tcFrames = 0, tcGame = 0;
    float fixedDt = 0.0f; // (the start's Time.fixedDeltaTime: the probe's next step)

    int IdOf(const game::EnemyProjectile* p)
    {
        const auto it = ids.find(p);
        if (it != ids.end()) return it->second;
        ids[p] = nextId;
        return nextId++;
    }
    game::EnemyProjectile* ById(int id)
    {
        for (const auto& [p, i] : ids)
            if (i == id) return const_cast<game::EnemyProjectile*>(p);
        return nullptr;
    }
    int TurretId(const game::Enemy& e) const
    {
        for (const auto& [id, en] : enemies)
            if (en == &e) return id;
        return -1;
    }

    void OnScene(const Value& start, game::EnemyScene& scene, game::EnemyHero& h, unity::PhysicsWorld& world, game::EnemyPhysics& physics,
                 unity::Physics2D& roomColliders, unity::Rigidbody2D& heroBody, Tally& t) override
    {
        hero = &h;
        fixedDt = Bits(start["fixedDeltaTime"]);
        sys = std::make_unique<game::EnemyProjectiles>(scene);
        sys->Register(projectilePrefab, projectileController);
        sys->SetPhysics(world, physics);
        sys->SetHero(&heroBody);
        sys->heroFollowsBody = false; // (the game's transform comes with each "hit")
        if (const Value* chain = start.Find("roomChain"))
            for (const Value& c : chain->arr) sys->roomParents.push_back(V2(c));
        else
            std::printf("note: no roomChain in the recording: shots parented at the origin\n");
        scene.projectiles = sys.get();
        scene.queries = &roomColliders;
        h.addForce = [this](Vector2 force) { pushes.emplace_back(step, force); };
        // DealDamageToPlayer is the hero's (not this replay's): his own Random draws in it (the
        // will_damaged sound's, the rumble's) from the game's states around it ("dmgToPlayer"'s
        // rng0 and rng1, where recorded), the state it starts from compared (the shot's draws
        // before it), so a shot's OnCollisionEnter2D on him compares the turret's own draws.
        auto dealDamage = h.dealDamage;
        h.dealDamage = [this, dealDamage, &t](float strength, const std::string& source, game::Enemy& e) {
            if (dealDamage) dealDamage(strength, source, e);
            const Value* events = frameLine ? frameLine->Find("events") : nullptr;
            if (!events) return;
            size_t seen = 0;
            for (const Value& ev : events->arr) {
                if (ev["k"].s != "dmgToPlayer" || ev["step"].AsInt() != step || seen++ != heroHits) continue;
                if (ev.Find("rng0")) {
                    t.Add("Random state as DealDamageToPlayer starts (the hero's draws in it are his)",
                          SameState(unity::Random::GetState(), State(ev["rng0"])), "frame " + std::to_string(frameLine->Find("f")->AsInt()));
                    unity::Random::SetState(State(ev["rng1"]));
                }
                break;
            }
            heroHits++;
        };
        t.Add("projectile prefab and controller (ML_DATA)", projectilePrefab && projectileController, "");
        // Each shot's OnCollisionEnter2D against the game's next "hit" of its step: the hero's
        // transform and the Random state the game had then; its draws compared after.
        sys->onCollisionEnter = [this, &t](game::EnemyProjectile& p, const game::Collision2D& c, bool after) {
            const Value* game = nullptr;
            if (frameLine)
                if (const Value* tev = frameLine->Find("tev")) {
                    size_t seen = 0;
                    for (const Value& e : tev->arr)
                        if (e["k"].s == "hit" && seen++ == nextHit) game = &e;
                }
            if (!after) {
                hits.push_back(std::to_string(IdOf(&p)) + "/" + std::to_string(step) + "/" + c.tag);
                if (game && game->Find("rng0")) {
                    if (game->Find("hero")) hero->position = V2((*game)["hero"]);
                    unity::Random::SetState(State((*game)["rng0"]));
                }
                return;
            }
            if (game && game->Find("rng1")) {
                t.Add("Random draws in a shot's OnCollisionEnter2D (the impact sound)", SameState(unity::Random::GetState(), State((*game)["rng1"])),
                      "projectile " + std::to_string(IdOf(&p)) + " on " + c.tag + ": " +
                          std::to_string(unity::Random::DrawsBetween(State((*game)["rng0"]), unity::Random::GetState(), 64)) + " draws, the game " +
                          std::to_string(unity::Random::DrawsBetween(State((*game)["rng0"]), State((*game)["rng1"]), 64)));
                unity::Random::SetState(State((*game)["rng1"]));
            }
            nextHit++;
        };
    }

    void OnEnemy(game::Enemy& e, int id, const Value& line, Tally& t) override
    {
        auto* turret = dynamic_cast<game::BrokenGolemTurretBehaviour*>(e.enemyBehaviour.get());
        t.Add("the prefab makes a BrokenGolemTurretBehaviour", turret != nullptr, "enemy " + std::to_string(id));
        if (!turret) return;
        enemies[id] = &e;
        if (const Value* tu = line.Find("turret")) {
            turret->patrolLapse = Bits((*tu)["lapse"]);
            turret->canShoot = (*tu)["canShoot"].b;
            turret->enemyDirection = static_cast<game::HeroDirection>((*tu)["dir"].AsInt());
            if (const Value* b = tu->Find("bools"); b && e.enemyAnimator) {
                e.enemyAnimator->SetBool("DirectionUp", (*b)[0].b);
                e.enemyAnimator->SetBool("DirectionDown", (*b)[1].b);
                e.enemyAnimator->SetBool("DirectionLeft", (*b)[2].b);
                e.enemyAnimator->SetBool("DirectionRight", (*b)[3].b);
            }
            if (const Value* sh = tu->Find("shoot"); sh && sh->b && e.enemyAnimator) e.enemyAnimator->SetTrigger("Shoot");
        }
        // Each look from where the game had the hero then (his own Update can come between the
        // turret's and the animation pass's QuitAttackState).
        turret->beforeLook = [this](game::BrokenGolemTurretBehaviour& b) {
            const int id = TurretId(b.enemy);
            const size_t k = looks[id]++;
            if (!frameEvents) return;
            size_t seen = 0;
            for (const Value& e : frameEvents->arr)
                if (e["k"].s == "look" && e["s"].AsInt() == id && seen++ == k) hero->position = V2(e["hero"]);
        };
        turret->onShootAnimation = [this](game::BrokenGolemTurretBehaviour& b) { shootAnims.emplace_back(TurretId(b.enemy), step); };
        turret->onShot = [this](game::BrokenGolemTurretBehaviour& b, game::BrokenGolemTurretProjectile* p) {
            shots.push_back({TurretId(b.enemy), step, IdOf(p), static_cast<int>(p->projectileDirection), p->position()});
            p->onDestroyNow = [this](game::BrokenGolemTurretProjectile& q) { destroys.emplace_back(IdOf(&q), step); };
        };
    }

    void OnEnemiesBuilt(const Value& start, Tally& t) override
    {
        // The pools as the game has them (6 more for every turret of the floor), their objects
        // given the recorded ids in order.
        const Value* pools = start.Find("pools");
        if (!pools) {
            std::printf("note: no pools in the recording\n");
            return;
        }
        int maxId = -1;
        for (const Value& pool : pools->arr) {
            game::PoolManager::Pool* port = sys->poolManager.Find(pool["name"].s);
            t.Add("the shots' pool (Init's CreateOrFillGameObjectPool)", port != nullptr, pool["name"].s);
            if (!port) continue;
            const size_t want = pool["ids"].size();
            if (port->pooledObjects.size() < want) sys->poolManager.FillPool(*port, static_cast<int>(want - port->pooledObjects.size()));
            if (port->pooledObjects.size() > want) port->pooledObjects.resize(want);
            for (size_t k = 0; k < want; k++) {
                const int id = static_cast<int>(pool["ids"][k].AsInt());
                ids[dynamic_cast<game::EnemyProjectile*>(port->pooledObjects[k])] = id;
                if (id > maxId) maxId = id;
            }
        }
        nextId = maxId + 1;
    }

    void BeginFrame(long long, const Value& f) override
    {
        frameLine = &f;
        frameEvents = f.Find("tev");
        heroHits = 0;
    }

    void BeforeStep(long long fn, size_t k, const Value& f, Tally& t) override
    {
        step = static_cast<int>(k);
        heroHits = 0;
        const Value* psteps = f.Find("psteps");
        if (!psteps || k >= psteps->size()) return;
        const Value& rows = (*psteps)[k];
        std::vector<int> listed;
        for (const Value& r : rows.arr) {
            const int id = static_cast<int>(r[0].AsInt());
            listed.push_back(id);
            game::EnemyProjectile* p = ById(id);
            const std::string at = "frame " + std::to_string(fn) + " step " + std::to_string(k) + " projectile " + std::to_string(id);
            if (!p || !p->active || !p->body) {
                t.Add("shot at a fixed step's start (transform, body, awake, trigger)", false, at + ": not active in the port");
                continue;
            }
            const bool ok = Same(p->position(), {Bits(r[1]), Bits(r[2])}) && Same(p->body->position(), {Bits(r[3]), Bits(r[4])}) &&
                            Same(p->body->velocity(), {Bits(r[5]), Bits(r[6])}) && p->body->IsAwake() == r[7].b && p->isTrigger == r[8].b;
            t.Add("shot at a fixed step's start (transform, body, awake, trigger)", ok,
                  at + ": port " + Str(p->position()) + " body " + Str(p->body->position()) + " v " + Str(p->body->velocity()) +
                      (p->body->IsAwake() ? " awake" : " asleep") + (p->isTrigger ? " trigger" : "") + "; game " + Str({Bits(r[1]), Bits(r[2])}) +
                      " body " + Str({Bits(r[3]), Bits(r[4])}) + " v " + Str({Bits(r[5]), Bits(r[6])}) + (r[7].b ? " awake" : " asleep") +
                      (r[8].b ? " trigger" : ""));
            const int points = sys->GetContactsCount(*p);
            t.Add("shot's contact points at a fixed step's start (Rigidbody2D.GetContacts: the last step's touching manifolds)",
                  points == static_cast<int>(r[9].size()), at + ": port " + std::to_string(points) + " game " + r[9].Dump());
        }
        for (game::EnemyProjectile* p : sys->All())
            if (p->active && std::find(listed.begin(), listed.end(), IdOf(p)) == listed.end())
                t.Add("shot at a fixed step's start (transform, body, awake, trigger)", false,
                      "frame " + std::to_string(fn) + " step " + std::to_string(k) + " projectile " + std::to_string(IdOf(p)) + ": active in the port only");
    }

    void AfterStep(long long, size_t, const Value&, Tally&) override
    {
        step = -1;
        heroHits = 0;
    }

    void BeforeAnimators(float dt) override { sys->UpdateAnimators(dt); }

    void AtFrameEnd(long long fn, const Value& f, Tally& t) override
    {
        const std::string at = "frame " + std::to_string(fn);
        const Value* tev = f.Find("tev");
        if (tev) {
            // The looks' raycasts, per turret in order.
            std::map<int, std::vector<const Value*>> gameRays;
            for (const Value& e : tev->arr)
                if (e["k"].s == "look")
                    for (const Value& r : e["rays"].arr) gameRays[static_cast<int>(e["s"].AsInt())].push_back(&r);
            for (const auto& [id, en] : enemies) {
                auto* turret = dynamic_cast<game::BrokenGolemTurretBehaviour*>(en->enemyBehaviour.get());
                const std::vector<const Value*>& game = gameRays[id];
                const auto& mine = turret->rays;
                if (game.empty() && mine.empty()) continue;
                bool same = game.size() == mine.size(), where = same;
                std::string list = at + " turret " + std::to_string(id) + ": port";
                for (const auto& r : mine) list += " " + Str(r.direction) + (r.hit.collider ? "->" + r.hit.collider->path : "->none");
                list += "; game";
                for (const Value* r : game) list += " " + Str(V2((*r)["d"])) + "->" + ((*r)["hit"].IsString() ? (*r)["hit"].s : "none");
                for (size_t k = 0; same && k < game.size(); k++) {
                    const Value& g = *game[k];
                    const std::string hit = g["hit"].IsString() ? g["hit"].s : "";
                    same = Same(mine[k].origin, V2(g["o"])) && Same(mine[k].direction, V2(g["d"])) && Same(mine[k].distance, Bits(g["dist"])) &&
                           mine[k].layerMask == g["mask"].AsInt() && (mine[k].hit.collider ? mine[k].hit.collider->path : "") == hit;
                    where = same && (hit.empty() || (Same(mine[k].hit.point, V2(g["point"])) && Same(mine[k].hit.fraction, Bits(g["fraction"])) &&
                                                     Same(mine[k].hit.distance, Bits(g["hd"]))));
                }
                t.Add("turret's raycasts (origin, direction, distance, mask, what they hit)", same, list);
                if (same) t.Info("turret's raycasts' hits (point, fraction, distance)", where, list);
            }
            // ShootAnimation's runs, the shots, the pushes, DestroyNow.
            std::vector<std::pair<int, int>> gameAnims, gameDestroys;
            std::vector<Shot> gameShots;
            std::vector<std::pair<int, Vector2>> gamePushes;
            std::vector<std::string> gameHits;
            for (const Value& e : tev->arr) {
                const std::string& k = e["k"].s;
                const int st = static_cast<int>(e["step"].AsInt());
                if (k == "shootAnim") gameAnims.emplace_back(static_cast<int>(e["s"].AsInt()), st);
                else if (k == "destroyNow") gameDestroys.emplace_back(static_cast<int>(e["p"].AsInt()), st);
                else if (k == "push") gamePushes.emplace_back(st, V2(e["force"]));
                else if (k == "hit") gameHits.push_back(std::to_string(e["p"].AsInt()) + "/" + std::to_string(st) + "/" + e["tag"].s);
                else if (k == "shot" && e["canShoot"].b)
                    gameShots.push_back({static_cast<int>(e["s"].AsInt()), st, static_cast<int>(e["p"].AsInt()), static_cast<int>(e["dir"].AsInt()), V2(e["pos"])});
            }
            auto pairs = [](const std::vector<std::pair<int, int>>& v) {
                std::string out;
                for (const auto& p : v) out += " " + std::to_string(p.first) + "@" + std::to_string(p.second);
                return out;
            };
            if (!gameAnims.empty() || !shootAnims.empty())
                t.Add("ShootAnimation runs (turret, fixed step: the Invoke's timing)", gameAnims == shootAnims, at + ": port" + pairs(shootAnims) + "; game" + pairs(gameAnims));
            if (!gameDestroys.empty() || !destroys.empty())
                t.Add("DestroyNow runs (projectile, fixed step)", gameDestroys == destroys, at + ": port" + pairs(destroys) + "; game" + pairs(gameDestroys));
            if (!gameShots.empty() || !shots.empty()) {
                bool same = gameShots.size() == shots.size();
                for (size_t k = 0; same && k < shots.size(); k++)
                    same = shots[k].s == gameShots[k].s && shots[k].step == gameShots[k].step && shots[k].p == gameShots[k].p &&
                           shots[k].dir == gameShots[k].dir && Same(shots[k].pos, gameShots[k].pos);
                std::string list = at + ": port";
                for (const Shot& s : shots) list += " " + std::to_string(s.s) + ":" + std::to_string(s.p) + "@" + Str(s.pos) + " dir " + std::to_string(s.dir);
                list += "; game";
                for (const Shot& s : gameShots) list += " " + std::to_string(s.s) + ":" + std::to_string(s.p) + "@" + Str(s.pos) + " dir " + std::to_string(s.dir);
                t.Add("shots taken (turret, step, pool id, where, direction)", same, list);
            }
            if (!gamePushes.empty() || !pushes.empty()) {
                bool same = gamePushes.size() == pushes.size();
                for (size_t k = 0; same && k < pushes.size(); k++) same = pushes[k].first == gamePushes[k].first && Same(pushes[k].second, gamePushes[k].second);
                t.Add("the hero pushed (PushPlayerBackwards: step, force)", same,
                      at + ": port " + std::to_string(pushes.size()) + (pushes.empty() ? "" : " " + Str(pushes[0].second)) + "; game " +
                          std::to_string(gamePushes.size()) + (gamePushes.empty() ? "" : " " + Str(gamePushes[0].second)));
            }
            if (!gameHits.empty() || !hits.empty()) {
                std::string list = at + ": port";
                for (const auto& h : hits) list += " " + h;
                list += "; game";
                for (const auto& h : gameHits) list += " " + h;
                t.Add("shots' OnCollisionEnter2D (projectile, step, what's hit)", gameHits == hits, list);
            }
        }
        // The shots at the frame's end.
        if (const Value* proj = f.Find("proj")) {
            std::vector<int> listed;
            for (const Value& v : proj->arr) {
                const int id = static_cast<int>(v["p"].AsInt());
                listed.push_back(id);
                game::EnemyProjectile* p = ById(id);
                const std::string where = at + " projectile " + std::to_string(id);
                if (!p || !p->active || !p->body) {
                    t.Add("shot at the frame's end (transform, body, awake, trigger)", false, where + ": not active in the port");
                    continue;
                }
                const Value& rb = v["rb"];
                t.Add("shot at the frame's end (transform, body, awake, trigger)",
                      Same(p->position(), V2(v["pos"])) && Same(p->localPosition, V2(v["local"])) &&
                          Same(p->body->position(), {Bits(rb[0]), Bits(rb[1])}) && Same(p->body->velocity(), {Bits(rb[2]), Bits(rb[3])}) &&
                          p->body->IsAwake() == v["awake"].b && p->isTrigger == v["trigger"].b,
                      where + ": port " + Str(p->position()) + " local " + Str(p->localPosition) + " body " + Str(p->body->position()) + " v " +
                          Str(p->body->velocity()) + "; game " + Str(V2(v["pos"])) + " local " + Str(V2(v["local"])) + " body " +
                          Str({Bits(rb[0]), Bits(rb[1])}) + " v " + Str({Bits(rb[2]), Bits(rb[3])}));
                if (const Value* an = v.Find("anim"); an && p->animator) {
                    const unity::AnimatorStateInfo info = p->animator->GetCurrentAnimatorStateInfo();
                    t.Add("shot's Animator (state, normalized time)",
                          info.shortNameHash == static_cast<int32_t>((*an)["state"].AsInt()) && Same(info.normalizedTime, Bits((*an)["t"])),
                          where + ": port t " + std::to_string(info.normalizedTime) + " game " + std::to_string(Bits((*an)["t"])));
                }
                t.Add("shot's sprite", v["sprite"].IsString() && p->sprite == v["sprite"].s, where + ": port " + p->sprite + " game " + v["sprite"].Dump());
                if (const Value* art = v.Find("art"))
                    t.Add("shot's art rotation", std::fabs(Bits(*art) - p->artRotation) < 0.01f,
                          where + ": port " + std::to_string(p->artRotation) + " game " + std::to_string(Bits(*art)));
            }
            for (game::EnemyProjectile* p : sys->All())
                if (p->active && std::find(listed.begin(), listed.end(), IdOf(p)) == listed.end())
                    t.Add("shot at the frame's end (transform, body, awake, trigger)", false, at + " projectile " + std::to_string(IdOf(p)) + ": active in the port only");
        }
        if (const Value* pooled = f.Find("pooled"))
            for (const Value& pool : pooled->arr) {
                const game::PoolManager::Pool* port = sys->poolManager.Find(pool["name"].s);
                std::vector<int> mine, game;
                if (port)
                    for (const game::PooledObject* o : port->pooledObjects) mine.push_back(IdOf(dynamic_cast<const game::EnemyProjectile*>(o)));
                for (const Value& id : pool["ids"].arr) game.push_back(static_cast<int>(id.AsInt()));
                std::string list = at + ": port";
                for (int i : mine) list += " " + std::to_string(i);
                list += "; game";
                for (int i : game) list += " " + std::to_string(i);
                t.Add("the pool's order (GetObject's first, ReturnObject's last)", mine == game, list);
            }
        // Each turret's own fields.
        if (const Value* list = f.Find("enemies"))
            for (const Value& v : list->arr) {
                const auto it = enemies.find(static_cast<int>(v["s"].AsInt()));
                const Value* tu = v.Find("turret");
                if (it == enemies.end() || !tu || it->second->destroyed) continue;
                auto* turret = dynamic_cast<game::BrokenGolemTurretBehaviour*>(it->second->enemyBehaviour.get());
                std::vector<int> mine, game;
                for (auto* p : turret->projectilesShot) mine.push_back(IdOf(p));
                for (const Value& id : (*tu)["shot"].arr) game.push_back(static_cast<int>(id.AsInt()));
                bool ok = Same(turret->patrolLapse, Bits((*tu)["lapse"])) && turret->canShoot == (*tu)["canShoot"].b &&
                          static_cast<int>(turret->enemyDirection) == (*tu)["dir"].AsInt() && mine == game;
                if (const Value* b = tu->Find("bools"); b && it->second->enemyAnimator && it->second->animatorEnabled) {
                    const unity::Animator& a = *it->second->enemyAnimator;
                    ok = ok && a.GetBool("DirectionUp") == (*b)[0].b && a.GetBool("DirectionDown") == (*b)[1].b && a.GetBool("DirectionLeft") == (*b)[2].b &&
                         a.GetBool("DirectionRight") == (*b)[3].b;
                }
                t.Add("turret (patrolLapse, canShoot, enemyDirection, shots in flight, Direction bools)", ok,
                      at + " turret " + std::to_string(v["s"].AsInt()) + ": port lapse " + std::to_string(turret->patrolLapse) + " canShoot " +
                          std::to_string(turret->canShoot) + " dir " + std::to_string(static_cast<int>(turret->enemyDirection)) + "; game " +
                          std::to_string(Bits((*tu)["lapse"])) + " " + std::to_string((*tu)["canShoot"].b) + " " + std::to_string((*tu)["dir"].AsInt()));
                tcFrames++;
                tcGame += (*tu)["tc"].b;
            }
        // The port's log starts over each frame.
        for (auto& [id, en] : enemies)
            if (auto* turret = dynamic_cast<game::BrokenGolemTurretBehaviour*>(en->enemyBehaviour.get())) turret->rays.clear();
        shootAnims.clear();
        shots.clear();
        pushes.clear();
        destroys.clear();
        hits.clear();
        nextHit = 0;
        frameLine = nullptr;
        frameEvents = nullptr;
        looks.clear();
    }

    // The Animator's root motion on a turret body that moves ("rootMotionProbe", made after the
    // recording: TurretRootMotionProbe.cs): the probe's velocity set on the port's turret, then the
    // port's Animator pass (game/EnemyRootMotion.cpp), against the game's body at the frame's end
    // (velocity bits, awake); and the next fixed step against the velocity the pass left (a body
    // without contacts moved by it: the game's position after the step is the one before plus
    // that velocity times the step, here not moved at all).
    void AfterFrames(const std::vector<Value>& lines, Tally& t) override
    {
        bool probed = false;
        for (const Value& l : lines) {
            if (l["ev"].s != "rootMotionProbe") continue;
            probed = true;
            const auto it = enemies.find(static_cast<int>(l["s"].AsInt()));
            game::Enemy* e = it != enemies.end() ? it->second : nullptr;
            const unity::AnimatorController* c = e ? e->Controller() : nullptr;
            t.Add("rootMotionProbe: applyRootMotion as the port's controller has it (animators.py)",
                  c && (c->applyRootMotion == 1) == l["applyRootMotion"].b,
                  "turret " + std::to_string(l["s"].AsInt()) + ": port " + std::to_string(c ? c->applyRootMotion : -2) + ", game " +
                      std::to_string(l["applyRootMotion"].b));
            const Value &before = l["before"], &set = l["set"], &afterSet = l["afterSet"], &end = l["frameEnd"], &step = l["afterStep"];
            std::string port = "no turret " + std::to_string(l["s"].AsInt()) + " in the port";
            bool ok = false;
            if (e && e->body && e->physics && !e->destroyed) {
                const Vector2 saved = e->body->velocity();
                e->body->SetVelocity(V2(set));
                const bool setTook = Same(e->body->velocity(), V2(afterSet["vel"]));
                e->physics->ApplyRootMotion(*e);
                const Vector2 v = e->body->velocity();
                const bool awake = e->body->IsAwake();
                e->body->SetVelocity(saved);
                // (b2Island::Solve: the velocity damped by the body's drag, the position moved by it.)
                const Vector2 b = V2(before["pos"]);
                const float damp = 1.0f / (1.0f + fixedDt * e->body->Body()->GetLinearDamping());
                const Vector2 vd = {v.x * damp, v.y * damp};
                const Vector2 moved = {b.x + fixedDt * vd.x, b.y + fixedDt * vd.y};
                ok = setTook && Same(v, V2(end["vel"])) && awake == end["awake"].b && Same(V2(end["pos"]), b) && Same(V2(step["pos"]), moved) &&
                     Same(V2(step["vel"]), vd) && step["awake"].b == awake;
                port = "port: after the pass v " + Str(v) + (awake ? " awake" : " asleep") + ", next step pos " + Str(moved);
            }
            t.Add("rootMotionProbe: the Animator pass on a moving body (the port's root motion: the velocity it leaves, awake; the next step)", ok,
                  port + "; game: set " + Str(V2(set)) + ", frame end v " + Str(V2(end["vel"])) + (end["awake"].b ? " awake" : " asleep") +
                      ", next step pos " + Str(V2(before["pos"])) + " -> " + Str(V2(step["pos"])) + " v " + Str(V2(step["vel"])) +
                      (step["awake"].b ? " awake" : " asleep") + " (updateMode " + std::to_string(l["updateMode"].AsInt()) + ")");
        }
        // (The probe needs a turret still active when the recording ends: none after the hero
        // leaves the room, as he falls into its secret room at frame 208 of the first recording,
        // or kills its one turret (-oracle-hero-off-holes and -oracle-hero-no-attacks keep both
        // from happening).)
        if (!probed)
            std::printf("note: no \"rootMotionProbe\" line (TurretRootMotionProbe.cs: no turret active at the recording's end): the root "
                        "motion's model (game/EnemyRootMotion.cpp) unchecked\n");
    }

    // After the replay: how often the turrets' transforms were touched after their Update.
    void Summary() const
    {
        if (tcFrames)
            std::printf("turret transform changed after its Update (the Animator's root motion) in %d of %d turret-frames (the port: never)\n",
                        tcGame, tcFrames);
    }
};

} // namespace
