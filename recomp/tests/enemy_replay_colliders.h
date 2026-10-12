#pragma once
// enemy_replay.h's part for what the HermitSlime brought (game/HermitSlime, game/EnemyColliders,
// game/HeroDamage), from what `-oracle-enemy` records of it (EnemyOracleColliders.cs); every
// row only where the recording has its fields:
//   - the start: the room floor into the scene (the patrol's tiles); per enemy the root box
//     (trigger flag against the prefab's, offset and size as the clips have them), the
//     hurtboxes against the prefab's, a HermitSlimeBehaviourNEW's patrol and look lock;
//   - each fixed step's start: the root box and the body's centre of mass;
//   - the events: DealDamageToPlayer's damage type (per step, against the port's hits);
//     DealDamageToEnemy through a hurtbox (replayed through the port's, so a hurtbox that
//     advises nobody drops it); "patrol" (ChooseNewDestination from the game's Random state:
//     the port's destination and draws); "drain" (DropRandomItems from the game's state, with
//     the game's count of droppable slots: the port's draws); "recvHit" (the hurtbox the game hit
//     is the port's, advising as the game's does); "weaponHit" (the tag of what the weapon hit,
//     informing);
//   - each frame's end: the root box, the body's mass and centre of mass, the hurtboxes, the
//     patrol and look lock.
// Included by enemy_replay.h inside its anonymous namespace's helpers (Tally, Bits, V2, Same).

#include <functional>

#include "game/HermitSlime.h"
#include "game/HeroDamage.h"

struct ColliderReplay {
    using EnemyById = std::function<game::Enemy*(int)>;
    std::vector<std::pair<int, int>> portTypes; // (enemy, HeroDamageType) of the port's hits this step

    static game::HermitSlimeBehaviourNEW* Hermit(game::Enemy& e) { return dynamic_cast<game::HermitSlimeBehaviourNEW*>(e.enemyBehaviour.get()); }

    static const game::EnemyDamageReceiver* Receiver(const game::Enemy& e, const std::string& path)
    {
        for (const game::EnemyDamageReceiver& r : e.damageReceivers)
            if (r.path == path) return &r;
        return nullptr;
    }

    static bool SameReceiver(const game::EnemyDamageReceiver& r, const Value& g)
    {
        return r.active == g["active"].b && r.layer == g["layer"].AsInt() && r.tag == g["tag"].s && (!g.Find("on") || (r.enabled == g["on"].b &&
               r.trigger == g["trigger"].b && Same(r.offset, V2(g["offset"])) && Same(r.size, V2(g["size"])))) && Same(r.position, V2(g["pos"])) &&
               (r.advise != nullptr) == g["advise"].b;
    }

    // The hurtboxes against the recording's ("receivers"): the same children, as they are.
    static void Receivers(Tally& t, const game::Enemy& e, const Value& list, const std::string& what, const std::string& at)
    {
        bool ok = list.size() == e.damageReceivers.size();
        for (const Value& g : list.arr) {
            const game::EnemyDamageReceiver* r = Receiver(e, g["path"].s);
            ok = ok && r && SameReceiver(*r, g);
        }
        t.Add(what, ok, at + ": port " + std::to_string(e.damageReceivers.size()) + " game " + list.Dump());
    }

    static void Scene(game::EnemyScene& scene, const Value& start)
    {
        if (const Value* p = start.Find("roomPos")) scene.roomPosition = V2(*p);
        if (const Value* f = start.Find("roomFloor")) {
            scene.roomFloorWidth = static_cast<int>((*f)[0].AsInt());
            scene.roomFloorHeight = static_cast<int>((*f)[1].AsInt());
            scene.roomCenterYOffset = static_cast<int>((*f)[2].AsInt());
        }
    }

    // An enemy as the recording starts it (before EnemyPhysics::Add).
    static void Restore(Tally& t, game::Enemy& e, const Value& v, int id)
    {
        const std::string at = "enemy " + std::to_string(id);
        if (const Value* trig = v.Find("rootTrigger")) t.Add("root box trigger (prefab)", e.colliderTrigger == trig->b, at);
        if (const Value* rb = v.Find("rootBox")) {
            e.colliderOffset = {Bits((*rb)[0]), Bits((*rb)[1])};
            e.colliderSize = {Bits((*rb)[2]), Bits((*rb)[3])};
        }
        if (const Value* list = v.Find("receivers")) Receivers(t, e, *list, "hurtboxes (prefab: path, box, layer, tag, advise)", at);
        const Value& be = v["behaviour"];
        if (game::HermitSlimeBehaviourNEW* h = Hermit(e)) {
            if (const Value* locked = be.Find("locked")) h->iDirectionLocked = locked->b;
            if (const Value* p = be.Find("patrol")) h->patrolLapse = Bits(*p);
            if (const Value* d = be.Find("dest")) h->destinyPosition = V2(*d);
            if (const Value* o = be.Find("gridOrigin")) h->roomGridOrigin = V2(*o);
            if (const Value* s = be.Find("gridSize")) h->roomGridSize = V2(*s);
        }
    }

