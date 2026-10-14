#pragma once
// The walk replays' common part: the recording (data/oracle/walk*.jsonl, the oracle's
// `-oracle-walk`), the room's colliders built as the game's (the DungeonRoom prefab placed where
// the game's room is, its doors in the state generation left them: game/RoomDoors from the
// session's generation trace), checked against the game's own (the recording's first lines), the
// colliders his body steps among in Box2D, and the rows every frame is compared on. Two drivers
// replay the hero on it: player_walk_test (the hand port, game::HeroMerchantController) and
// translated/player_translated_test (the game's own code, translated): the same rows, the same
// pass criterion.

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "game/Prefabs.h"
#include "game/RoomDatabase.h"
#include "game/RoomDoors.h"
#include "game/RoomPhysics.h"
#include "game/SceneTransform.h"
#include "game/Session.h"
#include "unity/Animator.h"
#include "unity/Physics2D.h"
#include "unity/PhysicsWorld.h"
#include "unity/PolygonCollider2D.h"
#include "util/Json.h"

namespace walkreplay {

using json::Value;
using unity::Vector2;

// Where the replay's lines go: stdout, or a buffer (a replay that may run itself again prints only
// the run that finishes: the translated one's ties).
inline std::string*& PrintBuffer()
{
    static std::string* buffer = nullptr;
    return buffer;
}
inline void Print(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    if (std::string* b = PrintBuffer()) {
        va_list copy;
        va_copy(copy, args);
        const int n = std::vsnprintf(nullptr, 0, format, copy);
        va_end(copy);
        if (n > 0) {
            std::string line(static_cast<size_t>(n) + 1, '\0');
            std::vsnprintf(&line[0], line.size(), format, args);
            line.resize(static_cast<size_t>(n));
            *b += line;
        }
    } else {
        std::vprintf(format, args);
    }
    va_end(args);
}

inline std::string ReadFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

inline float Bits(const Value& v)
{
    int32_t i = static_cast<int32_t>(v.AsInt());
    float f;
    std::memcpy(&f, &i, 4);
    return f;
}

inline Vector2 V2(const Value& v) { return {Bits(v[0]), Bits(v[1])}; }

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
inline std::string FloorDoors(const std::string& tracePath, const Value& walkStart, const std::string& data, std::vector<FloorRoom>& rooms)
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

// The recording: walkStart, the game's colliders, the frames.
struct Recording {
    Value start;
    unity::Physics2D dumped;
    std::vector<std::pair<std::string, std::string>> bodies; // path -> its Rigidbody2D's type, where recorded
    std::vector<Value> frames;
    std::vector<unity::Collider2D> holes; // the room's hole colliders (their outlines, as tiles)

    // 0, or the exit code (what's wrong printed).
    int Load(const std::string& walkPath)
    {
        std::ifstream in(walkPath);
        if (!in) {
            Print("no %s (record it with -oracle-walk)\n", walkPath.c_str());
            return 1;
        }
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
                if (const Value* b = v.Find("body")) bodies.emplace_back(c.path, b->s);
                dumped.Add(c);
            } else if (ev == "collider" && v["type"].s == "PolygonCollider2D" && v["tag"].s == "Hole") {
                // A hole region's outline (local to its object, at the room's origin): back to the
                // tiles it covers, as game/HoleColliders makes them (the outline's grid: 24 per tile
                // from (-240, -132), pushed out 10 at the 20 x 11 grid's edges).
                std::vector<std::vector<Vector2>> paths;
                for (const Value& path : v["paths"].arr) {
                    paths.emplace_back();
                    for (size_t k = 0; k + 1 < path.arr.size(); k += 2) paths.back().push_back({Bits(path[k]), Bits(path[k + 1])});
                }
                auto inside = [&](float x, float y) { // even-odd over every path
                    bool in = false;
                    for (const auto& poly : paths)
                        for (size_t a = 0, b = poly.size() - 1; a < poly.size(); b = a++)
                            if ((poly[a].y > y) != (poly[b].y > y) && x < (poly[b].x - poly[a].x) * (y - poly[a].y) / (poly[b].y - poly[a].y) + poly[a].x)
                                in = !in;
                    return in;
                };
                unity::Collider2D c;
                c.path = v["path"].s;
                c.tag = "Hole";
                c.layer = static_cast<int>(v["layer"].AsInt());
                c.position = V2(v["pos"]);
                c.hasWorldBox = true;
                c.worldMin = V2(v["min"]);
                c.worldMax = V2(v["max"]);
                for (int j = 0; j < 11; j++)
                    for (int i = 0; i < 20; i++) {
                        if (!inside(24.0f * static_cast<float>(i) - 228.0f, 24.0f * static_cast<float>(j) - 120.0f)) continue;
                        float x0 = 24.0f * static_cast<float>(i) - 240.0f, x1 = x0 + 24.0f, y0 = 24.0f * static_cast<float>(j) - 132.0f, y1 = y0 + 24.0f;
                        if (i == 0) x0 -= 10.0f;
                        if (i == 19) x1 += 10.0f;
                        if (j == 0) y0 -= 10.0f;
                        if (j == 10) y1 += 10.0f;
                        c.parts.emplace_back(Vector2{c.position.x + x0, c.position.y + y0}, Vector2{c.position.x + x1, c.position.y + y1});
                    }
                // In Box2D: the paths cut as Unity cuts a PolygonCollider2D (unity/PolygonCollider2D.h).
                unity::SetPolygonColliderShapes(c, paths);
                holes.push_back(c);
            }
        }
        if (frames.size() < 2) {
            Print("walk.jsonl has no frames\n");
            return 1;
        }
        return 0;
    }
};

