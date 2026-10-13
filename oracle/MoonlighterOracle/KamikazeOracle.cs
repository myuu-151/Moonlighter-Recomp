using System;
using System.Collections.Generic;
using System.Reflection;
using DG.Tweening;
using HarmonyLib;
using UnityEngine;

namespace MoonlighterOracle
{
    /// The KamikazeGolem's part of `-oracle-enemy=KamikazeGolem` (EnemyOracle records the
    /// enemies; this adds what the kamikaze does on its own), written into EnemyOracle's lines:
    ///   - each enemy ("enemy", a frame's "enemies"): "kami": "enter" (EnemyRoom_OnRoomEnter's
    ///     clocks, the InvokeRepeating("MoveEnemy", 0.65, 0.65) it made counts from: [Time.time,
    ///     Time.fixedTime, in a fixed step (0/1), Time.frameCount], null if it never ran), "moves"
    ///     (how many times MoveEnemy has run since), "lastMove" (the last one's [Time.time,
    ///     Time.fixedTime, in a fixed step], null: none yet), "jump"
    ///     (EnemyBehaviour's jumpEndPosition x, y and jumpTime), "trig" (the Animator's Awake,
    ///     Attacking and Jump triggers, while it's active and enabled), "ab" (the AttackCollider's
    ///     own Rigidbody2D: position x, y, awake, the box's enabled flag), "tw" (the tweens on its
    ///     transform: [elapsed, duration, playing] each), "subs" (whether it's in
    ///     HeroMerchant.EnteredInCombat's list);
    ///   - a frame's "ksteps": per fixed step (as "steps"), each kamikaze at the step's start:
    ///     [id, attack body x, y, awake];
    ///   - a frame's "kev": what happened in order, with the fixed step (-1: none), Time.time
    ///     and Time.fixedTime: "move" (MoveEnemy, from the InvokeRepeating: canMove, the
    ///     distance to the hero, the Random state before and after; "dest" and "valid" where it
    ///     wandered (MoveRandomly's point, worked out from the state before it, and whether
    ///     PolyNav took it), the agent's goal after), "jump" (JumpTowardsPlayer: where it and
    ///     he were, the Random state before and after, the end it picked), "continueJump",
    ///     "endJump" (its position), "stop" (StopOnExplode: its position and body velocity
    ///     before), "explode" (CreateExplosionToDamageEnemies: its position, the radius, each
    ///     collider OverlapCircleAll finds (the same query made first: queries change nothing)
    ///     with its path, tag, layer, whether its object has an EnemyBehaviour and that enemy's
    ///     id (-1: not one recorded), its body's position and its transform's z, in the order
    ///     Unity returned them), "hit" (AttackHit(other): the other's name, tag, layer,
    ///     whether it's the Player), "selfDestroy", "combat" (HeroMerchantEnteredInCombat: the
    ///     object active or not), "exitRoom".
    /// The damage a kamikaze deals to an enemy (the explosion's, AttackHit's on an "Enemy") is
    /// marked on EnemyOracle's "dmgToEnemy" as "by" (DamageBy). The SoundsKamikaze component's
    /// clip-event methods join EnemyOracle's "anim" events (its AnimPrefix / AnimPostfix).
    internal static class KamikazeOracle
    {
        private static bool active;
        private static readonly Dictionary<GolemKamikazeBehaviour, float[]> entered = new Dictionary<GolemKamikazeBehaviour, float[]>();
        // Per kamikaze, since its last EnemyRoom_OnRoomEnter (kept from before the recording):
        // how many times MoveEnemy ran, and the last one's [Time.time, Time.fixedTime, in a fixed step].
        private static readonly Dictionary<GolemKamikazeBehaviour, int> moveCount = new Dictionary<GolemKamikazeBehaviour, int>();
        private static readonly Dictionary<GolemKamikazeBehaviour, float[]> lastMove = new Dictionary<GolemKamikazeBehaviour, float[]>();
        private static readonly List<List<string>> stepRows = new List<List<string>>();
        private static readonly List<Ev> events = new List<Ev>();
        private static bool soundsHooked;
        private static string damageBy;

        /// What a kamikaze is dealing damage through right now (null: none).
        internal static string DamageBy => damageBy;

        private class Ev
        {
            public string k;
            public int s = -1, step = -1, layer;
            public float time, fixedTime, a;
            public bool flag, valid, player;
            public string other, tag;
            public Vector2 pos, hero, dest, goal, vel;
            public uint[] rng0, rng1;
            public List<string> hits;
        }