    // A fixed step's start: the root boxes and centres of mass ("box").
    static void StepStart(Tally& t, const Value& st, const EnemyById& enemyById, long long fn, size_t k)
    {
        const Value* boxes = st.Find("box");
        if (!boxes) return;
        for (const Value& b : boxes->arr) {
            game::Enemy* e = enemyById(static_cast<int>(b[0].AsInt()));
            if (!e || e->destroyed || !e->body) continue;
            const std::string at = "frame " + std::to_string(fn) + " step " + std::to_string(k) + " enemy " + std::to_string(b[0].AsInt());
            t.Add("root box at a fixed step's start (offset, size)", Same(e->colliderOffset, {Bits(b[1]), Bits(b[2])}) && Same(e->colliderSize, {Bits(b[3]), Bits(b[4])}),
                  at + ": port " + Str(e->colliderOffset) + " " + Str(e->colliderSize) + " game " + Str({Bits(b[1]), Bits(b[2])}) + " " +
                      Str({Bits(b[3]), Bits(b[4])}));
            const b2Vec2 c = e->body->Body()->GetLocalCenter();
            t.Add("Rigidbody2D centre of mass at a fixed step's start", Same({c.x, c.y}, {Bits(b[5]), Bits(b[6])}),
                  at + ": port " + Str({c.x, c.y}) + " game " + Str({Bits(b[5]), Bits(b[6])}));
        }
    }

    // The hits on the hero in step k: their damage types against the game's.
    void StepTypes(Tally& t, const Value& events, size_t k, long long fn)
    {
        std::vector<std::pair<int, int>> theirs;
        bool typed = false;
        for (const Value& ev : events.arr) {
            if (ev["k"].s != "dmgToPlayer" || ev["step"].AsInt() != static_cast<long long>(k)) continue;
            if (const Value* type = ev.Find("type")) {
                typed = true;
                theirs.emplace_back(static_cast<int>(ev["s"].AsInt()), static_cast<int>(type->AsInt()));
            }
        }
        if (typed || !portTypes.empty()) {
            std::vector<std::pair<int, int>> mine = portTypes;
            std::sort(mine.begin(), mine.end());
            std::sort(theirs.begin(), theirs.end());
            std::string which = "frame " + std::to_string(fn) + " step " + std::to_string(k) + ": port";
            for (const auto& h : mine) which += " " + std::to_string(h.first) + "/" + std::to_string(h.second);
            which += ", game";
            for (const auto& h : theirs) which += " " + std::to_string(h.first) + "/" + std::to_string(h.second);
            if (typed) t.Add("DealDamageToPlayer's damage type (ItemDrain: 1)", mine == theirs, which);
        }
        portTypes.clear();
    }

    // A weapon hit (DealDamageToEnemy) as the game had it: through the hurtbox it names, if any.
    static void DealDamage(game::Enemy& e, const Value& ev)
    {
        const float strength = Bits(ev["str"]);
        const auto type = static_cast<game::EnemyDamageReceivedType>(ev["type"].AsInt());
        const Value* recv = ev.Find("recv");
        const game::EnemyDamageReceiver* r = recv && recv->IsString() ? Receiver(e, recv->s) : nullptr;
        if (r) r->DealDamageToEnemy(strength, type);
        else e.DealDamageToEnemy(strength, type);
    }

