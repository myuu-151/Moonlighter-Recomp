#pragma once
// The FlyingKamikazeGolem's part of the translated replay (tests/flykami_replay.h's FlyKamiReplay,
// on the game's own FlyingKamikazeGolemBehaviour), for enemy_FlyingKamikazeGolem.jsonl
// (FlyKamiOracle.cs):
//   - each golem from its recorded state ("fly": isCharging, isReturning, desiredDirection,
//     angle, bounds, chargePosition, enemyDirection, the root box's enabled flag);
//   - each fixed step's start ("fsteps"): the root box's enabled flag and the Attack child's
//     active flag per golem;
//   - MakeChargeAttack (its clip event) from where the game had the hero then ("fev" "charge"),
//     its result (bounds, chargePosition, desiredDirection) bit for bit; its draws;
//   - the return tween's OnComplete (bind::Scene::onTweenComplete, its delegate's target the
//     golem) from the game's Random state ("retDone"), its draws (EnableCharge's Range(2, 6));
//   - per frame, in order: the golems' PrepareChargeAttack (ChooseAndApplyAction wrapped by the
//     probe FlyKamiProbe: isCharging turned on), MakeChargeAttack, EndCharge and EnableCharge
//     (their Invokes: bind::Scene::onInvoke), the return's OnComplete (whose, which fixed step);
//   - each frame's end: each golem's "fly" fields, its pending Invokes;
//   - its child's EventSounds.OnEnable ("enSnd"): the enable sounds the prefab's EventSounds name
//     (prefabs.json: the facade has no EventSounds yet; flykami_replay.h's Enemy::enableSounds),
//     the first, at the floor's generation, against a MasterAudio without the dungeon's groups.
// (Included by enemy_translated_test.cpp after its helpers.)

#include <map>
#include <string>
#include <vector>

namespace {

// FlyingKamikazeGolemBehaviour with ChooseAndApplyAction wrapped (the game's, run as it is): its
// PrepareChargeAttack is the one way isCharging comes on there.
class FlyKamiProbe : public gen::FlyingKamikazeGolemBehaviour {
public:
    std::function<void(FlyKamiProbe&, bool after)> onChoose;
    void ChooseAndApplyAction() override
    {
        if (onChoose) onChoose(*this, false);
        gen::FlyingKamikazeGolemBehaviour::ChooseAndApplyAction();
        if (onChoose) onChoose(*this, true);
    }
};

// The prefab's EventSounds' enable sounds on the objects that are on (game/EnemyFactory.cpp's).
std::vector<std::string> EnableSoundsOf(const game::Prefab& prefab)
{
    std::vector<std::string> out;
    for (const game::PrefabNode& n : prefab.nodes) {
        bool on = n.active;
        for (const game::PrefabNode& m : prefab.nodes)
            on = on && (m.path.empty() || n.path.compare(0, m.path.size() + 1, m.path + "/") != 0 || m.active);
        for (const game::Behaviour& b : n.behaviours) {
            if (!on || b.cls != "EventSounds" || b.Field("m_Enabled", 1.0) == 0.0 || b.Field("useEnableSound", 0.0) == 0.0) continue;
            const auto it = b.raw.find("enableSound");
            if (it != b.raw.end())
                for (const json::Value& s : it->second.arr)
                    if (s.IsString()) out.push_back(s.s);
        }
    }
    return out;
}

struct FlyKamiHooks : Hooks {
    Ctx* c = nullptr;
    Tally* tally = nullptr;
    int step = -1;
    long long frame = -1;
    const Value* fev = nullptr;
    std::vector<std::string> portEvents; // "kind/golem/step"
    std::map<int, size_t> charges, returns;
    std::map<int, bool> wasCharging;
    std::vector<std::string> enableSounds;

    UnityEngine::Component* AddScript(GameObject* go, const char* cls) override
    {
        if (std::strcmp(cls, "FlyingKamikazeGolemBehaviour") != 0) return nullptr;
        return go->AddComponent<FlyKamiProbe*>();
    }

    static FlyKamiProbe* Of(const Replayed& e) { return dynamic_cast<FlyKamiProbe*>(e.behaviour); }