// The room: its colliders from the prefab placed like the game's, its doors as the floor left them,
// compared with the game's; then what his queries and his body meet (`physics`).
struct Room {
    std::string prefix; // "Dungeon/<room>/"
    Vector2 position;
    const FloorRoom* room = nullptr;
    std::vector<FloorRoom> floorRooms;
    unity::Physics2D physics;
    int same = 0, differ = 0, doors = 0, turned = 0, closedSkipped = 0;

    // 0, or the exit code (what's wrong printed). Prints each collider that differs.
    int Build(const Recording& rec, const std::string& data, const std::string& tracePath)
    {
        // The room's colliders from the prefab, placed like the game's.
        game::PrefabLibrary::Load(json::Parse(ReadFile(data + "/prefabs.json")));
        const game::Prefab* roomPrefab = game::PrefabLibrary::Find("DungeonRoom", "dungeon_common");
        if (!roomPrefab) {
            Print("no DungeonRoom prefab in %s/prefabs.json\n", data.c_str());
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
        {
            unity::Physics2D atOrigin;
            game::AddRoomColliders(rows, {}, atOrigin);
            for (const unity::Collider2D& c : rec.dumped.Colliders()) {
                size_t at = c.path.find("/DungeonLevelBoundaries/");
                if (c.path.rfind("Dungeon/", 0) != 0 || at == std::string::npos) continue;
                for (const unity::Collider2D& o : atOrigin.Colliders())
                    if (o.path == c.path.substr(at + 1)) {
                        prefix = c.path.substr(0, at + 1);
                        position = {c.position.x - o.position.x, c.position.y - o.position.y};
                    }
                if (!prefix.empty()) break;
            }
        }
        if (prefix.empty()) {
            Print("the recording has no room boundaries\n");
            return 1;
        }
        // Its doors as the floor left them.
        std::string missing = FloorDoors(tracePath, rec.start, data, floorRooms);
        if (!missing.empty()) {
            Print("%s: can't tell the room's doors\n", missing.c_str());
            return 1;
        }
        for (const FloorRoom& r : floorRooms)
            if (r.position.x == position.x && r.position.y == position.y && prefix == "Dungeon/" + r.name + "/") room = &r;
        if (!room) {
            Print("the trace's floor has no room %s at %g, %g\n", prefix.c_str(), position.x, position.y);
            return 1;
        }
        game::ApplyDoors(rows, room->doors, position);
        // A room whose doors the game shows closed (its boundaries on LevelBoundary: CloseAllDoors at
        // runtime, after the oracle's teleport in) isn't the generated state this test builds: its
        // doors and boundaries are reported, not compared.
        bool closedDoors = false;
        for (const unity::Collider2D& d : rec.dumped.Colliders())
            closedDoors |= d.path.rfind(prefix + "DungeonLevelBoundaries/DoorBoundary", 0) == 0 && d.layer == game::Layer::LevelBoundary;
        game::AddRoomColliders(rows, position, physics, prefix);
        for (const unity::Collider2D& c : physics.Colliders()) {
            doors += c.path.find("/DungeonLevelDoors/") != std::string::npos || c.path.find("/DoorBoundary") != std::string::npos;
            const unity::Collider2D* g = nullptr;
            for (const unity::Collider2D& d : rec.dumped.Colliders())
                if (d.path == c.path) g = &d;
            // (a disabled one: where its object is, its offset and size)
            bool box = g && (g->enabled ? g->Min().x == c.Min().x && g->Min().y == c.Min().y && g->Max().x == c.Max().x && g->Max().y == c.Max().y
                                        : g->position.x == c.position.x && g->position.y == c.position.y && g->offset.x == c.offset.x &&
                                              g->offset.y == c.offset.y && g->size.x == c.size.x && g->size.y == c.size.y);
            // The side doors are turned a quarter: the game's bounds go through its rotation in floats
            // (a few ulps off a whole number, not modelled yet), so those are held to 2e-5.
            bool quarter = false;
            for (const game::SceneRow& r : rows)
                if (prefix + r.path == c.path) quarter = std::fmod(std::fabs(r.rotation), 180.0f) == 90.0f;
            if (g && !box && quarter && g->enabled) {
                auto near = [](float x, float y) { return std::fabs(x - y) <= 2e-5f; };
                box = near(g->Min().x, c.Min().x) && near(g->Min().y, c.Min().y) && near(g->Max().x, c.Max().x) && near(g->Max().y, c.Max().y);
                turned += box;
            }
            bool ok = g && g->layer == c.layer && g->tag == c.tag && g->enabled == c.enabled && g->trigger == c.trigger && box;
            const bool door = c.path.find("/DungeonLevelDoors/") != std::string::npos || c.path.find("/DoorBoundary") != std::string::npos;
            if (ok) same++;
            else if (closedDoors && door) {
                closedSkipped++;
                Print("collider %s: the game's doors are closed (runtime), not compared\n", c.path.c_str());
            } else {
                differ++;
                Print("collider %s: port [%g %g]-[%g %g] layer %d %s; game %s\n", c.path.c_str(), c.Min().x, c.Min().y, c.Max().x, c.Max().y,
                            c.layer, c.tag.c_str(), g ? (std::to_string(g->Min().x) + " " + std::to_string(g->Min().y) + " - " + std::to_string(g->Max().x) + " " + std::to_string(g->Max().y) + " layer " + std::to_string(g->layer) + " " + g->tag).c_str() : "none");
            }
        }
        // The game's doors and boundaries the port doesn't have.
        for (const unity::Collider2D& d : rec.dumped.Colliders()) {
            if (d.path.rfind(prefix + "DungeonLevelDoors/", 0) != 0 && d.path.rfind(prefix + "DungeonLevelBoundaries/", 0) != 0) continue;
            bool there = false;
            for (const unity::Collider2D& c : physics.Colliders()) there |= c.path == d.path;
            if (!there) {
                differ++;
                Print("collider %s: the game has it, the port doesn't\n", d.path.c_str());
            }
        }
        if (std::getenv("ML_WALK_DUMPED")) {
            physics.Clear();
            for (const unity::Collider2D& c : rec.dumped.Colliders()) physics.Add(c);
        } else {
            // The oracle's rock, and the room's rocks and chests (what the room fill spawns, checked by
            // its own tests): the solid RisenObstacles.
            for (const unity::Collider2D& c : rec.dumped.Colliders())
                if (c.path.find("OracleRock") != std::string::npos ||
                    (c.path.rfind(prefix, 0) == 0 && c.layer == unity::kLayerRisenObstacle && c.enabled && !c.trigger))
                    physics.Add(c);
        }
        for (const unity::Collider2D& h : rec.holes) physics.Add(h);
        return 0;
    }

    // His body's world: the room's colliders on the recorded Rigidbody2D types, else the rocks' and
    // walls' layers on Kinematic bodies, the rest without.
    void AddToWorld(const Recording& rec, unity::PhysicsWorld& world) const
    {
        for (const unity::Collider2D& c : physics.Colliders()) {
            std::string type;
            for (const auto& [path, t] : rec.bodies)
                if (path == c.path) type = t;
            if (type.empty())
                type = c.layer == unity::kLayerRisenObstacle || c.layer == unity::kLayerLevelBoundary ? "Kinematic" : "none";
            world.AddCollider(c, type == "Kinematic" ? unity::RigidbodyType2D::Kinematic : unity::RigidbodyType2D::Static, type != "none");
        }
    }
};

// What a replay's hero shows after a frame (the rows' port side).
struct HeroFrame {
    Vector2 position;
    float velX = 0.0f, velY = 0.0f;
    int direction = 0;
    unity::AnimatorStateInfo info;           // his Animator's current state
    std::string stateName;                   // (its name, for the report)
    const std::string* sprite = nullptr;     // the sprite his Animator shows (nullptr: none)
    bool isIntangible = false, abilityInUse = false, isFalling = false;
    float animSpeed = 1.0f, speedFactor = 1.0f;
    // damage
    float health = 0.0f, absorption = 0.0f, shield = 0.0f;
    bool healthVariable = true, artVisible = true, playingDamagedEffect = false, isHolesIntangible = false;
    int layer = 0;
    // his Rigidbody2D (in Box2D)
    bool hasBody = false, touching = false;
    Vector2 bodyPosition, bodyVelocity;
    // the weapon
    int combo = 0;
    unity::AnimatorStateInfo weaponInfo;
    std::string weaponStateName;
    const std::string* weaponSprite = nullptr;
    bool hitboxActive = false;
    Vector2 hitboxOffset, hitboxSize;
};

// The rows: every frame compared with the recording, and the report.
class Rows {
public:
    Rows(const unity::AnimatorController& controller, const unity::AnimatorController* weaponController, bool box2d, bool debug)
        : controller_(controller), weaponController_(weaponController), box2d_(box2d), debug_(debug)
    {
    }

