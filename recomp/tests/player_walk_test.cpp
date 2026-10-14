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
// ML_WALK_DUMPED=1 uses the game's instead. (tests/player_walk_replay.h: the recording, the
// room and the rows, shared with the translated hero's replay, translated/player_translated_test.)
//
// ML_WALK_BOX2D=1 steps his Rigidbody2D in a unity::PhysicsWorld (Box2D) with the room's
// colliders, instead of the contact-free round trip; the oracle's rock (-oracle-walk=rocks,
// ".../OracleRock" in the recording) is added to the room, and where the recording has his
// body's position and velocity ("rb") they're compared too. Those recordings also have each
// frame's physics steps ("fixed"), which the replay takes as the game did.
//
// Usage: player_walk_test [walk.jsonl] [hero_merchant.json] [trace.jsonl]
//        (ML_WALK_DEBUG=1: every frame; the trace defaults to the one next to walk.jsonl)

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "game/Constants.h"
#include "game/HeroMerchantController.h"
#include "game/Items.h"
#include "game/Prefabs.h"
#include "game/Weapon.h"
#include "player_walk_replay.h"
#include "unity/Animator.h"
#include "unity/Physics2D.h"
#include "unity/PhysicsWorld.h"
#include "util/Json.h"

using json::Value;
using unity::Vector2;
using walkreplay::Bits;
using walkreplay::ReadFile;
using walkreplay::V2;

