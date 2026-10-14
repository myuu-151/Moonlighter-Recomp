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
#include <vector>

#include "bind/UnityEngine.h"
#include "game.h"

namespace standins {

struct Services {
    // The hero's own code (HeroMerchant.DealDamageToPlayer, HeroMerchantController's
    // CheckForHolesAfterPush and GetColliderOffsset: units.txt's `wrap`) stood in for by the hooks
    // below (the enemy replays: his doings are the recording's); false: the game's own runs.
    bool heroStandIns = true;
    // HeroMerchant.DealDamageToPlayer(attackStrength, damageSource, type, enemySource).
    std::function<void(float strength, cs::String* source, int type, gen::Enemy* enemy)> dealDamageToPlayer;
    // HeroMerchantController.AddForce(force): an impulse on the hero's body.
    std::function<void(unity::Vector2 force)> heroAddForce;
    // DungeonRoom.SpawnEnemyDeadSprite(sprite, position).
    std::function<void(gen::DungeonRoom* room, UnityEngine::Sprite* sprite, UnityEngine::Vector3 position)> spawnEnemyDeadSprite;
    // The room's chests (Chest.chestType as ints, in roomChests' order): DungeonRoom.UnLockRoomChests'.
    std::vector<int> roomChestTypes;
    // DungeonManager.SpawnWanderer (the Wanderer's arrival: not stood in for; a driver's check).
    std::function<void()> spawnWanderer;
    // OptionsManager.Get("GAME", "Difficulty", -1).
    int difficulty = -1;
    // TimeManagerV2.CurrrentDayTime == Afternoon.
    bool afternoon = false;
    // HeroMerchantController.GetColliderOffsset() (his feet: the collider's offset, the recording's).
    unity::Vector2 heroColliderOffset;
    // TimeManagerV2.Instance / GameManager.Instance (none: null, as in a scene without them).
    gen::TimeManagerV2* timeManager = nullptr;
    gen::GameManager* gameManager = nullptr;
    // GameManager's state the hero reads: paused (PauseGame sets it), the town's scene loaded (a
    // hit then does nothing: the recordings' "town").
    bool gamePaused = false;
    bool townSceneLoaded = false;
    // ItemDatabase.GetItemByName (the item database: the driver's, from items.json) and
    // ItemStack.Create (an item's object, its UI not made).
    std::function<gen::ItemMaster*(cs::String* name)> itemByName;
    std::function<gen::ItemStack*(gen::ItemMaster* master)> createItemStack;
};
Services& S();

// The HUD's health bar (WillHealthBar) as far as it acts on the hero: subscribed to his health,
// absorption and shield after his own code (WillHealthBar.Init runs after HeroMerchant.Init), each
// drop plays his damaged effect (HeroMerchantController.PlayDamagedEffect, the game's) as the bar
// does: the health falling (PlayAnimation), the absorption below what the bar shows, each shield
// image removed. The bar itself (its sliders, texts, sprites' tweens) is presentation.
class HealthBar : public cs::Object {
public:
    const char* cs_typename() const override { return "standins.HealthBar"; }
    bool cs_traced() const override { return true; }
    void cs_trace(cs::Tracer& t) override { t.Mark(stats_); }
    static HealthBar* Subscribe(gen::HeroMerchantStats* stats);
    void CurrentHealth_OnStateUpdated(float value);
    void Absorption_OnStateUpdated(float value);
    void Shield_OnStateUpdated(float value);

private:
    gen::HeroMerchantStats* stats_ = nullptr;
    int shields_ = 0; // the shield images shown
};

} // namespace standins
