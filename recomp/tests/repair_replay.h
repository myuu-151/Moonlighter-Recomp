#pragma once
// The FlyingRepairGolem's part of enemy_test's replay (tests/enemy_replay.h's ReplayHooks), for
// enemy_FlyingRepairGolem.jsonl (`-oracle-enemy=FlyingRepairGolem`; EnemyOracleRepair.cs writes
// the fields read here). Items on the floor aren't ported: the room's drop list (game/RoomDrops.h)
// is fed from the recording, and what the golems do with it is compared:
//   - the scene gets the room's drop list, its items as the start has them ("repairItems",
//     "drops"); OnDropListChanged's handlers as the game had them ("dropSubs", in order: the
//     replay's own StartTargetingPlayerAfterTime calls are undone);
//   - each golem from its recorded fields ("repair": patrolLapse, moveDirection, myPos, wasHit,
//     velocityPercent, its target and what it carries, the item sprite, its transform's z);
//   - what changes the list from outside the golems, where the game had it ("rev"): AddDropItems
//     (in the fixed step it came in, after it; in the frame's part before the golem Update it came
//     before, else after the Updates; an enemy's DropItemsOnDeath where the port destroys it),
//     the hero's pick-ups (OnHeroPickedItem), RemoveDropItem by others; the golems' own
//     RemoveDropItem (their pick) is the port's;
//   - each handler call (OnItemDroppedInRoom), per golem in order: from the game's Random state
//     before it (resynced), the list it sees, its draws and the target it took;
//   - each golem's Update from where the game had its target item then ("upd": position,
//     isActiveAndEnabled) and the hero's and its own z; the target it has, compared;
//   - each OnTriggerEnter2D the game's golems had ("trigger", in its fixed step), given to the
//     port's: whether it's the target's object, compared;
//   - DropAllStolenItems' ItemStack.Drop calls (item, where, radius) against the game's, its
//     Random draw (the drop's direction, not ported) taken from the game's states;
//   - each frame's end: the golems' fields, the room's list and its handlers, the items the port
//     knows (active, carried by whom).

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "enemy_replay.h"
#include "game/FlyingRepairGolem.h"
#include "game/RoomDrops.h"

namespace {

struct RepairReplay : ReplayHooks {
    game::RoomDrops drops;
    std::map<int, std::unique_ptr<game::DroppedItem>> items;
    std::map<int, game::Enemy*> golems;
    Tally* tally = nullptr;
    long long frame = -1;
    const Value* rev = nullptr;
    // The frame's game events per golem (handler calls, Updates) and what's been used of them.
    std::map<int, std::vector<const Value*>> gameCalls, gameUpds;
    std::map<int, size_t> usedCalls, usedUpds;
    // The frame's outside changes to the list, and whether each has been applied.
    std::vector<const Value*> outside;
    std::vector<bool> applied;
    int updsStarted = 0;
    bool subsSet = false;
    int resyncs = 0;
    // The port's drops (DropAllStolenItems) this frame: (item id, where, radius, in the room).
    struct PortDrop {
        int item;
        Vector2 at;
        float radius;
        bool inRoom;
    };
    std::vector<PortDrop> portDrops;
    const Value* pendingCall = nullptr; // the game's handler call matched to the port's running one

    game::DroppedItem* Item(int id)
    {
        if (id < 0) return nullptr;
        auto& p = items[id];
        if (!p) {
            p = std::make_unique<game::DroppedItem>();
            p->id = id;
        }
        return p.get();
    }
    static int IdOf(const game::DroppedItem* item) { return item ? item->id : -1; }
    int GolemId(const void* key) const
    {
        for (const auto& [id, e] : golems)
            if (e->enemyBehaviour.get() == key) return id;
        return -1;
    }
    game::FlyingRepairGolemBehaviour* Golem(int id) const
    {
        const auto it = golems.find(id);
        return it == golems.end() ? nullptr : dynamic_cast<game::FlyingRepairGolemBehaviour*>(it->second->enemyBehaviour.get());
    }
    // An "items" row: [id, x, y, z, active, enabled, destroyed, parent golem, sprite, master].
    void SetItem(const Value& row)
    {
        game::DroppedItem* it = Item(static_cast<int>(row[0].AsInt()));
        it->position = {Bits(row[1]), Bits(row[2])};
        it->active = row[4].b;
        it->enabled = row[5].b;
        it->destroyed = row[6].b;
        const int parent = static_cast<int>(row[7].AsInt());
        it->parent = parent >= 0 && golems.count(parent) ? golems[parent] : nullptr;
        it->sprite = row[8].IsString() ? row[8].s : "";
        it->item = row[9].IsString() ? row[9].s : "";
    }
    std::vector<int> PortList()
    {
        std::vector<int> ids;
        for (const game::DroppedItem* i : drops.GetDropList()) ids.push_back(IdOf(i));
        return ids;
    }
    static std::vector<int> Ids(const Value& v)
    {
        std::vector<int> ids;
        for (const Value& x : v.arr) ids.push_back(static_cast<int>(x.AsInt()));
        return ids;
    }
    static std::string IdsStr(const std::vector<int>& ids)
    {
        std::string s = "[";
        for (size_t k = 0; k < ids.size(); k++) s += (k ? " " : "") + std::to_string(ids[k]);
        return s + "]";
    }

