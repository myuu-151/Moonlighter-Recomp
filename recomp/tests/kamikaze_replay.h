#pragma once
// The KamikazeGolem's part of enemy_test's replay (tests/enemy_replay.h's ReplayHooks), for
// enemy_KamikazeGolem.jsonl (`-oracle-enemy=KamikazeGolem`; KamikazeOracle.cs writes the fields
// read here):
//   - the scene gets the room's colliders for the hole check (Update's OverlapBox);
//   - each kamikaze from its recorded state ("kami": the Animator's Awake / Attacking / Jump
//     triggers, MakeJump's end and time, its attack body's position and rest in the first frame)
//     and its InvokeRepeating("MoveEnemy", 0.65, 0.65) made when its room was entered ("enter":
//     from its Time.fixedTime, as every Invoke, on the replay's fixed clock), on from the run after the
//     "moves" it has made already (each 0.65 after the last one was due);
//   - each fixed step's start ("ksteps"): the AttackCollider's own body (position, awake);
//   - each MoveEnemy ("kev" "move", in order per kamikaze): when it runs (the frame and the
//     fixed step, -1: the frame's pass), from the game's Random state before it (resynced), its
//     draws compared after, where it wandered (the point, whether PolyNav took it) and the
//     agent's goal after; one the port runs that the game didn't (or the other way) differs;
//   - each explosion ("explode"): the enemies OverlapCircleAll found (those with an
//     EnemyBehaviour, by id) against the port's; colliders the port doesn't hold (a breakable's)
//     inform without failing;
//   - each frame's end ("kami" in "enemies"): MakeJump's end and time, the triggers (an active
//     Animator's), the attack body (position, awake, the box on), its EnteredInCombat
//     subscription;
//   - the room's and the hero's parts the kamikaze's code reaches (the start line's "kroom" and
//     "heroEffects", KamikazeOracle.WriteStart): when the last enemy leaves roomEnemies (its
//     Die: SelfDestroy's too) the room's OnAllEnemiesDefeated runs (game/EnemyRoom.h:
//     UnLockRoomChests' sounds draw); every hit raises Enemy.OnEnemyRecivesDamage, which the
//     hero's Wanderer Amulet draws on (game/AmuletEffects.h). Recordings without them: neither.

#include <cmath>
#include <map>
#include <string>
#include <vector>

#include <Box2D/Box2D.h>

#include "enemy_replay.h"
#include "game/AmuletEffects.h"
#include "game/EnemyRoom.h"
#include "game/KamikazeGolem.h"

namespace {

struct KamikazeReplay : ReplayHooks {
    std::map<int, game::Enemy*> enemies;
    unity::DelayedCalls* calls = nullptr;
    unity::Tweens* tweens = nullptr;
    Tally* tally = nullptr;
    long long frame = -1;
    int step = -1; // the fixed step running (-1: the frame's part)
    const Value* frameLine = nullptr;
    std::map<int, size_t> movesSeen; // per kamikaze: the frame's game "move" events matched so far
    std::map<int, int> explosionsBefore; // per kamikaze: the port's explosions before this frame
    struct Enter {
        int id;
        float base; // Time.fixedTime then
        bool fixed;
        int moves;      // MoveEnemy runs before the recording
        float lastMove; // the last one's Time.time (-1: none)
    };
    std::vector<Enter> enters;
    float startTime = 0.0f;
    float fixedDt = 0.02f; // Time.fixedDeltaTime (the start line's)
    // The attack bodies as the recording starts ([x, y, awake]: "ab"), set in the first frame
    // (after the steps the replay runs before it, which sync them to the transform).
    struct StartBody {
        Vector2 position;
        bool awake;
    };
    std::map<int, StartBody> startBodies;
    bool bodiesPlaced = false;
    // The room ("kroom"): its chests' types, OnAllEnemiesDefeated's handlers, isLocked.
    std::vector<int> chestTypes;
    std::vector<std::string> allDefeated;
    bool roomLocked = false;
    game::MasterAudio* audio = nullptr;
    // The hero's Wanderer Amulet ("heroEffects").
    game::WandererAmuletEffect wanderer;

