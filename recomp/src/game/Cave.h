#pragma once
// Dungeon cave layout: CaveRoomUnit.cs, CaveRoom.cs, SingleCaveRoom.cs, BossCaveRoom.cs,
// CaveGenerator.cs, Miner.cs, CommonRoomMiner.cs, BossRoomMiner.cs, FixedStepsMiner.cs.
// Translated line for line; names match the C# so behaviour can be traced to the decomp.

#include "cs/Dictionary.h"
#include "cs/List.h"
#include "cs/Object.h"
#include "unity/Vector2.h"

namespace game {

using unity::Vector2;

class CaveRoom;
class CaveGenerator;

class CaveRoomUnit : public cs::Object {
public:
    CaveRoom* room = nullptr;
    Vector2 localPos;
    CaveRoom* connectedNorth = nullptr;
    CaveRoom* connectedSouth = nullptr;
    CaveRoom* connectedEast = nullptr;
    CaveRoom* connectedWest = nullptr;
    bool blockedNorth = false;
    bool blockedSouth = false;
    bool blockedEast = false;
    bool blockedWest = false;

    bool Connect(Vector2 v, CaveRoom* room);
};

class CaveRoom : public cs::Object {
public:
    CaveRoom();

    Vector2 position;
    cs::List<Vector2>* pathFromInit;
    cs::List<CaveRoomUnit*>* units = nullptr;

    void AddRoomUnit(CaveRoomUnit* unit);
    int GetConnectedCount();
    void GetAvailableConnectionOffsets(cs::List<Vector2>* availableOffsets);
    bool CanConnectNorth();
    bool CanConnectSouth();
    bool CanConnectEast();
    bool CanConnectWest();

private:
    int anchorIndex = 0;
};

class SingleCaveRoom : public CaveRoom {
public:
    SingleCaveRoom();
};

class BossCaveRoom : public CaveRoom {
public:
    BossCaveRoom();
};

class Miner : public cs::Object {
public:
    bool isDead = false;
    Vector2 currentPosition;
    CaveGenerator* generator = nullptr;

    virtual void ConsumeMovement() {}
    virtual void Die() { isDead = true; }
    virtual void Init(CaveGenerator* generator) { this->generator = generator; }
};

class CaveGenerator : public cs::Object {
public:
    cs::Dictionary<Vector2, CaveRoomUnit*>* usedRoomUnits = nullptr;
    cs::List<CaveRoom*>* rooms = nullptr;
    Vector2 size;
    Vector2 maxPos;
    Vector2 minPos;
    cs::List<Miner*>* miners = nullptr;
    bool isFlipped = false;
    bool areMinersAlive = false;

    void Init();
    bool CanPlaceRoomAt(CaveRoom* caveRoom, Vector2 pos);
    void IterateMinners();
    void KillAllMiners();
    Miner* CreateMiner(Miner* miner);
    Miner* AddMiner(Miner* miner);
    void AddRoom(CaveRoom* room);
    void Flip(Vector2 pivot);

private:
    cs::List<Miner*>* newMiners = nullptr;
};

class CommonRoomMiner : public Miner {
public:
    static cs::List<Vector2>* directions();
    static float changeDirectionProb;
    static float deadProb;
    static float currentChangeDirectionProbIncremment;
    static int maxCommonMiners;

    Vector2 direction;

    void Init(CaveGenerator* generator) override;
    void ConsumeMovement() override;

private:
    void CreateNormalRoom();
    void SpawnMiners();
    void ChooseDirection();

    int sameDirectionMoves = 0;
};

class BossRoomMiner : public Miner {
public:
    BossCaveRoom* bossRoom = nullptr;
    cs::List<Miner*>* positionsToCheck = nullptr;

    void Init(CaveGenerator* generator) override;
    void ConsumeMovement() override;

private:
    void CreateBossRoom();
};

class FixedStepsMiner : public Miner {
public:
    static cs::List<Vector2>* directions();

    void Init(int steps) { _steps = steps; }
    using Miner::Init;
    void ConsumeMovement() override;

private:
    void ChooseDirection();

    int _steps = 0;
    int sameDirectionMoves = 0;
    Vector2 direction;
    const float changeDirectionProb = 0.1f;
    const float currentChangeDirectionProbIncremment = 4.0f;
};

} // namespace game
