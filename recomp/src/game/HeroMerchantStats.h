#pragma once
// HeroMerchantStats: Will's health, defense and strength as the game's Stat keeps them, and
// the invincibility his health gets after a hit (SetInvencible: the Stat refuses changes until
// SetVencible, a scaled DOVirtual.DelayedCall later).
//
// Stat.Value's setter clamps to [minValue, maxValue] and reports the new value
// (OnStateUpdated) BEFORE storing it; while variableStat is off it refuses the change but still
// reports, with the old value. HeroMerchant.CalcHitDamage: the hit less its defense's share,
// rounded half to even (Mathf.RoundToInt) in floats.

#include <functional>

namespace unity {
class Tweens;
}

namespace game {

struct Stat {
    float value = 0.0f, minValue = 0.0f, maxValue = 0.0f;
    bool variableStat = true;
    std::function<void(float)> onStateUpdated; // the subscribers, in their order

    Stat() = default;
    Stat(float max, float min, float base) : value(base), minValue(min), maxValue(max) {}
    void Set(float v);
};

// HeroMerchant.CalcHitDamage (and Enemy's).
float CalcHitDamage(float hitStrength, float targetDefense);

class HeroMerchantStats {
public:
    // Init: kMax/kMin/kBase from the constants table (Health, Defense, Strength, Absorption,
    // Shield).
    void Init();

    Stat currentHealth, defense, strength;
    Stat absorption; // extra health taken before it (the health bar's second slider)
    Stat shield;     // whole hits blocked, one each
    bool isDead = false;

    // SetInvencible(time): the health stops changing; after `time` (scaled) SetVencible.
    void SetInvencible(unity::Tweens& tweens, float time = 0.0f);
    void SetVencible(unity::Tweens& tweens);

private:
    int tweenInvincible_ = 0;
};

} // namespace game
