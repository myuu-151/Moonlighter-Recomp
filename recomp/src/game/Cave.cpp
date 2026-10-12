#include "game/Cave.h"

#include "unity/Random.h"

namespace game {

namespace Random = unity::Random;

// ---- CaveRoomUnit ---------------------------------------------------------------------------

bool CaveRoomUnit::Connect(Vector2 v, CaveRoom* room)
{
    if (v.x != 0.0f) {
        if (v.x > 0.0f) {
            if (blockedEast) return false;
            connectedEast = room;
        } else {
            if (blockedWest) return false;
            connectedWest = room;
        }
    } else if (v.y != 0.0f) {
        if (v.y > 0.0f) {
            if (blockedNorth) return false;
            connectedNorth = room;
        } else {
            if (blockedSouth) return false;
            connectedSouth = room;
        }
    }
    return true;
}

// ---- CaveRoom -------------------------------------------------------------------------------

CaveRoom::CaveRoom() : pathFromInit(cs::gcnew<cs::List<Vector2>>()) {}

void CaveRoom::AddRoomUnit(CaveRoomUnit* unit)
{
    units->Add(unit);
    unit->room = this;
}

int CaveRoom::GetConnectedCount()
{
    int num = 0;
    for (CaveRoomUnit* unit : *units) {
        if (unit->connectedEast != nullptr) num++;
        if (unit->connectedNorth != nullptr) num++;
        if (unit->connectedSouth != nullptr) num++;
        if (unit->connectedWest != nullptr) num++;
    }
    return num;
}

void CaveRoom::GetAvailableConnectionOffsets(cs::List<Vector2>* availableOffsets)
{
    CaveRoomUnit* u = (*units)[0];
    if (!u->blockedEast && u->connectedEast == nullptr) availableOffsets->Add(Vector2(1.0f, 0.0f));
    if (!u->blockedNorth && u->connectedNorth == nullptr) availableOffsets->Add(Vector2(0.0f, 1.0f));
    if (!u->blockedSouth && u->connectedSouth == nullptr) availableOffsets->Add(Vector2(0.0f, -1.0f));
    if (!u->blockedWest && u->connectedWest == nullptr) availableOffsets->Add(Vector2(-1.0f, 0.0f));
}

bool CaveRoom::CanConnectNorth() { return units->FindIndex([](CaveRoomUnit* x) { return x->connectedNorth == nullptr; }) != -1; }
bool CaveRoom::CanConnectSouth() { return units->FindIndex([](CaveRoomUnit* x) { return x->connectedSouth == nullptr; }) != -1; }
bool CaveRoom::CanConnectEast() { return units->FindIndex([](CaveRoomUnit* x) { return x->connectedEast == nullptr; }) != -1; }
bool CaveRoom::CanConnectWest() { return units->FindIndex([](CaveRoomUnit* x) { return x->connectedWest == nullptr; }) != -1; }

SingleCaveRoom::SingleCaveRoom()
{
    CaveRoomUnit* item = cs::gcnew<CaveRoomUnit>();
    item->room = this;
    units = cs::gcnew<cs::List<CaveRoomUnit*>>();
    units->Add(item);
}

BossCaveRoom::BossCaveRoom()
{
    units = cs::gcnew<cs::List<CaveRoomUnit*>>();
    for (int i = -1; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            CaveRoomUnit* u = cs::gcnew<CaveRoomUnit>();
            u->localPos = Vector2(static_cast<float>(i), static_cast<float>(j));
            u->room = this;
            AddRoomUnit(u);
        }
    }
}

// ---- CaveGenerator --------------------------------------------------------------------------

void CaveGenerator::Init()
{
    usedRoomUnits = cs::gcnew<cs::Dictionary<Vector2, CaveRoomUnit*>>();
    rooms = cs::gcnew<cs::List<CaveRoom*>>();
    miners = cs::gcnew<cs::List<Miner*>>();
    newMiners = cs::gcnew<cs::List<Miner*>>();
    areMinersAlive = true;
}

bool CaveGenerator::CanPlaceRoomAt(CaveRoom* caveRoom, Vector2 pos)
{
    for (int i = 0; i < caveRoom->units->Count(); i++) {
        Vector2 key = (*caveRoom->units)[i]->localPos + pos;
        if (usedRoomUnits->ContainsKey(key)) return false;
    }
    return true;
}