    // DungeonRoom.EnemyDefeated's end, the recorded enemies being the room's roomEnemies (the
    // oracle takes the others out before EnableEnemies).
    void RoomEnemyDefeated()
    {
        std::vector<const game::Enemy*> list;
        for (const auto& [id, e] : enemies) list.push_back(e);
        if (!game::AreAllEnemiesDefeated(list, roomLocked)) return;
        for (const std::string& h : allDefeated) {
            if (h == "DungeonRoom.UnLockRoomChests") game::UnLockRoomChests(chestTypes, audio);
            else if (h == "DungeonRoom.OpenAllDoors" || h == "DungeonRoom.ResetHeroMerchantIsInCombat") {
                // (No draws: the doors' Animators, the hero's IsInCombat. recq kamifix-3's probe.)
            } else if (tally) {
                tally->Add("the room's OnAllEnemiesDefeated handlers (ported: UnLockRoomChests, OpenAllDoors, ResetHeroMerchantIsInCombat)",
                           false, "frame " + std::to_string(frame) + ": " + h);
            }
        }
    }

    static game::GolemKamikazeBehaviour* Kami(game::Enemy& e) { return dynamic_cast<game::GolemKamikazeBehaviour*>(e.enemyBehaviour.get()); }

    int IdOf(const game::Enemy* e) const
    {
        for (const auto& [id, en] : enemies)
            if (en == e) return id;
        return -1;
    }

    // The game's "move" events of kamikaze `id` in this frame, in order.
    std::vector<const Value*> GameMoves(int id) const
    {
        std::vector<const Value*> out;
        if (!frameLine) return out;
        if (const Value* kev = frameLine->Find("kev"))
            for (const Value& e : kev->arr)
                if (e["k"].s == "move" && e["s"].AsInt() == id) out.push_back(&e);
        return out;
    }

    void OnScene(const Value& start, game::EnemyScene& scene, game::EnemyHero&, unity::PhysicsWorld&, game::EnemyPhysics&,
                 unity::Physics2D& roomColliders, unity::Rigidbody2D&, Tally& t) override
    {
        calls = scene.calls;
        tweens = scene.tweens;
        tally = &t;
        scene.queries = &roomColliders; // (Update's hole check: the holes are among them)
        if (const Value* tm = start.Find("time")) startTime = Bits(*tm);
        fixedDt = Bits(start["fixedDeltaTime"]);
        audio = scene.audio;
        if (const Value* kr = start.Find("kroom")) {
            for (const Value& c : (*kr)["chests"].arr) chestTypes.push_back(static_cast<int>(c[0].AsInt()));
            for (const Value& h : (*kr)["allDefeated"].arr) allDefeated.push_back(h.s);
            if (const Value* locked = kr->Find("locked")) roomLocked = locked->b;
            else std::printf("note: no \"locked\" in \"kroom\" (recorded before it): the room taken as not locked\n");
            scene.onEnemyDefeated = [this](game::Enemy&) { RoomEnemyDefeated(); };
        } else {
            std::printf("note: no \"kroom\" (recorded before KamikazeOracle.WriteStart): the room's OnAllEnemiesDefeated isn't run\n");
        }
        if (const Value* he = start.Find("heroEffects")) {
            bool wandererOn = false;
            for (const Value& h : (*he)["damage"].arr) {
                if (h.s != "WandererAmuletEffect.Enemy_OnEnemyRecivesDamage") {
                    t.Add("the hero's Enemy.OnEnemyRecivesDamage handlers (ported: the Wanderer Amulet's)", false, h.s);
                    continue;
                }
                for (const Value& eff : (*he)["effects"].arr)
                    if (eff.size() >= 3 && eff[0].s == "WandererAmuletEffect") {
                        wanderer.probToCallWanderer = Bits(eff[2]);
                        wandererOn = true;
                    }
            }
            wanderer.wandererActive = (*he)["wandererActive"].b;
            wanderer.spawnWanderer = [this] {
                if (tally) tally->Add("the Wanderer Amulet called the Wanderer (DungeonManager.SpawnWanderer: not ported)", false, "frame " + std::to_string(frame));
            };
            if (wandererOn)
                scene.onEnemyReceivesDamage = [this](game::Enemy& e, float damage, game::EnemyDamageReceivedType type) {
                    wanderer.Enemy_OnEnemyRecivesDamage(e, damage, type);
                };
        } else {
            std::printf("note: no \"heroEffects\" (recorded before KamikazeOracle.WriteStart): the hero's amulets' handlers aren't run\n");
        }
    }

