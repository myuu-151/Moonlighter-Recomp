#pragma once
// The hero in the enemy replays (enemy_translated_test, tangle_translated_test): his body, moves and
// weapon's hits are the recording's, his own code the game's, translated: HeroMerchant.DealDamageToPlayer
// (what a hit takes: the shield, the absorption, the health; invincibility, the blink, his intangible
// layer; its sound's and the camera shake's Random draws, an ItemDrain's drops),
// HeroMerchantController.CheckForHolesAfterPush (the hole check after an enemy's push) and
// GetColliderOffsset. His controller's own messages (Start, Update) don't run: his doings are the
// recording's (its Start's holesMask set as Start sets it).
//
// His stats: the recording's start line's ("hero" "stats", the oracle's since 2026-10-14); a recording
// without them takes the oracle's save slot as every -oracle-* run loads it, which walk.jsonl's
// walkStart records at a floor's start (the replays' recordings start where he enters the enemy's
// room, unhit). The oracle refills his health after each hit (EnemyOracle's DamageToPlayerHook:
// RecoverAllHealth, after DealDamageToPlayer): so does the replay.

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#include "bind/UnityEngine.h"
#include "game.h"
#include "scene/Standins.h"
#include "util/Json.h"

namespace hero_replay {

struct Hero {
    UnityEngine::GameObject* go = nullptr;
    gen::HeroMerchant* merchant = nullptr;
    gen::HeroMerchantController* controller = nullptr;
    gen::HeroMerchantStats* stats = nullptr;
    UnityEngine::BoxCollider2D* collider = nullptr; // his body's (the recording's body steps in the world)
    UnityEngine::Rigidbody2D* rigidbody = nullptr;  // AddForce's (outside the world: the pushes reported)
    std::string statsFrom;                          // where his stats came from (the replay's note)
};

inline float StatBits(const json::Value& v)
{
    int32_t i = static_cast<int32_t>(v.AsInt());
    float f;
    std::memcpy(&f, &i, 4);
    return f;
}

// The save's hero as walk.jsonl (next to the recording) has him: its walkStart, or null.
inline json::Value SaveHeroStats(const std::string& recordingPath)
{
    const std::string dir = recordingPath.substr(0, recordingPath.find_last_of("/\\") + 1);
    std::ifstream in(dir + "walk.jsonl");
    std::string line;
    if (!in || !std::getline(in, line)) return json::Value();
    json::Value v = json::Parse(line);
    if (v["ev"].s != "walkStart" || !v.Find("maxHealth")) return json::Value();
    return v;
}

// The hero's objects and code, and the services his DealDamageToPlayer reaches (GameManager's
// state, the GUI's panels, the HUD the stats' Init reaches). `heroLine`: the recording's "hero".
// Returns false (what's missing printed) if his stats can't be told.
inline bool Make(Hero& h, const json::Value& heroLine, const std::string& recordingPath)
{
    using UnityEngine::GameObject;
    auto S = [](const char* s) { return cs::String::FromUtf8(s); };
    // The services: GameManager (the save's slot: not New Game+), the GUI (no panel open), the HUD.
    auto* gameManager = cs::gcnew<gen::GameManager>();
    gameManager->currentGameSlot = cs::gcnew<gen::GameSlot>();
    gameManager->currentGameSlot->gameType = gen::GameSlot_GameType::Normal;
    standins::S().gameManager = gameManager;
    auto* gui = cs::gcnew<gen::GUIManager>();
    gui->canOpenPanels = true;
    gen::GUIManager::Instance = gui;
    if (!gen::HUDManager::Instance) gen::HUDManager::Instance = cs::gcnew<gen::HUDManager>();
    GameObject* hudGo = GameObject::Create("HUDSPanel");
    hudGo->components_.push_back(gen::HUDManager::Instance);
    gen::HUDManager::Instance->gameObject_ = hudGo;
    gui->HUDSPanel = gen::HUDManager::Instance;
    if (!gen::HUDManager::Instance->pendantShortcutHUD) gen::HUDManager::Instance->pendantShortcutHUD = cs::gcnew<gen::PendantShortcutHUD>();

    // His objects: HeroMerchant (HeroMerchant.Instance finds it), his controller, stats and inventory,
    // his art (the blink's), his body's collider.
    h.go = GameObject::Create("HeroMerchant");
    h.go->tag_ = cs::String::Intern(S("Player"));
    h.go->layer_ = static_cast<int>(heroLine["layer"].AsInt());
    GameObject* art = GameObject::Create("HeroMerchantArt", h.go->transform_);
    GameObject* will = GameObject::Create("Will", art->transform_);
    will->Attach(cs::gcnew<UnityEngine::SpriteRenderer>());
    h.merchant = cs::New<gen::HeroMerchant>();
    h.controller = cs::New<gen::HeroMerchantController>();
    h.stats = cs::New<gen::HeroMerchantStats>();
    h.go->Attach(h.merchant);
    // (his controller's messages are his doings, the recording's here: off, no Start or Update)
    h.controller->enabled_ = false;
    h.go->Attach(h.controller);
    h.go->Attach(h.stats);
    h.collider = cs::gcnew<UnityEngine::BoxCollider2D>();
    h.collider->offset_ = {StatBits(heroLine["colliderOffset"][0]), StatBits(heroLine["colliderOffset"][1])};
    h.collider->size_ = {StatBits(heroLine["colliderSize"][0]), StatBits(heroLine["colliderSize"][1])};
    h.go->Attach(h.collider);
    h.rigidbody = cs::gcnew<UnityEngine::Rigidbody2D>();
    h.merchant->heroMerchantController = h.controller;
    h.merchant->heroMerchantStats = h.stats;
    h.merchant->heroMerchantInventory = cs::gcnew<gen::HeroMerchantInventory>();
    h.merchant->currentMode = gen::HeroMerchant_HeroMerchantMode::DungeonMode;
    h.controller->heroMerchant = h.merchant;
    h.controller->characterCollider = h.collider;
    h.controller->characterRigidbody = h.rigidbody;
    h.controller->heroMerchantArt = art;
    h.controller->_canMove = true;
    // (Start's: the holes' layer)
    h.controller->holesMask = UnityEngine::LayerMask::op_Implicit_LayerMask(1 << UnityEngine::LayerMask::NameToLayer(S("SunkenObstacle")));

    // His stats: HeroMerchantStats.Init's, then the recording's (else the save's).
    h.stats->Init();
    json::Value saved;
    const json::Value* st = heroLine.Find("stats");
    if (st) h.statsFrom = "the recording's";
    else {
        saved = SaveHeroStats(recordingPath);
        if (saved.IsNull()) {
            std::printf("the recording has no hero stats, and there is no walk.jsonl next to it for the save's\n");
            return false;
        }
        st = &saved;
        h.statsFrom = "the save's (walk.jsonl's walkStart)";
    }
    using Obscured = CodeStage::AntiCheat::ObscuredTypes::ObscuredFloat;
    auto set = [](gen::Stat* s, float v) { s->_value = Obscured::op_Implicit(v); };
    h.stats->currentHealth->_maxValue = Obscured::op_Implicit(StatBits((*st)["maxHealth"]));
    set(h.stats->currentHealth, StatBits((*st)["health"]));
    set(h.stats->defense, StatBits((*st)["defense"]));
    set(h.stats->strength, StatBits((*st)["strength"]));
    h.stats->absorption->_maxValue = Obscured::op_Implicit(StatBits((*st)["absorptionMax"]));
    set(h.stats->absorption, StatBits((*st)["absorption"]));
    set(h.stats->shield, StatBits((*st)["shield"]));
    // The oracle's refill after each hit (RecoverAllHealth: the game's).
    standins::S().heroStandIns = false;
    standins::S().afterDealDamageToPlayer = [stats = h.stats] { stats->RecoverAllHealth(); };
    return true;
}

} // namespace hero_replay
