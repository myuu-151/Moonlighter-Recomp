// The `hand` members of tools/translate/units.txt: the scene's services, as Standins.h says.

#include "Standins.h"

#include <algorithm>

#include "game/EnemyRoom.h"
#include "unity/Tween.h"
#include "util/Json.h"

namespace standins {

Services& S()
{
    static Services s;
    return s;
}

namespace {

// The DLC-less platform's achievements: counting toward one is the save's, not the game's.
class Achievements : public cs::Object, public gen::DigitalSunGames::Platforms::IAchievementController {
public:
    void IAchievementController_AddToStatValue(cs::String* statID, float value) override { (void)statID, (void)value; }
    void IAchievementController_UnlockAchievement(cs::String* achievementID) override { (void)achievementID; }
};

} // namespace

} // namespace standins

using standins::S;

namespace gen {

// ---- the hero: his own code (the game's), or the recording's where a replay stands in for him -----------

void HeroMerchant::DealDamageToPlayer(float p_attackStrength, cs::String* p_damageSource, HeroMerchant_DamageType p_type, Enemy* p_enemySource)
{
    if (!S().heroStandIns) {
        DealDamageToPlayer__game(p_attackStrength, p_damageSource, p_type, p_enemySource);
        return;
    }
    if (S().dealDamageToPlayer) S().dealDamageToPlayer(p_attackStrength, p_damageSource, static_cast<int>(p_type), p_enemySource);
}

// The hero's hole check after a push (EnemyBehaviour.PushPlayerBackwards' WaitAndCheckForHoles): stood
// in for, his fall is his own doing, in the recording: none here.
bool HeroMerchantController::CheckForHolesAfterPush() { return S().heroStandIns ? false : CheckForHolesAfterPush__game(); }

unity::Vector2 HeroMerchantController::GetColliderOffsset() { return S().heroStandIns ? S().heroColliderOffset : GetColliderOffsset__game(); }

// The pad's rumble (GolemSoldierBehaviour.PlayHitOnGroundSound, the hero's hits and roll): presentation.
void HeroMerchantController::VibrateForTime(float p_intensity, float p_time) { (void)p_intensity, (void)p_time; }
void HeroMerchantController::VibrateForTime(float p_intensityLeftMotor, float p_intensityRightMotor, float p_time)
{
    (void)p_intensityLeftMotor, (void)p_intensityRightMotor, (void)p_time;
}

// ---- the save's statistics, analytics, achievements: not the game's state ------------------------------

HeroStatistics* HeroStatistics::get_Instance()
{
    static HeroStatistics* instance = [] {
        cs::StaticScope keep;
        return cs::gcnew<HeroStatistics>();
    }();
    return instance;
}
void HeroStatistics::RegisterGolemBabySlimeDeath() {}
void HeroStatistics::RegisterForestBabySlimeDeath() {}
void HeroStatistics::RegisterDesertBabySlimeDeath() {}
void HeroStatistics::RegisterTechBabySlimeDeath() {}
void HeroStatistics::RegisterGolemSoldierDeath() {}
void HeroStatistics::RegisterGolemHermitDeath() {}
void HeroStatistics::RegisterGolemKamikazeDeath() {}
void HeroStatistics::RegisterFlyingKamikazeDeath() {}
void HeroStatistics::RegisterGolemTurretDeath() {}
void HeroStatistics::RegisterGolemTangleDeath() {}
void HeroStatistics::RegisterForestTangleDeath() {}

void MoonlighterAnalytics::EnemySpawnLogEvent(cs::String* p_enemyName) { (void)p_enemyName; }
void MoonlighterAnalytics::EnemyKilledLogEvent(cs::String* p_enemyName) { (void)p_enemyName; }

DigitalSunGames::Platforms::IAchievementController* DigitalSunGames::Platforms::Platform::get_Achievements()
{
    static standins::Achievements* a = [] {
        cs::StaticScope keep;
        return cs::gcnew<standins::Achievements>();
    }();
    return a;
}

} // namespace gen

// ---- the HUD's health bar: what it does to the hero (Standins.h) --------------------------------------------

namespace {
float StatValue(gen::Stat* s) { return CodeStage::AntiCheat::ObscuredTypes::ObscuredFloat::op_Implicit(cs::nc(s)->_value); }
gen::HeroMerchantController* Controller()
{
    gen::HeroMerchant* hero = gen::HeroMerchant::get_Instance();
    return hero ? hero->heroMerchantController : nullptr;
}
// WillHealthBar's colours (its Color32s as Color: each / 255).
UnityEngine::Color Color32(int r, int g, int b) { return UnityEngine::ColorOf(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f); }
} // namespace