    void OnEnemy(game::Enemy& e, int id, const Value& line, Tally& t) override
    {
        game::GolemKamikazeBehaviour* k = Kami(e);
        t.Add("the prefab makes a GolemKamikazeBehaviour", k != nullptr, "enemy " + std::to_string(id));
        if (!k) return;
        enemies[id] = &e;
        t.Add("its AttackCollider has a Kinematic Rigidbody2D of its own (prefab)",
              e.attackHasRigidbody && e.attackRigidbody.bodyType == unity::RigidbodyType2D::Kinematic, "enemy " + std::to_string(id));
        const Value* ka = line.Find("kami");
        if (!ka) {
            std::printf("note: enemy %d has no \"kami\" (KamikazeOracle.cs): MoveEnemy isn't scheduled\n", id);
            return;
        }
        if (const Value* trig = ka->Find("trig")) {
            const char* const names[3] = {"Awake", "Attacking", "Jump"};
            for (size_t i = 0; i < 3 && i < trig->size(); i++)
                if ((*trig)[i].b) e.enemyAnimator->SetTrigger(names[i]);
                else e.enemyAnimator->ResetTrigger(names[i]);
        }
        if (const Value* j = ka->Find("jump")) {
            k->jumpEndPosition = {Bits((*j)[0]), Bits((*j)[1])};
            k->jumpTime = Bits((*j)[2]);
        }
        if (const Value* ab = ka->Find("ab"); ab && ab->IsArray() && ab->size() >= 3)
            startBodies[id] = {{Bits((*ab)[0]), Bits((*ab)[1])}, (*ab)[2].b};
        const Value* en = ka->Find("enter");
        if (en && en->IsArray()) {
            // An Invoke counts from Time.fixedTime wherever it's made (unity/DelayedCalls.h): an
            // EnemyRoom_OnRoomEnter run in a frame too (enemy_KamikazeGolem.jsonl: entered at
            // Time.time 191.755310, fixedTime 191.75, MoveEnemy in the steps at 192.4, 193.05...).
            const bool fixed = en->size() > 2 && (*en)[2].b;
            const Value* lm = ka->Find("lastMove");
            enters.push_back({id, Bits((*en)[en->size() > 1 ? 1 : 0]), fixed,
                              ka->Find("moves") ? static_cast<int>((*ka)["moves"].AsInt()) : 0, lm && lm->IsArray() ? Bits((*lm)[0]) : -1.0f});
        } else {
            std::printf("note: enemy %d never entered its room (no EnemyRoom_OnRoomEnter): no MoveEnemy\n", id);
        }
        // Each MoveEnemy against the game's: when, from its Random state, its draws and its wander.
        k->onMoveEnemy = [this, id](game::GolemKamikazeBehaviour& self, bool after) {
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
            bool wander = true;
            if ((*game)["wander"].b) wander = Same(self.lastWander, V2((*game)["dest"])) && self.lastWanderValid == (*game)["valid"].b;
            tally->Add("MoveEnemy from the game's Random state (draws, wander point, valid, agent goal)",
                       SameState(unity::Random::GetState(), State((*game)["rng1"])) && wander && Same(self.enemy.navAgent.primeGoal(), V2((*game)["goal"])),
                       at + ": port " + Str(self.lastWander) + " goal " + Str(self.enemy.navAgent.primeGoal()) + " game " +
                           ((*game)["wander"].b ? Str(V2((*game)["dest"])) : std::string("(no wander)")) + " goal " + Str(V2((*game)["goal"])));
            unity::Random::SetState(State((*game)["rng1"]));
        };
    }

