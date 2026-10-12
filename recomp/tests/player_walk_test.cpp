// The hero walking, frame by frame against the game (data/oracle/walk.jsonl, the oracle's
// `-oracle-walk`): the port starts from the first recorded frame (position, Animator state
// and time, velocity, direction), then replays every later frame's deltaTime and stick
// through HeroMerchantController and its Animator against the room's colliders the game
// had, and compares his position (bit for bit), velocity, direction, Animator state, its
// normalized time and the sprite shown.
//
// The room's colliders come from the DungeonRoom prefab (prefabs.json) placed where the
// game's room is, its doors in the state generation left them (game/RoomDoors, rebuilt
// from the session's generation trace: the floor's rooms, their connections and special
// doors), each checked against the game's own (the recording's first lines), both ways.
// ML_WALK_DUMPED=1 uses the game's instead.
//
// Usage: player_walk_test [walk.jsonl] [hero_merchant.json] [trace.jsonl]
//        (ML_WALK_DEBUG=1: every frame; the trace defaults to the one next to walk.jsonl)

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "game/HeroMerchantController.h"
#include "game/Prefabs.h"
#include "game/RoomDatabase.h"
#include "game/RoomDoors.h"
#include "game/RoomPhysics.h"
#include "game/SceneTransform.h"
#include "game/Session.h"
#include "unity/Animator.h"
#include "unity/Physics2D.h"
#include "util/Json.h"

using json::Value;
using unity::Vector2;

