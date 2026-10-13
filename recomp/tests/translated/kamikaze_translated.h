#pragma once
// The KamikazeGolem's part of the translated replay (tests/kamikaze_replay.h's KamikazeReplay, on
// the game's own GolemKamikazeBehaviour), for enemy_KamikazeGolem.jsonl (KamikazeOracle.cs):
//   - each kamikaze from its recorded state ("kami": the Animator's Awake / Attacking / Jump
//     triggers, MakeJump's end and time, its attack body's position and rest in the first frame)
//     and its InvokeRepeating("MoveEnemy", 0.65, 0.65) made when its room was entered ("enter":
//     from its Time.fixedTime, on the replay's fixed clock), on from the run after the "moves" it
//     has made already;
//   - each fixed step's start ("ksteps"): the AttackCollider's own body (position, awake);
//   - each MoveEnemy (the game's, wrapped by the probe KamikazeProbe; "kev" "move", in order per
//     kamikaze): when it runs (the frame and the fixed step, -1: the frame's pass), from the game's
//     Random state before it (resynced), its draws compared after, where it wandered (a valid point
//     is the agent's goal after; an invalid one isn't) and the agent's goal after;
//   - each explosion ("explode"): the enemies Physics2D.OverlapCircleAll found (the colliders with
//     an EnemyBehaviour, by id: bind::PhysicsScene::onOverlapCircle in CreateExplosionToDamageEnemies)
//     against the game's;
//   - each frame's end ("kami" in "enemies"): MakeJump's end and time, the triggers, the attack
//     body (position, awake, the box on), its HeroMerchant.EnteredInCombat subscription;
//   - the room's and the hero's parts the kamikaze's code reaches (the start's "kroom" and
//     "heroEffects"): the room's OnAllEnemiesDefeated handlers as InitializeRoom subscribed them
//     (the game's DungeonRoom.EnemyDefeated runs them; DungeonRoom.UnLockRoomChests stood in for:
//     the chests' unlock sounds' draws), the hero's Wanderer Amulet on Enemy.OnEnemyRecivesDamage
//     (the game's WandererAmuletEffect handler, translated).
// (Included by enemy_translated_test.cpp after its helpers.)

#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace {

// GolemKamikazeBehaviour with its MoveEnemy wrapped (the game's, run as it is).
class KamikazeProbe : public gen::GolemKamikazeBehaviour {
public:
    std::function<void(KamikazeProbe&, bool after)> onMove;
    void MoveEnemy() override
    {
        if (onMove) onMove(*this, false);
        gen::GolemKamikazeBehaviour::MoveEnemy();
        if (onMove) onMove(*this, true);
    }
};

struct KamikazeHooks : Hooks {
    Ctx* c = nullptr;
    Tally* tally = nullptr;
    long long frame = -1;
    int step = -1;
    const Value* frameLine = nullptr;
    std::map<int, size_t> movesSeen;
    std::map<int, int> explosions, explosionsBefore;
    std::map<int, std::vector<int>> lastHits; // per kamikaze: its last explosion's enemies (ids), in order
    int exploding = -1;
    struct Enter {
        int id;
        float base;
        bool fixed;
        int moves;
        float lastMove;
    };
    std::vector<Enter> enters;
    float startTime = 0.0f, fixedDt = 0.02f;
    struct StartBody {
        Vector2 position;
        bool awake;
    };
    std::map<int, StartBody> startBodies;
    bool bodiesPlaced = false;
    gen::WandererAmuletEffect* wanderer = nullptr;

    UnityEngine::Component* AddScript(GameObject* go, const char* cls) override
    {
        if (std::strcmp(cls, "GolemKamikazeBehaviour") != 0) return nullptr;
        return go->AddComponent<KamikazeProbe*>();
    }

    static KamikazeProbe* Kami(const Replayed& e) { return dynamic_cast<KamikazeProbe*>(e.behaviour); }

    std::vector<const Value*> GameMoves(int id) const
    {
        std::vector<const Value*> out;
        if (!frameLine) return out;
        if (const Value* kev = frameLine->Find("kev"))
            for (const Value& e : kev->arr)
                if (e["k"].s == "move" && e["s"].AsInt() == id) out.push_back(&e);
        return out;
    }