        public static bool Wants(string prefab) => prefab == "KamikazeGolem";

        private static int Step => EnemyOracle.StepIndex;
        private static bool Recording => active && EnemyOracle.Recording;

        public static void Begin(string prefab)
        {
            active = Wants(prefab);
            if (active) HookSounds();
        }

        public static void End() => active = false;

        /// SoundsKamikaze's clip-event methods with EnemyOracle's "anim" hooks (SendMessage
        /// reaches that component too).
        private static void HookSounds()
        {
            if (soundsHooked) return;
            soundsHooked = true;
            try
            {
                var prefix = AccessTools.Method(typeof(EnemyOracle), "AnimPrefix");
                var postfix = AccessTools.Method(typeof(EnemyOracle), "AnimPostfix");
                var harmony = new Harmony("moonlighter.oracle.kamikaze.sounds");
                foreach (var name in new[] { "PlayKamikazeJumpSound", "PlayKamikazeExplosionSound" })
                {
                    var m = AccessTools.Method(typeof(SoundsKamikaze), name);
                    if (m != null) harmony.Patch(m, prefix: new HarmonyMethod(prefix), postfix: new HarmonyMethod(postfix));
                }
            }
            catch (Exception e)
            {
                OraclePlugin.Log.LogWarning("enemy KamikazeGolem: can't hook SoundsKamikaze: " + e.Message);
            }
        }

        private static Json V2(Json j, Vector2 v) => j.Arr().Bits(v.x).Bits(v.y).EndArr();

        private static string PathOf(Transform t)
        {
            string p = t.name;
            for (t = t.parent; t != null; t = t.parent) p = t.name + "/" + p;
            return p;
        }

        private static Rigidbody2D AttackBody(Component b)
        {
            var ac = b.transform.Find("AttackCollider");
            return ac != null ? ac.GetComponent<Rigidbody2D>() : null;
        }

        // ---- EnemyOracle's lines -----------------------------------------------------------

        public static void WriteEnemy(Json j, EnemyBehaviour b)
        {
            if (!active || !(b is GolemKamikazeBehaviour k)) return;
            j.Key("kami").Obj();
            j.Key("enter");
            if (entered.TryGetValue(k, out float[] en)) j.Arr().Bits(en[0]).Bits(en[1]).Val(en[2] != 0f).Val((int)en[3]).EndArr();
            else j.Val((string)null);
            j.Prop("moves", moveCount.TryGetValue(k, out int moves) ? moves : 0);
            j.Key("lastMove");
            if (lastMove.TryGetValue(k, out float[] lm)) j.Arr().Bits(lm[0]).Bits(lm[1]).Val(lm[2] != 0f).EndArr();
            else j.Val((string)null);
            var tr = Traverse.Create(k);
            Vector3 end = tr.Field("jumpEndPosition").GetValue<Vector3>();
            j.Key("jump").Arr().Bits(end.x).Bits(end.y).Bits(tr.Field("jumpTime").GetValue<float>()).EndArr();
            var anim = k.enemyAnimator;
            if (anim != null && anim.isActiveAndEnabled)
                j.Key("trig").Arr().Val(anim.GetBool("Awake")).Val(anim.GetBool("Attacking")).Val(anim.GetBool("Jump")).EndArr();
            var rb = AttackBody(k);
            if (rb != null)
            {
                var box = rb.GetComponent<BoxCollider2D>();
                j.Key("ab").Arr().Bits(rb.position.x).Bits(rb.position.y).Val(rb.IsAwake()).Val(box != null && box.enabled).EndArr();
            }
            j.Key("tw").Arr();
            var tweens = DOTween.TweensByTarget(k.transform);
            if (tweens != null)
                foreach (var t in tweens) j.Arr().Bits(t.Elapsed(false)).Bits(t.Duration(false)).Val(t.IsPlaying()).EndArr();
            j.EndArr();
            bool subscribed = false;
            try
            {
                var field = AccessTools.Field(typeof(HeroMerchant), "EnteredInCombat");
                var d = field != null ? field.GetValue(HeroMerchant.Instance) as Delegate : null;
                if (d != null)
                    foreach (var h in d.GetInvocationList()) subscribed |= h.Target == (object)k;
            }
            catch (Exception) { }
            j.Prop("subs", subscribed);
            j.EndObj();
        }

