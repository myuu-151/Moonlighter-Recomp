#pragma once
// The GolemButler's part of enemy_test's replay (tests/enemy_replay.h's ReplayHooks), for
// enemy_GolemButler.jsonl (`-oracle-enemy=GolemButler`; ButlerOracle.cs writes the fields read
// here):
//   - the scene gets the room's projectiles (game/EnemyProjectile.h: the pool manager, the shots'
//     bodies in the replay's world, the room's transform chain "roomChain") and the room's
//     colliders (the holes' check); the shots' pool is made as the recording has it ("pools": its
//     ids in order, the port's objects given them); Time.fixedDeltaTime for a hit's MoveEnemy;
//   - each butler from its recorded state ("butler": previousEnemyHealth, isWaiting,
//     shotEnabled, distanceX / distanceY, the "ShootAttack" and "Attacking" triggers);
//   - each fixed step's start ("psteps"): every active shot by id (transform, body position and
//     velocity, awake, isTrigger); what its body touches informs without failing;
//   - each shot's OnCollisionEnter2D ("hit", in order within its step): which shot, what it hit
//     (tag), from the game's Random state before it (resynced) and the hero's transform then,
//     its draws compared after (the impact sound's);
//   - per frame, in order: each LookToPosition ("look") and each ShootAttack ("shot") from where
//     the game had the hero then; the field of vision's and the back's results ("vision",
//     "back", per butler in order); the shots taken (butler, step, projectile id, where, the
//     direction given, the PixelRotation's angle); DestroyNow (projectile, step); the pushes;
//   - each frame's end: every active shot ("proj": transform world and local, body, awake,
//     isTrigger, Animator state and normalized time, sprite, the PixelRotation's angle), the
//     pools' order ("pooled"), each butler's fields (previousEnemyHealth, isWaiting,
//     shotEnabled, distanceX / distanceY, the Invokes pending, the two triggers); the hero's and
//     the butlers' z (the port's 0) inform.

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "enemy_replay.h"
#include "game/EnemyProjectile.h"
#include "game/GolemButler.h"

namespace {

struct ButlerReplay : ReplayHooks {
    const game::Prefab* projectilePrefab = nullptr;
    const unity::AnimatorController* projectileController = nullptr;

    std::unique_ptr<game::EnemyProjectiles> sys;
    game::EnemyHero* hero = nullptr;
    unity::DelayedCalls* calls = nullptr;
    std::map<int, game::Enemy*> enemies;
    std::map<const game::EnemyProjectile*, int> ids;
    int nextId = 0;
    int step = -1; // the fixed step running (-1: the frame's part)
    const Value* frameLine = nullptr;
    const Value* frameEvents = nullptr;
    size_t nextHit = 0;
    std::map<int, size_t> looks, shootAttacks; // per butler so far this frame

    struct Shot {
        int s, step, p, angle;
        Vector2 pos, dir;
    };
    std::vector<Shot> shots;
    std::vector<std::pair<int, Vector2>> pushes; // (step, force)
    std::vector<std::pair<int, int>> destroys;   // (projectile, step)
    std::vector<std::string> hits;               // "p/step/tag"
    std::map<int, int> lookCount;                // per butler this frame
    int zFrames = 0, zNonZero = 0;

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
    int ButlerId(const game::Enemy& e) const
    {
        for (const auto& [id, en] : enemies)
            if (en == &e) return id;
        return -1;
    }
    // The k-th event of kind `k` of butler `id` in the frame's "tev".
    const Value* Event(const char* k, int id, size_t n) const
    {
        if (!frameEvents) return nullptr;
        size_t seen = 0;
        for (const Value& e : frameEvents->arr)
            if (e["k"].s == k && e["s"].AsInt() == id && seen++ == n) return &e;
        return nullptr;
    }