    const Value* GameEvent(const char* k, int id, size_t n) const
    {
        if (!fev) return nullptr;
        size_t seen = 0;
        for (const Value& e : fev->arr)
            if (e["k"].s == k && e["s"].AsInt() == id && seen++ == n) return &e;
        return nullptr;
    }

    static unity::Rect RectOf(const Value& v) { return {Bits(v[0]), Bits(v[1]), Bits(v[2]), Bits(v[3])}; }
    static bool SameRect(const unity::Rect& a, const unity::Rect& b) { return Same(a.x, b.x) && Same(a.y, b.y) && Same(a.width, b.width) && Same(a.height, b.height); }
    static std::string RectStr(const unity::Rect& r) { return Str({r.x, r.y}) + " " + Str({r.width, r.height}); }

    void OnScene(const Value&, Ctx& ctx, Tally& t) override
    {
        c = &ctx;
        tally = &t;
        enableSounds = EnableSoundsOf(*ctx.data->prefab);
        // EndCharge and EnableCharge (the golem's Invokes): the log.
        ctx.scene->onInvoke = [this](UnityEngine::MonoBehaviour* target, cs::String* method, bool after) {
            if (after) return;
            const int id = c->IdOf(target);
            if (id < 0) return;
            const std::string m = Utf8(method);
            if (m == "EndCharge") portEvents.push_back("endCharge/" + std::to_string(id) + "/" + std::to_string(step));
            else if (m == "EnableCharge") portEvents.push_back("enable/" + std::to_string(id) + "/" + std::to_string(step));
        };
        // The return's OnComplete: from the game's Random state, its draws compared.
        ctx.scene->onTweenComplete = [this](cs::Object* target, bool after) {
            const int id = c->IdOf(dynamic_cast<UnityEngine::Object*>(target));
            if (id < 0) return;
            const Value* game = GameEvent("retDone", id, after ? returns[id] - 1 : returns[id]);
            if (!after) {
                portEvents.push_back("retDone/" + std::to_string(id) + "/" + std::to_string(step));
                returns[id]++;
                if (game) unity::Random::SetState(State((*game)["rng0"]));
                return;
            }
            if (!game) return;
            tally->Add("Random draws in the return's OnComplete (EnableCharge's Range(2, 6))", SameState(unity::Random::GetState(), State((*game)["rng1"])),
                       "frame " + std::to_string(frame) + " golem " + std::to_string(id) + ": " +
                           std::to_string(unity::Random::DrawsBetween(State((*game)["rng0"]), unity::Random::GetState(), 64)) + " draws, the game " +
                           std::to_string(unity::Random::DrawsBetween(State((*game)["rng0"]), State((*game)["rng1"]), 64)));
            unity::Random::SetState(State((*game)["rng1"]));
        };
    }

    // `beforeRecording`: the start's list, whose first is the floor's generation's.
    void CheckEnableSounds(const Value& fl, const std::string& at, Tally& t, bool beforeRecording)
    {
        const Value* snd = fl.Find("enSnd");
        if (!snd) return;
        size_t index = 0;
        for (const Value& s : snd->arr) {
            const unity::Random::State r0 = State(s["rng0"]), r1 = State(s["rng1"]);
            const int game = unity::Random::DrawsBetween(r0, r1, 64);
            if (beforeRecording && index++ == 0) {
                // MasterAudio at the floor's generation: none of the dungeon's sound groups yet.
                const unity::Random::State saved = unity::Random::GetState();
                game::MasterAudio atGeneration;
                unity::Random::SetState(r0);
                for (const std::string& name : enableSounds) atGeneration.PlaySound(name);
                const int port = unity::Random::DrawsBetween(r0, unity::Random::GetState(), 64);
                t.Add("EventSounds.OnEnable at the floor's generation (SpawnEnemies before GenerateSounds: no group yet), its draws",
                      SameState(unity::Random::GetState(), r1),
                      at + " (frameCount " + std::to_string(s["fc"].AsInt()) + "): the port " + std::to_string(port) + " draws, the game " + std::to_string(game));
                unity::Random::SetState(saved);
                continue;
            }
            t.Add("EventSounds.OnEnable: the golem's enable sound draws (Enemy::enableSounds)", (game > 0) == !enableSounds.empty(),
                  at + " (frameCount " + std::to_string(s["fc"].AsInt()) + "): the game " + std::to_string(game) + " draws, the port's sounds " +
                      std::to_string(enableSounds.size()));
            const unity::Random::State saved = unity::Random::GetState();
            game::MasterAudio copy = *c->audio;
            unity::Random::SetState(r0);
            for (const std::string& name : enableSounds) copy.PlaySound(name);
            std::printf("%s: OnEnable's sound: the port %d draws from the game's state (the replay's pools), the game %d\n", at.c_str(),
                        unity::Random::DrawsBetween(r0, unity::Random::GetState(), 64), game);
            unity::Random::SetState(saved);
        }
    }

