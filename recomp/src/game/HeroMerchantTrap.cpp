#include "game/HeroMerchantTrap.h"

namespace game {

using unity::Vector2;

void BindHeroTrap(HeroTrap& trap, HeroMerchantController& c)
{
    HeroTrap* t = &trap;
    HeroMerchantController* hero = &c;
    trap.canMove = c.canMove;
    trap.canAttack = c.canAttack;
    trap.artVisible = c.artVisible;
    trap.onSetCanMove = [hero](bool value) { hero->canMove = value; };
    trap.onSetCanAttack = [hero](bool value) { hero->canAttack = value; };
    trap.onSetDirection = [hero](HeroTrap::Direction d) { hero->SetDirection(static_cast<HeroMerchantController::Direction>(static_cast<int>(d))); };
    trap.onMoveAutomaticallyToPoint = [t, hero](Vector2 endPosition, float time, bool useAnimation) {
        t->movedExternally = true;
        if (useAnimation) {
            hero->animator.SetBool("Walk", true);
            // (SetArmorAnimatorBool("Walk", true) and the trail's Play: presentation.)
            if (hero->weapon && hero->weapon->animator.Controller().Param("Walk") >= 0) hero->weapon->animator.SetBool("Walk", true);
        }
        const Vector2 position = hero->position;
        const Vector2 vector = unity::Normalized(endPosition - position) * 10.0f;
        hero->UpdateDirection(vector.x, vector.y);
        // transform.DOMove(new Vector3(end.x, end.y, position.z), time).SetUpdate(false), Linear;
        // OnComplete and OnKill: ismovedExternally = false.
        const int id = hero->tweens.Move([hero] { return hero->position; }, [hero](Vector2 p) { hero->position = p; }, endPosition, time,
                                         unity::Ease::Linear, 0.0f, false);
        hero->tweens.OnComplete(id, [t] { t->movedExternally = false; });
    };
    trap.onChangeToDungeonMode = [hero] {
        if (hero->animator.Controller().Param("ChangeToDungeonMode") >= 0) hero->animator.SetTrigger("ChangeToDungeonMode");
    };
    trap.onResetCurrentWeaponAnimator = [hero] { hero->ResetCurrentWeaponAnimator(); };
    trap.onSetArt = [hero](bool visible) {
        if (visible) hero->EnableHeroMerchantArt();
        else hero->DisableHeroMerchantArt();
    };
}

} // namespace game