    void OnScene(const Value& start, game::EnemyScene& scene, game::EnemyHero&, unity::PhysicsWorld&, game::EnemyPhysics&, unity::Physics2D&,
                 unity::Rigidbody2D&, Tally& t) override
    {
        tally = &t;
        scene.drops = &drops;
        if (const Value* list = start.Find("repairItems"))
            for (const Value& row : list->arr) SetItem(row);
        if (const Value* d = start.Find("drops")) {
            std::vector<game::DroppedItem*> list;
            for (const Value& id : d->arr) list.push_back(Item(static_cast<int>(id.AsInt())));
            drops.GetDropList() = list;
        }
        // DropAllStolenItems' drops: the item's Random.Range(0, 360) from the game's states.
        drops.drop = [this](game::DroppedItem& item, Vector2 at, float radius, bool inRoom) {
            portDrops.push_back({item.id, at, radius, inRoom});
            if (!rev) return;
            for (const Value& ev : rev->arr)
                if (ev["k"].s == "drop" && ev["item"].AsInt() == item.id) {
                    unity::Random::SetState(State(ev["rng1"]));
                    break;
                }
        };
        // The handlers: each call against the game's, in order per golem.
        drops.onHandler = [this](const void* key, bool after) {
            const int id = GolemId(key);
            const std::string at = "frame " + std::to_string(frame) + " golem " + std::to_string(id);
            if (!after) {
                pendingCall = nullptr;
                const auto g = gameCalls.find(id);
                size_t& k = usedCalls[id];
                if (g == gameCalls.end() || k >= g->second.size()) {
                    tally->Add("OnItemDroppedInRoom calls (per golem, in order)", false, at + ": the port's call, none in the game");
                    return;
                }
                pendingCall = g->second[k++];
                const Value& ev = *pendingCall;
                tally->Add("OnItemDroppedInRoom calls (per golem, in order)", true);
                tally->Add("OnItemDroppedInRoom: the list it sees (ids)", PortList() == Ids(ev["ids"]),
                           at + ": port " + IdsStr(PortList()) + " game " + IdsStr(Ids(ev["ids"])));
                if (!SameState(unity::Random::GetState(), State(ev["rng0"]))) {
                    resyncs++;
                    unity::Random::SetState(State(ev["rng0"]));
                }
                return;
            }
            if (!pendingCall) return;
            const Value& ev = *pendingCall;
            game::FlyingRepairGolemBehaviour* g = Golem(id);
            tally->Add("OnItemDroppedInRoom: Random draws (GetRandom)", SameState(unity::Random::GetState(), State(ev["rng1"])),
                       at + ": " + std::to_string(unity::Random::DrawsBetween(State(ev["rng0"]), unity::Random::GetState(), 16)) + " draws, the game " +
                           std::to_string(unity::Random::DrawsBetween(State(ev["rng0"]), State(ev["rng1"]), 16)));
            tally->Add("OnItemDroppedInRoom: the target it takes", g && IdOf(g->targetItem) == ev["target"].AsInt(),
                       at + ": port " + std::to_string(g ? IdOf(g->targetItem) : -9) + " game " + std::to_string(ev["target"].AsInt()));
            unity::Random::SetState(State(ev["rng1"]));
            pendingCall = nullptr;
        };
        // An enemy's DropItemsOnDeath: the game's AddDropItems from it, where the port destroys it.
        scene.dropItemsOnDeath = [this](game::Enemy& e) {
            int id = -1;
            for (const auto& [i, g] : golems)
                if (g == &e) id = i;
            for (size_t k = 0; k < outside.size(); k++)
                if (!applied[k] && (*outside[k])["k"].s == "add" && (*outside[k])["by"].AsInt() == id) Apply(k);
        };
    }

