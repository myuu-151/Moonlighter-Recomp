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
};

} // namespace

} // namespace standins

using standins::S;

namespace gen {

// ---- the hero: his own code is the recording's -------------------------------------------------------

void HeroMerchant::DealDamageToPlayer(float p_attackStrength, cs::String* p_damageSource, HeroMerchant_DamageType p_type, Enemy* p_enemySource)
{
    if (S().dealDamageToPlayer) S().dealDamageToPlayer(p_attackStrength, p_damageSource, static_cast<int>(p_type), p_enemySource);
}

// No shield up (the enemy replays' hero: the hand port's isShieldProtected unset).
bool HeroMerchantController::CheckIfPlayerIsShieldProtected(UnityEngine::GameObject* p_enemy)
{
    (void)p_enemy;
    return false;
}

// The hero's hole check after a push (EnemyBehaviour.PushPlayerBackwards' WaitAndCheckForHoles): his
// fall is his own doing, in the recording: none here.
bool HeroMerchantController::CheckForHolesAfterPush() { return false; }

void HeroMerchantController::AddForce(unity::Vector2 p_force)
{
    if (S().heroAddForce) S().heroAddForce(p_force);
}

unity::Vector2 HeroMerchantController::GetColliderOffsset() { return S().heroColliderOffset; }

// The pad's rumble (GolemSoldierBehaviour.PlayHitOnGroundSound): presentation.
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

// ---- the camera: DOShakePosition's Random draws (the shake itself is presentation) ---------------------

// Rumble(duration, strength, direction, vibration): DOShakePosition(duration, strength * (x, y, 0),
// vibration, randomness 0): a Vector3 strength (vectorBased), as the hand port's EnemyBehaviour has it.
DG::Tweening::Tweener* CameraManager::Rumble(float p_duration, float p_strength, UnityEngine::Vector3 p_direction, int32_t p_vibration)
{
    (void)p_strength, (void)p_direction;
    unity::ShakeDraws(p_duration, p_vibration, 0.0f, true, false);
    return nullptr;
}

// ---- the game's managers ------------------------------------------------------------------------------

GameManager* GameManager::get_Instance() { return S().gameManager; }
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

// ---- the room: the dead sprite's stamp (the replay compares where), the drops' bookkeeping -------------

void DungeonRoom::SpawnEnemyDeadSprite(UnityEngine::Sprite* p_s, UnityEngine::Vector3 p_position)
{
    if (S().spawnEnemyDeadSprite) S().spawnEnemyDeadSprite(this, p_s, p_position);
}
void DungeonRoom::AddDropItems(cs::List<ItemStack*>* p_items) { (void)p_items; }

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
void MinimapPanel::__ctor() {}
void MinimapPanel::UpdateRoom(DungeonRoom* p_room) { (void)p_room; }

// ---- the items (the drops' draws are resynced by the replays; their items aren't recorded) --------------

cs::List<ItemStack*>* ItemDrop::DropItem(UnityEngine::Vector3 p_position, float p_radius, int32_t p__minDropRate, UnityEngine::Transform* p_parent)
{
    (void)p_position, (void)p_radius, (void)p__minDropRate, (void)p_parent;
    return cs::New<cs::List<ItemStack*>>();
}

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
