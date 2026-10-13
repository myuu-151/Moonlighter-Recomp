#pragma once
// What the Golem dungeon's Slime (Slime2_0Behaviour.cs, game/Slime2_0.h) reads of the hero and
// does to him beyond EnemyHero (game/Enemy.h): its trap swallows him (mash the stick to get out).
//
// HeroTrap is the hero's side as the slimes see it: HeroMerchantController's isTrapped (a public
// field the slimes set and clear; HeroMerchantStats.Die clears it too), the left stick's Value and
// LastValue (InControl's, read by the slime that holds him: four sign changes free him), his
// equipped ability's isInUse (no swallowing mid-roll), whether he's in the enemy's room
// (DungeonManager.currentDungeonRoom == enemy.enemyRoom), the HeroMerchant.OnDie event, and the
// controller's calls the trap makes. The calls keep what they set here (canMove, canAttack, the
// ability trail, the art) and pass it on through their hooks: the driver binds them to the hero
// (game/HeroMerchantTrap.h binds a HeroMerchantController), a replay records them.
//
// EnemyHero::trap points at it (none: the slimes don't trap; they still chase and bite).

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

#include "unity/Vector2.h"

namespace game {

class Enemy;

struct HeroTrap {
    // HeroMerchantController.Direction.
    enum class Direction { Left = 0, Right = 1, Back = 2, Front = 3 };

    // HeroMerchantController.isTrapped.
    bool isTrapped = false;
    // HeroMerchantController.currentEquipedAbility.isInUse.
    bool abilityInUse = false;
    // inputDevice.LeftStick.Value and .LastValue (InControl's, this frame's and the last).
    unity::Vector2 stick, lastStick;
    // DungeonManager.Instance.currentDungeonRoom == enemy.enemyRoom (none: true).
    std::function<bool(const Enemy&)> inEnemyRoom;
    // HeroMerchant.Instance.heroMerchantStats.currentHealth.variableStat is set (MoonlighterEffect's
    // target: the slime's effectOnAttack needs it).
    bool hasHealthStat = true;

    // What the calls below left (the hero's own, mirrored). movedExternally: ismovedExternally
    // (MoveAutomaticallyToPoint's tween running: his Update leaves his Walk animation alone).
    bool canMove = true, canAttack = true, abilityTrail = false, artVisible = true, movedExternally = false;

    // HeroMerchantController.MoveAutomaticallyToPoint(endPosition, time, useAnimation) (a
    // transform.DOMove, Linear, scaled time, that marks him moved externally until it ends).
    std::function<void(unity::Vector2 endPosition, float time, bool useAnimation)> onMoveAutomaticallyToPoint;
    void MoveAutomaticallyToPoint(unity::Vector2 endPosition, float time, bool useAnimation)
    {
        if (onMoveAutomaticallyToPoint) onMoveAutomaticallyToPoint(endPosition, time, useAnimation);
    }
    // HeroMerchantController.SetDirection(direction).
    std::function<void(Direction)> onSetDirection;
    void SetDirection(Direction direction)
    {
        if (onSetDirection) onSetDirection(direction);
    }
    // AllowMovement / ForbidMovement (canMove), AllowAttack / ForbidAttack (canAttack).
    std::function<void(bool)> onSetCanMove, onSetCanAttack;
    void AllowMovement() { SetCanMove(true); }
    void ForbidMovement() { SetCanMove(false); }
    void AllowAttack() { SetCanAttack(true); }
    void ForbidAttack() { SetCanAttack(false); }
    // DisableAbilityTrail (abilityTrail.enabled = false: the roll's trail renderer).
    std::function<void()> onDisableAbilityTrail;
    void DisableAbilityTrail()
    {
        abilityTrail = false;
        if (onDisableAbilityTrail) onDisableAbilityTrail();
    }
    // characterAnimator.SetTrigger("ChangeToDungeonMode").
    std::function<void()> onChangeToDungeonMode;
    void ChangeToDungeonMode()
    {
        if (onChangeToDungeonMode) onChangeToDungeonMode();
    }
    // ResetCurrentWeaponAnimator, DisableHeroMerchantArt, EnableHeroMerchantArt (the art's
    // SetActive, the blink's tweens killed).
    std::function<void()> onResetCurrentWeaponAnimator;
    std::function<void(bool visible)> onSetArt;
    void ResetCurrentWeaponAnimator()
    {
        if (onResetCurrentWeaponAnimator) onResetCurrentWeaponAnimator();
    }
    void DisableHeroMerchantArt() { SetArt(false); }
    void EnableHeroMerchantArt() { SetArt(true); }
    // MoonlighterEffect.Create(effect, hero, enemy's stats) for an enemy's effectOnAttack.
    std::function<void(Enemy&)> onCreateEffect;

    // HeroMerchant.OnDie (static event): `+=` adds, `-=` takes the last one of that owner off
    // (none: nothing); Die runs them in order (OnWillDeathAnimationEnded) and the death clears
    // isTrapped (HeroMerchantStats.Die).
    void SubscribeOnDie(const void* owner, std::function<void()> handler) { onDie_.emplace_back(owner, std::move(handler)); }
    void UnsubscribeOnDie(const void* owner)
    {
        for (size_t k = onDie_.size(); k-- > 0;)
            if (onDie_[k].first == owner) {
                onDie_.erase(onDie_.begin() + static_cast<std::ptrdiff_t>(k));
                return;
            }
    }
    bool SubscribedOnDie(const void* owner) const
    {
        for (const auto& h : onDie_)
            if (h.first == owner) return true;
        return false;
    }
    void InvokeOnDie()
    {
        const auto handlers = onDie_; // (a handler may unsubscribe)
        for (const auto& h : handlers)
            if (h.second) h.second();
    }

private:
    void SetCanMove(bool value)
    {
        canMove = value;
        if (onSetCanMove) onSetCanMove(value);
    }
    void SetCanAttack(bool value)
    {
        canAttack = value;
        if (onSetCanAttack) onSetCanAttack(value);
    }
    void SetArt(bool visible)
    {
        artVisible = visible;
        if (onSetArt) onSetArt(visible);
    }
    std::vector<std::pair<const void*, std::function<void()>>> onDie_;
};

} // namespace game
