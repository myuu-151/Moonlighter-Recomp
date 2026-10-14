#pragma once
// The BrokenGolemTurret's part of the translated replay (tests/turret_replay.h's TurretReplay, on
// the game's own BrokenGolemTurretBehaviour, BrokenGolemTurretProjectile and PoolManager, their
// shots made from the BrokenGolemTurretProjectile prefab by the pool's Instantiate):
//   - the scene: the shots' prefab as an asset the turret's projectilePrefab names
//     (bind::JsonSerialValue::assets: the prefab made under an inactive holder, its Animator
//     animators/GolemTurretProjectile.json's), the shots' pool made as the recording has it
//     ("pools": its ids in order, the translated pool's objects given them);
//   - each turret from its recorded state ("turret": patrolLapse, canShoot, enemyDirection, the
//     Animator's Direction* bools and "Shoot");
//   - each fixed step's start ("psteps"): every active shot by id (transform, body position and
//     velocity, awake, isTrigger) and Rigidbody2D.GetContacts' points (bind::PhysicsScene::ContactPoints);
//   - each shot's OnCollisionEnter2D ("hit", in order within its step): which shot, what it hit
//     (tag), from the game's Random state before it (resynced) and the hero's transform then, its
//     draws compared after (the impact sound's); several in one step called in the recording's
//     order once they're the recording's enters (replay input: Box2D's pair order, from proxy ids
//     the whole session sets; bind::PhysicsScene::orderEnters), counted in the summary;
//   - per frame, in order: the looks' raycasts ("look": origin, direction, distance, mask, the
//     collider hit; point, fraction and distance inform: bind::PhysicsScene::onRaycast while the
//     game's LookToPosition runs, the probe TurretProbe), ShootAnimation's runs (the Invoke's:
//     which turret, which step), the shots taken (ShootProjectile's clip event: turret, step,
//     projectile id, where, direction), the pushes on the hero (HeroMerchantController.AddForce:
//     step, force), DestroyNow (its Invoke after a crash, the flight's OnComplete, the turret's
//     DestroyProjectiles: projectile, step);
//   - each frame's end: every active shot ("proj": transform world and local, body, awake,
//     isTrigger, Animator state and normalized time, sprite, the art's rotation), the pools' order
//     ("pooled"), each turret's fields;
//   - after the last frame, "rootMotionProbe" (TurretRootMotionProbe.cs): the Animator's root
//     motion on a moving body (UnityEngine::Animator::ApplyRootMotion) against the game's.
// (Included by enemy_translated_test.cpp after its helpers.)

#include <map>
#include <string>
#include <vector>

namespace {

// BrokenGolemTurretBehaviour with its look and its DestroyProjectiles' callers wrapped (the game's
// methods run as they are).
class TurretProbe : public gen::BrokenGolemTurretBehaviour {
public:
    std::function<void(TurretProbe&, bool after)> onLook;
    std::function<void(TurretProbe&, bool after)> onDestroyProjectiles;
    void LookToPosition() override
    {
        if (onLook) onLook(*this, false);
        gen::BrokenGolemTurretBehaviour::LookToPosition();
        if (onLook) onLook(*this, true);
    }
    void OnPlayerLeaveRoom() override
    {
        if (onDestroyProjectiles) onDestroyProjectiles(*this, false);
        gen::BrokenGolemTurretBehaviour::OnPlayerLeaveRoom();
        if (onDestroyProjectiles) onDestroyProjectiles(*this, true);
    }
    void OnEnemyDestroyed() override
    {
        if (onDestroyProjectiles) onDestroyProjectiles(*this, false);
        gen::BrokenGolemTurretBehaviour::OnEnemyDestroyed();
        if (onDestroyProjectiles) onDestroyProjectiles(*this, true);
    }
};

struct TurretHooks : Hooks {
    Ctx* c = nullptr;
    Tally* tally = nullptr;
    std::unique_ptr<unity::AnimatorController> projectileController;
    GameObject* assetHolder = nullptr;
    std::map<std::string, GameObject*> assets;
    std::map<const GameObject*, int> ids;
    int nextId = 0;
    int step = -1;
    const Value* frameLine = nullptr;
    const Value* frameEvents = nullptr; // "tev"
    size_t nextHit = 0, heroHits = 0;
    std::map<int, size_t> looks;
    TurretProbe* looking = nullptr;
    struct Ray {
        Vector2 origin, direction;
        float distance = 0.0f;
        int mask = 0;
        unity::RaycastHit2D hit;
    };
    std::map<int, std::vector<Ray>> rays;
    struct Shot {
        int s, step, p, dir;
        Vector2 pos;
    };
    std::vector<std::pair<int, int>> shootAnims, destroys;
    std::vector<Shot> shots;
    std::vector<std::pair<int, Vector2>> pushes;
    std::vector<std::string> hits;
    std::map<int, std::vector<gen::BrokenGolemTurretProjectile*>> leaving; // DestroyProjectiles' lists before
    int tcFrames = 0, tcGame = 0, orderedSteps = 0, reorderedSteps = 0;
    float fixedDt = 0.0f;
    size_t shotsBefore = 0;

