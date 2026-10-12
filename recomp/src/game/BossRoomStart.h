#pragma once
// The boss room's Start() frame. AddBossRoom instantiates the culture's boss room prefab and
// generation yields a frame; with the boss already defeated the room is live, and its
// objects' Start() run, in hierarchy order, drawing from Random:
//   BreakableDetail             flipX, then the destroyed-at-start roll   (2 draws)
//   GolemKingRockBehaviour      RandomRockSprites.GetRandom()             (1 draw)
//   BreakableObjectBehaviour    the flip, then skins.GetRandom() if the   (1 or 2 draws)
//   (and BreakableBoxBehaviour)   prefab sets no skin
//   CarnivorousMutaeBossRoots   rootType, then the x flip                 (2 draws)
// Found with the oracle's frame probe (every Start/Awake/Update wrapped while the frame
// ran): these are all the calls that moved the RNG there (Golem 132 draws, Forest 72).

#include <string>
#include <vector>

#include "game/Prefabs.h"

namespace game {

struct StartPick {
    std::string path; // the object below the boss room's root
    std::string cls;
    bool flip = false;      // BreakableDetail flipX / BreakableObjectBehaviour scale -1 / roots scale -1
    int pick = -1;          // the sprite or skin picked, or the roots' rootType
    bool destroyed = false; // BreakableDetail broken at start
};

// Runs the Start() of every active object of `bossRoom` that draws; returns what each chose.
std::vector<StartPick> BossRoomStart(const Prefab& bossRoom);

} // namespace game
