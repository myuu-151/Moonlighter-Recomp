#pragma once
// The hero's side of the Slime's trap (game/HeroTrap.h) on the port's HeroMerchantController:
// what Slime2_0Behaviour calls on HeroMerchant.Instance.heroMerchantController, from the
// decompiled class.
//
//   - AllowMovement / ForbidMovement: canMove; AllowAttack / ForbidAttack: canAttack;
//   - SetDirection: his own;
//   - MoveAutomaticallyToPoint(endPosition, time, useAnimation): ismovedExternally (HeroTrap's
//     movedExternally), with useAnimation his Animator's Walk on (and his weapon's: his armor's
//     and the trail are presentation), UpdateDirection along (end - position).normalized * 10,
//     then transform.DOMove(end, time), Linear, scaled time (SetUpdate(false)): his position
//     start + change * t (Vector3Plugin: the product rounded, then the sum), ismovedExternally off
//     when it completes (or is killed);
//   - characterAnimator.SetTrigger("ChangeToDungeonMode"); ResetCurrentWeaponAnimator;
//     DisableHeroMerchantArt / EnableHeroMerchantArt (the armor's show and hide: presentation);
//     DisableAbilityTrail (a renderer: presentation).
// Not here (his controller's, for the integrator): his Update skipping its Walk animation while
// ismovedExternally (HeroMerchantController.cs:2401); HeroMerchantStats.Die clearing isTrapped and
// OnWillDeathAnimationEnded raising HeroMerchant.OnDie (HeroTrap::InvokeOnDie); the pad's
// LeftStick Value and LastValue into HeroTrap::stick / lastStick each frame.

#include "game/HeroMerchantController.h"
#include "game/HeroTrap.h"

namespace game {

// Hooks `trap`'s calls to `controller` (both outlive the hooks).
void BindHeroTrap(HeroTrap& trap, HeroMerchantController& controller);

} // namespace game
