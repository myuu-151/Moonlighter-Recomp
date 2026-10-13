#pragma once
// The scene's services the translated enemies call, stood in for by the replay driver
// (tools/translate/units.txt's `hand` lines declare them; Standins.cpp defines them): what the
// hero's own code does to him (DealDamageToPlayer, AddForce), the camera's shake (its Random
// draws only), the room's dead-sprite stamp and drop bookkeeping, the HUD, the save's
// statistics and analytics, the item drops (their draws: the replays resync Random at each
// Update), the options and the time of day the stats read, the DLC-less achievements.
//
// Each records or answers through Services(), which the driver fills; what a replay doesn't
// expect to reach is NotTranslated (an error naming it) rather than a silent guess.

#include <functional>

#include "bind/UnityEngine.h"
#include "game.h"

namespace standins {

struct Services {
    // HeroMerchant.DealDamageToPlayer(attackStrength, damageSource, type, enemySource).
    std::function<void(float strength, cs::String* source, int type, gen::Enemy* enemy)> dealDamageToPlayer;
    // HeroMerchantController.AddForce(force): an impulse on the hero's body.
    std::function<void(unity::Vector2 force)> heroAddForce;
    // DungeonRoom.SpawnEnemyDeadSprite(sprite, position).
    std::function<void(gen::DungeonRoom* room, UnityEngine::Sprite* sprite, UnityEngine::Vector3 position)> spawnEnemyDeadSprite;
    // OptionsManager.Get("GAME", "Difficulty", -1).
    int difficulty = -1;
    // TimeManagerV2.CurrrentDayTime == Afternoon.
    bool afternoon = false;
    // TimeManagerV2.Instance / GameManager.Instance (none: null, as in a scene without them).
    gen::TimeManagerV2* timeManager = nullptr;
    gen::GameManager* gameManager = nullptr;
};
Services& S();

} // namespace standins