void CaveGenerator::IterateMinners()
{
    int num = 0;
    for (int i = 0; i < miners->Count(); i++) {
        Miner* miner = (*miners)[i];
        if (!miner->isDead) {
            miner->ConsumeMovement();
            num++;
        }
    }
    for (Miner* newMiner : *newMiners) {
        num++;
        miners->Add(newMiner);
    }
    newMiners->Clear();
    areMinersAlive = num > 0;
}

void CaveGenerator::KillAllMiners()
{
    for (Miner* miner : *miners) miner->isDead = true;
    newMiners->Clear();
}

Miner* CaveGenerator::CreateMiner(Miner* miner)
{
    miner->Init(this);
    miners->Add(miner);
    return miner;
}

Miner* CaveGenerator::AddMiner(Miner* miner)
{
    miner->Init(this);
    newMiners->Add(miner);
    return miner;
}

void CaveGenerator::AddRoom(CaveRoom* room)
{
    rooms->Add(room);
    for (CaveRoomUnit* unit : *room->units) {
        Vector2 key = room->position + unit->localPos;
        if (key.x > maxPos.x) maxPos.x = key.x;
        if (key.y > maxPos.y) maxPos.y = key.y;
        if (key.x < minPos.x) minPos.x = key.x;
        if (key.y < minPos.y) minPos.y = key.y;
        size = maxPos - minPos;
        usedRoomUnits->Set(key, unit);
    }
}

void CaveGenerator::Flip(Vector2 pivot)
{
    isFlipped = true;
    usedRoomUnits->Clear();
    for (int i = 0; i < rooms->Count(); i++) {
        CaveRoom* caveRoom = (*rooms)[i];
        caveRoom->position = Vector2(pivot.x - (caveRoom->position.x - pivot.x), pivot.y - (caveRoom->position.y - pivot.y));
        for (int j = 0; j < caveRoom->units->Count(); j++) {
            CaveRoomUnit* u = (*caveRoom->units)[j];
            u->localPos = -u->localPos;
            CaveRoom* e = u->connectedEast;
            CaveRoom* n = u->connectedNorth;
            CaveRoom* s = u->connectedSouth;
            CaveRoom* w = u->connectedWest;
            u->connectedEast = w;
            u->connectedNorth = s;
            u->connectedSouth = n;
            u->connectedWest = e;
            bool be = u->blockedEast;
            bool bn = u->blockedNorth;
            bool bs = u->blockedSouth;
            bool bw = u->blockedWest;
            u->blockedEast = bw;
            u->blockedNorth = bs;
            u->blockedSouth = bn;
            u->blockedWest = be;
            usedRoomUnits->Add(caveRoom->position + u->localPos, u);
        }
        for (int k = 0; k < caveRoom->pathFromInit->Count(); k++) {
            Vector2 v = (*caveRoom->pathFromInit)[k];
            caveRoom->pathFromInit->Set(k, Vector2(pivot.x - (v.x - pivot.x), pivot.y - (v.y - pivot.y)));
        }
    }
}

// ---- CommonRoomMiner ------------------------------------------------------------------------

namespace {
cs::List<Vector2>* MakeDirections()
{
    // Static lists live for the whole program, outside any per-floor heap.
    static cs::Heap heap;
    auto* l = heap.New<cs::List<Vector2>>();
    l->Add(Vector2::right());
    l->Add(Vector2::up());
    l->Add(Vector2::left());
    l->Add(Vector2::down());
    return l;
}
} // namespace

cs::List<Vector2>* CommonRoomMiner::directions()
{
    static cs::List<Vector2>* d = MakeDirections();
    return d;
}

float CommonRoomMiner::changeDirectionProb = 0.1f;
float CommonRoomMiner::deadProb = 0.12f;
float CommonRoomMiner::currentChangeDirectionProbIncremment = 4.0f;
int CommonRoomMiner::maxCommonMiners = 200;

