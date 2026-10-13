#include "game/EnemyJump.h"

#include "unity/Mathf.h"

namespace game {

using unity::Vector2;
using unity::mono::D;
using unity::mono::F;

namespace {

// transform.DOMoveX(end, duration) (x) / DOMoveY: a tween of that one component on the scaled
// clock (SetUpdate(false)), its target the transform. 0: no tweens in the scene.
int MoveAxis(Enemy& enemy, bool x, float end, float duration, unity::Ease ease)
{
    unity::Tweens* tweens = enemy.scene.tweens;
    if (!tweens) return 0;
    Enemy* e = &enemy;
    const int id = tweens->To([e, x] {
                                  const Vector2 p = e->position();
                                  return Vector2{x ? p.x : p.y, 0.0f};
                              },
                              [e, x](Vector2 v) {
                                  Vector2 p = e->position();
                                  (x ? p.x : p.y) = v.x;
                                  e->setPosition(p);
                              },
                              Vector2{end, 0.0f}, duration, ease, 0.0f, false);
    tweens->SetTarget(id, e);
    return id;
}

void EndJump(Enemy& enemy)
{
    if (!enemy.enemyBehaviour) return;
    // (A copy: the callback may start the next jump.)
    const std::function<void()> callback = enemy.enemyBehaviour->jumpCallback;
    if (callback) callback();
}

void ContinueJump(Enemy& enemy)
{
    if (!enemy.enemyBehaviour) return;
    const EnemyBehaviour& b = *enemy.enemyBehaviour;
    const int id = MoveAxis(enemy, false, b.jumpEndPosition.y, b.jumpTime * 0.5f, unity::Ease::InCubic);
    Enemy* e = &enemy;
    if (id) enemy.scene.tweens->OnComplete(id, [e] { EndJump(*e); });
}

} // namespace

void MakeJump(Enemy& enemy, Vector2 endPosition, float height, float time, std::function<void()> callback, unity::Ease ease)
{
    if (!enemy.enemyBehaviour) return;
    EnemyBehaviour& b = *enemy.enemyBehaviour;
    b.jumpTime = time;
    b.jumpEndPosition = endPosition;
    b.jumpCallback = std::move(callback);
    // (endPosition.y + transform.position.y) / 2f + height: one expression, a float argument.
    const float middle = F((D(endPosition.y) + D(enemy.position().y)) / 2.0 + D(height));
    Enemy* e = &enemy;
    const int rise = MoveAxis(enemy, false, middle, time * 0.5f, unity::Ease::OutCubic);
    if (rise) enemy.scene.tweens->OnComplete(rise, [e] { ContinueJump(*e); });
    MoveAxis(enemy, true, endPosition.x, time, ease);
}

} // namespace game