    // One recorded frame against the hero after it.
    void Frame(const Value& f, const HeroFrame& hero)
    {
        checked++;
        Vector2 pos = V2(f["pos"]);
        float vx = Bits(f["vel"][0]), vy = Bits(f["vel"][1]);
        const unity::AnimatorStateInfo& info = hero.info;
        int32_t wantState = static_cast<int32_t>(f["state"].AsInt());
        float wantT = Bits(f["t"]);
        const std::string* sprite = hero.sprite;
        const Value* wantSprite = f.Find("sprite");
        // While he falls DOTween moves him on its real-time clock (the move starts in a
        // DOVirtual.DelayedCall, which ignores the time scale): those positions depend on the
        // machine's timing, so they're held to a tolerance; the landing and respawn are exact.
        // (DOTween's clock is Time.realtimeSinceStartup between its own updates, not the frame's
        // unscaledDeltaTime the recordings have: walk_fall.jsonl, recorded minimized at Idle
        // priority, is up to 0.262 off along the move, 0.25 before.)
        const float fallTolerance = 0.5f;
        const Value* falling = f.Find("falling");
        bool pb = falling && falling->b && hero.isFalling
                      ? std::fabs(pos.x - hero.position.x) > fallTolerance || std::fabs(pos.y - hero.position.y) > fallTolerance
                      : pos.x != hero.position.x || pos.y != hero.position.y;
        bool vb = vx != hero.velX || vy != hero.velY;
        bool sb = wantState != info.shortNameHash;
        double dt = std::fabs(static_cast<double>(wantT) - info.normalizedTime);
        bool tb = !sb && dt > 1e-4;
        bool db = f["dir"].AsInt() != hero.direction;
        bool spb = wantSprite && wantSprite->IsString() && (!sprite || *sprite != wantSprite->s);
        // The roll's state (recordings from -oracle-walk=roll on): intangible, the Animator's
        // speed, the ability in use, the speed factor (bit for bit).
        bool rb = false;
        if (const Value* in = f.Find("intangible")) {
            rollFrames++;
            rb = in->b != hero.isIntangible || Bits(f["animSpeed"]) != hero.animSpeed || f["abilityInUse"].b != hero.abilityInUse ||
                 Bits(f["speedFactor"]) != hero.speedFactor;
        }
        rollBad += rb;
        // Damage (recordings with "hp"): health bit for bit, invincible, the blink, the damaged
        // effect, the layer.
        if (const Value* hp = f.Find("hp")) {
            damageFrames++;
            const Value* abs = f.Find("abs");
            const bool db2 = Bits(*hp) != hero.health || (abs && (Bits(*abs) != hero.absorption || Bits(f["shield"]) != hero.shield)) ||
                             f["hpVar"].b != hero.healthVariable || f["art"].b != hero.artVisible || f["damaged"].b != hero.playingDamagedEffect ||
                             f["holesIntangible"].b != hero.isHolesIntangible || f["layer"].AsInt() != hero.layer;
            damageBad += db2;
            if (db2 && (debug_ || shown < 12))
                Print("  damage game: hp %.9g abs %.9g shield %g var %d art %d damaged %d layer %lld | port: hp %.9g abs %.9g shield %g var %d art %d damaged %d layer %d\n",
                            Bits(*hp), abs ? Bits(*abs) : 0.0f, abs ? Bits(f["shield"]) : 0.0f, f["hpVar"].b, f["art"].b, f["damaged"].b,
                            static_cast<long long>(f["layer"].AsInt()), hero.health, hero.absorption, hero.shield, hero.healthVariable,
                            hero.artVisible, hero.playingDamagedEffect, hero.layer);
        }
        touching += box2d_ && hero.hasBody && hero.touching;
        // His body after the frame's last step (recordings with "rb": position, velocity).
        if (const Value* body = f.Find("rb"); body && box2d_ && hero.hasBody) {
            bodyChecked++;
            const Vector2 bp = hero.bodyPosition, bv = hero.bodyVelocity;
            const bool tweened = falling && falling->b && hero.isFalling; // (see the position check)
            const bool bb = tweened ? std::fabs(Bits((*body)[0]) - bp.x) > fallTolerance || std::fabs(Bits((*body)[1]) - bp.y) > fallTolerance
                                    : Bits((*body)[0]) != bp.x || Bits((*body)[1]) != bp.y || Bits((*body)[2]) != bv.x || Bits((*body)[3]) != bv.y;
            bodyBad += bb;
            if (bb && shown < 12)
                Print("  body game: (%.4f, %.4f) v (%.4f, %.4f) | port: (%.4f, %.4f) v (%.4f, %.4f)\n", Bits((*body)[0]), Bits((*body)[1]),
                            Bits((*body)[2]), Bits((*body)[3]), bp.x, bp.y, bv.x, bv.y);
        }
        spb = spb || rb;
        // The weapon: the combo, its Animator's state and time, the main attack's hitbox (on,
        // offset, size: bit for bit) and sprite.
        bool wb = false, hb = false;
        if (weaponController_) {
            const unity::AnimatorStateInfo& wi = hero.weaponInfo;
            const std::string* ws = hero.weaponSprite;
            const Value& want = f["wsprite"];
            wb = f["combo"].AsInt() != hero.combo || static_cast<int32_t>(f["wstate"].AsInt()) != wi.shortNameHash ||
                 std::fabs(static_cast<double>(Bits(f["wt"])) - wi.normalizedTime) > 1e-4 || (want.IsString() ? !ws || *ws != want.s : ws != nullptr);
            hb = f["hitbox"].b != hero.hitboxActive || V2(f["hitOffset"]).x != hero.hitboxOffset.x || V2(f["hitOffset"]).y != hero.hitboxOffset.y ||
                 V2(f["hitSize"]).x != hero.hitboxSize.x || V2(f["hitSize"]).y != hero.hitboxSize.y;
            weaponBad += wb;
            hitboxBad += hb;
            if ((wb || hb) && (debug_ || shown < 12)) {
                int gs = weaponController_->StateByHash(static_cast<int32_t>(f["wstate"].AsInt()));
                Print("  weapon game: combo %lld %s t %.9g hitbox %d (%.9g, %.9g) %.9gx%.9g sprite %s\n"
                            "         port: combo %d %s t %.9g hitbox %d (%.9g, %.9g) %.9gx%.9g sprite %s\n",
                            static_cast<long long>(f["combo"].AsInt()), gs >= 0 ? weaponController_->states[static_cast<size_t>(gs)].name.c_str() : "?",
                            Bits(f["wt"]), f["hitbox"].b, V2(f["hitOffset"]).x, V2(f["hitOffset"]).y, V2(f["hitSize"]).x, V2(f["hitSize"]).y,
                            want.IsString() ? want.s.c_str() : "-", hero.combo, hero.weaponStateName.c_str(), wi.normalizedTime, hero.hitboxActive,
                            hero.hitboxOffset.x, hero.hitboxOffset.y, hero.hitboxSize.x, hero.hitboxSize.y, ws ? ws->c_str() : "-");
            }
        }
        if (!sb) worstTime = std::max(worstTime, dt);
        posBad += pb;
        velBad += vb;
        stateBad += sb;
        timeBad += tb;
        dirBad += db;
        spriteBad += spb;
        if (debug_ || ((pb || vb || sb || tb || db || spb || wb || hb) && shown < 12)) {
            if (!debug_) shown++;
            int ps = controller_.StateByHash(wantState);
            if (f.Find("intangible"))
                Print("  roll  game: intangible %d anim speed %g in use %d speed factor %.9g | port: %d %g %d %.9g\n", f["intangible"].b,
                            Bits(f["animSpeed"]), f["abilityInUse"].b, Bits(f["speedFactor"]), hero.isIntangible, hero.animSpeed, hero.abilityInUse,
                            hero.speedFactor);
            Print("frame %3lld%s dt %.5f stick (%.3f, %.3f)\n  game: pos (%.4f, %.4f) vel (%.3f, %.3f) %s t %.4f dir %lld sprite %s\n"
                        "  port: pos (%.4f, %.4f) vel (%.3f, %.3f) %s t %.4f dir %d sprite %s\n",
                        static_cast<long long>(f["f"].AsInt()), (pb || vb || sb || tb || db || spb || wb || hb) ? " MISMATCH" : "", Bits(f["dt"]),
                        Bits(f["stick"][0]), Bits(f["stick"][1]), pos.x, pos.y, vx, vy,
                        ps >= 0 ? controller_.states[static_cast<size_t>(ps)].name.c_str() : "?", wantT, static_cast<long long>(f["dir"].AsInt()),
                        wantSprite && wantSprite->IsString() ? wantSprite->s.c_str() : "-", hero.position.x, hero.position.y, hero.velX, hero.velY,
                        hero.stateName.c_str(), info.normalizedTime, hero.direction, sprite ? sprite->c_str() : "-");
        }
    }

