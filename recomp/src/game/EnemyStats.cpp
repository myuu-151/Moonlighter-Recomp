#include "game/EnemyStats.h"

#include "game/Constants.h"
#include "unity/Mathf.h"

namespace game {

namespace Mathf = unity::Mathf;

namespace {
constexpr int kCultureWanderer = 7;

float Number(const json::Value& v, const char* key, float fallback)
{
    const json::Value* x = v.Find(key);
    return x ? static_cast<float>(x->AsDouble()) : fallback;
}
} // namespace

bool EnemyStatModificator::FromJson(const json::Value& enemyStats, const std::string& name, EnemyStatModificator& out)
{
    const json::Value* v = enemyStats.Find(name);
    if (!v || !v->IsObject()) return false;
    EnemyStatModificator m; // the default constructor's values, then the file's
    if (const json::Value* b = v->Find("isModifiedByFloor")) m.isModifiedByFloor = b->b;
    m.size = Number(*v, "size", m.size);
    if (const json::Value* t = v->Find("tint")) {
        m.tint[0] = Number(*t, "r", 0.0f);
        m.tint[1] = Number(*t, "g", 0.0f);
        m.tint[2] = Number(*t, "b", 0.0f);
        m.tint[3] = Number(*t, "a", 0.0f);
    }
    m.maxHealth = Number(*v, "maxHealth", 0.0f);
    m.speed = Number(*v, "speed", 0.0f);
    m.strength = Number(*v, "strength", 0.0f);
    m.defense = Number(*v, "defense", 0.0f);
    m.weight = Number(*v, "weight", 0.0f);
    m.shieldPushForce = Number(*v, "shieldPushForce", 0.0f);
    if (const json::Value* c = v->Find("culture")) m.culture = static_cast<int>(c->AsInt());
    out = m;
    return true;
}

void EnemyStats::Init(const EnemyStatModificator& base, const EnemyStatsContext& ctx, float* rigidbodyMass)
{
    base_ = base;
    current_ = base;
    EnemyStatModificator& s = current_;
    const int culture = s.culture;
    if (s.isModifiedByFloor) {
        const int num = ctx.hasDungeon ? ctx.level : 0;
        float num2 = Constants::GetFloat("floorLevelingValue");
        if (ctx.afternoon) num2 = Constants::GetFloat("floorLevelingValueAtNight");
        float num3, num4;
        if (culture == kCultureWanderer) {
            num2 = Constants::GetFloat("floorLevelingValueForWanderer");
            const float p = Constants::GetFloat("floorLevelingStrengthPowForWanderer");
            num3 = num2 * Mathf::Max(0.0f, Mathf::Pow(static_cast<float>(num), p) - 0.5f);
            num4 = num2 * Mathf::Max(0.0f, Mathf::Pow(static_cast<float>(num), 0.8f) - 0.5f);
        } else {
            num3 = num2 * Mathf::Max(0.0f, Mathf::Pow(static_cast<float>(num), 0.6f) - 0.5f);
            num4 = num2 * static_cast<float>(num > 0 ? num : 0);
        }
        s.maxHealth += num4 * s.maxHealth + (isChampion ? Constants::GetFloat("championHealthPercent") * s.maxHealth : 0.0f);
        s.strength += num3 * s.strength + (isChampion ? Constants::GetFloat("championStrengthPercent") * s.strength : 0.0f);
        if (isSpeedModifiedByFloor) s.speed += num4 * s.speed;
    }
    const std::vector<float> difficultyMod = Constants::GetFloatArray("difficultyMod");
    if (ctx.difficulty > -1 && ctx.difficulty < static_cast<int>(difficultyMod.size())) {
        s.maxHealth *= difficultyMod[static_cast<size_t>(ctx.difficulty)];
        s.strength *= difficultyMod[static_cast<size_t>(ctx.difficulty)];
    }
    const bool ngp = ctx.newGamePlus && !ctx.dlcCompleted;
    const bool ngpPostDlc = ctx.newGamePlus && ctx.dlcCompleted;
    if (ngp) {
        s.maxHealth *= Constants::GetFloat("ngpEnemiesHealthFactor");
        s.strength *= Constants::GetFloat("ngpEnemiesStrengthFactor");
        s.defense *= Constants::GetFloat("ngpEnemiesDefenseFactor");
        s.speed *= Constants::GetFloat("ngpEnemiesSpeedFactor");
    }
    if (ngpPostDlc) {
        s.maxHealth *= Constants::GetFloat("ngpPostDLCEnemiesHealthFactor");
        s.strength *= Constants::GetFloat("ngpPostDLCEnemiesStrengthFactor");
        s.defense *= Constants::GetFloat("ngpPostDLCEnemiesDefenseFactor");
        s.speed *= Constants::GetFloat("ngpPostDLCEnemiesSpeedFactor");
    }
    const auto health = Constants::GetFloat2DArray(ngpPostDlc ? "ngpPostDLCEnemyAutoScaleHealth" : "enemyAutoScaleHealth");
    const auto strength = Constants::GetFloat2DArray(ngpPostDlc ? "ngpPostDLCEnemyAutoScaleStrength" : "enemyAutoScaleStrength");
    const int row = ctx.hasDungeon ? ctx.dungeonCulture : 0; // ItemMaster.Culture.Merchant without a dungeon
    // (C# indexes the tables unchecked; a culture past them throws there.)
    if (row >= 0 && static_cast<size_t>(row) < health.size() && culture >= 0 && static_cast<size_t>(culture) < health[static_cast<size_t>(row)].size())
        s.maxHealth *= health[static_cast<size_t>(row)][static_cast<size_t>(culture)];
    if (row >= 0 && static_cast<size_t>(row) < strength.size() && culture >= 0 && static_cast<size_t>(culture) < strength[static_cast<size_t>(row)].size())
        s.strength *= strength[static_cast<size_t>(row)][static_cast<size_t>(culture)];
    if (rigidbodyMass) *rigidbodyMass = s.weight;
    SetCurrentHealth(s.maxHealth);
}

void EnemyStats::SetCurrentHealth(float value)
{
    if (invincible_ || !isAlive_) return;
    if (OnHealthChanged) OnHealthChanged(value);
    if (OnHealthDecreased && value < currentHealth_) OnHealthDecreased(value);
    currentHealth_ = Mathf::Max(0.0f, value);
    if (currentHealth_ == 0.0f && isAlive_) {
        isAlive_ = false;
        if (OnDeath) OnDeath();
        if (OnDie) OnDie();
    }
}

void EnemyStats::SetInvencible(bool value) { invincible_ = value; }

void EnemyStats::Resurrect()
{
    isAlive_ = true;
    currentHealth_ = current_.maxHealth;
}

} // namespace game
