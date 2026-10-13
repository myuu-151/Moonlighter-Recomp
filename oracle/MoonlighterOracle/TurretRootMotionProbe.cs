using System;
using System.Collections;
using System.Collections.Generic;
using UnityEngine;

namespace MoonlighterOracle
{
    /// After `-oracle-enemy=BrokenGolemTurret`'s last frame (the recording over): what the turret
    /// Animator's root motion (applyRootMotion on, on the object of its Dynamic Rigidbody2D) does to
    /// a body that moves. In the recording the turrets never move and never sleep, and their
    /// transforms aren't written after their Update, which the port models as the root motion
    /// restarting the body's rest every Animator pass, its velocity kept (game/EnemyRootMotion.cpp);
    /// it can't tell that from a root motion that sets the velocity (deltaPosition / deltaTime,
    /// zero here) or moves the body to where the clip puts it (MovePosition: pinned in place).
    /// The probe, on the first turret still active: after a frame's Updates (a coroutine's
    /// `yield return null`, before the frame's Animator pass) it sets rb.velocity to (30, 0);
    /// writes one "rootMotionProbe" line: applyRootMotion, the Animator's updateMode, the body's
    /// position, velocity and awake before, right after the set, at the frame's end
    /// (WaitForEndOfFrame) and after the next fixed step (WaitForFixedUpdate), with the
    /// transform's position at the frame's end; then puts the velocity back to what it was.
    internal static class TurretRootMotionProbe
    {
        private static Json V2(Json j, Vector2 v) => j.Arr().Bits(v.x).Bits(v.y).EndArr();

        private static Json Body(Json j, string key, Rigidbody2D rb)
        {
            j.Key(key).Obj().Key("pos");
            V2(j, rb.position);
            j.Key("vel");
            V2(j, rb.velocity);
            return j.Prop("awake", rb.IsAwake()).EndObj();
        }

        public static IEnumerator Run(List<EnemyBehaviour> enemies, Action<Json> emit)
        {
            BrokenGolemTurretBehaviour turret = null;
            int id = -1;
            for (int i = 0; i < enemies.Count; i++)
                if (enemies[i] is BrokenGolemTurretBehaviour t && t != null && t.isActiveAndEnabled)
                {
                    turret = t;
                    id = i;
                    break;
                }
            if (turret == null) yield break;
            var rb = turret.GetComponent<Rigidbody2D>();
            var anim = turret.enemyAnimator;
            if (rb == null) yield break;
            yield return null; // (this frame's Updates done, its Animator pass to come)
            if (turret == null || !turret.isActiveAndEnabled) yield break;
            var j = new Json().Obj().Prop("ev", "rootMotionProbe").Prop("s", id).Prop("fc", Time.frameCount)
                .Prop("applyRootMotion", anim != null && anim.applyRootMotion).Prop("updateMode", anim != null ? (int)anim.updateMode : -1);
            Body(j, "before", rb);
            Vector2 saved = rb.velocity;
            Vector2 set = new Vector2(30f, 0f);
            j.Key("set");
            V2(j, set);
            rb.velocity = set;
            Body(j, "afterSet", rb);
            yield return new WaitForEndOfFrame();
            Body(j, "frameEnd", rb);
            j.Key("transform");
            V2(j, turret.transform.position);
            yield return new WaitForFixedUpdate();
            Body(j, "afterStep", rb);
            rb.velocity = saved;
            emit(j);
        }
    }
}