    UnityEngine::Component* AddScript(GameObject* go, const char* cls) override
    {
        if (std::strcmp(cls, "BrokenGolemTurretBehaviour") != 0) return nullptr;
        return go->AddComponent<TurretProbe*>();
    }

    int IdOf(GameObject* g)
    {
        const auto it = ids.find(g);
        if (it != ids.end()) return it->second;
        ids[g] = nextId;
        return nextId++;
    }
    GameObject* ById(int id) const
    {
        for (const auto& [g, i] : ids)
            if (i == id) return const_cast<GameObject*>(g);
        return nullptr;
    }
    int TurretId(const UnityEngine::Object* o) const { return c->IdOf(o); }
    static gen::BrokenGolemTurretProjectile* ShotOf(GameObject* g) { return g ? g->GetComponent<gen::BrokenGolemTurretProjectile*>() : nullptr; }
    static UnityEngine::Rigidbody2D* BodyOf(GameObject* g) { return g ? g->GetComponent<UnityEngine::Rigidbody2D*>() : nullptr; }
    static bool Active(GameObject* g) { return g && !g->destroyed_ && g->get_activeInHierarchy(); }
    static bool TriggerOf(GameObject* g)
    {
        auto* box = g->GetComponent<UnityEngine::BoxCollider2D*>();
        return box && box->isTrigger_;
    }
    static std::string SpriteOfShot(GameObject* g)
    {
        GameObject* art = bind::FindBelow(g, "ProjectileArt/Projectile");
        auto* r = art ? art->GetComponent<UnityEngine::SpriteRenderer*>() : g->GetComponentInChildren<UnityEngine::SpriteRenderer*>(true);
        return r ? r->SpriteName() : std::string();
    }
    gen::PoolManager_GameObjectPool* Pool(const std::string& name) const
    {
        gen::PoolManager_GameObjectPool* pool = nullptr;
        gen::PoolManager* pm = gen::DungeonManager::_instance ? gen::DungeonManager::_instance->poolManager : nullptr;
        if (pm && pm->poolsCollection) pm->poolsCollection->TryGetValue(S(name), pool);
        return pool;
    }
    // Every shot the pools made, in id order.
    std::vector<GameObject*> All() const
    {
        std::vector<std::pair<int, GameObject*>> v;
        for (const auto& [g, i] : ids) v.emplace_back(i, const_cast<GameObject*>(g));
        std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        std::vector<GameObject*> out;
        for (const auto& p : v) out.push_back(p.second);
        return out;
    }

    // The shots' prefab, an asset outside the scene (under an inactive holder: not awake, no body).
    GameObject* Asset(const std::string& name)
    {
        const auto it = assets.find(name);
        if (it != assets.end()) return it->second;
        const game::Prefab* prefab = game::PrefabLibrary::Find(name, "enemies_golem");
        if (!prefab) return nullptr;
        if (!assetHolder) {
            assetHolder = GameObject::Create("[prefab assets]");
            assetHolder->SetActive(false);
        }
        bind::PrefabOptions options;
        options.controller = name == "BrokenGolemTurretProjectile" ? projectileController.get() : nullptr;
        GameObject* g = bind::InstantiatePrefab(*prefab, assetHolder->transform_, options, &gen::cs_AddComponent);
        g->name_ = cs::String::Intern(S(name));
        assets[name] = g;
        return g;
    }