    void OnEnemy(game::Enemy& e, int id, const Value& line, Tally&) override
    {
        golems[id] = &e;
        auto* g = dynamic_cast<game::FlyingRepairGolemBehaviour*>(e.enemyBehaviour.get());
        const Value* r = line.Find("repair");
        if (!g || !r) return;
        g->patrolLapse = Bits((*r)["lapse"]);
        g->moveDirection = {Bits((*r)["md"][0]), Bits((*r)["md"][1]), Bits((*r)["md"][2])};
        g->myPos = V2((*r)["myPos"]);
        g->wasHit = (*r)["wasHit"].b;
        g->velocityPercent = Bits((*r)["vp"]);
        g->targetItem = Item(static_cast<int>((*r)["target"].AsInt()));
        g->stolenItems.clear();
        for (const Value& s : (*r)["stolen"].arr) {
            g->stolenItems.push_back(Item(static_cast<int>(s.AsInt())));
            g->stolenItems.back()->parent = &e;
        }
        g->itemSprite = (*r)["itemSprite"].IsString() ? (*r)["itemSprite"].s : "";
        g->transformZ = Bits((*r)["z"]);
        g->beforeUpdate = [this, id](game::FlyingRepairGolemBehaviour& b) { BeforeUpdate(id, b); };
    }

    // A change from outside: applied now.
    void Apply(size_t k)
    {
        applied[k] = true;
        const Value& ev = *outside[k];
        const std::string& kind = ev["k"].s;
        if (kind == "add") {
            std::vector<game::DroppedItem*> list;
            for (size_t i = 0; i < ev["ids"].size(); i++) {
                game::DroppedItem* it = Item(static_cast<int>(ev["ids"][i].AsInt()));
                it->position = V2(ev["pos"][i]);
                list.push_back(it);
            }
            drops.AddDropItems(list);
        } else if (kind == "heroPick") {
            drops.OnHeroPickedItem(Item(static_cast<int>(ev["item"].AsInt())));
        } else if (kind == "remove") {
            drops.RemoveDropItem(Item(static_cast<int>(ev["item"].AsInt())));
        }
    }

    void BeforeUpdate(int id, game::FlyingRepairGolemBehaviour& g)
    {
        // The frame's outside changes that came before this Update.
        for (size_t k = 0; k < outside.size(); k++) {
            const Value& ev = *outside[k];
            if (applied[k] || ev["step"].AsInt() >= 0 || (ev["k"].s == "add" && ev["by"].AsInt() >= 0)) continue;
            if (ev["upd"].AsInt() <= updsStarted) Apply(k);
        }
        updsStarted++;
        const auto u = gameUpds.find(id);
        size_t& k = usedUpds[id];
        if (u == gameUpds.end() || k >= u->second.size()) return;
        const Value& ev = *u->second[k++];
        g.transformZ = Bits(ev["z"]);
        g.heroZ = Bits(ev["heroZ"]);
        const int target = static_cast<int>(ev["target"].AsInt());
        if (game::DroppedItem* it = target >= 0 ? Item(target) : nullptr) {
            it->position = V2(ev["pos"]);
            it->active = ev["on"].b;
            it->enabled = true;
            it->destroyed = false;
        }
        tally->Add("the target item at Update (id)", IdOf(g.targetItem) == target || (target == -2 && !game::Alive(g.targetItem)),
                   "frame " + std::to_string(frame) + " golem " + std::to_string(id) + ": port " + std::to_string(IdOf(g.targetItem)) + " game " +
                       std::to_string(target));
    }