namespace {

std::string ReadFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

float Bits(const Value& v)
{
    int32_t i = static_cast<int32_t>(v.AsInt());
    float f;
    std::memcpy(&f, &i, 4);
    return f;
}

Vector2 V2(const Value& v) { return {Bits(v[0]), Bits(v[1])}; }

// One room of the recorded floor and its doors.
struct FloorRoom {
    std::string name;
    Vector2 position;
    game::RoomDoors doors;
};

// The floor's doors as generation left them, from the session's generation trace: every
// room of the floor (its floorEnd snapshot) placed with its design's doors, then in the
// trace's order the connections ConnectRoomsByDirection made (rooms by name, neighbours by
// position), the init room's door and the special doors. Returns "" or what's missing.
std::string FloorDoors(const std::string& tracePath, const Value& walkStart, const std::string& data, std::vector<FloorRoom>& rooms)
{
    std::ifstream in(tracePath);
    if (!in) return "no " + tracePath;
    const std::string key = "{\"session\":\"" + walkStart["session"].s + "\",\"floor\":" + std::to_string(walkStart["floor"].AsInt()) + ",";
    std::vector<Value> recs;
    for (std::string line; std::getline(in, line);)
        if (line.rfind(key, 0) == 0) recs.push_back(json::Parse(line));
    const Value* begin = nullptr;
    const Value* end = nullptr;
    for (const Value& r : recs) {
        if (r["ev"].s == "floorBegin") begin = &r;
        if (r["ev"].s == "floorEnd") end = &r;
    }
    if (!begin || !end) return "the trace has no floor " + walkStart["session"].s + " #" + std::to_string(walkStart["floor"].AsInt());

    const auto culture = static_cast<game::Culture>((*begin)["culture"].AsInt());
    Value roomsJson = json::Parse(ReadFile(data + "/packs/" + game::Session::PackName(culture) + "/rooms.json"));
    auto* db = cs::gcnew<game::DungeonRoomDatabase>();
    db->LoadSpecialRooms(roomsJson);
    db->LoadFromJSON(roomsJson["database"]);
    auto info = [&](std::string name) -> game::DungeonRoomInfo* {
        if (name.size() > 7 && name.compare(name.size() - 7, 7, "_Shrine") == 0) name.resize(name.size() - 7);
        for (game::DungeonRoomInfo* i : {db->initRoomInfo, db->campRoomInfo, db->healingRoomInfo, db->waveRoomInfo, db->sendToHomeRoomInfo,
                                         db->wandererNestRoomInfo, db->abandonedItemsRoomInfo, db->specialForestRoomInfo})
            if (i && i->name == name) return i;
        for (game::DungeonRoomInfo* i : *db->database.rooms)
            if (i->name == name) return i;
        return nullptr;
    };
    for (const Value& o : (*end)["objects"].arr) {
        if (o[0].s.find('/') != std::string::npos) continue;
        game::DungeonRoomInfo* i = info(o[0].s);
        if (!i) continue; // (the boss room)
        FloorRoom room;
        room.name = o[0].s;
        room.position = {Bits(o[2]), Bits(o[3])};
        room.doors.Place(*i);
        rooms.push_back(room);
    }
    // A room by name; with a position, the one there.
    auto find = [&](const std::string& name, const Vector2* at) -> FloorRoom* {
        for (FloorRoom& r : rooms)
            if (r.name == name && (!at || (r.position.x == at->x && r.position.y == at->y))) return &r;
        return nullptr;
    };
    std::string special; // the room the next ReplaceSpecialDoor is for
    Vector2 specialAt;
    bool specialPlaced = false;
    for (const Value& r : recs) {
        if (r["ev"].s != "stage" || r["phase"].s != "pre") continue;
        const std::string& stage = r["name"].s;
        const Value* args = r.Find("args");
        if (!args) continue;
        if (stage == "DungeonGenerator.ConnectRoomsByDirection") {
            // The new room and a neighbour already placed: the pair of those names side by side.
            for (FloorRoom& a : rooms)
                for (FloorRoom& b : rooms) {
                    if (a.name != (*args)[0].s || b.name != (*args)[1].s) continue;
                    float dx = b.position.x - a.position.x, dy = b.position.y - a.position.y;
                    int side = dx == game::kRoomSizeX && dy == 0.0f    ? 3
                               : dx == -game::kRoomSizeX && dy == 0.0f ? 2
                               : dx == 0.0f && dy == game::kRoomSizeY  ? 0
                               : dx == 0.0f && dy == -game::kRoomSizeY ? 1
                                                                       : -1;
                    if (side >= 0) game::RoomDoors::Connect(a.doors, a.position, side, b.doors, b.position);
                }
        } else if (stage == "DungeonRoom.AddSpecialInitDoor") {
            if (FloorRoom* init = find("Init", nullptr)) {
                init->doors.isInit = true;
                init->doors.AddSpecialInitDoor(static_cast<int>((*args)[1].AsInt()));
            }
        } else if (stage == "DungeonGenerator.AddNextLevelDoor") {
            special = (*args)[1].s;
            specialPlaced = false;
        } else if (stage == "DungeonGenerator.AddBossRoom") { // (enter room, ..., the boss room's cell)
            special = (*args)[0].s;
            specialAt = game::RoomPosition(static_cast<int>((*args)[3].AsInt()), static_cast<int>((*args)[4].AsInt()) - 1);
            specialPlaced = true;
        } else if (stage == "DungeonRoom.ReplaceSpecialDoor") {
            int side = -1;
            for (int i = 0; i < 4; i++)
                if ("DungeonLevelDoors/" + (*args)[1].s == game::kDoorPaths[i]) side = i;
            if (FloorRoom* room = find(special, specialPlaced ? &specialAt : nullptr)) {
                room->doors.isEnd = true;
                room->doors.ReplaceSpecialDoor(side, (*args)[0].s.rfind("NextLevelDoor", 0) == 0);
            }
        }
    }
    return "";
}

} // namespace

