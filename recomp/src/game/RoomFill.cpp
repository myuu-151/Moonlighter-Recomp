#include "game/RoomFill.h"

#include "game/Constants.h"
#include "unity/Random.h"

namespace game {

namespace Random = unity::Random;
using DungeonTile = DungeonRoomInfo::DungeonTile;

const char* RoomFill::CultureName(Culture c)
{
    switch (c) {
    case Culture::Merchant: return "Merchant";
    case Culture::Golem: return "Golem";
    case Culture::Forest: return "Forest";
    case Culture::Desert: return "Desert";
    case Culture::Tech: return "Tech";
    case Culture::Unknown: return "Unknown";
    case Culture::Developer: return "Developer";
    case Culture::Wanderer: return "Wanderer";
    case Culture::ICT: return "ICT";
    }
    return "";
}

void RoomFill::LoadGridFromInfo()
{
    if (roomInfo == nullptr || roomInfo->obstaclesSpawnPacks == nullptr || roomInfo->obstaclesSpawnPacks->Count() <= 0) return;
    currentSpawnPack = Random::Range(0, roomInfo->enemiesSpawnPacks == nullptr ? 1 : roomInfo->enemiesSpawnPacks->Count());
    DungeonRoomInfo::ObstaclePack* pack = (*roomInfo->obstaclesSpawnPacks)[0];
    // sic: X + X*Y entries, one row more than the room has
    obstaclesGrid.assign(gridSlicesX + gridSlicesX * gridSlicesY, 0);
    DungeonTile noneOrHoles = Random::Range(0.0f, 1.0f) > 0.5f ? DungeonTile::None : DungeonTile::Holes;
    DungeonTile rocksOrHoles = Random::Range(0.0f, 1.0f) > 0.5f ? DungeonTile::Rocks : DungeonTile::Holes;
    for (int i = 0; i < pack->spawns->Count(); i++) {
        DungeonRoomInfo::TileSpawn* s = (*pack->spawns)[i];
        if (static_cast<unsigned>(s->index) >= obstaclesGrid.size()) cs::ThrowIndexOutOfRange();
        obstaclesGrid[s->index] = static_cast<int>(s->tile);
        if (s->tile == DungeonTile::None_Holes) obstaclesGrid[s->index] = static_cast<int>(noneOrHoles);
        if (s->tile == DungeonTile::Holes_Rocks) obstaclesGrid[s->index] = static_cast<int>(rocksOrHoles);
    }
}

int RoomFill::ChooseChampion(bool afternoon)
{
    int num = -1;
    float roll = Random::Range(0.0f, 1.0f);
    if (roomInfo->enemiesSpawnPacks != nullptr && roomInfo->enemiesSpawnPacks->Count() > 0 && roomInfo->canContainChampion) {
        float prob = Constants::GetFloat(afternoon ? "championProbAtNight" : "championProb");
        num = roll < prob ? Random::Range(0, (*roomInfo->enemiesSpawnPacks)[currentSpawnPack]->spawns->Count()) : -1;
    }
    if (roomInfo->enemiesSpawnPacks != nullptr && roomInfo->enemiesSpawnPacks->Count() <= currentSpawnPack) currentSpawnPack = 0;
    return num;
}

std::string RoomFill::GetChestNameByFloor(int dungeonLevel, bool morning, bool intermediate)
{
    std::vector<float> table = Constants::GetFloatArray(morning ? "firstFloorChestProbabilities" : "firstFloorChestProbabilitiesAtNight");
    if (dungeonLevel == 1) table = Constants::GetFloatArray(morning ? "secondFloorChestProbabilities" : "secondFloorChestProbabilitiesAtNight");
    if (dungeonLevel == 2) table = Constants::GetFloatArray(morning ? "thirdFloorChestProbabilities" : "thirdFloorChestProbabilitiesAtNight");
    float num = Random::Range(0.0f, 1.0f);
    int tier = 0;
    for (tier = 0; tier < static_cast<int>(table.size()); tier++) {
        num -= table[tier];
        if (num <= 0.0f) break;
    }
    std::string text = Constants::GetStringArray("ChestsIDs").at(tier);
    if (!intermediate && canContainMimics && !roomInfo->isTimedRoom &&
        Random::Range(0.0f, 1.0f) < Constants::GetFloatArray("mimicChestProbabilities").at(tier))
        text = std::string(CultureName(roomInfo->culture)) + "Mimic" + text;
    return text;
}

std::string RoomFill::GetWandererChestNameByFloor(int dungeonLevel, bool intermediate)
{
    float probs[4] = {
        Constants::GetFloatArray("wandererWoodChestFloorProbabilities").at(dungeonLevel),
        Constants::GetFloatArray("wandererIronChestFloorProbabilities").at(dungeonLevel),
        Constants::GetFloatArray("wandererGoldChestFloorProbabilities").at(dungeonLevel),
        Constants::GetFloatArray("wandererMagicChestFloorProbabilities").at(dungeonLevel),
    };
    float num = Random::Range(0.0f, 1.0f);
    int tier = 0;
    for (tier = 0; tier < 4; tier++) {
        num -= probs[tier];
        if (num <= 0.0f) break;
    }
    std::string text = Constants::GetStringArray("ChestsIDs").at(tier);
    if (!intermediate && canContainMimics && !roomInfo->isTimedRoom &&
        Random::Range(0.0f, 1.0f) < Constants::GetFloatArray("mimicChestProbabilities").at(tier))
        text = std::string(CultureName(roomInfo->culture)) + "Mimic" + text;
    return text;
}

} // namespace game
