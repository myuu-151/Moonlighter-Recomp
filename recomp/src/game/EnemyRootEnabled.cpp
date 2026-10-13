// The root box's Collider2D.enabled as a clip drives it (game/Enemy.h): the Wood mimic's
// SlamAttack and the Iron mimic's turn the BoxCollider2D on the Animator's own object (path "",
// m_Enabled) off for the leap and on again. Unity binds the property's default to the box as it
// is when the Animator is bound (write defaults put it back), and an enabled flag the pass
// writes takes effect then: off destroys the box's shape, on makes it anew
// (EnemyPhysics::SetRootColliderEnabled, game/EnemyRootCollider.cpp). A value the pass writes
// unchanged changes nothing.

#include "game/Enemy.h"

namespace game {

namespace {

const char* const kRootPath = "";
const char* const kEnabled = "m_Enabled";

} // namespace

void Enemy::BindRootEnabled()
{
    rootEnabledProp_ = -1;
    if (!controller_ || !enemyAnimator) return;
    const int p = controller_->Property(kRootPath, kEnabled);
    // (Only a BoxCollider2D's: the root's other components' m_Enabled aren't this.)
    if (p < 0 || controller_->properties[static_cast<size_t>(p)].type != 61) return;
    rootEnabledProp_ = p;
    enemyAnimator->SetDefault(kRootPath, kEnabled, colliderEnabled ? 1.0f : 0.0f);
}

void Enemy::ApplyRootEnabled()
{
    if (rootEnabledProp_ < 0 || !enemyAnimator) return;
    const unity::Animator& a = *enemyAnimator;
    if (!a.Driven(rootEnabledProp_)) return;
    const bool on = a.Property(rootEnabledProp_) > 0.5f;
    if (on == colliderEnabled) return;
    if (physics) physics->SetRootColliderEnabled(*this, on);
    else colliderEnabled = on;
}

} // namespace game
