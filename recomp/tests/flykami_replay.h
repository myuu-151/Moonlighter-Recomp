#pragma once
// The FlyingKamikazeGolem's part of enemy_test's replay (tests/enemy_replay.h's ReplayHooks), for
// enemy_FlyingKamikazeGolem.jsonl (`-oracle-enemy=FlyingKamikazeGolem`; FlyKamiOracle.cs writes
// the fields read here):
//   - each golem from its recorded state ("fly": isCharging, isReturning, desiredDirection, angle,
//     bounds, chargePosition, enemyDirection, the root box's enabled flag, before its body is
//     made; a recording that starts with EndCharge or EnableCharge pending can't replay them);
//   - each fixed step's start ("fsteps"): the root box's enabled flag and the attack child's
//     active flag per golem;
//   - MakeChargeAttack from where the game had the hero then ("fev" "charge"), its result
//     (bounds, chargePosition, desiredDirection) bit for bit; the hero's and the golem's z (the
//     port's transforms are 2D: 0); its draws are the clip events' rows (enemy_replay.h);
//   - the return tween's OnComplete from the game's Random state ("retDone"), its draws
//     (EnableCharge's Range(2, 6)) against the game's after;
//   - per frame, in order: the golems' PrepareChargeAttack, MakeChargeAttack, EndCharge, the
//     return's OnComplete and EnableCharge (whose, which fixed step: the Invokes' timing);
//   - each frame's end: each golem's "fly" fields, its pending Invokes;
//   - its child's EventSounds.OnEnable ("enSnd": the golem switched on, mostly before the
//     recording, by EnableEnemies): that the game drew exactly when the port's golem has an
//     enable sound (Enemy::enableSounds); the port's draws from the game's state are printed
//     (the replay's MasterAudio pools aren't the game's then).

#include <map>
#include <string>
#include <vector>

#include "enemy_replay.h"
#include "game/FlyingKamikazeGolem.h"
#include "game/MasterAudio.h"

namespace {

struct FlyKamiReplay : ReplayHooks {
    using Golem = game::FlyingKamikazeGolemBehaviour;
    using Ev = Golem::Event;

    game::EnemyHero* hero = nullptr;
    unity::DelayedCalls* calls = nullptr;
    game::MasterAudio* audio = nullptr;
    Tally* tally = nullptr;
    std::map<int, game::Enemy*> enemies;
    int step = -1; // the fixed step running (-1: the frame's part)
    long long frame = -1;
    const Value* fev = nullptr;
    std::vector<std::string> portEvents;    // "kind/golem/step"
    std::map<int, size_t> charges, returns; // per golem, this frame's so far
    bool sawFly = false;

    static const char* Name(Ev e)
    {
        switch (e) {
        case Ev::Prepare: return "prepare";
        case Ev::Charge: return "charge";
        case Ev::EndCharge: return "endCharge";
        case Ev::ReturnDone: return "retDone";
        case Ev::EnableCharge: return "enable";
        }
        return "?";
    }

    static Golem* Of(game::Enemy& e) { return dynamic_cast<Golem*>(e.enemyBehaviour.get()); }

    int IdOf(const game::Enemy& e) const
    {
        for (const auto& [id, en] : enemies)
            if (en == &e) return id;
        return -1;
    }

    // The frame's n-th game event of kind `k` for golem `id`.
    const Value* GameEvent(const char* k, int id, size_t n) const
    {
        if (!fev) return nullptr;
        size_t seen = 0;
        for (const Value& e : fev->arr)
            if (e["k"].s == k && e["s"].AsInt() == id && seen++ == n) return &e;
        return nullptr;
    }

    static unity::Rect RectOf(const Value& v) { return {Bits(v[0]), Bits(v[1]), Bits(v[2]), Bits(v[3])}; }
    static bool SameRect(const unity::Rect& a, const unity::Rect& b)
    {
        return Same(a.x, b.x) && Same(a.y, b.y) && Same(a.width, b.width) && Same(a.height, b.height);
    }
    static std::string RectStr(const unity::Rect& r) { return Str({r.x, r.y}) + " " + Str({r.width, r.height}); }

    void OnScene(const Value&, game::EnemyScene& scene, game::EnemyHero& h, unity::PhysicsWorld&, game::EnemyPhysics&, unity::Physics2D&,
                 unity::Rigidbody2D&, Tally& t) override
    {
        hero = &h;
        calls = scene.calls;
        audio = scene.audio;
        tally = &t;
    }