    // The report; returns whether every row matched.
    bool Report(const Room& room, int tiesSettled, const std::string& tieFrames) const
    {
        Print("\nroom colliders from the prefab %5d match %5d differ   (room %s at %g, %g; %d of doors and their openings; %d turned, within 2e-5; %d closed, not compared)\n",
                    room.same, room.differ, room.room->name.c_str(), room.position.x, room.position.y, room.doors, room.turned, room.closedSkipped);
        Print("frames replayed                %5d\n", checked);
        Print("position (bit-exact)           %5d match %5d differ\n", checked - posBad, posBad);
        Print("velocity                       %5d match %5d differ\n", checked - velBad, velBad);
        if (tiesSettled)
            Print("  (OverlapBox ties the recording settled: %d, frames %s%s)\n", tiesSettled, tieFrames.c_str(), tiesSettled > 8 ? " ..." : "");
        Print("direction                      %5d match %5d differ\n", checked - dirBad, dirBad);
        Print("Animator state                 %5d match %5d differ\n", checked - stateBad, stateBad);
        Print("Animator normalized time       %5d match %5d differ   (worst %.2e)\n", checked - stateBad - timeBad, timeBad, worstTime);
        Print("sprite (and roll state)        %5d match %5d differ\n", checked - spriteBad, spriteBad);
        if (rollFrames)
            Print("roll state (intangible, Animator speed, in use, speed factor) %d match %d differ\n", rollFrames - rollBad, rollBad);
        if (weaponController_) {
            Print("weapon (combo, Animator, sprite) %3d match %5d differ\n", checked - weaponBad, weaponBad);
            Print("main attack hitbox             %5d match %5d differ\n", checked - hitboxBad, hitboxBad);
        }
        if (damageFrames)
            Print("damage (health, invincible, blink, damaged effect, layer) %d match %d differ\n", damageFrames - damageBad, damageBad);
        if (box2d_) Print("his Rigidbody2D in Box2D: touching a collider in %d frames\n", touching);
        if (bodyChecked)
            Print("his Rigidbody2D (Box2D)        %5d match %5d differ\n", bodyChecked - bodyBad, bodyBad);
        bool ok = !room.differ && !bodyBad && !posBad && !velBad && !dirBad && !stateBad && !timeBad && !spriteBad && !weaponBad && !hitboxBad && !damageBad;
        Print("\n%s\n", ok ? "ALL MATCH" : "MISMATCHES");
        return ok;
    }

    int checked = 0, posBad = 0, velBad = 0, stateBad = 0, timeBad = 0, dirBad = 0, spriteBad = 0, rollBad = 0, rollFrames = 0, shown = 0;
    int weaponBad = 0, hitboxBad = 0, damageBad = 0, damageFrames = 0, bodyChecked = 0, bodyBad = 0, touching = 0;
    double worstTime = 0.0;

private:
    const unity::AnimatorController& controller_;
    const unity::AnimatorController* weaponController_;
    bool box2d_, debug_;
};

} // namespace walkreplay