        /// A fixed step's start (EnemyOracle's Recorder.FixedUpdate).
        public static void OnStep()
        {
            if (!active) return;
            var rows = new List<string>();
            foreach (var k in UnityEngine.Object.FindObjectsOfType<GolemKamikazeBehaviour>())
            {
                int id = EnemyOracle.IdOf(k);
                var rb = AttackBody(k);
                if (id < 0 || rb == null) continue;
                rows.Add(new Json().Arr().Val(id).Bits(rb.position.x).Bits(rb.position.y).Val(rb.IsAwake()).EndArr().ToString());
            }
            stepRows.Add(rows);
        }

        public static void Clear()
        {
            stepRows.Clear();
            events.Clear();
        }

        public static void WriteFrame(Json j)
        {
            if (!active) return;
            j.Key("ksteps").Arr();
            foreach (var rows in stepRows)
            {
                j.Arr();
                foreach (var r in rows) j.Raw(r);
                j.EndArr();
            }
            j.EndArr();
            j.Key("kev").Arr();
            foreach (var e in events)
            {
                j.Obj().Prop("k", e.k).Prop("s", e.s).Prop("step", e.step).Key("time").Bits(e.time).Key("ft").Bits(e.fixedTime);
                switch (e.k)
                {
                    case "move":
                        j.Prop("canMove", e.flag).Key("dist").Bits(e.a).Prop("wander", e.hits != null);
                        if (e.hits != null)
                        {
                            j.Prop("valid", e.valid).Key("dest");
                            V2(j, e.dest);
                        }
                        j.Key("goal"); V2(j, e.goal);
                        Rng.WriteState(j.Key("rng0"), e.rng0);
                        Rng.WriteState(j.Key("rng1"), e.rng1 ?? e.rng0);
                        break;
                    case "jump":
                        j.Key("pos"); V2(j, e.pos);
                        j.Key("hero"); V2(j, e.hero);
                        j.Key("end"); V2(j, e.dest);
                        Rng.WriteState(j.Key("rng0"), e.rng0);
                        Rng.WriteState(j.Key("rng1"), e.rng1 ?? e.rng0);
                        break;
                    case "continueJump":
                    case "endJump":
                    case "selfDestroy":
                        j.Key("pos"); V2(j, e.pos);
                        j.Prop("layer", e.layer);
                        break;
                    case "stop":
                        j.Key("pos"); V2(j, e.pos);
                        j.Key("vel"); V2(j, e.vel);
                        break;
                    case "explode":
                        j.Key("pos"); V2(j, e.pos);
                        j.Key("radius").Bits(e.a);
                        j.Key("hits").Arr();
                        foreach (var h in e.hits) j.Raw(h);
                        j.EndArr();
                        break;
                    case "hit":
                        j.Prop("other", e.other).Prop("tag", e.tag).Prop("layer", e.layer).Prop("player", e.player);
                        break;
                    case "combat":
                        j.Prop("active", e.flag);
                        break;
                }
                j.EndObj();
            }
            j.EndArr();
        }

        // ---- Hooks ------------------------------------------------------------------------

        private static Ev Add(string k, GolemKamikazeBehaviour b)
        {
            var e = new Ev { k = k, s = EnemyOracle.IdOf(b), step = Step, time = Time.time, fixedTime = Time.fixedTime };
            events.Add(e);
            return e;
        }

        [HarmonyPatch(typeof(GolemKamikazeBehaviour), "EnemyRoom_OnRoomEnter")]
        private static class EnterHook
        {
            // Kept for every kamikaze from before the recording (when its room is entered).
            private static void Prefix(GolemKamikazeBehaviour __instance)
            {
                entered[__instance] = new[] { Time.time, Time.fixedTime, Time.inFixedTimeStep ? 1f : 0f, (float)Time.frameCount };
                moveCount[__instance] = 0;
                lastMove.Remove(__instance);
            }
        }

        [HarmonyPatch(typeof(GolemKamikazeBehaviour), "MoveEnemy")]
        private static class MoveHook
        {
            private static void Prefix(GolemKamikazeBehaviour __instance, out Ev __state)
            {
                __state = null;
                // (Counted from the room's entry on, recording or not: the replay's InvokeRepeating
                // goes on from the next one.)
                moveCount[__instance] = (moveCount.TryGetValue(__instance, out int n) ? n : 0) + 1;
                lastMove[__instance] = new[] { Time.time, Time.fixedTime, Time.inFixedTimeStep ? 1f : 0f };
                if (!Recording || EnemyOracle.IdOf(__instance) < 0) return;
                var e = Add("move", __instance);
                e.flag = __instance.canMove;
                e.a = Vector2.Distance(__instance.transform.position, HeroMerchant.Instance.transform.position);
                e.rng0 = Rng.Get();
                if (e.flag && e.a > 80f)
                {
                    // MoveRandomly's point, from the same draws (put back after).
                    Vector2 c = UnityEngine.Random.insideUnitCircle;
                    Vector3 p = __instance.transform.position;
                    Vector3 v = p + new Vector3(c.x, c.y, p.z) * UnityEngine.Random.Range(60, 90);
                    Rng.Set(e.rng0);
                    e.dest = v;
                    e.valid = PolyNav2D.current != null && PolyNav2D.current.PointIsValid(v);
                    e.hits = new List<string>();
                }
                __state = e;
            }