    void CheckEnableSounds(const game::Enemy& e, const Value& fl, const std::string& at, Tally& t)
    {
        const Value* snd = fl.Find("enSnd");
        if (!snd) return;
        for (const Value& s : snd->arr) {
            const unity::Random::State r0 = State(s["rng0"]), r1 = State(s["rng1"]);
            const int game = unity::Random::DrawsBetween(r0, r1, 64);
            t.Add("EventSounds.OnEnable: the golem's enable sound draws (Enemy::enableSounds)", (game > 0) == !e.enableSounds.empty(),
                  at + " (frameCount " + std::to_string(s["fc"].AsInt()) + "): the game " + std::to_string(game) + " draws, the port's sounds " +
                      std::to_string(e.enableSounds.size()));
            if (!audio) continue;
            const unity::Random::State saved = unity::Random::GetState();
            game::MasterAudio copy = *audio;
            unity::Random::SetState(r0);
            for (const std::string& name : e.enableSounds) copy.PlaySound(name);
            std::printf("%s: OnEnable's sound: the port %d draws from the game's state (the replay's pools), the game %d\n", at.c_str(),
                        unity::Random::DrawsBetween(r0, unity::Random::GetState(), 64), game);
            unity::Random::SetState(saved);
        }
    }

    void OnEnemy(game::Enemy& e, int id, const Value& line, Tally& t) override
    {
        Golem* golem = Of(e);
        t.Add("the prefab makes a FlyingKamikazeGolemBehaviour", golem != nullptr, "enemy " + std::to_string(id));
        if (!golem) return;
        enemies[id] = &e;
        if (const Value* fl = line.Find("fly")) {
            sawFly = true;
            golem->isCharging = (*fl)["charging"].b;
            golem->isReturning = (*fl)["returning"].b;
            golem->desiredDirection = V2((*fl)["dd"]);
            golem->angle = Bits((*fl)["angle"]);
            golem->bounds = RectOf((*fl)["bounds"]);
            golem->chargePosition = V2((*fl)["cp"]);
            golem->enemyDirection = static_cast<Golem::Direction>((*fl)["dir"].AsInt());
            e.colliderEnabled = (*fl)["colOn"].b; // (before EnemyPhysics::Add makes the body)
            if ((*fl)["invEnd"].b || (*fl)["invEnable"].b || golem->isReturning)
                std::printf("note: golem %d starts with EndCharge, EnableCharge or its return pending (not replayable)\n", id);
            CheckEnableSounds(e, *fl, "golem " + std::to_string(id) + " before the recording", t);
        } else {
            std::printf("note: no \"fly\" in the recording (FlyKamiOracle.cs): the golems' own rows skipped\n");
        }
        golem->onEvent = [this, id](Golem& g, Ev ev, bool after) { OnGolemEvent(g, id, ev, after); };
    }

    void OnGolemEvent(Golem& g, int id, Ev ev, bool after)
    {
        Tally& t = *tally;
        const std::string at = "frame " + std::to_string(frame) + " golem " + std::to_string(id);
        if (!after) portEvents.push_back(std::string(Name(ev)) + "/" + std::to_string(id) + "/" + std::to_string(step));
        if (ev == Ev::Charge) {
            const Value* game = GameEvent("charge", id, after ? charges[id] - 1 : charges[id]);
            if (!after) {
                charges[id]++;
                if (game) {
                    hero->position = V2((*game)["hero"]);
                    unity::Random::SetState(State((*game)["rng0"]));
                    t.Add("the hero's and the golem's z at a charge (the port's 2D transforms: 0)",
                          Bits((*game)["heroZ"]) == 0.0f && Bits((*game)["posZ"]) == 0.0f,
                          at + ": hero z " + std::to_string(Bits((*game)["heroZ"])) + " golem z " + std::to_string(Bits((*game)["posZ"])));
                    t.Add("the golem where the game had it at a charge", Same(g.enemy.position(), V2((*game)["pos"])),
                          at + ": port " + Str(g.enemy.position()) + " game " + Str(V2((*game)["pos"])));
                }
                return;
            }
            if (!game) return;
            t.Add("MakeChargeAttack: bounds, chargePosition (the turn's bug), desiredDirection",
                  SameRect(g.bounds, RectOf((*game)["bounds"])) && Same(g.chargePosition, V2((*game)["cp"])) &&
                      Same(g.desiredDirection, V2((*game)["dd"])),
                  at + ": port turn " + std::to_string(g.lastTurn) + " cp " + Str(g.chargePosition) + " dd " + Str(g.desiredDirection) + " bounds " +
                      RectStr(g.bounds) + "; game cp " + Str(V2((*game)["cp"])) + " dd " + Str(V2((*game)["dd"])) + " bounds " +
                      RectStr(RectOf((*game)["bounds"])));
            t.Add("Random draws in MakeChargeAttack (the sound's, Range(-20, 21))", SameState(unity::Random::GetState(), State((*game)["rng1"])),
                  at + ": " + std::to_string(unity::Random::DrawsBetween(State((*game)["rng0"]), unity::Random::GetState(), 64)) + " draws, the game " +
                      std::to_string(unity::Random::DrawsBetween(State((*game)["rng0"]), State((*game)["rng1"]), 64)));
            return;
        }
        if (ev == Ev::ReturnDone) {
            const Value* game = GameEvent("retDone", id, after ? returns[id] - 1 : returns[id]);
            if (!after) {
                returns[id]++;
                if (game) unity::Random::SetState(State((*game)["rng0"]));
                return;
            }
            if (!game) return;
            t.Add("Random draws in the return's OnComplete (EnableCharge's Range(2, 6))", SameState(unity::Random::GetState(), State((*game)["rng1"])),
                  at + ": port delay " + std::to_string(g.lastEnableDelay) + ", " +
                      std::to_string(unity::Random::DrawsBetween(State((*game)["rng0"]), unity::Random::GetState(), 64)) + " draws, the game " +
                      std::to_string(unity::Random::DrawsBetween(State((*game)["rng0"]), State((*game)["rng1"]), 64)));
            unity::Random::SetState(State((*game)["rng1"]));
        }
    }