    void OnScene(const Value& start, Ctx& ctx, Tally& t) override
    {
        c = &ctx;
        tally = &t;
        if (const Value* tm = start.Find("time")) startTime = Bits(*tm);
        fixedDt = Bits(start["fixedDeltaTime"]);
        if (const Value* kr = start.Find("kroom")) {
            standins::S().roomChestTypes.clear();
            for (const Value& ch : (*kr)["chests"].arr) standins::S().roomChestTypes.push_back(static_cast<int>(ch[0].AsInt()));
            if (const Value* locked = kr->Find("locked")) ctx.room->_isLocked = locked->b;
            else std::printf("note: no \"locked\" in \"kroom\" (recorded before it): the room taken as not locked\n");
            // DungeonRoom.InitializeRoom's subscriptions, in the recording's order.
            for (const Value& hv : (*kr)["allDefeated"].arr) {
                if (hv.s == "DungeonRoom.UnLockRoomChests")
                    ctx.room->OnAllEnemiesDefeated = cs::Delegate<void()>::Combine(
                        ctx.room->OnAllEnemiesDefeated, cs::Delegate<void()>::Bind<gen::DungeonRoom, &gen::DungeonRoom::UnLockRoomChests>(ctx.room));
                else if (hv.s == "DungeonRoom.OpenAllDoors" || hv.s == "DungeonRoom.ResetHeroMerchantIsInCombat") {
                    // (No draws: the doors' Animators, the hero's IsInCombat. recq kamifix-3's probe.)
                } else {
                    t.Add("the room's OnAllEnemiesDefeated handlers (ported: UnLockRoomChests, OpenAllDoors, ResetHeroMerchantIsInCombat)", false, hv.s);
                }
            }
        } else {
            std::printf("note: no \"kroom\" (recorded before KamikazeOracle.WriteStart): the room's OnAllEnemiesDefeated isn't run\n");
        }
        if (const Value* he = start.Find("heroEffects")) {
            bool wandererOn = false;
            for (const Value& hv : (*he)["damage"].arr) {
                if (hv.s != "WandererAmuletEffect.Enemy_OnEnemyRecivesDamage") {
                    t.Add("the hero's Enemy.OnEnemyRecivesDamage handlers (ported: the Wanderer Amulet's)", false, hv.s);
                    continue;
                }
                for (const Value& eff : (*he)["effects"].arr)
                    if (eff.size() >= 3 && eff[0].s == "WandererAmuletEffect") {
                        if (!wanderer) wanderer = cs::New<gen::WandererAmuletEffect>();
                        wanderer->probToCallWanderer = Bits(eff[2]);
                        wandererOn = true;
                    }
            }
            gen::DungeonManager::_instance->isWandererActive = (*he)["wandererActive"].b;
            standins::S().spawnWanderer = [this] {
                if (tally) tally->Add("the Wanderer Amulet called the Wanderer (DungeonManager.SpawnWanderer: not ported)", false, "frame " + std::to_string(frame));
            };
            // (ApplyToHeroMerchant's subscription: the static event holds it.)
            if (wandererOn)
                gen::Enemy::add_OnEnemyRecivesDamage(
                    cs::Delegate<void(gen::Enemy*, float, gen::Enemy_EnemyDamageReceivedType)>::Bind<gen::WandererAmuletEffect, &gen::WandererAmuletEffect::Enemy_OnEnemyRecivesDamage>(
                        wanderer));
        } else {
            std::printf("note: no \"heroEffects\" (recorded before KamikazeOracle.WriteStart): the hero's amulets' handlers aren't run\n");
        }
        // The explosions' OverlapCircleAll: the colliders with an EnemyBehaviour (the enemies' roots).
        ctx.physics->onOverlapCircle = [this](Vector2, float, const std::vector<UnityEngine::Collider2D*>& found) {
            if (exploding < 0) return;
            std::vector<int> ids;
            for (UnityEngine::Collider2D* col : found)
                if (col && col->GetComponent<gen::EnemyBehaviour*>()) ids.push_back(c->IdOf(col->gameObject_));
            lastHits[exploding] = ids;
            explosions[exploding]++;
        };
    }