    void BeginFrame(long long fn, const Value& f) override
    {
        frame = fn;
        rev = f.Find("rev");
        gameCalls.clear();
        gameUpds.clear();
        usedCalls.clear();
        usedUpds.clear();
        outside.clear();
        applied.clear();
        portDrops.clear();
        updsStarted = 0;
        if (!subsSet && tally) {
            // OnDropListChanged as the game had it at the start (the replay's own
            // StartTargetingPlayerAfterTime subscribed them again).
            subsSet = true;
            for (const auto& [id, e] : golems)
                while (drops.Subscriptions(e->enemyBehaviour.get()) > 0) drops.Unsubscribe(e->enemyBehaviour.get());
            if (startSubs)
                for (const Value& id : startSubs->arr)
                    if (game::FlyingRepairGolemBehaviour* g = Golem(static_cast<int>(id.AsInt())))
                        drops.Subscribe(g, [g](game::RoomDrops::List& l) { g->OnItemDroppedInRoom(l); });
        }
        if (!rev) return;
        for (const Value& ev : rev->arr) {
            const std::string& k = ev["k"].s;
            const int s = static_cast<int>(ev["s"].AsInt());
            if (k == "dropList") gameCalls[s].push_back(&ev);
            else if (k == "upd") gameUpds[s].push_back(&ev);
            else if (k == "add" || k == "heroPick" ||
                     (k == "remove" && !(ev["caller"].IsString() && ev["caller"].s.find("PickItemCollisionedAndRemoveItFromRoom") != std::string::npos))) {
                outside.push_back(&ev);
                applied.push_back(false);
            }
        }
    }
    const Value* startSubs = nullptr;
    void OnEnemiesBuilt(const Value& start, Tally&) override { startSubs = start.Find("dropSubs"); }

    void AfterStep(long long, size_t k, const Value&, Tally& t) override
    {
        if (!rev) return;
        // The golems' OnTriggerEnter2D in this step, then the outside changes in it.
        for (const Value& ev : rev->arr) {
            if (ev["k"].s != "trigger" || ev["step"].AsInt() != static_cast<long long>(k)) continue;
            game::FlyingRepairGolemBehaviour* g = Golem(static_cast<int>(ev["s"].AsInt()));
            if (!g) continue;
            const int item = static_cast<int>(ev["item"].AsInt());
            game::DroppedItem* it = item >= 0 ? Item(item) : nullptr;
            const bool isTarget = game::Alive(g->targetItem) && g->targetItem == it;
            t.Add("OnTriggerEnter2D: the target's object (as the game's)", isTarget == ev["isTarget"].b,
                  "frame " + std::to_string(frame) + " golem " + std::to_string(ev["s"].AsInt()) + " with " + (ev["other"].IsString() ? ev["other"].s : "?"));
            g->OnTriggerEnter2D(ev["tag"].IsString() ? ev["tag"].s : "", it);
        }
        for (size_t n = 0; n < outside.size(); n++) {
            const Value& ev = *outside[n];
            if (!applied[n] && ev["step"].AsInt() == static_cast<long long>(k) && !(ev["k"].s == "add" && ev["by"].AsInt() >= 0)) Apply(n);
        }
    }

    void BeforeAnimators(float) override
    {
        // The rest of the frame's part: after the Updates (the oracle's scripted drops, the
        // room's CleanDropList coroutine).
        for (size_t k = 0; k < outside.size(); k++) {
            const Value& ev = *outside[k];
            if (!applied[k] && ev["step"].AsInt() < 0 && !(ev["k"].s == "add" && ev["by"].AsInt() >= 0)) Apply(k);
        }
        drops.CleanDropList();
    }