    void BeginFrame(long long fn, const Value& f) override
    {
        frame = fn;
        fev = f.Find("fev");
    }

    void BeforeStep(long long fn, size_t k, const Value& f, Tally& t) override
    {
        step = static_cast<int>(k);
        const Value* fsteps = f.Find("fsteps");
        if (!fsteps || k >= fsteps->size()) return;
        for (const Value& r : (*fsteps)[k].arr) {
            const int id = static_cast<int>(r[0].AsInt());
            const auto it = enemies.find(id);
            if (it == enemies.end() || it->second->destroyed) continue;
            const game::Enemy& e = *it->second;
            t.Add("root box enabled, attack child active at a fixed step's start", e.colliderEnabled == r[1].b && e.attackActive == r[2].b,
                  "frame " + std::to_string(fn) + " step " + std::to_string(k) + " golem " + std::to_string(id) + ": port " +
                      std::to_string(e.colliderEnabled) + "/" + std::to_string(e.attackActive) + " game " + r.Dump());
        }
    }

    void AfterStep(long long, size_t, const Value&, Tally&) override { step = -1; }

    void AtFrameEnd(long long fn, const Value& f, Tally& t) override
    {
        const std::string at = "frame " + std::to_string(fn);
        if (fev) {
            std::vector<std::string> game;
            for (const Value& e : fev->arr) game.push_back(e["k"].s + "/" + std::to_string(e["s"].AsInt()) + "/" + std::to_string(e["step"].AsInt()));
            if (!game.empty() || !portEvents.empty()) {
                std::string list = at + ": port";
                for (const auto& e : portEvents) list += " " + e;
                list += "; game";
                for (const auto& e : game) list += " " + e;
                t.Add("the golems' prepare, charge, endCharge, return done, enable (whose, which step)", game == portEvents, list);
            }
        }
        if (const Value* list = f.Find("enemies"))
            for (const Value& v : list->arr) {
                const auto it = enemies.find(static_cast<int>(v["s"].AsInt()));
                const Value* fl = v.Find("fly");
                if (it == enemies.end() || !fl || v["gone"].b || it->second->destroyed) continue;
                game::Enemy& e = *it->second;
                Golem* g = Of(e);
                const std::string who = at + " golem " + std::to_string(v["s"].AsInt());
                t.Add("golem (charging, returning, desiredDirection, angle)",
                      g->isCharging == (*fl)["charging"].b && g->isReturning == (*fl)["returning"].b && Same(g->desiredDirection, V2((*fl)["dd"])) &&
                          Same(g->angle, Bits((*fl)["angle"])),
                      who + ": port " + std::to_string(g->isCharging) + std::to_string(g->isReturning) + " dd " + Str(g->desiredDirection) + " angle " +
                          std::to_string(g->angle) + "; game " + std::to_string((*fl)["charging"].b) + std::to_string((*fl)["returning"].b) + " dd " +
                          Str(V2((*fl)["dd"])) + " angle " + std::to_string(Bits((*fl)["angle"])));
                t.Add("golem (bounds, chargePosition, enemyDirection)",
                      SameRect(g->bounds, RectOf((*fl)["bounds"])) && Same(g->chargePosition, V2((*fl)["cp"])) &&
                          static_cast<int>(g->enemyDirection) == (*fl)["dir"].AsInt(),
                      who + ": port " + RectStr(g->bounds) + " cp " + Str(g->chargePosition) + "; game " + RectStr(RectOf((*fl)["bounds"])) + " cp " +
                          Str(V2((*fl)["cp"])));
                t.Add("root box enabled, attack child active (frame end)", e.colliderEnabled == (*fl)["colOn"].b && e.attackActive == (*fl)["attackActive"].b,
                      who + ": port " + std::to_string(e.colliderEnabled) + "/" + std::to_string(e.attackActive) + " game " +
                          std::to_string((*fl)["colOn"].b) + "/" + std::to_string((*fl)["attackActive"].b));
                if (calls)
                    t.Add("golem's Invokes pending (EndCharge, EnableCharge)",
                          calls->IsInvoking(g, "EndCharge") == (*fl)["invEnd"].b && calls->IsInvoking(g, "EnableCharge") == (*fl)["invEnable"].b, who);
                t.Add("golem's z (the port's 2D transforms: 0)", Bits((*fl)["z"]) == 0.0f, who + ": " + std::to_string(Bits((*fl)["z"])));
                CheckEnableSounds(e, *fl, who, t);
            }
        portEvents.clear();
        charges.clear();
        returns.clear();
        fev = nullptr;
    }
};

} // namespace
