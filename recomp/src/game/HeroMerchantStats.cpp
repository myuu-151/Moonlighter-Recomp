#include "game/HeroMerchantStats.h"

#include <cmath>

#include "game/Constants.h"
#include "unity/Tween.h"

namespace game {

void Stat::Set(float v)
{
    if (!variableStat) {
        if (onStateUpdated) onStateUpdated(value); // ("Stat can not be modified")
        return;
    }
    const float clamped = v > maxValue ? maxValue : v < minValue ? minValue : v;
    if (onStateUpdated) onStateUpdated(clamped);
    value = clamped;
}

float CalcHitDamage(float hitStrength, float targetDefense)
{
    // Mono evaluates the expression in double (rounding to float only at the call's float
    // parameter); Mathf.RoundToInt is Math.Round: half to even (nearbyint).
    const float product = static_cast<float>(static_cast<double>(hitStrength) * (static_cast<double>(targetDefense) / 100.0));
    const float num = static_cast<float>(static_cast<int>(std::nearbyint(static_cast<double>(product))));
    const float d = hitStrength - num;
    return d < 0.0f ? 0.0f : d;
}

void HeroMerchantStats::Init()
{
    currentHealth = Stat(Constants::GetFloat("kMaxHealth"), Constants::GetFloat("kMinHealth"), Constants::GetFloat("kBaseHealth"));
    strength = Stat(Constants::GetFloat("kMaxStrength"), Constants::GetFloat("kMinStrength"), Constants::GetFloat("kBaseStrength"));
    defense = Stat(Constants::GetFloat("kMaxDefense"), Constants::GetFloat("kMinDefense"), Constants::GetFloat("kBaseDefense"));
    absorption = Stat(Constants::GetFloat("kMaxAbsorption"), Constants::GetFloat("kMinAbsorption"), Constants::GetFloat("kBaseAbsorption"));
    shield = Stat(Constants::GetFloat("kMaxShield"), Constants::GetFloat("kMinShield"), Constants::GetFloat("kBaseShield"));
    isDead = false;
}

void HeroMerchantStats::SetInvencible(unity::Tweens& tweens, float time)
{
    tweens.Kill(tweenInvincible_);
    tweenInvincible_ = 0;
    currentHealth.variableStat = false;
    if (time > 0.0f) tweenInvincible_ = tweens.DelayedCall(time, [this, &tweens] { SetVencible(tweens); }, false);
}

void HeroMerchantStats::SetVencible(unity::Tweens& tweens)
{
    tweens.Kill(tweenInvincible_);
    tweenInvincible_ = 0;
    currentHealth.variableStat = true;
}

} // namespace game