    void OnEnemy(Replayed& e, const Value& line, Tally& t) override
    {
        KamikazeProbe* k = Kami(e);
        t.Add("the prefab makes a GolemKamikazeBehaviour", k != nullptr, "enemy " + std::to_string(e.id));
        if (!k) return;
        t.Add("its AttackCollider has a Kinematic Rigidbody2D of its own (prefab)",
              e.attackRb && e.attackRb->def_.bodyType == unity::RigidbodyType2D::Kinematic, "enemy " + std::to_string(e.id));
        const Value* ka = line.Find("kami");
        if (!ka) {
            std::printf("note: enemy %d has no \"kami\" (KamikazeOracle.cs): MoveEnemy isn't scheduled\n", e.id);
            return;
        }
        if (const Value* trig = ka->Find("trig")) {
            const char* const names[3] = {"Awake", "Attacking", "Jump"};
            for (size_t i = 0; i < 3 && i < trig->size(); i++)
                if ((*trig)[i].b) e.animator->SetTrigger(S(names[i]));
                else e.animator->ResetTrigger(S(names[i]));
        }
        if (const Value* j = ka->Find("jump")) {
            k->jumpEndPosition = UnityEngine::Vector3{Bits((*j)[0]), Bits((*j)[1]), 0.0f};
            k->jumpTime = Bits((*j)[2]);
        }
        if (const Value* ab = ka->Find("ab"); ab && ab->IsArray() && ab->size() >= 3) startBodies[e.id] = {{Bits((*ab)[0]), Bits((*ab)[1])}, (*ab)[2].b};
        const Value* en = ka->Find("enter");
        if (en && en->IsArray()) {
            const bool fixed = en->size() > 2 && (*en)[2].b;
            const Value* lm = ka->Find("lastMove");
            enters.push_back({e.id, Bits((*en)[en->size() > 1 ? 1 : 0]), fixed, ka->Find("moves") ? static_cast<int>((*ka)["moves"].AsInt()) : 0,
                              lm && lm->IsArray() ? Bits((*lm)[0]) : -1.0f});
        } else {
            std::printf("note: enemy %d never entered its room (no EnemyRoom_OnRoomEnter): no MoveEnemy\n", e.id);
        }
        // Each MoveEnemy against the game's: when, from its Random state, its draws and its wander.
        const int id = e.id;
        k->onMove = [this, id](KamikazeProbe& self, bool after) {
            const std::vector<const Value*> moves = GameMoves(id);
            size_t& seen = movesSeen[id];
            const Value* game = seen < moves.size() ? moves[seen] : nullptr;
            const std::string at = "frame " + std::to_string(frame) + " step " + std::to_string(step) + " enemy " + std::to_string(id);
            if (!after) {
                tally->Add("MoveEnemy (InvokeRepeating): when it runs (frame, fixed step)", game && game->Find("step") && (*game)["step"].AsInt() == step,
                           at + ": the game's " + (game ? "in step " + std::to_string((*game)["step"].AsInt()) : std::string("none")));
                if (game && game->Find("rng0")) unity::Random::SetState(State((*game)["rng0"]));
                return;
            }
            if (!game) return;
            seen++;
            (void)self;
            const Vector2 goal = c->ById(id)->agent->_primeGoal;
            bool wander = true;
            // (A valid wander point is SetDestination's: the agent's goal; an invalid one isn't.)
            if ((*game)["wander"].b) wander = (*game)["valid"].b ? Same(goal, V2((*game)["dest"])) : !Same(goal, V2((*game)["dest"]));
            tally->Add("MoveEnemy from the game's Random state (draws, wander point, valid, agent goal)",
                       SameState(unity::Random::GetState(), State((*game)["rng1"])) && wander && Same(goal, V2((*game)["goal"])),
                       at + ": port goal " + Str(goal) + " game " + ((*game)["wander"].b ? Str(V2((*game)["dest"])) : std::string("(no wander)")) + " goal " +
                           Str(V2((*game)["goal"])));
            unity::Random::SetState(State((*game)["rng1"]));
        };
    }

