// The hero walking, frame by frame against the game (data/oracle/walk.jsonl, the oracle's
// `-oracle-walk`): the port starts from the first recorded frame (position, Animator state
// and time, velocity, direction), then replays every later frame's deltaTime and stick
// through HeroMerchantController and its Animator against the room's colliders the game
// had, and compares his position (bit for bit), velocity, direction, Animator state, its
// normalized time and the sprite shown.
//
// The room's colliders come from the DungeonRoom prefab (prefabs.json) placed where the
// game's room is, each checked against the game's own (the recording's first lines); doors
// the floor removed are the ones missing there. ML_WALK_DUMPED=1 uses the game's instead.
//
// Usage: player_walk_test [walk.jsonl] [hero_merchant.json]   (ML_WALK_DEBUG=1: every frame)

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
#include "game/RoomPhysics.h"
#include "game/SceneTransform.h"
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

} // namespace

int main(int argc, char** argv)
{
    std::string walkPath = argc > 1 ? argv[1] : "data/oracle/walk.jsonl";
    std::string data = std::getenv("ML_DATA") ? std::getenv("ML_DATA") : "data/extracted";
    std::string animPath = argc > 2 ? argv[2] : data + "/animators/hero_merchant.json";
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
    // Objects with colliders the game's room no longer has (doors the floor removed): gone.
    for (game::SceneRow& r : rows) {
        if (r.colliders.empty()) continue;
        bool there = false;
        for (const unity::Collider2D& c : dumped.Colliders()) there |= c.path == prefix + r.path;
        if (!there) r.active = false;
    }
    int built = game::AddRoomColliders(rows, roomPos, physics, prefix);
    // Doors and the boundaries in their openings change at runtime (DungeonRoom opens and
    // closes them with the room's enemies; the doors' Animators size their colliders): not
    // ported yet, so counted apart.
    int same = 0, differ = 0, doorState = 0;
    for (const unity::Collider2D& c : physics.Colliders()) {
        if (c.path.find("/DungeonLevelDoors/") != std::string::npos || c.path.find("/DoorBoundary") != std::string::npos) {
            doorState++;
            continue;
        }
        const unity::Collider2D* g = nullptr;
        for (const unity::Collider2D& d : dumped.Colliders())
            if (d.path == c.path) g = &d;
        bool ok = g && g->layer == c.layer && g->tag == c.tag && g->enabled == c.enabled && g->trigger == c.trigger &&
                  g->Min().x == c.Min().x && g->Min().y == c.Min().y && g->Max().x == c.Max().x && g->Max().y == c.Max().y;
        if (ok) same++;
        else {
            differ++;
            std::printf("collider %s: port [%g %g]-[%g %g] layer %d %s; game %s\n", c.path.c_str(), c.Min().x, c.Min().y, c.Max().x, c.Max().y,
                        c.layer, c.tag.c_str(), g ? (std::to_string(g->Min().x) + " " + std::to_string(g->Min().y) + " - " + std::to_string(g->Max().x) + " " + std::to_string(g->Max().y) + " layer " + std::to_string(g->layer) + " " + g->tag).c_str() : "none");
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
    std::printf("\nroom colliders from the prefab %5d match %5d differ   (room at %g, %g; %d door colliders: door state not ported)\n",
                same, differ, roomPos.x, roomPos.y, doorState);
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