    void OnEnemy(Replayed& e, const Value& line, Tally& t) override
    {
        FlyKamiProbe* g = Of(e);
        t.Add("the prefab makes a FlyingKamikazeGolemBehaviour", g != nullptr, "enemy " + std::to_string(e.id));
        if (!g) return;
        if (const Value* fl = line.Find("fly")) {
            g->isCharging = (*fl)["charging"].b;
            g->isReturning = (*fl)["returning"].b;
            g->desiredDirection = V2((*fl)["dd"]);
            g->angle = Bits((*fl)["angle"]);
            g->bounds = RectOf((*fl)["bounds"]);
            g->chargePosition = UnityEngine::Vector3{Bits((*fl)["cp"][0]), Bits((*fl)["cp"][1]), 0.0f};
            g->enemyDirection = static_cast<gen::HeroMerchantController_Direction>((*fl)["dir"].AsInt());
            e.box->set_enabled((*fl)["colOn"].b); // (its body's box made or taken at the setup's sync)
            if ((*fl)["invEnd"].b || (*fl)["invEnable"].b || g->isReturning)
                std::printf("note: golem %d starts with EndCharge, EnableCharge or its return pending (not replayable)\n", e.id);
            CheckEnableSounds(*fl, "golem " + std::to_string(e.id) + " before the recording", t, true);
        } else {
            std::printf("note: no \"fly\" in the recording (FlyKamiOracle.cs): the golems' own rows skipped\n");
        }
        const int id = e.id;
        g->onChoose = [this, id](FlyKamiProbe& self, bool after) {
            if (!after) {
                wasCharging[id] = self.isCharging;
                return;
            }
            if (!wasCharging[id] && self.isCharging) portEvents.push_back("prepare/" + std::to_string(id) + "/" + std::to_string(step));
        };
    }

