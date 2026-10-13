using System;
using System.Collections.Generic;
using System.Reflection;
using HarmonyLib;
using UnityEngine;

namespace MoonlighterOracle
{
    /// The FlyingKamikazeGolem's part of `-oracle-enemy=FlyingKamikazeGolem` (EnemyOracle records
    /// the enemies; this adds the golem's own state and what its charges do), written into
    /// EnemyOracle's lines:
    ///   - each enemy ("enemy", a frame's "enemies"): "fly": isCharging, isReturning,
    ///     desiredDirection, angle, bounds (x, y, width, height), chargePosition, enemyDirection,
    ///     the root collider's enabled flag (_collider.enabled), the Attack object's activeSelf,
    ///     IsInvoking("EndCharge") / ("EnableCharge"), its transform's z; "enSnd": its
    ///     "SoundsFlyingKamikaze" child's EventSounds.OnEnable calls since the last row (its
    ///     enable sound, MasterAudio.PlaySound's draws), each with Time.frameCount, Time.time and
    ///     the Random state before and after (kept from before the recording too: EnableEnemies
    ///     switches the golems on before it starts);
    ///   - a frame's "fsteps": per fixed step (as "steps"), each golem at the step's start:
    ///     [id, root collider enabled, Attack active];
    ///   - a frame's "fev": what happened in order, with the fixed step (-1: none) and Time.time:
    ///     "prepare" (PrepareChargeAttack), "charge" (MakeChargeAttack: before it the hero's and the
    ///     golem's positions with z, the Random state; after it the state, chargePosition,
    ///     desiredDirection, bounds), "endCharge" (EndCharge, the Invoke), "retDone" (the return
    ///     tween's OnComplete: the Random state before and after, its Range(2, 6)), "enable"
    ///     (EnableCharge, the Invoke).
    internal static class FlyKamiOracle
    {
        private static bool active, lambdaHooked;
        private static readonly List<List<string>> stepRows = new List<List<string>>();
        private static readonly List<Ev> events = new List<Ev>();
        private static readonly Dictionary<FlyingKamikazeGolemBehaviour, List<EnableSnd>> enableSnds = new Dictionary<FlyingKamikazeGolemBehaviour, List<EnableSnd>>();

        private class EnableSnd
        {
            public int frame;
            public float time;
            public uint[] rng0, rng1;
        }

        private class Ev
        {
            public string k;
            public int s = -1, step = -1;
            public float time, heroZ, posZ;
            public Vector2 hero, pos, cp, dd;
            public Rect bounds;
            public uint[] rng0, rng1;
        }

        public static bool Wants(string prefab) => prefab == "FlyingKamikazeGolem";

        private static int Step => EnemyOracle.StepIndex;

        private static bool Recording => active && EnemyOracle.Recording;

        private static Json V2(Json j, Vector2 v) => j.Arr().Bits(v.x).Bits(v.y).EndArr();

        private static Json RectBits(Json j, Rect r) => j.Arr().Bits(r.x).Bits(r.y).Bits(r.width).Bits(r.height).EndArr();

        public static void Begin(string prefab)
        {
            active = Wants(prefab);
            if (active) HookReturnLambda();
        }

        public static void End() => active = false;

        /// The return tween's OnComplete: the compiler's `<EndCharge>b__*` instance method that
        /// takes nothing and returns nothing (the getter returns the velocity, the setter takes it).
        private static void HookReturnLambda()
        {
            if (lambdaHooked) return;
            lambdaHooked = true;
            var harmony = new Harmony("moonlighter.oracle.flykami");
            foreach (var m in typeof(FlyingKamikazeGolemBehaviour).GetMethods(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly))
            {
                if (!m.Name.StartsWith("<EndCharge>", StringComparison.Ordinal) || m.ReturnType != typeof(void) || m.GetParameters().Length != 0) continue;
                try
                {
                    harmony.Patch(m, prefix: new HarmonyMethod(typeof(FlyKamiOracle), nameof(ReturnPrefix)),
                                  postfix: new HarmonyMethod(typeof(FlyKamiOracle), nameof(ReturnPostfix)));
                    OraclePlugin.Log.LogInfo("enemy FlyingKamikazeGolem: hooked " + m.Name);
                }
                catch (Exception e)
                {
                    OraclePlugin.Log.LogWarning("enemy FlyingKamikazeGolem: can't hook " + m.Name + ": " + e.Message);
                }
            }
        }