    // The explosion's OverlapCircleAll runs in its clip event.
    void OnAnimEvent(Replayed& e, const unity::AnimationEvent& ev, bool after) override
    {
        if (ev.function != "CreateExplosionToDamageEnemies") return;
        exploding = after ? -1 : e.id;
    }

    void OnEnemiesBuilt(const Value&, Tally&) override
    {
        // InvokeRepeating("MoveEnemy", 0.65, 0.65) as EnemyRoom_OnRoomEnter made it, from its
        // Time.fixedTime as the engine's double (kamikaze_replay.h's).
        unity::DelayedCalls& calls = c->scene->calls;
        const double now = calls.now, fixedNow = calls.fixedNow;
        const bool inFixed = calls.inFixedStep;
        for (const Enter& en : enters) {
            Replayed* e = c->ById(en.id);
            double at = EngineFixedTime(en.base, fixedDt);
            for (int n = 0; n < en.moves; n++) at += static_cast<double>(0.65f);
            if (en.moves > 0 && std::fabs(static_cast<double>(en.lastMove) - at) > 0.02)
                std::printf("note: enemy %d's last MoveEnemy before the recording ran at %.6f, due at %.6f (InvokeRepeating's period?)\n", en.id,
                            static_cast<double>(en.lastMove), at);
            if (at + static_cast<double>(0.65f) <= static_cast<double>(startTime))
                std::printf("note: enemy %d's next MoveEnemy was due before the recording (%.6f): not replayable\n", en.id, at + 0.65);
            calls.inFixedStep = en.fixed;
            calls.now = at;
            calls.fixedNow = at;
            e->behaviour->InvokeRepeating(S("MoveEnemy"), 0.65f, 0.65f);
        }
        calls.now = now;
        calls.fixedNow = fixedNow;
        calls.inFixedStep = inFixed;
        explosionsBefore = explosions;
    }

    void BeginFrame(long long fn, const Value& f) override
    {
        if (!bodiesPlaced) {
            bodiesPlaced = true;
            for (const auto& [id, sb] : startBodies) {
                Replayed* e = c->ById(id);
                if (!e || !e->attackRb || !e->attackRb->body_) continue;
                b2Body* b = e->attackRb->body_->Body();
                b->SetTransform(b2Vec2(sb.position.x, sb.position.y), b->GetAngle());
                b->SetAwake(sb.awake);
            }
        }
        frame = fn;
        frameLine = &f;
        step = -1;
        movesSeen.clear();
    }

    void BeforeStep(long long fn, size_t k, const Value& f, Tally& t) override
    {
        step = static_cast<int>(k);
        const Value* ks = f.Find("ksteps");
        if (!ks || k >= ks->size()) return;
        for (const Value& row : (*ks)[k].arr) {
            Replayed* e = c->ById(static_cast<int>(row[0].AsInt()));
            if (!e || e->Gone() || !e->attackRb || !e->attackRb->body_) continue;
            const unity::Rigidbody2D& ab = *e->attackRb->body_;
            t.Add("AttackCollider's own body at a fixed step's start (position, awake)", Same(ab.position(), {Bits(row[1]), Bits(row[2])}) && ab.IsAwake() == row[3].b,
                  "frame " + std::to_string(fn) + " step " + std::to_string(k) + " enemy " + std::to_string(row[0].AsInt()) + ": port " + Str(ab.position()) +
                      (ab.IsAwake() ? " awake" : " asleep") + " game " + Str({Bits(row[1]), Bits(row[2])}) + (row[3].b ? " awake" : " asleep"));
        }
    }

    void AfterStep(long long, size_t, const Value&, Tally&) override { step = -1; }