int main(int argc, char** argv)
{
    std::string walkPath = argc > 1 ? argv[1] : "data/oracle/walk.jsonl";
    std::string data = std::getenv("ML_DATA") ? std::getenv("ML_DATA") : "data/extracted";
    std::string animPath = argc > 2 ? argv[2] : data + "/animators/hero_merchant.json";
    std::string tracePath = argc > 3 ? argv[3] : walkPath.substr(0, walkPath.find_last_of("/\\") + 1) + "trace.jsonl";
    bool debug = std::getenv("ML_WALK_DEBUG") != nullptr;

    std::ifstream in(walkPath);
    if (!in) {
        std::printf("no %s (record it with -oracle-walk)\n", walkPath.c_str());
        return 1;
    }
    Value start;
    unity::Physics2D dumped, physics;
    std::vector<Value> frames;
    for (std::string line; std::getline(in, line);) {
        if (line.empty()) continue;
        Value v = json::Parse(line);
        const std::string& ev = v["ev"].s;
        if (ev == "walkStart") start = v;
        else if (ev == "walk") frames.push_back(std::move(v));
        else if (ev == "collider" && v["type"].s == "BoxCollider2D") {
            unity::Collider2D c;
            c.path = v["path"].s;
            c.tag = v["tag"].s;
            c.layer = static_cast<int>(v["layer"].AsInt());
            c.enabled = v["enabled"].b;
            c.trigger = v["trigger"].b;
            c.position = V2(v["pos"]);
            c.scale = V2(v["scale"]);
            c.offset = V2(v["offset"]);
            c.size = V2(v["size"]);
            // Collider2D.bounds: the box through its object's rotation (the side doors are turned);
            // a disabled collider's are empty, at its object.
            c.hasWorldBox = c.enabled;
            c.worldMin = V2(v["min"]);
            c.worldMax = V2(v["max"]);
            if (c.path.rfind("HeroMerchant", 0) == 0) continue; // his own
            dumped.Add(c);
        }
    }
    if (frames.size() < 2) {
        std::printf("walk.jsonl has no frames\n");
        return 1;
    }

    // The room's colliders from the prefab, placed like the game's.
    game::PrefabLibrary::Load(json::Parse(ReadFile(data + "/prefabs.json")));
    const game::Prefab* roomPrefab = game::PrefabLibrary::Find("DungeonRoom", "dungeon_common");
    if (!roomPrefab) {
        std::printf("no DungeonRoom prefab in %s/prefabs.json\n", data.c_str());
        return 1;
    }
    std::vector<game::SceneRow> rows;
    for (const game::PrefabNode& n : roomPrefab->nodes) {
        game::SceneRow r;
        r.path = n.path;
        r.active = n.active;
        r.x = n.x;
        r.y = n.y;
        r.sx = n.sx;
        r.sy = n.sy;
        r.rotation = n.rotation;
        r.objectLayer = n.objectLayer;
        r.tag = n.tag;
        r.colliders = n.colliders;
        rows.push_back(r);
    }
    // Where the game's room is: its "Dungeon/<room>/" objects against the prefab's at the origin.
    std::string prefix;
    Vector2 roomPos;
    {
        unity::Physics2D atOrigin;
        game::AddRoomColliders(rows, {}, atOrigin);
        for (const unity::Collider2D& c : dumped.Colliders()) {
            size_t at = c.path.find("/DungeonLevelBoundaries/");
            if (c.path.rfind("Dungeon/", 0) != 0 || at == std::string::npos) continue;
            for (const unity::Collider2D& o : atOrigin.Colliders())
                if (o.path == c.path.substr(at + 1)) {
                    prefix = c.path.substr(0, at + 1);
                    roomPos = {c.position.x - o.position.x, c.position.y - o.position.y};
                }
            if (!prefix.empty()) break;
        }
    }
    if (prefix.empty()) {
        std::printf("the recording has no room boundaries\n");
        return 1;
    }
    // Its doors as the floor left them.
    std::vector<FloorRoom> floorRooms;
    std::string missing = FloorDoors(tracePath, start, data, floorRooms);
    if (!missing.empty()) {
        std::printf("%s: can't tell the room's doors\n", missing.c_str());
        return 1;
    }
    const FloorRoom* room = nullptr;
    for (const FloorRoom& r : floorRooms)
        if (r.position.x == roomPos.x && r.position.y == roomPos.y && prefix == "Dungeon/" + r.name + "/") room = &r;
    if (!room) {
        std::printf("the trace's floor has no room %s at %g, %g\n", prefix.c_str(), roomPos.x, roomPos.y);
        return 1;
    }
    game::ApplyDoors(rows, room->doors, roomPos);
    int built = game::AddRoomColliders(rows, roomPos, physics, prefix);
    int same = 0, differ = 0, doors = 0;
    for (const unity::Collider2D& c : physics.Colliders()) {
        doors += c.path.find("/DungeonLevelDoors/") != std::string::npos || c.path.find("/DoorBoundary") != std::string::npos;
        const unity::Collider2D* g = nullptr;
        for (const unity::Collider2D& d : dumped.Colliders())
            if (d.path == c.path) g = &d;
        // (a disabled one: where its object is, its offset and size)
        bool box = g && (g->enabled ? g->Min().x == c.Min().x && g->Min().y == c.Min().y && g->Max().x == c.Max().x && g->Max().y == c.Max().y
                                    : g->position.x == c.position.x && g->position.y == c.position.y && g->offset.x == c.offset.x &&
                                          g->offset.y == c.offset.y && g->size.x == c.size.x && g->size.y == c.size.y);
        bool ok = g && g->layer == c.layer && g->tag == c.tag && g->enabled == c.enabled && g->trigger == c.trigger && box;
        if (ok) same++;
        else {
            differ++;
            std::printf("collider %s: port [%g %g]-[%g %g] layer %d %s; game %s\n", c.path.c_str(), c.Min().x, c.Min().y, c.Max().x, c.Max().y,
                        c.layer, c.tag.c_str(), g ? (std::to_string(g->Min().x) + " " + std::to_string(g->Min().y) + " - " + std::to_string(g->Max().x) + " " + std::to_string(g->Max().y) + " layer " + std::to_string(g->layer) + " " + g->tag).c_str() : "none");
        }
    }
    // The game's doors and boundaries the port doesn't have.
    for (const unity::Collider2D& d : dumped.Colliders()) {
        if (d.path.rfind(prefix + "DungeonLevelDoors/", 0) != 0 && d.path.rfind(prefix + "DungeonLevelBoundaries/", 0) != 0) continue;
        bool there = false;
        for (const unity::Collider2D& c : physics.Colliders()) there |= c.path == d.path;
        if (!there) {
            differ++;
            std::printf("collider %s: the game has it, the port doesn't\n", d.path.c_str());
        }
    }
    if (std::getenv("ML_WALK_DUMPED")) {
        physics.Clear();
        for (const unity::Collider2D& c : dumped.Colliders()) physics.Add(c);
    }

    unity::AnimatorController controller = unity::AnimatorController::FromJson(json::Parse(ReadFile(animPath)));
    game::HeroMerchantController::Settings settings;
    settings.colliderSize = V2(start["colliderSize"]);
    settings.colliderOffset = V2(start["colliderOffset"]);
    settings.collisionLayers = static_cast<int>(start["collisionLayers"].AsInt());
    settings.slideLayers = static_cast<int>(start["slideLayers"].AsInt());
    if (const Value* g = start.Find("godModeLayers")) settings.godModeLayers = static_cast<int>(g->AsInt());
    if (const Value* c = start.Find("cooldown")) settings.abilityCooldown = Bits(*c);
    game::HeroMerchantController hero(controller, settings, physics);
    hero.currentPlayerMoveSpeed = Bits(start["moveSpeed"]);
    hero.speedFactor = Bits(start["speedFactor"]);

    // From the first frame as recorded.
    const Value& f0 = frames[0];
    hero.position = V2(f0["pos"]);
    hero.velX = Bits(f0["vel"][0]);
    hero.velY = Bits(f0["vel"][1]);
    hero.SetDirection(static_cast<game::HeroMerchantController::Direction>(f0["dir"].AsInt()));
    int s0 = controller.StateByHash(static_cast<int32_t>(f0["state"].AsInt()));
    if (s0 < 0) {
        std::printf("frame 0: state %lld isn't in the controller\n", static_cast<long long>(f0["state"].AsInt()));
        return 1;
    }
    hero.animator.Play(controller.states[static_cast<size_t>(s0)].name, Bits(f0["t"]));
    hero.animator.SetBool("Walk", controller.states[static_cast<size_t>(s0)].tag == "Walk");

    int checked = 0, posBad = 0, velBad = 0, stateBad = 0, timeBad = 0, dirBad = 0, spriteBad = 0, rollBad = 0, rollFrames = 0, shown = 0;
    double worstTime = 0.0;
    for (size_t i = 1; i < frames.size(); i++) {
        const Value& f = frames[i];
        game::HeroMerchantController::Input input;
        input.stick = V2(f["stick"]);
        input.roll = (f["in"][2].AsInt() & 16) != 0; // LeftTrigger pressed this frame
        hero.Update(Bits(f["dt"]), input);
        checked++;
        Vector2 pos = V2(f["pos"]);
        float vx = Bits(f["vel"][0]), vy = Bits(f["vel"][1]);
        unity::AnimatorStateInfo info = hero.animator.GetCurrentAnimatorStateInfo();
        int32_t wantState = static_cast<int32_t>(f["state"].AsInt());
        float wantT = Bits(f["t"]);
        const std::string* sprite = hero.animator.Sprite();
        const Value* wantSprite = f.Find("sprite");
        bool pb = pos.x != hero.position.x || pos.y != hero.position.y;
        bool vb = vx != hero.velX || vy != hero.velY;
        bool sb = wantState != info.shortNameHash;
        double dt = std::fabs(static_cast<double>(wantT) - info.normalizedTime);
        bool tb = !sb && dt > 1e-4;
        bool db = f["dir"].AsInt() != hero.heroDirection;
        bool spb = wantSprite && wantSprite->IsString() && (!sprite || *sprite != wantSprite->s);
        // The roll's state (recordings from -oracle-walk=roll on): intangible, the Animator's
        // speed, the ability in use, the speed factor (bit for bit).
        bool rb = false;
        if (const Value* in = f.Find("intangible")) {
            rollFrames++;
            rb = in->b != hero.isIntangible || Bits(f["animSpeed"]) != hero.animator.speed || f["abilityInUse"].b != hero.abilityInUse ||
                 Bits(f["speedFactor"]) != hero.speedFactor;
        }
        rollBad += rb;
        spb = spb || rb;
        if (!sb) worstTime = std::max(worstTime, dt);
        posBad += pb;
        velBad += vb;
        stateBad += sb;
        timeBad += tb;
        dirBad += db;
        spriteBad += spb;
        if (debug || ((pb || vb || sb || tb || db || spb) && shown < 12)) {
            if (!debug) shown++;
            int ps = controller.StateByHash(wantState);
            if (f.Find("intangible"))
                std::printf("  roll  game: intangible %d anim speed %g in use %d speed factor %.9g | port: %d %g %d %.9g\n", f["intangible"].b,
                            Bits(f["animSpeed"]), f["abilityInUse"].b, Bits(f["speedFactor"]), hero.isIntangible, hero.animator.speed,
                            hero.abilityInUse, hero.speedFactor);
            std::printf("frame %3lld%s dt %.5f stick (%.3f, %.3f)\n  game: pos (%.4f, %.4f) vel (%.3f, %.3f) %s t %.4f dir %lld sprite %s\n"
                        "  port: pos (%.4f, %.4f) vel (%.3f, %.3f) %s t %.4f dir %d sprite %s\n",
                        static_cast<long long>(f["f"].AsInt()), (pb || vb || sb || tb || db || spb) ? " MISMATCH" : "", Bits(f["dt"]),
                        Bits(f["stick"][0]), Bits(f["stick"][1]), pos.x, pos.y, vx, vy,
                        ps >= 0 ? controller.states[static_cast<size_t>(ps)].name.c_str() : "?", wantT,
                        static_cast<long long>(f["dir"].AsInt()), wantSprite && wantSprite->IsString() ? wantSprite->s.c_str() : "-",
                        hero.position.x, hero.position.y, hero.velX, hero.velY,
                        controller.states[static_cast<size_t>(hero.animator.CurrentState())].name.c_str(), info.normalizedTime,
                        hero.heroDirection, sprite ? sprite->c_str() : "-");
        }
    }
    std::printf("\nroom colliders from the prefab %5d match %5d differ   (room %s at %g, %g; %d of doors and their openings)\n", same,
                differ, room->name.c_str(), roomPos.x, roomPos.y, doors);
    std::printf("frames replayed                %5d\n", checked);
    std::printf("position (bit-exact)           %5d match %5d differ\n", checked - posBad, posBad);
    std::printf("velocity                       %5d match %5d differ\n", checked - velBad, velBad);
    std::printf("direction                      %5d match %5d differ\n", checked - dirBad, dirBad);
    std::printf("Animator state                 %5d match %5d differ\n", checked - stateBad, stateBad);
    std::printf("Animator normalized time       %5d match %5d differ   (worst %.2e)\n", checked - stateBad - timeBad, timeBad, worstTime);
    std::printf("sprite (and roll state)        %5d match %5d differ\n", checked - spriteBad, spriteBad);
    if (rollFrames)
        std::printf("roll state (intangible, Animator speed, in use, speed factor) %d match %d differ\n", rollFrames - rollBad, rollBad);
    (void)built;
    bool ok = !differ && !posBad && !velBad && !dirBad && !stateBad && !timeBad && !spriteBad;
    std::printf("\n%s\n", ok ? "ALL MATCH" : "MISMATCHES");
    return ok ? 0 : 1;
}