        private static void ReturnPrefix(object __instance, out Ev __state)
        {
            __state = null;
            if (!Recording) return;
            int s = EnemyOracle.IdOf(__instance);
            if (s < 0) return;
            __state = new Ev { k = "retDone", s = s, step = Step, rng0 = Rng.Get() };
            Add(__state);
        }

        private static void ReturnPostfix(Ev __state)
        {
            if (__state != null) __state.rng1 = Rng.Get();
        }

        // ---- EnemyOracle's lines -----------------------------------------------------------

        public static void WriteEnemy(Json j, EnemyBehaviour b)
        {
            if (!active || !(b is FlyingKamikazeGolemBehaviour golem)) return;
            var tr = Traverse.Create(golem);
            var collider = tr.Field("_collider").GetValue<Collider2D>();
            j.Key("fly").Obj().Prop("charging", tr.Field("isCharging").GetValue<bool>()).Prop("returning", tr.Field("isReturning").GetValue<bool>());
            j.Key("dd"); V2(j, tr.Field("desiredDirection").GetValue<Vector2>());
            j.Key("angle").Bits(tr.Field("angle").GetValue<float>());
            j.Key("bounds"); RectBits(j, tr.Field("bounds").GetValue<Rect>());
            j.Key("cp"); V2(j, tr.Field("chargePosition").GetValue<Vector3>());
            j.Prop("dir", (int)golem.enemyDirection);
            j.Prop("colOn", collider != null ? collider.enabled : golem.GetComponent<Collider2D>().enabled);
            j.Prop("attackActive", golem.Attack != null && golem.Attack.activeSelf);
            j.Prop("invEnd", golem.IsInvoking("EndCharge")).Prop("invEnable", golem.IsInvoking("EnableCharge"));
            j.Key("z").Bits(golem.transform.position.z);
            if (enableSnds.TryGetValue(golem, out var snds) && snds.Count > 0)
            {
                j.Key("enSnd").Arr();
                foreach (var e in snds)
                {
                    j.Obj().Prop("fc", e.frame).Key("time").Bits(e.time);
                    Rng.WriteState(j.Key("rng0"), e.rng0);
                    Rng.WriteState(j.Key("rng1"), e.rng1 ?? e.rng0);
                    j.EndObj();
                }
                j.EndArr();
                snds.Clear();
            }
            j.EndObj();
        }