namespace standins {

HealthBar* HealthBar::Subscribe(gen::HeroMerchantStats* stats)
{
    auto* bar = cs::gcnew<HealthBar>();
    bar->stats_ = stats;
    bar->shields_ = static_cast<int>(StatValue(stats->shield)); // (Init: Shield_OnStateUpdated adds them)
    using F = cs::Delegate<void(float)>;
    stats->currentHealth->add_OnStateUpdated(F::Bind<HealthBar, &HealthBar::CurrentHealth_OnStateUpdated>(bar));
    stats->absorption->add_OnStateUpdated(F::Bind<HealthBar, &HealthBar::Absorption_OnStateUpdated>(bar));
    stats->shield->add_OnStateUpdated(F::Bind<HealthBar, &HealthBar::Shield_OnStateUpdated>(bar));
    return bar;
}

// PlayAnimation(finalValue): finalValue - currentHealth.Value (the stat not yet set: its subscribers
// run first) below 0 plays the damaged effect.
void HealthBar::CurrentHealth_OnStateUpdated(float value)
{
    if (value - StatValue(stats_->currentHealth) < 0.0f)
        if (auto* c = Controller()) c->PlayDamagedEffect(Color32(232, 71, 71));
}

// obj < absorptionSlider.value (the bar shows the stat's value once its 0.2 s tween is done).
void HealthBar::Absorption_OnStateUpdated(float value)
{
    if (value < StatValue(stats_->absorption))
        if (auto* c = Controller()) c->PlayDamagedEffect(Color32(232, 71, 71));
}

// (int)obj shields shown: each removed plays the damaged effect.
void HealthBar::Shield_OnStateUpdated(float value)
{
    const int want = static_cast<int>(value);
    while (shields_ > want) {
        shields_--;
        if (auto* c = Controller()) c->PlayDamagedEffect(Color32(30, 118, 91));
    }
    shields_ = want;
}

} // namespace standins

namespace gen {

// ---- the items: the database and the items' objects ------------------------------------------------------

ItemMaster* ItemDatabase::GetItemByName(cs::String* p_name)
{
    if (!S().itemByName) cs::NotTranslated("ItemDatabase.GetItemByName (the item database: not stood in for)");
    return S().itemByName(p_name);
}
ItemMaster* ItemDatabase::GetItemByName(cs::String* p_name, int32_t p_plusLevel)
{
    if (p_plusLevel != 0) cs::NotTranslated("ItemDatabase.GetItemByName of a New Game+ level");
    return GetItemByName(p_name);
}
ItemStack* ItemStack::Create(ItemMaster* p_master, int32_t p_quantity)
{
    if (p_quantity != 1) cs::NotTranslated("ItemStack.Create of a stack");
    if (!S().createItemStack) cs::NotTranslated("ItemStack.Create (the items' objects: not stood in for)");
    return p_master ? S().createItemStack(p_master) : nullptr;
}
// The item's icon, its canvas: presentation.
void ItemStack::Hide() {}
void ItemStack::SetParent(UnityEngine::Transform* p_p, bool p_reset) { (void)p_p, (void)p_reset; }

} // namespace gen

