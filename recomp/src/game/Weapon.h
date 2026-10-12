#pragma once
// The equipped weapon as the hero carries it (Equipment.InitializeAnimators: its prefab under
// his Weapon child, ItemStack/Logic, both at his origin): its own Animator (the weapon's
// controller, e.g. EmptyTrainingSpear with the BroomSpear override), the main attack's
// hitbox (MainAttack/Collider: active, offset and size come from the clips' curves; write
// defaults put the prefab's back) and the sprite it shows (MainAttack/Art). Its Animator runs in
// unscaled time (the weapons' update mode).

#include <string>

#include "unity/Animator.h"
#include "unity/Vector2.h"

namespace game {

struct Prefab;

// WeaponEquipmentMaster.WeaponType.
enum class WeaponType { None, ShortSword, BigSword, Spear, Gloves, Bow };
// From an item master's weaponType ("Spear", ...); None if it isn't one.
WeaponType WeaponTypeFromName(const std::string& name);

class Weapon {
public:
    // `prefab`: the weapon's (items bundle; its item's inGameArtPrefabName, e.g. "Broom Spear"),
    // for its hitbox's defaults.
    Weapon(const unity::AnimatorController& controller, const Prefab& prefab, WeaponType type);
    Weapon(const Weapon&) = delete;
    Weapon& operator=(const Weapon&) = delete;

    // The Animator's pass (unscaled time), then the curves' values onto the hitbox (what this
    // pass drove).
    void Update(float unscaledDeltaTime);

    WeaponType type;

    // The main attack's hitbox, local to the hero (a trigger).
    bool hitboxActive = false;
    unity::Vector2 hitboxOffset, hitboxSize;
    // MainAttack/Art's sprite (nullptr: none), and whether its SpriteRenderer is on.
    const std::string* Sprite() const;
    bool artEnabled = true;

    unity::Animator animator;

private:
    std::string sprite_;
    bool hasSprite_ = false;
};

} // namespace game