        /// A fixed step's start (EnemyOracle's Recorder.FixedUpdate).
        public static void OnStep(List<EnemyBehaviour> enemies)
        {
            if (!active) return;
            var rows = new List<string>();
            for (int i = 0; i < enemies.Count; i++)
            {
                if (!(enemies[i] is FlyingKamikazeGolemBehaviour golem) || golem == null) continue;
                var col = golem.GetComponent<Collider2D>();
                rows.Add(new Json().Arr().Val(i).Val(col != null && col.enabled).Val(golem.Attack != null && golem.Attack.activeSelf).EndArr().ToString());
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
            j.Key("fsteps").Arr();
            foreach (var rows in stepRows)
            {
                j.Arr();
                foreach (var r in rows) j.Raw(r);
                j.EndArr();
            }
            j.EndArr();
            j.Key("fev").Arr();
            foreach (var e in events)
            {
                j.Obj().Prop("k", e.k).Prop("s", e.s).Prop("step", e.step).Key("time").Bits(e.time);
                switch (e.k)
                {
                    case "charge":
                        j.Key("hero"); V2(j, e.hero);
                        j.Key("pos"); V2(j, e.pos);
                        j.Key("heroZ").Bits(e.heroZ).Key("posZ").Bits(e.posZ);
                        j.Key("cp"); V2(j, e.cp);
                        j.Key("dd"); V2(j, e.dd);
                        j.Key("bounds"); RectBits(j, e.bounds);
                        Rng.WriteState(j.Key("rng0"), e.rng0);
                        Rng.WriteState(j.Key("rng1"), e.rng1 ?? e.rng0);
                        break;
                    case "retDone":
                        Rng.WriteState(j.Key("rng0"), e.rng0);
                        Rng.WriteState(j.Key("rng1"), e.rng1 ?? e.rng0);
                        break;
                }
                j.EndObj();
            }
            j.EndArr();
        }

        // ---- Hooks ------------------------------------------------------------------------

        private static void Add(Ev e)
        {
            e.time = Time.time;
            events.Add(e);
        }

        private static void Simple(object instance, string k)
        {
            if (!Recording) return;
            int s = EnemyOracle.IdOf(instance);
            if (s >= 0) Add(new Ev { k = k, s = s, step = Step });
        }

        [HarmonyPatch(typeof(FlyingKamikazeGolemBehaviour), "PrepareChargeAttack")]
        private static class PrepareHook
        {
            private static void Prefix(FlyingKamikazeGolemBehaviour __instance) => Simple(__instance, "prepare");
        }

        [HarmonyPatch(typeof(FlyingKamikazeGolemBehaviour), "EndCharge")]
        private static class EndChargeHook
        {
            private static void Prefix(FlyingKamikazeGolemBehaviour __instance) => Simple(__instance, "endCharge");
        }

        [HarmonyPatch(typeof(FlyingKamikazeGolemBehaviour), "EnableCharge")]
        private static class EnableHook
        {
            private static void Prefix(FlyingKamikazeGolemBehaviour __instance) => Simple(__instance, "enable");
        }

        /// EventSounds.OnEnable on a golem's child (always patched: the golems are switched on
        /// before the recording starts), its draws for the golem's next "fly".
        [HarmonyPatch(typeof(DarkTonic.MasterAudio.EventSounds), "OnEnable")]
        private static class EventSoundsEnableHook
        {
            private static void Prefix(DarkTonic.MasterAudio.EventSounds __instance, out EnableSnd __state)
            {
                __state = null;
                Transform parent = __instance.transform.parent;
                FlyingKamikazeGolemBehaviour golem = parent != null ? parent.GetComponent<FlyingKamikazeGolemBehaviour>() : null;
                if (golem == null) return;
                __state = new EnableSnd { frame = Time.frameCount, time = Time.time, rng0 = Rng.Get() };
                if (!enableSnds.TryGetValue(golem, out var list)) enableSnds[golem] = list = new List<EnableSnd>();
                if (list.Count >= 16) list.RemoveAt(0);
                list.Add(__state);
            }

            private static void Postfix(EnableSnd __state)
            {
                if (__state != null) __state.rng1 = Rng.Get();
            }
        }

        [HarmonyPatch(typeof(FlyingKamikazeGolemBehaviour), "MakeChargeAttack")]
        private static class ChargeHook
        {
            private static void Prefix(FlyingKamikazeGolemBehaviour __instance, out Ev __state)
            {
                __state = null;
                if (!Recording) return;
                int s = EnemyOracle.IdOf(__instance);
                if (s < 0) return;
                Vector3 hero = HeroMerchant.Instance.transform.position, pos = __instance.transform.position;
                __state = new Ev { k = "charge", s = s, step = Step, hero = hero, pos = pos, heroZ = hero.z, posZ = pos.z, rng0 = Rng.Get() };
                Add(__state);
            }

            private static void Postfix(FlyingKamikazeGolemBehaviour __instance, Ev __state)
            {
                if (__state == null) return;
                var tr = Traverse.Create(__instance);
                __state.rng1 = Rng.Get();
                __state.cp = tr.Field("chargePosition").GetValue<Vector3>();
                __state.dd = tr.Field("desiredDirection").GetValue<Vector2>();
                __state.bounds = tr.Field("bounds").GetValue<Rect>();
            }
        }
    }
}
