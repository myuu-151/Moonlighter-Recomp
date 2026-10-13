#pragma once
// MasterAudio's DynamicSoundGroupCreators an enemy recording's scene had, in the order they made
// their groups (game::MasterAudio::Load: the first group of a name wins, as MasterAudio keeps the
// group it has): the recording's "soundCreators" (oracle EnemyOracleBiome.cs: the first scene's
// permanent creators, then DungeonManager's children in sibling order, which is the order
// DungeonGenerator made them: the dungeon's, a corrupted room's culture's, the general
// dungeons'), else, for the recordings made before it, the first scene's two, the dungeon's by
// "stats"."dungeonCulture" (DungeonGenerator.GetSoundPathByCulture) and the general one.
// `permanent`: with the first scene's ("Sounds - Will", "Sounds - interface").

#include <algorithm>
#include <string>
#include <vector>

#include "util/Json.h"

inline std::vector<std::string> SoundCreators(const json::Value& start, bool permanent = true)
{
    std::vector<std::string> out;
    if (const json::Value* sc = start.Find("soundCreators")) {
        const std::string dm = "DungeonManager/", clone = "(Clone)";
        for (const json::Value& c : sc->arr) {
            if (!c.IsString()) continue;
            const bool dungeon = c.s.compare(0, dm.size(), dm) == 0;
            if (!dungeon && !permanent) continue;
            std::string n = c.s.substr(c.s.rfind('/') == std::string::npos ? 0 : c.s.rfind('/') + 1);
            if (n.size() > clone.size() && n.compare(n.size() - clone.size(), clone.size(), clone) == 0) n.resize(n.size() - clone.size());
            if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
        }
        return out;
    }
    static const char* const kDungeon[] = {"Sounds - GolemDungeon", "Sounds - ForestDungeon", "Sounds - DesertDungeon", "Sounds - TeslaDungeon",
                                           "Sounds - UnknownDungeon"};
    long long culture = 1; // (ItemMaster.Culture: 1 Golem ... 5 Unknown)
    if (const json::Value* stats = start.Find("stats"))
        if (const json::Value* c = stats->Find("dungeonCulture")) culture = c->AsInt();
    if (permanent) out = {"Sounds - Will", "Sounds - interface"};
    out.push_back(kDungeon[culture >= 1 && culture <= 5 ? culture - 1 : 0]);
    out.push_back("Sounds - GeneralDungeons");
    return out;
}