            private static void Postfix(GolemKamikazeBehaviour __instance, Ev __state)
            {
                if (__state == null) return;
                __state.rng1 = Rng.Get();
                __state.goal = __instance.enemy.navAgent.primeGoal;
            }
        }

        [HarmonyPatch(typeof(GolemKamikazeBehaviour), "JumpTowardsPlayer")]
        private static class JumpHook
        {
            private static void Prefix(GolemKamikazeBehaviour __instance, out Ev __state)
            {
                __state = null;
                if (!Recording || EnemyOracle.IdOf(__instance) < 0) return;
                __state = Add("jump", __instance);
                __state.pos = __instance.transform.position;
                __state.hero = HeroMerchant.Instance.transform.position;
                __state.rng0 = Rng.Get();
            }

            private static void Postfix(GolemKamikazeBehaviour __instance, Ev __state)
            {
                if (__state == null) return;
                __state.rng1 = Rng.Get();
                __state.dest = Traverse.Create(__instance).Field("jumpEndPosition").GetValue<Vector3>();
            }
        }

        private static void Moment(string k, EnemyBehaviour b)
        {
            if (!Recording || !(b is GolemKamikazeBehaviour g) || EnemyOracle.IdOf(g) < 0) return;
            var e = Add(k, g);
            e.pos = g.transform.position;
            e.layer = g.gameObject.layer;
        }

        [HarmonyPatch(typeof(EnemyBehaviour), "ContinueJump")]
        private static class ContinueJumpHook
        {
            private static void Prefix(EnemyBehaviour __instance) => Moment("continueJump", __instance);
        }

        [HarmonyPatch(typeof(EnemyBehaviour), "EndJump")]
        private static class EndJumpHook
        {
            private static void Postfix(EnemyBehaviour __instance) => Moment("endJump", __instance);
        }

        [HarmonyPatch(typeof(GolemKamikazeBehaviour), "SelfDestroy")]
        private static class SelfDestroyHook
        {
            private static void Prefix(GolemKamikazeBehaviour __instance) => Moment("selfDestroy", __instance);
        }

        [HarmonyPatch(typeof(GolemKamikazeBehaviour), "StopOnExplode")]
        private static class StopHook
        {
            private static void Prefix(GolemKamikazeBehaviour __instance)
            {
                if (!Recording || EnemyOracle.IdOf(__instance) < 0) return;
                var e = Add("stop", __instance);
                e.pos = __instance.transform.position;
                var rb = __instance.GetComponent<Rigidbody2D>();
                e.vel = rb != null ? rb.velocity : Vector2.zero;
            }
        }

        [HarmonyPatch(typeof(GolemKamikazeBehaviour), "CreateExplosionToDamageEnemies")]
        private static class ExplodeHook
        {
            private static void Prefix(GolemKamikazeBehaviour __instance)
            {
                damageBy = "explosion";
                if (!Recording || EnemyOracle.IdOf(__instance) < 0) return;
                var e = Add("explode", __instance);
                e.pos = __instance.transform.position;
                e.a = Constants.GetFloat("tileSize") * 3.5f;
                e.hits = new List<string>();
                foreach (var c in Physics2D.OverlapCircleAll(__instance.transform.position, e.a))
                {
                    var eb = c.GetComponent<EnemyBehaviour>();
                    var j = new Json().Obj().Prop("path", PathOf(c.transform)).Prop("tag", c.tag).Prop("layer", c.gameObject.layer)
                        .Prop("type", c.GetType().Name).Prop("trigger", c.isTrigger).Prop("eb", eb != null).Prop("id", eb != null ? EnemyOracle.IdOf(eb) : -1)
                        .Prop("body", c.attachedRigidbody != null ? c.attachedRigidbody.bodyType.ToString() : "none");
                    j.Key("bodyPos"); V2(j, c.attachedRigidbody != null ? c.attachedRigidbody.position : (Vector2)c.transform.position);
                    j.Key("z").Bits(c.transform.position.z); // (the results' order: by depth?)
                    e.hits.Add(j.EndObj().ToString());
                }
            }