namespace gen {

// ---- the game's managers ------------------------------------------------------------------------------

GameManager* GameManager::get_Instance() { return S().gameManager; }
bool GameManager::get_isGamePaused() { return S().gamePaused; }
bool GameManager::IsTownSceneLoaded() { return S().townSceneLoaded; }
bool GameManager::IsWillInDungeon() { return !S().townSceneLoaded; }
bool GameManager::get_NotLoadingScene() { return true; }
// (the release build: Application.isEditor and Debug.isDebugBuild off, the constants' developerEnabled off)
bool GameManager::get_IsDebugEnabled() { return false; }
// The keyboards' and mice's lights: presentation.
void GameManager::SetDamageRedFlashLightsForLogitech() {}
void GameManager::SetDamageRedFlashLightsForRazer() {}
// PauseGame: Time.timeScale 0 and paused (the driver's clocks read them).
void GameManager::PauseGame(float p_delay)
{
    (void)p_delay;
    UnityEngine::Time::timeScale = 0.0f;
    S().gamePaused = true;
}

// The GUI's red flash over the screen (an Image's fade): presentation.
void GUIManager::Flash(UnityEngine::Color p_color, float p_duration) { (void)p_color, (void)p_duration; }

// The pad's settings: not inverted (InControl's default).
bool InControl::InputManager::get_InvertYAxis() { return false; }
void InControl::InputManager::set_InvertYAxis(bool p_value)
{
    if (p_value) cs::NotTranslated("InControl.InputManager.InvertYAxis set");
}
void GameManager::SetGameSpeedForTime(float p_timeScaleValue, float p_duration, bool p_ignorePreviousTimeScale)
{
    (void)p_timeScaleValue, (void)p_duration, (void)p_ignorePreviousTimeScale;
    cs::NotTranslated("GameManager.SetGameSpeedForTime (the time scale: not stood in for)");
}

int32_t OptionsManager::Get(cs::String* p_section, cs::String* p_key, int32_t p_def)
{
    if (p_section && p_key && p_section->ToUtf8() == "GAME" && p_key->ToUtf8() == "Difficulty") return S().difficulty;
    return p_def;
}
bool OptionsManager::Get(cs::String* p_section, cs::String* p_key, bool p_def)
{
    (void)p_section, (void)p_key;
    return p_def;
}
cs::String* OptionsManager::Get(cs::String* p_section, cs::String* p_key, cs::String* p_def)
{
    (void)p_section, (void)p_key;
    return p_def;
}
cs::Array<uint8_t>* OptionsManager::Get(cs::String* p_section, cs::String* p_key, cs::Array<uint8_t>* p_def)
{
    (void)p_section, (void)p_key;
    return p_def;
}
double OptionsManager::Get(cs::String* p_section, cs::String* p_key, double p_def)
{
    (void)p_section, (void)p_key;
    return p_def;
}
int64_t OptionsManager::Get(cs::String* p_section, cs::String* p_key, int64_t p_def)
{
    (void)p_section, (void)p_key;
    return p_def;
}

TimeManagerV2* TimeManagerV2::get_Instance() { return S().timeManager; }
DayTime TimeManagerV2::get_CurrrentDayTime() { return S().afternoon ? DayTime::Afternoon : DayTime::Morning; }

// ---- the room -----------------------------------------------------------------------------------------

// The dead sprite's stamp (units.txt's `wrap`): the game's (its Random draw for the flip, the stamp's
// pixels: presentation), the replay told where it goes.
void DungeonRoom::SpawnEnemyDeadSprite(UnityEngine::Sprite* p_s, UnityEngine::Vector3 p_position)
{
    if (S().spawnEnemyDeadSprite) S().spawnEnemyDeadSprite(this, p_s, p_position);
    SpawnEnemyDeadSprite__game(p_s, p_position);
}

// UnLockRoomChests: each of roomChests unlocked (isLocked = false: its Animator's trigger, the
// analytics) and its PlayUnlockChestSound: the chests are the room's, not the replays' (game/EnemyRoom.h's
// UnLockRoomChests: the sounds' MasterAudio draws, by the chests' types).
void DungeonRoom::UnLockRoomChests() { game::UnLockRoomChests(S().roomChestTypes, DarkTonic::MasterAudio::MasterAudio::engine); }

void DungeonManager::SpawnWanderer()
{
    if (S().spawnWanderer) S().spawnWanderer();
    else cs::NotTranslated("DungeonManager.SpawnWanderer (the Wanderer's arrival: not stood in for)");
}

void HUDManager::__ctor() {}
// The HUD's icons and texts: presentation.
void HUDManager::HideAttack(bool p_hide) { (void)p_hide; }
void HUDManager::ShowBagInfo() {}
void HUDManager::HideBagInfo() {}
void HUDManager::UpdateGoldQuantity(int32_t p_addedQuantity, float p_currentQuantity, bool p_isInit) { (void)p_addedQuantity, (void)p_currentQuantity, (void)p_isInit; }
void HUDManager::SetPotionQuantity(int32_t p_quantity) { (void)p_quantity; }
void HUDManager::SetEmptyConsumableIcon() {}
void HUDManager::PlayWeaponRumble(cs::String* p_id) { (void)p_id; }
void HUDManager::ShowForbiddenBagInfo() {}
void MinimapPanel::__ctor() {}
void MinimapPanel::UpdateRoom(DungeonRoom* p_room) { (void)p_room; }

// ---- the items: an item dropped on the floor (ItemDrop.DropItem's draws are the game's) ---------------

// SpawnItem(prefabName, position, radius, parent): the item's object (ItemStack.Create: its prefab, its
// UI) isn't made; its ItemStack.Drop's one Random draw is (the direction: Range(0, 360); the raycast
// and the jump's tweens after it draw nothing). Every item an enemy of the Golem dungeon drops is in
// the item database (data/extracted/items.json), so it always gets there.
ItemStack* ItemDrop::SpawnItem(cs::String* p_prefabName, UnityEngine::Vector3 p_position, float p_radius, UnityEngine::Transform* p_parent)
{
    (void)p_prefabName, (void)p_position, (void)p_radius, (void)p_parent;
    (void)unity::Random::Range(0, 360);
    return nullptr;
}

// The familiar's egg: it reads the save (the kills of the enemy's kind, the game slot's familiars)
// and the item database, which the recordings don't carry: no egg (and so none of its Random.value).
bool FamiliarsManager::GetEggItem(cs::String* p_name, FamiliarsManager_SpawnEggInfo& p_spawnEggInfo)
{
    (void)p_name;
    p_spawnEggInfo = FamiliarsManager_SpawnEggInfo{};
    return false;
}

// ---- the HUD's numbers and health bars: presentation ---------------------------------------------------

QuantitytIndicatorManager* QuantitytIndicatorManager::get_Instance()
{
    static QuantitytIndicatorManager* instance = [] {
        cs::StaticScope keep;
        return cs::gcnew<QuantitytIndicatorManager>();
    }();
    return instance;
}
void QuantitytIndicatorManager::InstantiateFloatingDamageIndicator(float p_damage, UnityEngine::Vector3 p_position, float p_totalTargetHealth,
                                                                   float p_remainingHealth, UnityEngine::GameObject* p_damagedEntity, bool p_isInvencible,
                                                                   int32_t p_size, bool p_parentEntity)
{
    (void)p_damage, (void)p_position, (void)p_totalTargetHealth, (void)p_remainingHealth, (void)p_damagedEntity, (void)p_isInvencible, (void)p_size,
        (void)p_parentEntity;
}

// Enemies/HealthBar: an object with the EnemyHealthBar script (outside the scene, as a prefab is).
UnityEngine::GameObject* EnemiesPrefabRegister::GetPrefabByName(cs::String* p_name)
{
    if (!p_name || p_name->ToUtf8() != "Enemies/HealthBar") cs::NotTranslated("EnemiesPrefabRegister.GetPrefabByName (a prefab other than the health bar)");
    static UnityEngine::GameObject* prefab = [] {
        cs::StaticScope keep;
        auto* go = cs::gcnew<UnityEngine::GameObject>();
        go->__ctor(CS_LIT(u"HealthBar"));
        if (bind::Scene::current) {
            auto& roots = bind::Scene::current->roots;
            roots.erase(std::remove(roots.begin(), roots.end(), go), roots.end());
        }
        auto* bar = cs::gcnew<EnemyHealthBar>();
        go->components_.push_back(bar);
        bar->gameObject_ = go;
        return go;
    }();
    return prefab;
}
void EnemyHealthBar::__ctor() {}
void EnemyHealthBar::Init(Enemy* p_enemy) { (void)p_enemy; }

// ---- FullSerializer's reflection: enemy_stats.json into the EnemyStatModificators ---------------------

// FsJSONManager.Deserialize(Resources.Load<TextAsset>(file).text, ref _mods, false): each entry an
// EnemyStatModificator (its constructor's values, then the file's fields), in the file's order.
void EnemyStatManager::ReadFile(cs::String* p_file)
{
    UnityEngine::TextAsset* t = UnityEngine::Resources::LoadText(p_file);
    if (!t) cs::ThrowNullReference();
    const json::Value root = json::Parse(t->get_text()->ToUtf8());
    for (const auto& [key, v] : root.obj) {
        auto* m = cs::New<EnemyStatModificator>();
        for (const auto& [field, fv] : v.obj) {
            UnityEngine::JsonSerialValue jv(fv, nullptr);
            m->cs_SetField(field.c_str(), jv);
        }
        cs::nc(_mods)->set_Item(cs::String::FromUtf8(key), m);
    }
}

} // namespace gen
