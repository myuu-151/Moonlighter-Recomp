#pragma once
// For the oracle tests built against the translated game code (ML_TRANSLATED=ON): the hand
// port's game/Cave.h names, naming the classes tools/translate made from CaveGenerator.cs,
// CaveRoom.cs, ... (recomp/gen/cpp). Same members: the translator keeps the C# names.

#include "game.h"

namespace game {

using unity::Vector2;

using CaveRoomUnit = ::gen::CaveRoomUnit;
using CaveRoom = ::gen::CaveRoom;
using SingleCaveRoom = ::gen::SingleCaveRoom;
using BossCaveRoom = ::gen::BossCaveRoom;
using Miner = ::gen::Miner;
using CaveGenerator = ::gen::CaveGenerator;
using CommonRoomMiner = ::gen::CommonRoomMiner;
using BossRoomMiner = ::gen::BossRoomMiner;
using FixedStepsMiner = ::gen::FixedStepsMiner;

} // namespace game