            private static void Postfix() => damageBy = null;
        }

        [HarmonyPatch(typeof(GolemKamikazeBehaviour), "AttackHit", new[] { typeof(GameObject) })]
        private static class HitHook
        {
            private static void Prefix(GolemKamikazeBehaviour __instance, GameObject other)
            {
                damageBy = "attackHit";
                if (!Recording || EnemyOracle.IdOf(__instance) < 0 || other == null) return;
                var e = Add("hit", __instance);
                e.other = other.name;
                e.tag = other.tag;
                e.layer = other.layer;
                e.player = other.CompareTag("Player");
            }

            private static void Postfix() => damageBy = null;
        }

        [HarmonyPatch(typeof(GolemKamikazeBehaviour), "HeroMerchantEnteredInCombat")]
        private static class CombatHook
        {
            private static void Prefix(GolemKamikazeBehaviour __instance)
            {
                if (!Recording || EnemyOracle.IdOf(__instance) < 0) return;
                Add("combat", __instance).flag = __instance.gameObject.activeInHierarchy;
            }
        }

        [HarmonyPatch(typeof(GolemKamikazeBehaviour), "OnExitRoom")]
        private static class ExitHook
        {
            private static void Prefix(GolemKamikazeBehaviour __instance)
            {
                if (Recording && EnemyOracle.IdOf(__instance) >= 0) Add("exitRoom", __instance);
            }
        }
    }

    /// EnemyOracle's own events for the KamikazeGolem, where its overrides go around the generic
    /// hooks:
    ///   - "attackHit": GolemKamikazeBehaviour.AttackHit overrides EnemyBehaviour.AttackHit
    ///     without calling it, so the generic AttackHitHook never sees its OnTriggerEnter2D; this
    ///     one adds the same event (recordings without "attackEnters"); since, its attack
    ///     collider's enters are EnemyOracle's "attackEnter", and this records only calls from
    ///     elsewhere, as AttackHitHook does;
    ///   - "anim" callers: a clip-event method its own clip-event methods call (StopOnExplode's
    ///     ForbidEnemyMovement; anything CreateExplosionToDamageEnemies' damage or SelfDestroy's
    ///     Die reach) is a script's call, not the Animator's. ScriptCaller finds the caller on the
    ///     stack, but these callers are Harmony-patched themselves (their frames are Harmony's
    ///     replacements, outside Assembly-CSharp), so an "anim" event added inside one with no
    ///     caller gets that method as its caller.
    internal static partial class EnemyOracle
    {
        [HarmonyPatch(typeof(GolemKamikazeBehaviour), "AttackHit", new[] { typeof(GameObject) })]
        private static class KamikazeAttackHitHook
        {
            private static void Prefix(GolemKamikazeBehaviour __instance, GameObject other)
            {
                if (attackEnterDepth > 0) return; // (recorded as "attackEnter": EnemyOracle.AttackTriggerHook)
                int id = Id(__instance);
                if (id >= 0) Add(new Ev { k = "attackHit", s = id, step = Step, player = other != null && other.CompareTag("Player"), other = other != null ? other.name : null });
            }
        }

        /// The events added from `mark` on: an "anim" one with no caller (but the method's own)
        /// was called by `method`.
        private static void NestedAnimCallers(int mark, string method)
        {
            for (int i = mark; i < events.Count; i++)
            {
                var e = events[i];
                if (e.k == "anim" && e.caller == null && e.other != method) e.caller = "GolemKamikazeBehaviour." + method;
            }
        }

        [HarmonyPatch(typeof(GolemKamikazeBehaviour), "StopOnExplode")]
        private static class KamikazeStopNesting
        {
            private static void Prefix(out int __state) => __state = events.Count;
            private static void Postfix(int __state) => NestedAnimCallers(__state, "StopOnExplode");
        }

        [HarmonyPatch(typeof(GolemKamikazeBehaviour), "CreateExplosionToDamageEnemies")]
        private static class KamikazeExplosionNesting
        {
            private static void Prefix(out int __state) => __state = events.Count;
            private static void Postfix(int __state) => NestedAnimCallers(__state, "CreateExplosionToDamageEnemies");
        }

        [HarmonyPatch(typeof(GolemKamikazeBehaviour), "SelfDestroy")]
        private static class KamikazeSelfDestroyNesting
        {
            private static void Prefix(out int __state) => __state = events.Count;
            private static void Postfix(int __state) => NestedAnimCallers(__state, "SelfDestroy");
        }
    }
}
