#pragma once
// EnemyBehaviour.MakeJump (EnemyBehaviour.cs:272-294), with ContinueJump and EndJump: a leap
// to `endPosition` made of DOTween moves on the enemy's transform (the shared helper of the
// KamikazeGolem and the chest mimics):
//   - MakeJump: jumpTime, jumpEndPosition and jumpCallback set (EnemyBehaviour's fields), then
//     DOMoveY to the middle height ((endPosition.y + transform.position.y) / 2 + height, one
//     expression in double) over time / 2, OutCubic, OnComplete ContinueJump, scaled time; and
//     DOMoveX to endPosition.x over time with `ease` (the C#'s default: Ease.Linear), scaled time;
//   - ContinueJump (the rise's OnComplete): DOMoveY to jumpEndPosition.y over jumpTime / 2,
//     InCubic, OnComplete EndJump, scaled time (the fields as they are then: a MakeJump in
//     between has changed them);
//   - EndJump: jumpCallback, if any.
// DOMoveX / DOMoveY are DOTween.To on transform.position with an axis constraint (Vector3Plugin):
// that component is `startValue + changeValue * ease`, one expression rounded once, the others
// as the getter reads them then, the whole position set. Their target is the transform (the
// enemy: Tweens::SetTarget), so transform.DOKill() (Tweens::KillTarget(&enemy)) kills them, an
// unfinished rise's ContinueJump included.

#include <functional>

#include "game/Enemy.h"
#include "unity/Tween.h"
#include "unity/Vector2.h"

namespace game {

void MakeJump(Enemy& enemy, unity::Vector2 endPosition, float height, float time, std::function<void()> callback, unity::Ease ease);

} // namespace game