    void AtFrameEnd(long long fn, const Value& f, Tally& t) override
    {
        // The game's MoveEnemy calls the port didn't make.
        for (const Replayed& e : *c->enemies) {
            const size_t n = GameMoves(e.id).size();
            const size_t seen = movesSeen.count(e.id) ? movesSeen.at(e.id) : 0;
            if (n > seen)
                t.Add("MoveEnemy (InvokeRepeating): when it runs (frame, fixed step)", false,
                      "frame " + std::to_string(fn) + " enemy " + std::to_string(e.id) + ": the game ran " + std::to_string(n - seen) + " more");
        }
        // The explosions: whom they found.
        if (const Value* kev = f.Find("kev"))
            for (const Value& ev : kev->arr) {
                if (ev["k"].s != "explode") continue;
                const int id = static_cast<int>(ev["s"].AsInt());
                if (!c->ById(id)) continue;
                std::vector<int> mine = lastHits[id], theirs;
                int others = 0;
                for (const Value& hv : ev["hits"].arr) {
                    if (!hv["eb"].b) continue;
                    if (hv["id"].AsInt() >= 0) theirs.push_back(static_cast<int>(hv["id"].AsInt()));
                    else others++;
                }
                std::string which = "frame " + std::to_string(fn) + " enemy " + std::to_string(id) + ": port";
                for (int m : mine) which += " " + std::to_string(m);
                which += ", game";
                for (int m : theirs) which += " " + std::to_string(m);
                const bool sameOrder = mine == theirs;
                std::sort(mine.begin(), mine.end());
                std::sort(theirs.begin(), theirs.end());
                t.Add("explosion: the enemies OverlapCircleAll found (ids)", explosions[id] == explosionsBefore[id] + 1 && mine == theirs, which);
                t.Add("explosion: the order OverlapCircleAll found them in (the damage's order)", sameOrder, which);
                t.Info("explosion: other EnemyBehaviours in the blast (breakables: not ported)", others == 0, which + " and " + std::to_string(others) + " others");
            }
        explosionsBefore = explosions;
        // Each kamikaze's own fields.
        const Value* rows = f.Find("enemies");
        if (!rows) return;
        for (const Value& v : rows->arr) {
            Replayed* e = c->ById(static_cast<int>(v["s"].AsInt()));
            if (!e || v["gone"].b || e->Gone()) continue;
            const Value* ka = v.Find("kami");
            KamikazeProbe* k = Kami(*e);
            if (!ka || !k) continue;
            const std::string at = "frame " + std::to_string(fn) + " enemy " + std::to_string(e->id);
            if (const Value* j = ka->Find("jump"))
                t.Add("MakeJump's end and time", Same(XY(k->jumpEndPosition), {Bits((*j)[0]), Bits((*j)[1])}) && Same(k->jumpTime, Bits((*j)[2])),
                      at + ": port " + Str(XY(k->jumpEndPosition)) + " game " + Str({Bits((*j)[0]), Bits((*j)[1])}));
            if (const Value* trig = ka->Find("trig")) {
                UnityEngine::Animator* a = e->animator;
                const bool same = a->GetBool(S("Awake")) == (*trig)[0].b && a->GetBool(S("Attacking")) == (*trig)[1].b && a->GetBool(S("Jump")) == (*trig)[2].b;
                t.Add("Animator triggers (Awake, Attacking, Jump)", same, at);
            }
            if (const Value* ab = ka->Find("ab"); ab && e->attackRb && e->attackRb->body_) {
                const unity::Rigidbody2D& body = *e->attackRb->body_;
                t.Add("AttackCollider's own body at the frame's end (position, awake, box on)",
                      Same(body.position(), {Bits((*ab)[0]), Bits((*ab)[1])}) && body.IsAwake() == (*ab)[2].b && e->attackBox->enabled_ == (*ab)[3].b,
                      at + ": port " + Str(body.position()) + (body.IsAwake() ? " awake" : " asleep") + " game " + Str({Bits((*ab)[0]), Bits((*ab)[1])}) +
                          ((*ab)[2].b ? " awake" : " asleep"));
            }
            if (const Value* subs = ka->Find("subs")) {
                bool subscribed = false;
                if (cs::Delegate<void()>* d = c->hero->EnteredInCombat)
                    for (const auto& entry : d->Entries()) subscribed = subscribed || entry.target == k;
                t.Add("on HeroMerchant.EnteredInCombat", subscribed == subs->b, at);
            }
        }
    }
};

} // namespace
