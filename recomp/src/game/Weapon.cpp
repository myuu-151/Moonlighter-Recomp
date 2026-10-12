#include "game/Weapon.h"

#include "game/Prefabs.h"

namespace game {

namespace {

const char* const kHitbox = "MainAttack/Collider";
const char* const kArt = "MainAttack/Art";

} // namespace

WeaponType WeaponTypeFromName(const std::string& name)
{
    static const char* const kNames[6] = {"None", "ShortSword", "BigSword", "Spear", "Gloves", "Bow"};
    for (int i = 0; i < 6; i++)
        if (name == kNames[i]) return static_cast<WeaponType>(i);
    return WeaponType::None;
}

Weapon::Weapon(const unity::AnimatorController& controller, const Prefab& prefab, WeaponType type) : type(type), animator(controller)
{
    // What the Animator binds as defaults: the prefab's.
    for (const PrefabNode& n : prefab.nodes) {
        if (n.path == kHitbox) {
            hitboxActive = n.active;
            for (const ColliderDef& c : n.colliders)
                if (c.type == "BoxCollider2D") {
                    hitboxOffset = {c.ox, c.oy};
                    hitboxSize = {c.sx, c.sy};
                }
        }
        if (n.path == kArt && !n.sprite.empty()) {
            sprite_ = n.sprite;
            hasSprite_ = true;
        }
    }
    animator.SetDefault(kHitbox, "m_IsActive", hitboxActive ? 1.0f : 0.0f);
    animator.SetDefault(kHitbox, "m_Offset.x", hitboxOffset.x);
    animator.SetDefault(kHitbox, "m_Offset.y", hitboxOffset.y);
    animator.SetDefault(kHitbox, "m_Size.x", hitboxSize.x);
    animator.SetDefault(kHitbox, "m_Size.y", hitboxSize.y);
    animator.SetDefaultSprite(kArt, sprite_);
    animator.SetDefault(kArt, "m_Enabled", 1.0f);
}

void Weapon::Update(float unscaledDeltaTime)
{
    animator.Update(unscaledDeltaTime);
    auto apply = [&](const char* attribute, float& to) {
        if (animator.Driven(kHitbox, attribute)) to = animator.Property(kHitbox, attribute, to);
    };
    float active = hitboxActive ? 1.0f : 0.0f;
    apply("m_IsActive", active);
    hitboxActive = active > 0.5f;
    apply("m_Offset.x", hitboxOffset.x);
    apply("m_Offset.y", hitboxOffset.y);
    apply("m_Size.x", hitboxSize.x);
    apply("m_Size.y", hitboxSize.y);
    if (animator.Driven(kArt, "m_Enabled")) artEnabled = animator.Property(kArt, "m_Enabled", 1.0f) > 0.5f;
    if (const std::string* s = animator.Sprite(kArt)) {
        sprite_ = *s;
        hasSprite_ = !s->empty();
    }
}

const std::string* Weapon::Sprite() const
{
    return hasSprite_ ? &sprite_ : nullptr;
}

} // namespace game