    void OnScene(const Value& start, Ctx& ctx, Tally& t) override
    {
        c = &ctx;
        tally = &t;
        fixedDt = Bits(start["fixedDeltaTime"]);
        const std::string anim = ReadFile(ctx.data->dir + "/animators/GolemTurretProjectile.json");
        if (!anim.empty()) projectileController = std::make_unique<unity::AnimatorController>(unity::AnimatorController::FromJson(json::Parse(anim)));
        t.Add("projectile prefab and controller (ML_DATA)", game::PrefabLibrary::Find("BrokenGolemTurretProjectile", "enemies_golem") && projectileController, "");
        UnityEngine::JsonSerialValue::assets = [this](const std::string& name) { return Asset(name); };
        standins::S().heroAddForce = [this](Vector2 force) { pushes.emplace_back(step, force); };
        // DealDamageToPlayer is the hero's: his own Random draws in it (the will_damaged sound's,
        // the rumble's) from the game's states around it ("dmgToPlayer"'s rng0 and rng1, where
        // recorded), the state it starts from compared (the shot's draws before it).
        auto dealDamage = standins::S().dealDamageToPlayer;
        standins::S().dealDamageToPlayer = [this, dealDamage](float strength, cs::String* source, int type, gen::Enemy* e) {
            if (dealDamage) dealDamage(strength, source, type, e);
            const Value* events = frameLine ? frameLine->Find("events") : nullptr;
            if (!events) return;
            size_t seen = 0;
            for (const Value& ev : events->arr) {
                if (ev["k"].s != "dmgToPlayer" || ev["step"].AsInt() != step || seen++ != heroHits) continue;
                if (ev.Find("rng0")) {
                    tally->Add("Random state as DealDamageToPlayer starts (the hero's draws in it are his)",
                               SameState(unity::Random::GetState(), State(ev["rng0"])), "frame " + std::to_string(frameLine->Find("f")->AsInt()));
                    unity::Random::SetState(State(ev["rng1"]));
                }
                break;
            }
            heroHits++;
        };
        // Several OnCollisionEnter2D starting in one fixed step: called in the recording's order
        // (turret_replay.h's orderEnters: replay input, the step's enters must be the recording's).
        ctx.physics->orderEnters = [this](std::vector<bind::PhysicsScene::Enter>& enters) {
            if (!frameEvents) return;
            std::vector<size_t> mine; // the shots' enters among them (in place)
            for (size_t i = 0; i < enters.size(); i++)
                if (enters[i].self && ShotOf(enters[i].self->gameObject_)) mine.push_back(i);
            if (mine.size() < 2) return;
            std::vector<const Value*> recorded;
            size_t seen = 0;
            for (const Value& e : frameEvents->arr) {
                if (e["k"].s != "hit") continue;
                if (seen++ >= nextHit && e["step"].AsInt() == step) recorded.push_back(&e);
            }
            if (recorded.size() != mine.size()) return;
            std::vector<bind::PhysicsScene::Enter> ordered;
            std::vector<bool> used(mine.size(), false);
            for (const Value* g : recorded) {
                const int p = static_cast<int>((*g)["p"].AsInt());
                const std::string& tag = (*g)["tag"].s;
                const std::string name = (*g)["other"].IsString() ? (*g)["other"].s : "";
                size_t pick = mine.size();
                for (size_t i = 0; i < mine.size(); i++) {
                    const bind::PhysicsScene::Enter& e = enters[mine[i]];
                    if (used[i] || IdOf(e.self->gameObject_) != p || !e.other || Utf8(e.other->gameObject_->get_tag()) != tag) continue;
                    if (pick == mine.size()) pick = i;
                    if (Utf8(e.other->gameObject_->get_name()) == name) {
                        pick = i;
                        break;
                    }
                }
                if (pick == mine.size()) return; // (not the recording's enters: left as they are)
                used[pick] = true;
                ordered.push_back(enters[mine[pick]]);
            }
            bool same = true;
            for (size_t i = 0; i < mine.size() && same; i++) same = ordered[i].selfFixture == enters[mine[i]].selfFixture && ordered[i].otherFixture == enters[mine[i]].otherFixture;
            orderedSteps++;
            if (!same) reorderedSteps++;
            for (size_t i = 0; i < mine.size(); i++) enters[mine[i]] = ordered[i];
        };
        // Each shot's OnCollisionEnter2D against the game's next "hit" of its step: the hero's
        // transform and the Random state the game had then; its draws compared after.
        ctx.physics->onCollisionEnter = [this](UnityEngine::Collider2D* self, UnityEngine::Collision2D* col, bool after) {
            GameObject* g = self->gameObject_;
            if (!ShotOf(g)) return;
            const std::string tag = Utf8(col->get_gameObject() ? col->get_gameObject()->get_tag() : nullptr);
            const Value* game = nullptr;
            if (frameEvents) {
                size_t seen = 0;
                for (const Value& e : frameEvents->arr)
                    if (e["k"].s == "hit" && seen++ == nextHit) game = &e;
            }
            if (!after) {
                hits.push_back(std::to_string(IdOf(g)) + "/" + std::to_string(step) + "/" + tag);
                if (game && game->Find("rng0")) {
                    if (game->Find("hero")) c->setHeroPosition(V2((*game)["hero"]));
                    unity::Random::SetState(State((*game)["rng0"]));
                }
                return;
            }
            if (game && game->Find("rng1")) {
                tally->Add("Random draws in a shot's OnCollisionEnter2D (the impact sound)", SameState(unity::Random::GetState(), State((*game)["rng1"])),
                           "projectile " + std::to_string(IdOf(g)) + " on " + tag + ": " +
                               std::to_string(unity::Random::DrawsBetween(State((*game)["rng0"]), unity::Random::GetState(), 64)) + " draws, the game " +
                               std::to_string(unity::Random::DrawsBetween(State((*game)["rng0"]), State((*game)["rng1"]), 64)));
                unity::Random::SetState(State((*game)["rng1"]));
            }
            nextHit++;
        };
        // ShootAnimation (the turret's Invoke) and DestroyNow (a shot's, after its crash), and the
        // flight's OnComplete (DestroyNow): the logs.
        ctx.scene->onInvoke = [this](UnityEngine::MonoBehaviour* target, cs::String* method, bool after) {
            if (after) return;
            const std::string m = Utf8(method);
            if (m == "ShootAnimation") shootAnims.emplace_back(TurretId(target), step);
            else if (m == "DestroyNow" && dynamic_cast<gen::BrokenGolemTurretProjectile*>(target)) destroys.emplace_back(IdOf(target->gameObject_), step);
        };
        ctx.scene->onTweenComplete = [this](cs::Object* target, bool after) {
            if (after) return;
            if (auto* shot = dynamic_cast<gen::BrokenGolemTurretProjectile*>(target)) destroys.emplace_back(IdOf(shot->gameObject_), step);
        };
        // The looks' raycasts.
        ctx.physics->onRaycast = [this](Vector2 origin, Vector2 direction, float distance, int32_t mask, const unity::RaycastHit2D& hit) {
            if (!looking) return;
            rays[TurretId(looking)].push_back(Ray{origin, direction, distance, mask, hit});
        };
    }

