#include "game/BossRoomStart.h"

#include <set>

#include "unity/Random.h"

namespace game {

namespace Random = unity::Random;

std::vector<StartPick> BossRoomStart(const Prefab& bossRoom)
{
    std::vector<StartPick> out;
    std::set<std::string> inactive; // objects inactive in the hierarchy (themselves or above)
    for (const PrefabNode& n : bossRoom.nodes) {
        std::string parent = n.path.rfind('/') == std::string::npos ? std::string() : n.path.substr(0, n.path.rfind('/'));
        if (!n.active || (!n.path.empty() && inactive.count(parent))) {
            inactive.insert(n.path);
            continue;
        }
        for (const Behaviour& b : n.behaviours) {
            StartPick p;
            p.path = n.path;
            p.cls = b.cls;
            if (b.cls == "BreakableDetail") {
                p.flip = Random::Range(0.0f, 1.0f) > 0.5f;
                p.destroyed = Random::Range(0.0f, 1.0f) < static_cast<float>(b.Field("destroyAtStartProb"));
            } else if (b.cls == "GolemKingRockBehaviour") {
                int count = static_cast<int>(b.Field("RandomRockSprites"));
                if (count > 0) p.pick = Random::Range(0, count);
            } else if (b.cls == "BreakableObjectBehaviour" || b.cls == "BreakableBoxBehaviour") {
                p.flip = Random::Range(0.0f, 1.0f) > 0.5f;
                int skins = static_cast<int>(b.Field("skins"));
                if (b.Field("currentSkin") == 0.0 && skins > 0) p.pick = Random::Range(0, skins);
            } else if (b.cls == "CarnivorousMutaeBossRoots") {
                p.pick = Random::Range(0.0f, 1.0f) > 0.5f ? 0 : 1;
                p.flip = !(Random::Range(0.0f, 1.0f) > 0.5f); // scale x -1
            } else {
                continue;
            }
            out.push_back(p);
        }
    }
    return out;
}

} // namespace game