    // MakeChargeAttack (its clip event): from where the game had the hero, its result compared.
    void OnAnimEvent(Replayed& e, const unity::AnimationEvent& ev, bool after) override
    {
        FlyKamiProbe* g = Of(e);
        if (!g || ev.function != "MakeChargeAttack") return;
        Tally& t = *tally;
        const int id = e.id;
        const std::string at = "frame " + std::to_string(frame) + " golem " + std::to_string(id);
        const Value* game = GameEvent("charge", id, after ? charges[id] - 1 : charges[id]);
        if (!after) {
            portEvents.push_back("charge/" + std::to_string(id) + "/" + std::to_string(step));
            charges[id]++;
            if (game) {
                c->setHeroPosition(V2((*game)["hero"]));
                unity::Random::SetState(State((*game)["rng0"]));
                t.Add("the hero's and the golem's z at a charge (the port's 2D transforms: 0)", Bits((*game)["heroZ"]) == 0.0f && Bits((*game)["posZ"]) == 0.0f,
                      at + ": hero z " + std::to_string(Bits((*game)["heroZ"])) + " golem z " + std::to_string(Bits((*game)["posZ"])));
                t.Add("the golem where the game had it at a charge", Same(Position(e), V2((*game)["pos"])),
                      at + ": port " + Str(Position(e)) + " game " + Str(V2((*game)["pos"])));
            }
            return;
        }
        if (!game) return;
        t.Add("MakeChargeAttack: bounds, chargePosition (the turn's bug), desiredDirection",
              SameRect(g->bounds, RectOf((*game)["bounds"])) && Same(XY(g->chargePosition), V2((*game)["cp"])) && Same(g->desiredDirection, V2((*game)["dd"])),
              at + ": port cp " + Str(XY(g->chargePosition)) + " dd " + Str(g->desiredDirection) + " bounds " + RectStr(g->bounds) + "; game cp " +
                  Str(V2((*game)["cp"])) + " dd " + Str(V2((*game)["dd"])) + " bounds " + RectStr(RectOf((*game)["bounds"])));
        t.Add("Random draws in MakeChargeAttack (the sound's, Range(-20, 21))", SameState(unity::Random::GetState(), State((*game)["rng1"])),
              at + ": " + std::to_string(unity::Random::DrawsBetween(State((*game)["rng0"]), unity::Random::GetState(), 64)) + " draws, the game " +
                  std::to_string(unity::Random::DrawsBetween(State((*game)["rng0"]), State((*game)["rng1"]), 64)));
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
            Replayed* e = c->ById(static_cast<int>(r[0].AsInt()));
            if (!e || e->Gone()) continue;
            FlyKamiProbe* g = Of(*e);
            const bool attackActive = g && g->Attack && g->Attack->activeSelf_;
            t.Add("root box enabled, attack child active at a fixed step's start", e->box->enabled_ == r[1].b && attackActive == r[2].b,
                  "frame " + std::to_string(fn) + " step " + std::to_string(k) + " golem " + std::to_string(e->id) + ": port " + std::to_string(e->box->enabled_) +
                      "/" + std::to_string(attackActive) + " game " + r.Dump());
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
                Replayed* e = c->ById(static_cast<int>(v["s"].AsInt()));
                const Value* fl = v.Find("fly");
                if (!e || !fl || v["gone"].b || e->Gone()) continue;
                FlyKamiProbe* g = Of(*e);
                if (!g) continue;
                const std::string who = at + " golem " + std::to_string(e->id);
                t.Add("golem (charging, returning, desiredDirection, angle)",
                      g->isCharging == (*fl)["charging"].b && g->isReturning == (*fl)["returning"].b && Same(g->desiredDirection, V2((*fl)["dd"])) &&
                          Same(g->angle, Bits((*fl)["angle"])),
                      who + ": port " + std::to_string(g->isCharging) + std::to_string(g->isReturning) + " dd " + Str(g->desiredDirection) + " angle " +
                          std::to_string(g->angle) + "; game " + std::to_string((*fl)["charging"].b) + std::to_string((*fl)["returning"].b) + " dd " +
                          Str(V2((*fl)["dd"])) + " angle " + std::to_string(Bits((*fl)["angle"])));
                t.Add("golem (bounds, chargePosition, enemyDirection)",
                      SameRect(g->bounds, RectOf((*fl)["bounds"])) && Same(XY(g->chargePosition), V2((*fl)["cp"])) &&
                          static_cast<int>(g->enemyDirection) == (*fl)["dir"].AsInt(),
                      who + ": port " + RectStr(g->bounds) + " cp " + Str(XY(g->chargePosition)) + "; game " + RectStr(RectOf((*fl)["bounds"])) + " cp " +
                          Str(V2((*fl)["cp"])));
                const bool attackActive = g->Attack && g->Attack->activeSelf_;
                t.Add("root box enabled, attack child active (frame end)", e->box->enabled_ == (*fl)["colOn"].b && attackActive == (*fl)["attackActive"].b,
                      who + ": port " + std::to_string(e->box->enabled_) + "/" + std::to_string(attackActive) + " game " + std::to_string((*fl)["colOn"].b) + "/" +
                          std::to_string((*fl)["attackActive"].b));
                t.Add("golem's Invokes pending (EndCharge, EnableCharge)",
                      g->IsInvoking(S("EndCharge")) == (*fl)["invEnd"].b && g->IsInvoking(S("EnableCharge")) == (*fl)["invEnable"].b, who);
                t.Add("golem's z (the port's 2D transforms: 0)", Bits((*fl)["z"]) == 0.0f, who + ": " + std::to_string(Bits((*fl)["z"])));
                CheckEnableSounds(*fl, who, t, false);
            }
        portEvents.clear();
        charges.clear();
        returns.clear();
        fev = nullptr;
    }
};

} // namespace