    void OnEnemy(Replayed& e, const Value& line, Tally& t) override
    {
        auto* turret = dynamic_cast<TurretProbe*>(e.behaviour);
        t.Add("the prefab makes a BrokenGolemTurretBehaviour", turret != nullptr, "enemy " + std::to_string(e.id));
        if (!turret) return;
        if (const Value* tu = line.Find("turret")) {
            turret->patrolLapse = Bits((*tu)["lapse"]);
            turret->canShoot = (*tu)["canShoot"].b;
            turret->enemyDirection = static_cast<gen::HeroMerchantController_Direction>((*tu)["dir"].AsInt());
            if (const Value* b = tu->Find("bools")) {
                e.animator->SetBool(S("DirectionUp"), (*b)[0].b);
                e.animator->SetBool(S("DirectionDown"), (*b)[1].b);
                e.animator->SetBool(S("DirectionLeft"), (*b)[2].b);
                e.animator->SetBool(S("DirectionRight"), (*b)[3].b);
            }
            if (const Value* sh = tu->Find("shoot"); sh && sh->b) e.animator->SetTrigger(S("Shoot"));
        }
        // Each look from where the game had the hero then (his own Update can come between the
        // turret's and the animation pass's QuitAttackState); its raycasts collected.
        turret->onLook = [this](TurretProbe& b, bool after) {
            if (after) {
                looking = nullptr;
                return;
            }
            looking = &b;
            const int id = TurretId(&b);
            const size_t k = looks[id]++;
            if (!frameEvents) return;
            size_t seen = 0;
            for (const Value& ev : frameEvents->arr)
                if (ev["k"].s == "look" && ev["s"].AsInt() == id && seen++ == k) c->setHeroPosition(V2(ev["hero"]));
        };
        // DestroyProjectiles (OnPlayerLeaveRoom, OnEnemyDestroyed): the shots each DestroyNow took
        // out of its list, in its order.
        turret->onDestroyProjectiles = [this](TurretProbe& b, bool after) {
            const int id = TurretId(&b);
            std::vector<gen::BrokenGolemTurretProjectile*> now;
            for (int32_t i = 0; i < b._projectilesShot->get_Count(); i++) now.push_back(cs::Down<gen::BrokenGolemTurretProjectile*>(b._projectilesShot->get_Item(i)));
            if (!after) {
                leaving[id] = now;
                return;
            }
            for (gen::BrokenGolemTurretProjectile* p : leaving[id])
                if (std::find(now.begin(), now.end(), p) == now.end()) destroys.emplace_back(IdOf(p->gameObject_), step);
        };
    }