int main(int argc, char** argv)
{
    std::string walkPath = argc > 1 ? argv[1] : "data/oracle/walk.jsonl";
    std::string data = std::getenv("ML_DATA") ? std::getenv("ML_DATA") : "data/extracted";
    // The game's constants (the stats' bounds: kMaxShield; the fall's cost; the blink's times).
    game::Constants::LoadFromFile(data + "/constants.json");
    std::string animPath = argc > 2 ? argv[2] : data + "/animators/hero_merchant.json";
    std::string tracePath = argc > 3 ? argv[3] : walkPath.substr(0, walkPath.find_last_of("/\\") + 1) + "trace.jsonl";
    bool debug = std::getenv("ML_WALK_DEBUG") != nullptr;

    walkreplay::Recording rec;
    if (int code = rec.Load(walkPath)) return code;
    const Value& start = rec.start;
    const std::vector<Value>& frames = rec.frames;

    walkreplay::Room room;
    if (int code = room.Build(rec, data, tracePath)) return code;
    unity::Physics2D& physics = room.physics;

    // His body in Box2D, with the room's colliders: the recorded Rigidbody2D types, else the
    // rocks' and walls' layers on Kinematic bodies, the rest without.
    const bool box2d = std::getenv("ML_WALK_BOX2D") != nullptr;
    unity::Physics2DSettings physicsSettings;
    {
        std::string text = ReadFile(data + "/physics2d.json");
        if (!text.empty() && !physicsSettings.Load(json::Parse(text))) std::printf("%s/physics2d.json: no layer matrix\n", data.c_str());
    }
    unity::PhysicsWorld world(physicsSettings);
    if (box2d) room.AddToWorld(rec, world);

    unity::AnimatorController controller = unity::AnimatorController::FromJson(json::Parse(ReadFile(animPath)));
    game::HeroMerchantController::Settings settings;
    settings.colliderSize = V2(start["colliderSize"]);
    settings.colliderOffset = V2(start["colliderOffset"]);
    settings.collisionLayers = static_cast<int>(start["collisionLayers"].AsInt());
    settings.slideLayers = static_cast<int>(start["slideLayers"].AsInt());
    if (const Value* g = start.Find("godModeLayers")) settings.godModeLayers = static_cast<int>(g->AsInt());
    if (const Value* c = start.Find("cooldown")) settings.abilityCooldown = Bits(*c);
    game::HeroMerchantController hero(controller, settings, physics);
    // Physics2D.OverlapBox's ties (unity/Physics2D.h): which of several overlapping colliders
    // the game's broad phase met first isn't recorded. Where the corner slide's nudge hangs on
    // one, the recording's velocity settles it (as its fixed steps and unscaled times are taken
    // from it): only among the colliders the port finds there, and counted below.
    float wantVel[2] = {0.0f, 0.0f};
    int currentFrame = 0, tiesSettled = 0;
    std::string tieFrames;
    hero.slideTie = [&](int axis, const std::vector<float>& outcomes) -> size_t {
        if (outcomes[0] == wantVel[axis]) return 0;
        for (size_t k = 1; k < outcomes.size(); k++)
            if (outcomes[k] == wantVel[axis]) {
                tiesSettled++;
                if (tiesSettled <= 8) tieFrames += (tieFrames.empty() ? "" : " ") + std::to_string(currentFrame);
                return k;
            }
        return 0;
    };
    if (box2d) hero.physicsWorld = &world;
    hero.currentPlayerMoveSpeed = Bits(start["moveSpeed"]);
    hero.speedFactor = Bits(start["speedFactor"]);
    // Where a fall puts him back: the door he came in by (recordings in a room past the first).
    hero.roomIsInit = room.room->name == "Init";
    if (const Value* r = start.Find("respawn")) hero.respawn = V2(*r);
    // His health and defense (his equipment's), from recordings that have them.
    if (const Value* m = start.Find("maxHealth")) {
        hero.stats.currentHealth.maxValue = Bits(*m);
        hero.stats.currentHealth.value = Bits(start["health"]);
        hero.stats.defense.value = Bits(start["defense"]);
        hero.stats.strength.value = Bits(start["strength"]);
        if (const Value* a = start.Find("absorption")) {
            hero.stats.absorption.maxValue = Bits(start["absorptionMax"]);
            hero.stats.absorption.value = Bits(*a);
            hero.stats.shield.value = Bits(start["shield"]);
        }
    }

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

    // The equipped weapon (recordings with "wstate"): the item walkStart names (else
    // ML_WALK_WEAPON), its prefab (the item's inGameArtPrefabName in the items bundle) and
    // controller (animators/<the prefab's name, snake case>.json), from the first frame.
    std::unique_ptr<unity::AnimatorController> weaponController;
    std::unique_ptr<game::Weapon> weapon;
    if (f0.Find("wstate")) {
        std::string name = start.Find("weapon") ? start["weapon"].s : std::getenv("ML_WALK_WEAPON") ? std::getenv("ML_WALK_WEAPON") : "";
        Value items = json::Parse(ReadFile(data + "/items.json"));
        const Value* item = game::Items::Find(items, name);
        if (!item) {
            std::printf("the recording's weapon \"%s\" isn't in items.json (walkStart's weapon, or set ML_WALK_WEAPON)\n", name.c_str());
            return 1;
        }
        const std::string art = (*item)["inGameArtPrefabName"].s;
        std::string file;
        for (char ch : art) file += ch == ' ' ? '_' : static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        const game::Prefab* weaponPrefab = game::PrefabLibrary::Find(art, "items");
        std::string weaponJson = ReadFile(data + "/animators/" + file + ".json");
        if (!weaponPrefab || weaponJson.empty()) {
            std::printf("%s: no prefab \"%s\" or animators/%s.json\n", name.c_str(), art.c_str(), file.c_str());
            return 1;
        }
        weaponController = std::make_unique<unity::AnimatorController>(unity::AnimatorController::FromJson(json::Parse(weaponJson)));
        weapon = std::make_unique<game::Weapon>(*weaponController, *weaponPrefab, game::WeaponTypeFromName((*item)["weaponType"].s));
        int w0 = weaponController->StateByHash(static_cast<int32_t>(f0["wstate"].AsInt()));
        if (w0 < 0) {
            std::printf("frame 0: weapon state %lld isn't in %s\n", static_cast<long long>(f0["wstate"].AsInt()), file.c_str());
            return 1;
        }
        weapon->animator.Play(weaponController->states[static_cast<size_t>(w0)].name, Bits(f0["wt"]));
        hero.weapon = weapon.get();
        hero.comboCounter = static_cast<int>(f0["combo"].AsInt());
    }

    walkreplay::Rows rows(controller, weaponController.get(), box2d, debug);
    for (size_t i = 1; i < frames.size(); i++) {
        const Value& f = frames[i];
        game::HeroMerchantController::Input input;
        input.stick = V2(f["stick"]);
        input.roll = (f["in"][2].AsInt() & 16) != 0;  // LeftTrigger pressed this frame
        input.attack = (f["in"][2].AsInt() & 1) != 0; // Action1
        if (const Value* u = f.Find("udt")) input.unscaledDeltaTime = Bits(*u);
        // The frame's physics steps as the game took them (recordings with "fixed"): the port's
        // own fixed clock starts at an arbitrary phase.
        if (const Value* fixed = f.Find("fixed")) input.fixedSteps = static_cast<int>(fixed->size());
        wantVel[0] = Bits(f["vel"][0]);
        wantVel[1] = Bits(f["vel"][1]);
        currentFrame = static_cast<int>(f["f"].AsInt());
        if (const Value* town = f.Find("town")) hero.townSceneLoaded = town->b; // (recordings that have it)
        hero.Update(Bits(f["dt"]), input);

        walkreplay::HeroFrame h;
        h.position = hero.position;
        h.velX = hero.velX;
        h.velY = hero.velY;
        h.direction = hero.heroDirection;
        h.info = hero.animator.GetCurrentAnimatorStateInfo();
        h.stateName = controller.states[static_cast<size_t>(hero.animator.CurrentState())].name;
        h.sprite = hero.animator.Sprite();
        h.isIntangible = hero.isIntangible;
        h.animSpeed = hero.animator.speed;
        h.abilityInUse = hero.abilityInUse;
        h.speedFactor = hero.speedFactor;
        h.isFalling = hero.isFalling;
        h.health = hero.stats.currentHealth.value;
        h.absorption = hero.stats.absorption.value;
        h.shield = hero.stats.shield.value;
        h.healthVariable = hero.stats.currentHealth.variableStat;
        h.artVisible = hero.artVisible;
        h.playingDamagedEffect = hero.playingDamagedEffect;
        h.isHolesIntangible = hero.isHolesIntangible;
        h.layer = hero.layer;
        h.hasBody = hero.Body() != nullptr;
        if (h.hasBody) {
            h.touching = hero.Body()->TouchingCount() > 0;
            h.bodyPosition = hero.Body()->position();
            h.bodyVelocity = hero.Body()->velocity();
        }
        if (weapon) {
            h.combo = hero.comboCounter;
            h.weaponInfo = weapon->animator.GetCurrentAnimatorStateInfo();
            h.weaponStateName = weaponController->states[static_cast<size_t>(weapon->animator.CurrentState())].name;
            h.weaponSprite = weapon->Sprite();
            h.hitboxActive = weapon->hitboxActive;
            h.hitboxOffset = weapon->hitboxOffset;
            h.hitboxSize = weapon->hitboxSize;
        }
        rows.Frame(f, h);
    }
    return rows.Report(room, tiesSettled, tieFrames) ? 0 : 1;
}