    void OnEnemiesBuilt(const Value&, Tally&) override
    {
        // InvokeRepeating("MoveEnemy", 0.65, 0.65) as EnemyRoom_OnRoomEnter made it, from its
        // Time.fixedTime as the engine's double (enable_clock.h's EngineFixedTime: the recorded
        // float can be 7.6e-6 off, while a repeat falls due 9.3e-9 before its step per period,
        // 65 steps of 0.0099999998 against 0.65f).
        if (!calls) return;
        const double now = calls->now, fixedNow = calls->fixedNow;
        const bool inFixed = calls->inFixedStep;
        for (const Enter& en : enters) {
            game::GolemKamikazeBehaviour* k = Kami(*enemies[en.id]);
            // The runs made before the recording: the next is due 0.65 after the last one was
            // (the repeat adds its rate to the due time, as unity/DelayedCalls has it).
            double at = EngineFixedTime(en.base, fixedDt);
            for (int n = 0; n < en.moves; n++) at += static_cast<double>(0.65f);
            if (en.moves > 0 && std::fabs(static_cast<double>(en.lastMove) - at) > 0.02)
                std::printf("note: enemy %d's last MoveEnemy before the recording ran at %.6f, due at %.6f (InvokeRepeating's period?)\n", en.id,
                            static_cast<double>(en.lastMove), at);
            if (at + static_cast<double>(0.65f) <= static_cast<double>(startTime))
                std::printf("note: enemy %d's next MoveEnemy was due before the recording (%.6f): not replayable\n", en.id, at + 0.65);
            calls->inFixedStep = en.fixed;
            calls->now = at;
            calls->fixedNow = at;
            calls->Invoke(k, "MoveEnemy", 0.65f, [k] { k->MoveEnemy(0.0f); }, 0.65f);
        }
        calls->now = now;
        calls->fixedNow = fixedNow;
        calls->inFixedStep = inFixed;
        for (const auto& [id, e] : enemies) explosionsBefore[id] = Kami(*e)->explosions;
    }