    void AtFrameEnd(long long fn, const Value& f, Tally& t) override
    {
        const std::string at = "frame " + std::to_string(fn);
        for (size_t k = 0; k < outside.size(); k++)
            if (!applied[k]) {
                t.Add("outside changes to the drop list applied", false, at + ": " + (*outside[k])["k"].s + " never applied (its enemy not destroyed?)");
                Apply(k);
            }
        for (const auto& [id, calls] : gameCalls)
            t.Add("OnItemDroppedInRoom calls (per golem, in order)", usedCalls[id] >= calls.size(),
                  at + " golem " + std::to_string(id) + ": " + std::to_string(calls.size() - std::min(calls.size(), usedCalls[id])) +
                      " of the game's calls not made by the port");
        // DropAllStolenItems' drops.
        std::vector<PortDrop> gameDrops;
        if (rev)
            for (const Value& ev : rev->arr) {
                if (ev["k"].s != "drop") continue;
                const int item = static_cast<int>(ev["item"].AsInt());
                bool stolen = false;
                for (const PortDrop& p : portDrops) stolen = stolen || p.item == item;
                if (stolen) gameDrops.push_back({item, V2(ev["pos"]), Bits(ev["radius"]), ev["inRoom"].b});
            }
        if (!portDrops.empty() || !gameDrops.empty()) {
            bool same = portDrops.size() == gameDrops.size();
            for (size_t k = 0; same && k < portDrops.size(); k++)
                same = portDrops[k].item == gameDrops[k].item && Same(portDrops[k].at, gameDrops[k].at) && Same(portDrops[k].radius, gameDrops[k].radius) &&
                       portDrops[k].inRoom == gameDrops[k].inRoom;
            t.Add("DropAllStolenItems: ItemStack.Drop (item, where, radius)", same,
                  at + ": port " + std::to_string(portDrops.size()) + " game " + std::to_string(gameDrops.size()));
        }
        // The golems.
        if (const Value* list = f.Find("enemies"))
            for (const Value& v : list->arr) {
                const int id = static_cast<int>(v["s"].AsInt());
                game::FlyingRepairGolemBehaviour* g = Golem(id);
                const Value* r = v.Find("repair");
                if (!g || !r || (v.Find("gone") && v["gone"].b)) continue;
                const std::string who = at + " golem " + std::to_string(id);
                const bool md = Same(g->moveDirection.x, Bits((*r)["md"][0])) && Same(g->moveDirection.y, Bits((*r)["md"][1])) &&
                                Same(g->moveDirection.z, Bits((*r)["md"][2]));
                t.Add("golem: patrolLapse, moveDirection, myPos, wasHit",
                      Same(g->patrolLapse, Bits((*r)["lapse"])) && md && Same(g->myPos, V2((*r)["myPos"])) && g->wasHit == (*r)["wasHit"].b,
                      who + ": port lapse " + std::to_string(g->patrolLapse) + " md (" + std::to_string(g->moveDirection.x) + ", " +
                          std::to_string(g->moveDirection.y) + ") wasHit " + std::to_string(g->wasHit) + "; game lapse " +
                          std::to_string(Bits((*r)["lapse"])) + " md (" + std::to_string(Bits((*r)["md"][0])) + ", " + std::to_string(Bits((*r)["md"][1])) +
                          ") wasHit " + std::to_string((*r)["wasHit"].b));
                std::vector<int> stolen;
                for (const game::DroppedItem* s : g->stolenItems) stolen.push_back(IdOf(s));
                const int target = static_cast<int>((*r)["target"].AsInt());
                t.Add("golem: target, stolen items, item sprite",
                      (IdOf(g->targetItem) == target || (target == -2 && !game::Alive(g->targetItem))) && stolen == Ids((*r)["stolen"]) &&
                          g->itemSprite == ((*r)["itemSprite"].IsString() ? (*r)["itemSprite"].s : ""),
                      who + ": port target " + std::to_string(IdOf(g->targetItem)) + " stolen " + IdsStr(stolen) + " sprite " + g->itemSprite +
                          "; game " + std::to_string(target) + " " + IdsStr(Ids((*r)["stolen"])));
                t.Add("golem: OnDropListChanged subscriptions", drops.Subscriptions(g) == (*r)["subs"].AsInt(),
                      who + ": port " + std::to_string(drops.Subscriptions(g)) + " game " + std::to_string((*r)["subs"].AsInt()));
            }
        if (const Value* d = f.Find("drops"))
            t.Add("the room's drop list at the frame's end (ids)", PortList() == Ids(*d), at + ": port " + IdsStr(PortList()) + " game " + IdsStr(Ids(*d)));
        // The items the port knows against the game's (then the game's state taken).
        if (const Value* list = f.Find("items"))
            for (const Value& row : list->arr) {
                const int id = static_cast<int>(row[0].AsInt());
                const auto it = items.find(id);
                if (it != items.end() && it->second) {
                    const game::DroppedItem& i = *it->second;
                    int parent = -1;
                    for (const auto& [gid, e] : golems)
                        if (i.parent == e) parent = gid;
                    const bool knownByGolems = parent >= 0 || static_cast<int>(row[7].AsInt()) >= 0;
                    if (knownByGolems)
                        t.Add("items the golems carry (active, under which golem)", i.active == row[4].b && parent == row[7].AsInt(),
                              at + " item " + std::to_string(id) + ": port " + (i.active ? "on" : "off") + " under " + std::to_string(parent) + "; game " +
                                  (row[4].b ? "on" : "off") + " under " + std::to_string(row[7].AsInt()));
                }
                SetItem(row);
            }
    }

    void Summary() const
    {
        if (resyncs) std::printf("Random resynced before an OnItemDroppedInRoom %d times\n", resyncs);
    }
};

} // namespace