    // The frame's events that need no step: the patrol, the ItemDrain draws, the hurtbox and
    // weapon hits (from the game's Random states; the state is put back after).
    static void FrameEvents(Tally& t, const Value& events, const EnemyById& enemyById, long long fn)
    {
        const unity::Random::State saved = unity::Random::GetState();
        for (const Value& ev : events.arr) {
            const std::string& kind = ev["k"].s;
            const std::string at = "frame " + std::to_string(fn) + " enemy " + std::to_string(ev["s"].AsInt());
            game::Enemy* e = enemyById(static_cast<int>(ev["s"].AsInt()));
            if (kind == "patrol" && e) {
                game::HermitSlimeBehaviourNEW* h = Hermit(*e);
                if (!h) continue;
                // ChooseNewDestination from the game's state, the grid as the port has it now.
                unity::Random::SetState(State(ev["rng0"]));
                const game::EnemyScene& room = e->scene;
                Vector2 origin = h->roomGridOrigin, size = h->roomGridSize;
                if (size.x == 0.0f) {
                    size = {static_cast<float>(room.roomFloorWidth), static_cast<float>(room.roomFloorHeight)};
                    origin = (room.roomPosition + Vector2{0.0f, static_cast<float>(room.roomCenterYOffset)}) - size * 0.5f;
                }
                const int x = unity::Random::Range(0, static_cast<int>(size.x));
                const int y = unity::Random::Range(0, static_cast<int>(size.y));
                const Vector2 dest = Vector2{static_cast<float>(x), static_cast<float>(y)} + origin;
                t.Add("patrol: ChooseNewDestination from the game's Random state (destination, draws)",
                      Same(dest, V2(ev["dest"])) && SameState(unity::Random::GetState(), State(ev["rng1"])),
                      at + ": port " + Str(dest) + " game " + Str(V2(ev["dest"])));
                t.Add("patrol: the port's lapse and destination after the Update", Same(h->patrolLapse, Bits(ev["lapse"])) && Same(h->destinyPosition, V2(ev["dest"])),
                      at + ": port " + std::to_string(h->patrolLapse) + " " + Str(h->destinyPosition) + " game " + std::to_string(Bits(ev["lapse"])) + " " +
                          Str(V2(ev["dest"])));
            } else if (kind == "drain") {
                unity::Random::SetState(State(ev["rng0"]));
                std::vector<int> slots;
                for (int i = 0; i < static_cast<int>(ev["eligible"].AsInt()); i++) slots.push_back(i);
                game::DropRandomItemsDraws(slots, static_cast<int>(ev["number"].AsInt()));
                t.Add("ItemDrain: DropRandomItems' draws (the game's droppable slots)", SameState(unity::Random::GetState(), State(ev["rng1"])),
                      "frame " + std::to_string(fn) + ": " + std::to_string(ev["eligible"].AsInt()) + " slots, the game drew " +
                          std::to_string(unity::Random::DrawsBetween(State(ev["rng0"]), State(ev["rng1"]), 64)));
            } else if (kind == "recvHit" && e) {
                const game::EnemyDamageReceiver* r = Receiver(*e, ev["path"].s);
                t.Add("weapon hit through a hurtbox: the port's, advising as the game's", r && (r->advise != nullptr) == ev["advise"].b,
                      at + ": " + ev["path"].s + (ev["advise"].b ? " advises" : " advises nobody"));
            } else if (kind == "weaponHit" && e) {
                const std::string& other = ev["other"].s;
                const game::EnemyDamageReceiver* r = Receiver(*e, other);
                const std::string tag = r ? r->tag : e->tag;
                t.Info("weapon hit: the tag of what it hit (root or hurtbox)", tag == ev["tag"].s, at + ": " + other + " port " + tag + " game " + ev["tag"].s);
            }
        }
        unity::Random::SetState(saved);
    }

    // The frame's end, per enemy.
    static void End(Tally& t, game::Enemy& e, const Value& v, const std::string& at)
    {
        if (const Value* rb = v.Find("rootBox"))
            t.Add("root box (offset, size)", Same(e.colliderOffset, {Bits((*rb)[0]), Bits((*rb)[1])}) && Same(e.colliderSize, {Bits((*rb)[2]), Bits((*rb)[3])}),
                  at + ": port " + Str(e.colliderOffset) + " " + Str(e.colliderSize) + " game " + rb->Dump());
        if (const Value* com = v.Find("com"))
            if (e.body) {
                const b2Vec2 c = e.body->Body()->GetLocalCenter();
                t.Add("Rigidbody2D mass, centre of mass", Same({c.x, c.y}, V2(*com)) && Same(e.body->Body()->GetMass(), Bits(v["mass"])),
                      at + ": port " + Str({c.x, c.y}) + " mass " + std::to_string(e.body->Body()->GetMass()) + " game " + Str(V2(*com)) + " mass " +
                          std::to_string(Bits(v["mass"])));
            }
        if (const Value* list = v.Find("receivers")) Receivers(t, e, *list, "hurtboxes (box, active, layer, position)", at);
        const Value& be = v["behaviour"];
        if (game::HermitSlimeBehaviourNEW* h = Hermit(e))
            if (be.Find("patrol"))
                t.Add("HermitSlime (patrolLapse, destinyPosition, grid, look lock)",
                      Same(h->patrolLapse, Bits(be["patrol"])) && Same(h->destinyPosition, V2(be["dest"])) && Same(h->roomGridOrigin, V2(be["gridOrigin"])) &&
                          Same(h->roomGridSize, V2(be["gridSize"])) && h->iDirectionLocked == be["locked"].b,
                      at + ": port lapse " + std::to_string(h->patrolLapse) + " dest " + Str(h->destinyPosition) + " game " +
                          std::to_string(Bits(be["patrol"])) + " " + Str(V2(be["dest"])));
    }
};