void CommonRoomMiner::CreateNormalRoom()
{
    float threshold = changeDirectionProb * static_cast<float>(sameDirectionMoves);
    threshold = threshold * currentChangeDirectionProbIncremment;
    if (Random::Range(0.0f, 1.0f) < threshold) {
        ChooseDirection();
        sameDirectionMoves = 0;
    }
    sameDirectionMoves++;
    if (!generator->usedRoomUnits->ContainsKey(currentPosition + direction)) {
        int num = 0;
        if (generator->usedRoomUnits->ContainsKey(currentPosition + direction + Vector2::up())) num++;
        if (generator->usedRoomUnits->ContainsKey(currentPosition + direction - Vector2::up())) num++;
        if (generator->usedRoomUnits->ContainsKey(currentPosition + direction + Vector2::right())) num++;
        if (generator->usedRoomUnits->ContainsKey(currentPosition + direction - Vector2::right())) num++;
        if (num < 3) {
            SingleCaveRoom* singleCaveRoom = cs::gcnew<SingleCaveRoom>();
            singleCaveRoom->position = currentPosition + direction;
            bool flag = true;
            if (generator->usedRoomUnits->ContainsKey(currentPosition)) {
                CaveRoomUnit* caveRoomUnit = generator->usedRoomUnits->Get(currentPosition);
                flag = flag && (*singleCaveRoom->units)[0]->Connect(-direction, caveRoomUnit->room);
                if (flag) flag = flag && caveRoomUnit->Connect(direction, singleCaveRoom);
                if (flag) {
                    singleCaveRoom->pathFromInit = cs::List<Vector2>::CopyOf(caveRoomUnit->room->pathFromInit);
                    singleCaveRoom->pathFromInit->Add(singleCaveRoom->position);
                }
            }
            if (flag) {
                currentPosition = currentPosition + direction;
                generator->AddRoom(singleCaveRoom);
            } else {
                ChooseDirection();
            }
        } else {
            ChooseDirection();
        }
    } else if (generator->usedRoomUnits->Get(currentPosition + direction)->room != nullptr) {
        currentPosition = currentPosition + direction;
    } else {
        ChooseDirection();
    }
}

void CommonRoomMiner::SpawnMiners()
{
    bool flag = true;
    cs::List<Vector2>* dirs = directions();
    for (int i = 0; i < dirs->Count(); i++) {
        if (!generator->usedRoomUnits->ContainsKey(currentPosition + (*dirs)[i])) {
            flag = false;
            break;
        }
    }
    if (!flag) {
        for (int j = 0; j < 2; j++) {
            CommonRoomMiner* m = cs::gcnew<CommonRoomMiner>();
            m->currentPosition = currentPosition;
            m->direction = direction;
            generator->AddMiner(m);
        }
    }
    Die();
}

void CommonRoomMiner::ChooseDirection()
{
    cs::List<Vector2>* dirs = directions();
    int num = dirs->IndexOf(-direction);
    int num2 = Random::Range(0, dirs->Count());
    if (num >= 0) {
        num2 = Random::Range(0, dirs->Count() - 1);
        if (num2 == num) num2 = (num2 + 1) % dirs->Count();
    }
    direction = (*dirs)[num2];
}

void CommonRoomMiner::Init(CaveGenerator* gen)
{
    Miner::Init(gen);
    ChooseDirection();
}

void CommonRoomMiner::ConsumeMovement()
{
    if (!isDead) {
        if (Random::Range(0.0f, 1.0f) < deadProb && generator->miners->Count() < maxCommonMiners) SpawnMiners();
        else CreateNormalRoom();
    }
}

// ---- BossRoomMiner --------------------------------------------------------------------------

void BossRoomMiner::Init(CaveGenerator* gen)
{
    Miner::Init(gen);
    // Aliases the generator's miner list: RemoveAt below removes from generator->miners itself.
    positionsToCheck = gen->miners;
    bossRoom = cs::gcnew<BossCaveRoom>();
    while (positionsToCheck->Count() > 0) {
        int index = Random::Range(0, positionsToCheck->Count() - 1);
        currentPosition = (*positionsToCheck)[index]->currentPosition;
        bool flag = true;
        for (int i = 0; i < bossRoom->units->Count(); i++) {
            Vector2 key = currentPosition + Vector2::up() + (*bossRoom->units)[i]->localPos;
            if (generator->usedRoomUnits->ContainsKey(key)) {
                flag = false;
                break;
            }
        }
        positionsToCheck->RemoveAt(index);
        if (flag) {
            CreateBossRoom();
            break;
        }
    }
}

