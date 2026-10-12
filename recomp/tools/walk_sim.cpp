// Will walking a generated floor on the PC, the way the GameCube build runs him: the floor
// from game/Session, the room's colliders (RoomPhysics), HeroMerchantController with his
// Animator, a stick script; prints where he goes, what he hits and the doors he takes.
//
// Usage: walk_sim <data dir> [culture 1-4] [level 0-2] [seed] [script: dx,dy,frames;...]

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "game/Constants.h"
#include "game/HeroMerchantController.h"
#include "game/Prefabs.h"
#include "game/RoomPhysics.h"
#include "game/RoomScene.h"
#include "game/Session.h"
#include "unity/Animator.h"
#include "util/Json.h"

using namespace game;

namespace {

class FileSource : public DataSource {
public:
    explicit FileSource(std::string root) : root_(std::move(root)) {}
    std::string Read(const std::string& path) override
    {
        std::ifstream f(root_ + "/" + path, std::ios::binary);
        if (!f) throw std::runtime_error("missing data file: " + path);
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

private:
    std::string root_;
};

} // namespace

int main(int argc, char** argv)
{
    std::string dataDir = argc > 1 ? argv[1] : "data/extracted";
    Culture culture = static_cast<Culture>(argc > 2 ? std::atoi(argv[2]) : 1);
    int level = argc > 3 ? std::atoi(argv[3]) : 0;
    int seed = argc > 4 ? std::atoi(argv[4]) : 1;
    std::string script = argc > 5 ? argv[5] : "1,0,120";
    try {
        FileSource data(dataDir);
        Session session(data);
        session.Load();
        session.Start(seed);
        std::unique_ptr<Floor> floor;
        for (int l = 0; l <= level; l++) floor = session.Generate(culture, l, false);
        unity::AnimatorController anim = unity::AnimatorController::FromJson(json::Parse(data.Read("animators/hero_merchant.json")));

        const Prefab* p = PrefabLibrary::Find("HeroMerchant", "hero_merchant");
        if (!p) throw std::runtime_error("no HeroMerchant prefab");
        HeroMerchantController::Settings s = HeroMerchantController::SettingsFrom(*p);
        unity::Physics2D physics;
        HeroMerchantController hero(anim, s, physics);
        hero.currentPlayerMoveSpeed = Constants::GetFloat("kBaseSpeed");

        int cell = 0;
        for (size_t i = 0; i < floor->dungeon->cells.size(); i++)
            if (floor->dungeon->cells[i].cave == floor->dungeon->layout.initCaveRoom) cell = static_cast<int>(i);
        auto enter = [&](int c) {
            cell = c;
            const PlacedCell& pc = floor->dungeon->cells[static_cast<size_t>(c)];
            unity::Vector2 at = RoomPosition(pc.x, pc.y);
            physics.Clear();
            int n = AddRoomColliders(RoomSceneTree(*pc.room, at), at, physics);
            hero.position = at;
            std::printf("room %d (%d, %d) %s at (%g, %g): %d colliders\n", c, pc.x, pc.y, pc.info->name.c_str(), at.x, at.y, n);
            for (const unity::Collider2D& k : physics.Colliders())
                if (k.tag == "DungeonLevelDoor" || std::getenv("ML_SIM_ALL"))
                    std::printf("  %-60s layer %2d %-16s [%g %g]-[%g %g]%s\n", k.path.c_str(), k.layer, k.tag.c_str(), k.Min().x, k.Min().y,
                                k.Max().x, k.Max().y, k.enabled ? "" : " (disabled)");
        };
        for (size_t i = 0; i < floor->dungeon->cells.size(); i++) {
            const PlacedCell& pc = floor->dungeon->cells[i];
            std::printf("cell %zu (%d, %d) %s\n", i, pc.x, pc.y, pc.info->name.c_str());
        }
        enter(cell);

        int frame = 0;
        std::stringstream ss(script);
        for (std::string seg; std::getline(ss, seg, ';');) {
            float dx = 0, dy = 0;
            int frames = 0;
            if (std::sscanf(seg.c_str(), "%f,%f,%d", &dx, &dy, &frames) != 3) continue;
            for (int f = 0; f < frames; f++, frame++) {
                hero.Update(1.0f / 60.0f, unity::Vector2{dx, dy});
                if (frame % 15 == 0 || hero.doorEntered)
                    std::printf("frame %4d pos (%.2f, %.2f) vel (%.2f, %.2f) %s pushing door %.3f%s\n", frame, hero.position.x, hero.position.y,
                                hero.velX, hero.velY, anim.states[static_cast<size_t>(hero.animator.CurrentState())].name.c_str(),
                                hero.timePushingDoors, hero.doorEntered ? (" -> " + hero.doorEntered->path).c_str() : "");
            }
        }
        return 0;
    } catch (const std::exception& e) {
        std::printf("walk_sim: %s\n", e.what());
        return 1;
    }
}