    // ShootProjectile (the turret's clip event): the shot it took (the list's new last), if any.
    void OnAnimEvent(Replayed& e, const unity::AnimationEvent& ev, bool after) override
    {
        auto* turret = dynamic_cast<gen::BrokenGolemTurretBehaviour*>(e.behaviour);
        if (!turret || ev.function != "ShootProjectile") return;
        const size_t n = static_cast<size_t>(turret->_projectilesShot->get_Count());
        if (!after) {
            shotsBefore = n;
            return;
        }
        if (n <= shotsBefore) return;
        auto* p = cs::Down<gen::BrokenGolemTurretProjectile*>(turret->_projectilesShot->get_Item(static_cast<int32_t>(n - 1)));
        shots.push_back({e.id, step, IdOf(p->gameObject_), static_cast<int>(p->projectileDirection), XY(p->gameObject_->transform_->get_position())});
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
            gen::PoolManager_GameObjectPool* port = Pool(pool["name"].s);
            t.Add("the shots' pool (Init's CreateOrFillGameObjectPool)", port != nullptr, pool["name"].s);
            if (!port) continue;
            const int32_t want = static_cast<int32_t>(pool["ids"].size());
            if (port->pooledObjects->get_Count() < want) port->FillPool(want - port->pooledObjects->get_Count());
            if (port->pooledObjects->get_Count() > want) port->pooledObjects->RemoveRange(want, port->pooledObjects->get_Count() - want);
            for (int32_t k = 0; k < want; k++) {
                const int id = static_cast<int>(pool["ids"][static_cast<size_t>(k)].AsInt());
                ids[port->pooledObjects->get_Item(k)] = id;
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
        const Value& rowsK = (*psteps)[k];
        std::vector<int> listed;
        for (const Value& r : rowsK.arr) {
            const int id = static_cast<int>(r[0].AsInt());
            listed.push_back(id);
            GameObject* g = ById(id);
            UnityEngine::Rigidbody2D* rb = BodyOf(g);
            const std::string at = "frame " + std::to_string(fn) + " step " + std::to_string(k) + " projectile " + std::to_string(id);
            if (!Active(g) || !rb || !rb->body_) {
                t.Add("shot at a fixed step's start (transform, body, awake, trigger)", false, at + ": not active in the port");
                continue;
            }
            unity::Rigidbody2D* body = rb->body_;
            const Vector2 pos = XY(g->transform_->get_position());
            const bool ok = Same(pos, {Bits(r[1]), Bits(r[2])}) && Same(body->position(), {Bits(r[3]), Bits(r[4])}) && Same(body->velocity(), {Bits(r[5]), Bits(r[6])}) &&
                            body->IsAwake() == r[7].b && TriggerOf(g) == r[8].b;
            t.Add("shot at a fixed step's start (transform, body, awake, trigger)", ok,
                  at + ": port " + Str(pos) + " body " + Str(body->position()) + " v " + Str(body->velocity()) + (body->IsAwake() ? " awake" : " asleep") +
                      (TriggerOf(g) ? " trigger" : "") + "; game " + Str({Bits(r[1]), Bits(r[2])}) + " body " + Str({Bits(r[3]), Bits(r[4])}) + " v " +
                      Str({Bits(r[5]), Bits(r[6])}) + (r[7].b ? " awake" : " asleep") + (r[8].b ? " trigger" : ""));
            const int points = c->physics->ContactPoints(rb);
            t.Add("shot's contact points at a fixed step's start (Rigidbody2D.GetContacts: the last step's touching manifolds)",
                  points == static_cast<int>(r[9].size()), at + ": port " + std::to_string(points) + " game " + r[9].Dump());
        }
        for (GameObject* g : All())
            if (Active(g) && std::find(listed.begin(), listed.end(), IdOf(g)) == listed.end())
                t.Add("shot at a fixed step's start (transform, body, awake, trigger)", false,
                      "frame " + std::to_string(fn) + " step " + std::to_string(k) + " projectile " + std::to_string(IdOf(g)) + ": active in the port only");
    }

    void AfterStep(long long, size_t, const Value&, Tally&) override
    {
        step = -1;
        heroHits = 0;
    }

    void AroundRoomExit(bool after, int roomStep) override { step = after ? -1 : roomStep; }

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
            for (const Replayed& s : *c->enemies) {
                const std::vector<const Value*>& game = gameRays[s.id];
                const std::vector<Ray>& mine = rays[s.id];
                if (game.empty() && mine.empty()) continue;
                bool same = game.size() == mine.size(), where = same;
                std::string list = at + " turret " + std::to_string(s.id) + ": port";
                for (const Ray& r : mine) list += " " + Str(r.direction) + (r.hit.collider ? "->" + r.hit.collider->path : "->none");
                list += "; game";
                for (const Value* r : game) list += " " + Str(V2((*r)["d"])) + "->" + ((*r)["hit"].IsString() ? (*r)["hit"].s : "none");
                for (size_t k = 0; same && k < game.size(); k++) {
                    const Value& g = *game[k];
                    const std::string hit = g["hit"].IsString() ? g["hit"].s : "";
                    same = Same(mine[k].origin, V2(g["o"])) && Same(mine[k].direction, V2(g["d"])) && Same(mine[k].distance, Bits(g["dist"])) &&
                           mine[k].mask == g["mask"].AsInt() && (mine[k].hit.collider ? mine[k].hit.collider->path : "") == hit;
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
                    same = shots[k].s == gameShots[k].s && shots[k].step == gameShots[k].step && shots[k].p == gameShots[k].p && shots[k].dir == gameShots[k].dir &&
                           Same(shots[k].pos, gameShots[k].pos);
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
                GameObject* g = ById(id);
                UnityEngine::Rigidbody2D* rb = BodyOf(g);
                const std::string where = at + " projectile " + std::to_string(id);
                if (!Active(g) || !rb || !rb->body_) {
                    t.Add("shot at the frame's end (transform, body, awake, trigger)", false, where + ": not active in the port");
                    continue;
                }
                unity::Rigidbody2D* body = rb->body_;
                const Value& rbv = v["rb"];
                const Vector2 pos = XY(g->transform_->get_position()), local = XY(g->transform_->localPosition_);
                t.Add("shot at the frame's end (transform, body, awake, trigger)",
                      Same(pos, V2(v["pos"])) && Same(local, V2(v["local"])) && Same(body->position(), {Bits(rbv[0]), Bits(rbv[1])}) &&
                          Same(body->velocity(), {Bits(rbv[2]), Bits(rbv[3])}) && body->IsAwake() == v["awake"].b && TriggerOf(g) == v["trigger"].b,
                      where + ": port " + Str(pos) + " local " + Str(local) + " body " + Str(body->position()) + " v " + Str(body->velocity()) + "; game " +
                          Str(V2(v["pos"])) + " local " + Str(V2(v["local"])) + " body " + Str({Bits(rbv[0]), Bits(rbv[1])}) + " v " +
                          Str({Bits(rbv[2]), Bits(rbv[3])}));
                auto* animator = g->GetComponent<UnityEngine::Animator*>();
                if (const Value* an = v.Find("anim"); an && animator && animator->Mecanim()) {
                    const unity::AnimatorStateInfo info = animator->Mecanim()->GetCurrentAnimatorStateInfo();
                    t.Add("shot's Animator (state, normalized time)",
                          info.shortNameHash == static_cast<int32_t>((*an)["state"].AsInt()) && Same(info.normalizedTime, Bits((*an)["t"])),
                          where + ": port t " + std::to_string(info.normalizedTime) + " game " + std::to_string(Bits((*an)["t"])));
                }
                const std::string sprite = SpriteOfShot(g);
                t.Add("shot's sprite", v["sprite"].IsString() && sprite == v["sprite"].s, where + ": port " + sprite + " game " + v["sprite"].Dump());
                if (const Value* art = v.Find("art")) {
                    gen::BrokenGolemTurretProjectile* shot = ShotOf(g);
                    const float mine = shot && shot->projectileArt ? shot->projectileArt->transform_->get_eulerAngles().z : -1.0f;
                    t.Add("shot's art rotation", std::fabs(Bits(*art) - mine) < 0.01f, where + ": port " + std::to_string(mine) + " game " + std::to_string(Bits(*art)));
                }
            }
            for (GameObject* g : All())
                if (Active(g) && std::find(listed.begin(), listed.end(), IdOf(g)) == listed.end())
                    t.Add("shot at the frame's end (transform, body, awake, trigger)", false, at + " projectile " + std::to_string(IdOf(g)) + ": active in the port only");
        }
        if (const Value* pooled = f.Find("pooled"))
            for (const Value& pool : pooled->arr) {
                gen::PoolManager_GameObjectPool* port = Pool(pool["name"].s);
                std::vector<int> mine, game;
                if (port)
                    for (int32_t i = 0; i < port->pooledObjects->get_Count(); i++) mine.push_back(IdOf(port->pooledObjects->get_Item(i)));
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
                Replayed* s = c->ById(static_cast<int>(v["s"].AsInt()));
                const Value* tu = v.Find("turret");
                if (!s || !tu || s->Gone()) continue;
                auto* turret = dynamic_cast<gen::BrokenGolemTurretBehaviour*>(s->behaviour);
                std::vector<int> mine, game;
                for (int32_t i = 0; i < turret->_projectilesShot->get_Count(); i++)
                    mine.push_back(IdOf(cs::Down<gen::BrokenGolemTurretProjectile*>(turret->_projectilesShot->get_Item(i))->gameObject_));
                for (const Value& id : (*tu)["shot"].arr) game.push_back(static_cast<int>(id.AsInt()));
                bool ok = Same(turret->patrolLapse, Bits((*tu)["lapse"])) && turret->canShoot == (*tu)["canShoot"].b &&
                          static_cast<int>(turret->enemyDirection) == (*tu)["dir"].AsInt() && mine == game;
                if (const Value* b = tu->Find("bools"); b && s->animator->enabled_) {
                    UnityEngine::Animator* a = s->animator;
                    ok = ok && a->GetBool(S("DirectionUp")) == (*b)[0].b && a->GetBool(S("DirectionDown")) == (*b)[1].b && a->GetBool(S("DirectionLeft")) == (*b)[2].b &&
                         a->GetBool(S("DirectionRight")) == (*b)[3].b;
                }
                t.Add("turret (patrolLapse, canShoot, enemyDirection, shots in flight, Direction bools)", ok,
                      at + " turret " + std::to_string(v["s"].AsInt()) + ": port lapse " + std::to_string(turret->patrolLapse) + " canShoot " +
                          std::to_string(turret->canShoot) + " dir " + std::to_string(static_cast<int>(turret->enemyDirection)) + "; game " +
                          std::to_string(Bits((*tu)["lapse"])) + " " + std::to_string((*tu)["canShoot"].b) + " " + std::to_string((*tu)["dir"].AsInt()));
                tcFrames++;
                tcGame += (*tu)["tc"].b;
            }
        // The log starts over each frame.
        rays.clear();
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
    // recording: TurretRootMotionProbe.cs): the probe's velocity set on the turret's body, then its
    // Animator's root motion (UnityEngine::Animator::ApplyRootMotion), against the game's body at
    // the frame's end (velocity bits, awake); and the next fixed step against the velocity it left
    // (turret_replay.h's AfterFrames).
    void AfterFrames(const std::vector<Value>& lines, Tally& t) override
    {
        bool probed = false;
        for (const Value& l : lines) {
            if (l["ev"].s != "rootMotionProbe") continue;
            probed = true;
            Replayed* e = c->ById(static_cast<int>(l["s"].AsInt()));
            const unity::AnimatorController* ctl = c->data->controller.get();
            t.Add("rootMotionProbe: applyRootMotion as the port's controller has it (animators.py)", ctl && (ctl->applyRootMotion == 1) == l["applyRootMotion"].b,
                  "turret " + std::to_string(l["s"].AsInt()) + ": port " + std::to_string(ctl ? ctl->applyRootMotion : -2) + ", game " +
                      std::to_string(l["applyRootMotion"].b));
            const Value &before = l["before"], &set = l["set"], &afterSet = l["afterSet"], &end = l["frameEnd"], &st = l["afterStep"];
            std::string port = "no turret " + std::to_string(l["s"].AsInt()) + " in the port";
            bool ok = false;
            if (e && !e->Gone() && e->rb->body_) {
                unity::Rigidbody2D* body = e->rb->body_;
                const Vector2 saved = body->velocity();
                body->SetVelocity(V2(set));
                const bool setTook = Same(body->velocity(), V2(afterSet["vel"]));
                e->animator->ApplyRootMotion();
                const Vector2 v = body->velocity();
                const bool awake = body->IsAwake();
                body->SetVelocity(saved);
                // (b2Island::Solve: the velocity damped by the body's drag, the position moved by it.)
                const Vector2 b = V2(before["pos"]);
                const float damp = 1.0f / (1.0f + fixedDt * body->Body()->GetLinearDamping());
                const Vector2 vd = {v.x * damp, v.y * damp};
                const Vector2 moved = {b.x + fixedDt * vd.x, b.y + fixedDt * vd.y};
                ok = setTook && Same(v, V2(end["vel"])) && awake == end["awake"].b && Same(V2(end["pos"]), b) && Same(V2(st["pos"]), moved) &&
                     Same(V2(st["vel"]), vd) && st["awake"].b == awake;
                port = "port: after the pass v " + Str(v) + (awake ? " awake" : " asleep") + ", next step pos " + Str(moved);
            }
            t.Add("rootMotionProbe: the Animator pass on a moving body (the port's root motion: the velocity it leaves, awake; the next step)", ok,
                  port + "; game: set " + Str(V2(set)) + ", frame end v " + Str(V2(end["vel"])) + (end["awake"].b ? " awake" : " asleep") + ", next step pos " +
                      Str(V2(before["pos"])) + " -> " + Str(V2(st["pos"])) + " v " + Str(V2(st["vel"])) + (st["awake"].b ? " awake" : " asleep") +
                      " (updateMode " + std::to_string(l["updateMode"].AsInt()) + ")");
        }
        if (!probed)
            std::printf("note: no \"rootMotionProbe\" line (TurretRootMotionProbe.cs: no turret active at the recording's end): the root "
                        "motion's model unchecked\n");
    }

    void Summary() const override
    {
        if (orderedSteps)
            std::printf("shots' OnCollisionEnter2D: %d steps with several, called in the recording's order (replay input: Unity's is Box2D's "
                        "broad-phase pair order, from proxy ids the whole session sets); %d of them in another order in this world\n",
                        orderedSteps, reorderedSteps);
        if (tcFrames)
            std::printf("turret transform changed after its Update (the Animator's root motion) in %d of %d turret-frames (the port: never)\n", tcGame,
                        tcFrames);
    }
};

} // namespace