void BossRoomMiner::ConsumeMovement()
{
    Miner::ConsumeMovement();
    bool flag = true;
    for (int i = 0; i < bossRoom->units->Count(); i++) {
        Vector2 key = currentPosition + Vector2::up() + (*bossRoom->units)[i]->localPos;
        if (generator->usedRoomUnits->ContainsKey(key)) {
            flag = false;
            break;
        }
    }
    if (!flag) {
        Vector2 up = Vector2::up();
        SingleCaveRoom* singleCaveRoom = cs::gcnew<SingleCaveRoom>();
        singleCaveRoom->position = currentPosition + up;
        bool flag2 = true;
        if (generator->usedRoomUnits->ContainsKey(currentPosition)) {
            CaveRoomUnit* caveRoomUnit = generator->usedRoomUnits->Get(currentPosition);
            flag2 = flag2 && caveRoomUnit->Connect(up, singleCaveRoom);
            if (flag2) flag2 = flag2 && (*singleCaveRoom->units)[0]->Connect(-up, caveRoomUnit->room);
            if (flag2) {
                singleCaveRoom->pathFromInit = cs::List<Vector2>::CopyOf(caveRoomUnit->room->pathFromInit);
                singleCaveRoom->pathFromInit->Add(currentPosition);
            }
        }
        if (flag2) {
            currentPosition = currentPosition + up;
            generator->AddRoom(singleCaveRoom);
        }
    } else {
        CreateBossRoom();
    }
}

void BossRoomMiner::CreateBossRoom()
{
    Vector2 position = currentPosition + Vector2::up();
    bossRoom->position = position;
    for (CaveRoomUnit* unit : *bossRoom->units) {
        if (unit->localPos == Vector2::zero()) {
            CaveRoomUnit* here = generator->usedRoomUnits->Get(currentPosition);
            unit->Connect(-Vector2::up(), here->room);
            here->Connect(Vector2::up(), bossRoom);
            bossRoom->pathFromInit = cs::List<Vector2>::CopyOf(here->room->pathFromInit);
            bossRoom->pathFromInit->Add(currentPosition);
        }
    }
    // Added to rooms only: the boss room's units never enter usedRoomUnits or the bounds.
    generator->rooms->Add(bossRoom);
    Die();
}

// ---- FixedStepsMiner ------------------------------------------------------------------------

cs::List<Vector2>* FixedStepsMiner::directions()
{
    static cs::List<Vector2>* d = MakeDirections();
    return d;
}

void FixedStepsMiner::ConsumeMovement()
{
    if (_steps <= 0) {
        Die();
        return;
    }
    float threshold = changeDirectionProb * static_cast<float>(sameDirectionMoves);
    threshold = threshold * currentChangeDirectionProbIncremment;
    if (Random::Range(0.0f, 1.0f) < threshold) {
        ChooseDirection();
        sameDirectionMoves = 0;
    }
    sameDirectionMoves++;
    if (generator->usedRoomUnits->ContainsKey(currentPosition + direction)) return;
    SingleCaveRoom* singleCaveRoom = cs::gcnew<SingleCaveRoom>();
    singleCaveRoom->position = currentPosition + direction;
    bool flag = true;
    if (generator->usedRoomUnits->ContainsKey(currentPosition)) {
        CaveRoomUnit* caveRoomUnit = generator->usedRoomUnits->Get(currentPosition);
        flag = flag && (*singleCaveRoom->units)[0]->Connect(-direction, caveRoomUnit->room);
        if (flag) flag = flag && caveRoomUnit->Connect(direction, singleCaveRoom);
        if (flag) {
            singleCaveRoom->pathFromInit = cs::List<Vector2>::CopyOf(caveRoomUnit->room->pathFromInit);
            singleCaveRoom->pathFromInit->Add(singleCaveRoom->position);
        }
    }
    if (flag) {
        currentPosition = currentPosition + direction;
        _steps--;
        generator->AddRoom(singleCaveRoom);
    } else {
        ChooseDirection();
    }
}

void FixedStepsMiner::ChooseDirection()
{
    cs::List<Vector2>* dirs = directions();
    int num = dirs->IndexOf(-direction);
    int num2 = Random::Range(0, dirs->Count());
    if (num >= 0) {
        num2 = Random::Range(0, dirs->Count() - 1);
        if (num2 == num) num2 = (num2 + 1) % dirs->Count();
    }
    direction = (*dirs)[num2];
}

} // namespace game