    void OnScene(const Value& start, game::EnemyScene& scene, game::EnemyHero& h, unity::PhysicsWorld& world, game::EnemyPhysics& physics,
                 unity::Physics2D& roomColliders, unity::Rigidbody2D& heroBody, Tally& t) override
    {
        hero = &h;
        calls = scene.calls;
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
        t.Add("projectile prefab and controller (ML_DATA)", projectilePrefab && projectileController, "");
        sys->onCollisionEnter = [this, &t](game::EnemyProjectile& p, const game::Collision2D& c, bool after) {
            const Value* game = nullptr;
            if (frameEvents) {
                size_t seen = 0;
                for (const Value& e : frameEvents->arr)
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
        auto* butler = dynamic_cast<game::GolemButlerBehaviour*>(e.enemyBehaviour.get());
        t.Add("the prefab makes a GolemButlerBehaviour", butler != nullptr, "enemy " + std::to_string(id));
        if (!butler) return;
        enemies[id] = &e;
        butler->fixedDeltaTime = 0.01f;
        if (const Value* bu = line.Find("butler")) {
            butler->previousEnemyHealth = Bits((*bu)["prevHp"]);
            butler->isWaiting = (*bu)["waiting"].b;
            butler->shotEnabled = (*bu)["shotEnabled"].b;
            butler->distanceX = Bits((*bu)["dx"]);
            butler->distanceY = Bits((*bu)["dy"]);
            if ((*bu)["lookInvoked"].b || (*bu)["recoverInvoked"].b)
                std::printf("note: butler %d starts with an Invoke pending (LookToPosition %d, RecoverShot %d): not replayable\n", id, (*bu)["lookInvoked"].b,
                            (*bu)["recoverInvoked"].b);
            if (e.enemyAnimator) {
                if (const Value* sa = bu->Find("shootAttack"); sa && sa->b) e.enemyAnimator->SetTrigger("ShootAttack");
                if (const Value* at = bu->Find("attacking"); at && at->b) e.enemyAnimator->SetTrigger("Attacking");
            }
        }
        // Each look and each shot from where the game had the hero then (the Invoked look runs in
        // the delayed calls' pass, the shot in the animation pass).
        butler->beforeLook = [this](game::GolemButlerBehaviour& b) {
            const int s = ButlerId(b.enemy);
            lookCount[s]++;
            if (const Value* ev = Event("look", s, looks[s]++)) hero->position = V2((*ev)["hero"]);
        };
        butler->beforeShootAttack = [this](game::GolemButlerBehaviour& b) {
            const int s = ButlerId(b.enemy);
            if (const Value* ev = Event("shot", s, shootAttacks[s]++)) hero->position = V2((*ev)["hero"]);
        };
        butler->onShot = [this](game::GolemButlerBehaviour& b, game::GolemButlerProjectile* p) {
            shots.push_back({ButlerId(b.enemy), step, IdOf(p), p->rotationAngle, p->position(), p->projectileDirection});
            p->onDestroyNow = [this](game::GolemButlerProjectile& q) { destroys.emplace_back(IdOf(&q), step); };
        };
    }

    void OnEnemiesBuilt(const Value& start, Tally& t) override
    {
        if (const Value* fdt = start.Find("fixedDeltaTime"))
            for (auto& [id, e] : enemies)
                if (auto* b = dynamic_cast<game::GolemButlerBehaviour*>(e->enemyBehaviour.get())) b->fixedDeltaTime = Bits(*fdt);
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
        // (Shots that the pool made but aren't in it are in flight: none at the start.)
        for (game::EnemyProjectile* p : sys->All())
            if (auto* s = dynamic_cast<game::GolemButlerProjectile*>(p)) s->onDestroyNow = [this](game::GolemButlerProjectile& q) { destroys.emplace_back(IdOf(&q), step); };
    }

    void BeginFrame(long long, const Value& f) override
    {
        frameLine = &f;
        frameEvents = f.Find("tev");
    }

    void BeforeStep(long long fn, size_t k, const Value& f, Tally& t) override
    {
        step = static_cast<int>(k);
        const Value* psteps = f.Find("psteps");
        if (!psteps || k >= psteps->size()) return;
        std::vector<int> listed;
        for (const Value& r : (*psteps)[k].arr) {
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
                  at + ": port " + Str(p->position()) + " body " + Str(p->body->position()) + " v " + Str(p->body->velocity()) + "; game " +
                      Str({Bits(r[1]), Bits(r[2])}) + " body " + Str({Bits(r[3]), Bits(r[4])}) + " v " + Str({Bits(r[5]), Bits(r[6])}));
            int touching = 0;
            for (const b2ContactEdge* ce = p->body->Body()->GetContactList(); ce; ce = ce->next) touching += ce->contact->IsTouching();
            t.Info("shot's contacts at a fixed step's start (how many: GetContacts against Box2D's touching)", touching == static_cast<int>(r[9].size()),
                   at + ": port " + std::to_string(touching) + " game " + r[9].Dump());
        }
        for (game::EnemyProjectile* p : sys->All())
            if (p->active && std::find(listed.begin(), listed.end(), IdOf(p)) == listed.end())
                t.Add("shot at a fixed step's start (transform, body, awake, trigger)", false,
                      "frame " + std::to_string(fn) + " step " + std::to_string(k) + " projectile " + std::to_string(IdOf(p)) + ": active in the port only");
    }

    void AfterStep(long long, size_t, const Value&, Tally&) override { step = -1; }

    void BeforeAnimators(float dt) override { sys->UpdateAnimators(dt); }

    void AtFrameEnd(long long fn, const Value& f, Tally& t) override
    {
        const std::string at = "frame " + std::to_string(fn);
        if (frameEvents) {
            // The looks (how many), the field of vision's and the back's results, per butler in order.
            for (const auto& [id, en] : enemies) {
                auto* b = dynamic_cast<game::GolemButlerBehaviour*>(en->enemyBehaviour.get());
                std::vector<bool> gameVision, gameBack;
                int gameLooks = 0;
                for (const Value& e : frameEvents->arr) {
                    if (e["s"].AsInt() != id) continue;
                    if (e["k"].s == "vision") gameVision.push_back(e["result"].b);
                    else if (e["k"].s == "back") gameBack.push_back(e["result"].b);
                    else if (e["k"].s == "look") gameLooks++;
                    if ((e["k"].s == "vision" || e["k"].s == "back")) {
                        zFrames++;
                        zNonZero += Bits(e["heroZ"]) != 0.0f || Bits(e["posZ"]) != 0.0f;
                    }
                }
                auto list = [](const std::vector<bool>& v) {
                    std::string s;
                    for (bool x : v) s += x ? "1" : "0";
                    return s;
                };
                if (!gameVision.empty() || !b->visionChecks.empty())
                    t.Add("IsPlayerInFieldOfVision (per butler, in order)", gameVision == b->visionChecks,
                          at + " butler " + std::to_string(id) + ": port " + list(b->visionChecks) + " game " + list(gameVision));
                if (!gameBack.empty() || !b->backChecks.empty())
                    t.Add("IsPlayerInBack (per butler, in order)", gameBack == b->backChecks,
                          at + " butler " + std::to_string(id) + ": port " + list(b->backChecks) + " game " + list(gameBack));
                if (gameLooks || lookCount[id])
                    t.Add("LookToPosition runs (per butler)", gameLooks == lookCount[id],
                          at + " butler " + std::to_string(id) + ": port " + std::to_string(lookCount[id]) + " game " + std::to_string(gameLooks));
                b->visionChecks.clear();
                b->backChecks.clear();
            }
            // The shots, DestroyNow, the pushes, the hits.
            std::vector<std::pair<int, int>> gameDestroys;
            std::vector<Shot> gameShots;
            std::vector<std::string> gameHits;
            for (const Value& e : frameEvents->arr) {
                const std::string& k = e["k"].s;
                const int st = static_cast<int>(e["step"].AsInt());
                if (k == "destroyNow") gameDestroys.emplace_back(static_cast<int>(e["p"].AsInt()), st);
                else if (k == "hit") gameHits.push_back(std::to_string(e["p"].AsInt()) + "/" + std::to_string(st) + "/" + e["tag"].s);
                else if (k == "shot" && e["p"].AsInt() >= 0)
                    gameShots.push_back({static_cast<int>(e["s"].AsInt()), st, static_cast<int>(e["p"].AsInt()), static_cast<int>(e["angle"].AsInt()), V2(e["pos"]),
                                         V2(e["dir"])});
            }
            auto pairs = [](const std::vector<std::pair<int, int>>& v) {
                std::string out;
                for (const auto& p : v) out += " " + std::to_string(p.first) + "@" + std::to_string(p.second);
                return out;
            };
            if (!gameDestroys.empty() || !destroys.empty())
                t.Add("DestroyNow runs (projectile, fixed step)", gameDestroys == destroys, at + ": port" + pairs(destroys) + "; game" + pairs(gameDestroys));
            if (!gameShots.empty() || !shots.empty()) {
                bool same = gameShots.size() == shots.size();
                for (size_t k = 0; same && k < shots.size(); k++)
                    same = shots[k].s == gameShots[k].s && shots[k].step == gameShots[k].step && shots[k].p == gameShots[k].p &&
                           shots[k].angle == gameShots[k].angle && Same(shots[k].pos, gameShots[k].pos) && Same(shots[k].dir, gameShots[k].dir);
                std::string list = at + ": port";
                for (const Shot& s : shots) list += " " + std::to_string(s.s) + ":" + std::to_string(s.p) + "@" + Str(s.pos) + " dir " + Str(s.dir) + " " + std::to_string(s.angle);
                list += "; game";
                for (const Shot& s : gameShots) list += " " + std::to_string(s.s) + ":" + std::to_string(s.p) + "@" + Str(s.pos) + " dir " + Str(s.dir) + " " + std::to_string(s.angle);
                t.Add("shots taken (butler, step, pool id, where, direction, PixelRotation)", same, list);
            }
            // (The butler pushes only from HealthNotChangedWhenHit, which a hit can't reach: the health
            // setter calls OnHealthDecreased only for a lower health.)
            if (!pushes.empty())
                t.Add("the hero pushed (none: the butler's push is unreachable)", false, at + ": port " + std::to_string(pushes.size()) + " " + Str(pushes[0].second));
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
                      Same(p->position(), V2(v["pos"])) && Same(p->localPosition, V2(v["local"])) && Same(p->body->position(), {Bits(rb[0]), Bits(rb[1])}) &&
                          Same(p->body->velocity(), {Bits(rb[2]), Bits(rb[3])}) && p->body->IsAwake() == v["awake"].b && p->isTrigger == v["trigger"].b,
                      where + ": port " + Str(p->position()) + " local " + Str(p->localPosition) + " body " + Str(p->body->position()) + " v " +
                          Str(p->body->velocity()) + "; game " + Str(V2(v["pos"])) + " local " + Str(V2(v["local"])) + " body " + Str({Bits(rb[0]), Bits(rb[1])}) +
                          " v " + Str({Bits(rb[2]), Bits(rb[3])}));
                if (const Value* an = v.Find("anim"); an && p->animator) {
                    const unity::AnimatorStateInfo info = p->animator->GetCurrentAnimatorStateInfo();
                    t.Add("shot's Animator (state, normalized time)",
                          info.shortNameHash == static_cast<int32_t>((*an)["state"].AsInt()) && Same(info.normalizedTime, Bits((*an)["t"])),
                          where + ": port t " + std::to_string(info.normalizedTime) + " game " + std::to_string(Bits((*an)["t"])));
                }
                t.Add("shot's sprite", v["sprite"].IsString() && p->sprite == v["sprite"].s, where + ": port " + p->sprite + " game " + v["sprite"].Dump());
                if (const Value* rot = v.Find("rot"))
                    if (const auto* shot = dynamic_cast<const game::GolemButlerProjectile*>(p))
                        t.Add("shot's PixelRotation angle", shot->rotationAngle == rot->AsInt(),
                              where + ": port " + std::to_string(shot->rotationAngle) + " game " + std::to_string(rot->AsInt()));
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
        // Each butler's own fields.
        if (const Value* list = f.Find("enemies"))
            for (const Value& v : list->arr) {
                const auto it = enemies.find(static_cast<int>(v["s"].AsInt()));
                const Value* bu = v.Find("butler");
                if (it == enemies.end() || !bu || it->second->destroyed) continue;
                auto* b = dynamic_cast<game::GolemButlerBehaviour*>(it->second->enemyBehaviour.get());
                const bool lookInvoked = calls && calls->IsInvoking(b, "LookToPosition");
                const bool recoverInvoked = calls && calls->IsInvoking(b, "RecoverShot");
                bool ok = Same(b->previousEnemyHealth, Bits((*bu)["prevHp"])) && b->isWaiting == (*bu)["waiting"].b && b->shotEnabled == (*bu)["shotEnabled"].b &&
                          Same(b->distanceX, Bits((*bu)["dx"])) && Same(b->distanceY, Bits((*bu)["dy"])) && lookInvoked == (*bu)["lookInvoked"].b &&
                          recoverInvoked == (*bu)["recoverInvoked"].b;
                if (const Value* sa = bu->Find("shootAttack"); sa && it->second->enemyAnimator && it->second->animatorEnabled) {
                    const unity::Animator& a = *it->second->enemyAnimator;
                    ok = ok && a.GetBool("ShootAttack") == sa->b && a.GetBool("Attacking") == (*bu)["attacking"].b;
                }
                t.Add("butler (previousEnemyHealth, isWaiting, shotEnabled, distanceX/Y, Invokes, ShootAttack/Attacking triggers)", ok,
                      at + " butler " + std::to_string(v["s"].AsInt()) + ": port waiting " + std::to_string(b->isWaiting) + " shotEnabled " +
                          std::to_string(b->shotEnabled) + " look " + std::to_string(lookInvoked) + " recover " + std::to_string(recoverInvoked) + " dx " +
                          std::to_string(b->distanceX) + " dy " + std::to_string(b->distanceY) + "; game " + bu->Dump());
            }
        shots.clear();
        pushes.clear();
        destroys.clear();
        hits.clear();
        lookCount.clear();
        looks.clear();
        shootAttacks.clear();
        nextHit = 0;
        frameLine = nullptr;
        frameEvents = nullptr;
    }

    void Summary() const
    {
        if (zFrames)
            std::printf("the hero's or a butler's z not 0 in %d of %d field-of-vision / back checks (the port's positions are 2D)\n", zNonZero, zFrames);
    }
};

} // namespace
