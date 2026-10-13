using System;
using HarmonyLib;
using UnityEngine;

namespace MoonlighterOracle
{
    /// `-oracle-enemy`'s "attackHit" events where an enemy's attack trigger meets something:
    /// EnemyAttackCollisionDetection.OnTriggerEnter2D / OnCollisionEnter2D (the AttackCollider
    /// child's script, advising its enemy). EnemyOracle's AttackHitHook patches
    /// EnemyBehaviour.AttackHit, which an override that doesn't call its base never reaches:
    /// HermitSlimeBehaviourNEW.AttackHit (ItemDrain) left enemy_HermitSlime.jsonl without a single
    /// "attackHit" while its attack trigger entered the hero at frame 243 and
    /// IronChest(Clone)/Trigger at frame 287. This hook records the event (the same fields, the
    /// enemy advised) before the enemy's AttackHit runs, and AttackHitHook doesn't record it
    /// again underneath. A SlimeTrap's scissors (which advise their paired slime) are left to
    /// AttackHitHook as before; AttackHit called from elsewhere (a turret's shot) too.
    internal static partial class EnemyOracle
    {
        private static int attackHitDepth; // inside an attack trigger's callback (the main thread's)

        private static bool AttackTriggerEnter(EnemyAttackCollisionDetection d, GameObject other)
        {
            var advise = d != null ? d.gameObjectToAdvise : null;
            if (advise == null || advise.name.Contains("SlimeTrap")) return false;
            if (attackHitDepth++ > 0) return true;
            int id = Id(advise.GetComponent<EnemyBehaviour>());
            if (id >= 0) Add(new Ev { k = "attackHit", s = id, step = Step, player = other != null && other.CompareTag("Player"), other = other != null ? other.name : null });
            return true;
        }

        [HarmonyPatch(typeof(EnemyAttackCollisionDetection), "OnTriggerEnter2D")]
        private static class AttackTriggerHook
        {
            private static void Prefix(EnemyAttackCollisionDetection __instance, Collider2D other, out bool __state)
            {
                __state = AttackTriggerEnter(__instance, other != null ? other.gameObject : null);
            }

            private static Exception Finalizer(Exception __exception, bool __state)
            {
                if (__state) attackHitDepth--;
                return __exception;
            }
        }

        [HarmonyPatch(typeof(EnemyAttackCollisionDetection), "OnCollisionEnter2D")]
        private static class AttackCollisionHook
        {
            private static void Prefix(EnemyAttackCollisionDetection __instance, Collision2D collision, out bool __state)
            {
                __state = AttackTriggerEnter(__instance, collision != null ? collision.gameObject : null);
            }

            private static Exception Finalizer(Exception __exception, bool __state)
            {
                if (__state) attackHitDepth--;
                return __exception;
            }
        }
    }
}