    void BeginFrame(long long fn, const Value& f) override
    {
        if (!bodiesPlaced) {
            bodiesPlaced = true;
            for (const auto& [id, sb] : startBodies) {
                const auto it = enemies.find(id);
                if (it == enemies.end() || !it->second->attackBody) continue;
                b2Body* b = it->second->attackBody->Body();
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
            const auto it = enemies.find(static_cast<int>(row[0].AsInt()));
            if (it == enemies.end() || it->second->destroyed || !it->second->attackBody) continue;
            const unity::Rigidbody2D& ab = *it->second->attackBody;
            t.Add("AttackCollider's own body at a fixed step's start (position, awake)",
                  Same(ab.position(), {Bits(row[1]), Bits(row[2])}) && ab.IsAwake() == row[3].b,
                  "frame " + std::to_string(fn) + " step " + std::to_string(k) + " enemy " + std::to_string(row[0].AsInt()) + ": port " +
                      Str(ab.position()) + (ab.IsAwake() ? " awake" : " asleep") + " game " + Str({Bits(row[1]), Bits(row[2])}) +
                      (row[3].b ? " awake" : " asleep"));
        }
    }

    void AfterStep(long long, size_t, const Value&, Tally&) override { step = -1; }

    void AtFrameEnd(long long fn, const Value& f, Tally& t) override
    {
        // The game's MoveEnemy calls the port didn't make.
        for (const auto& [id, e] : enemies) {
            (void)e;
            const size_t n = GameMoves(id).size();
            const size_t seen = movesSeen.count(id) ? movesSeen.at(id) : 0;
            if (n > seen)
                t.Add("MoveEnemy (InvokeRepeating): when it runs (frame, fixed step)", false,
                      "frame " + std::to_string(fn) + " enemy " + std::to_string(id) + ": the game ran " + std::to_string(n - seen) + " more");
        }
        // The explosions: whom they found.
        if (const Value* kev = f.Find("kev"))
            for (const Value& ev : kev->arr) {
                if (ev["k"].s != "explode") continue;
                const int id = static_cast<int>(ev["s"].AsInt());
                const auto it = enemies.find(id);
                if (it == enemies.end()) continue;
                game::GolemKamikazeBehaviour* k = Kami(*it->second);
                std::vector<int> mine, theirs;
                int others = 0;
                for (game::Enemy* hit : k->lastExplosionHits) mine.push_back(IdOf(hit));
                for (const Value& h : ev["hits"].arr) {
                    if (!h["eb"].b) continue;
                    if (h["id"].AsInt() >= 0) theirs.push_back(static_cast<int>(h["id"].AsInt()));
                    else others++;
                }
                std::string which = "frame " + std::to_string(fn) + " enemy " + std::to_string(id) + ": port";
                for (int m : mine) which += " " + std::to_string(m);
                which += ", game";
                for (int m : theirs) which += " " + std::to_string(m);
                // The order is the one the damage lands in (deaths, their drops' draws later):
                // the port's is its dynamic tree's; Unity's is its own tree's (or by depth: not
                // confirmed), so this one tells whether they agree.
                const bool sameOrder = mine == theirs;
                std::sort(mine.begin(), mine.end());
                std::sort(theirs.begin(), theirs.end());
                t.Add("explosion: the enemies OverlapCircleAll found (ids)", k->explosions == explosionsBefore[id] + 1 && mine == theirs, which);
                t.Add("explosion: the order OverlapCircleAll found them in (the damage's order)", sameOrder, which);
                t.Info("explosion: other EnemyBehaviours in the blast (breakables: not ported)", others == 0,
                       which + " and " + std::to_string(others) + " others");
            }
        for (const auto& [id, e] : enemies) explosionsBefore[id] = Kami(*e)->explosions;
        // Each kamikaze's own fields.
        const Value* rows = f.Find("enemies");
        if (!rows) return;
        for (const Value& v : rows->arr) {
            const auto it = enemies.find(static_cast<int>(v["s"].AsInt()));
            if (it == enemies.end() || v["gone"].b || it->second->destroyed) continue;
            const Value* ka = v.Find("kami");
            if (!ka) continue;
            game::Enemy& e = *it->second;
            game::GolemKamikazeBehaviour* k = Kami(e);
            const std::string at = "frame " + std::to_string(fn) + " enemy " + std::to_string(it->first);
            if (const Value* j = ka->Find("jump"))
                t.Add("MakeJump's end and time", Same(k->jumpEndPosition, {Bits((*j)[0]), Bits((*j)[1])}) && Same(k->jumpTime, Bits((*j)[2])),
                      at + ": port " + Str(k->jumpEndPosition) + " game " + Str({Bits((*j)[0]), Bits((*j)[1])}));
            if (const Value* trig = ka->Find("trig")) {
                const bool same = e.enemyAnimator->GetBool("Awake") == (*trig)[0].b && e.enemyAnimator->GetBool("Attacking") == (*trig)[1].b &&
                                  e.enemyAnimator->GetBool("Jump") == (*trig)[2].b;
                t.Add("Animator triggers (Awake, Attacking, Jump)", same, at);
            }
            if (const Value* ab = ka->Find("ab"); ab && e.attackBody)
                t.Add("AttackCollider's own body at the frame's end (position, awake, box on)",
                      Same(e.attackBody->position(), {Bits((*ab)[0]), Bits((*ab)[1])}) && e.attackBody->IsAwake() == (*ab)[2].b &&
                          e.attackEnabled == (*ab)[3].b,
                      at + ": port " + Str(e.attackBody->position()) + (e.attackBody->IsAwake() ? " awake" : " asleep") + " game " +
                          Str({Bits((*ab)[0]), Bits((*ab)[1])}) + ((*ab)[2].b ? " awake" : " asleep"));
            // ("tw", the tweens on its transform, is for reading: the jump's moves are the shared
            // MakeJump's (game/EnemyJump.h), seen in the positions.)
            if (const Value* subs = ka->Find("subs")) t.Add("on HeroMerchant.EnteredInCombat", k->subscribedToCombat == subs->b, at);
        }
    }
};

} // namespace
