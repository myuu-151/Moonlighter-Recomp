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
    ///     object active or not), "exitRoom"; and, for whose Random draws they are (s -1): "snd"
    ///     (every MasterAudio play, PlaySoundAtVolume: the group, whether a parent group played
    ///     it, whether it played, the Random state before and after, the scripts on the stack:
    ///     "callers", nearest first), "unlockChests" (DungeonRoom.UnLockRoomChests, the room's
    ///     OnAllEnemiesDefeated: the Random state before and after), "indicator"
    ///     (QuantitytIndicatorManager.InstantiateFloatingDamageIndicator: invincible or not, the
    ///     Random state before and after);
    ///   - inside CreateExplosionToDamageEnemies and SelfDestroy only (their Random draws the
    ///     decomp doesn't explain: enemy_KamikazeGolem.jsonl of recq kamifix-1 and -2, one draw
    ///     in the explosion before the floating damage number): "window" (its start and end:
    ///     "other" the method, and at the explosion's start Enemy.OnEnemyRecivesDamage's handlers
    ///     and MoonlighterEffectRegistry's effects), "rnd" (a call of one of UnityEngine.Random's
    ///     managed members: "other" the member, "callers" the stack's methods, nearest first, any
    ///     assembly), "br" (a call of a method on the explosion's path: Physics2D.OverlapCircleAll,
    ///     Enemy.DealDamageToEnemy / CalcHitDamage, EnemyStats.CurrentHealth's setter, the
    ///     singletons' getters...: "other" the method), each with the Random state before and
    ///     after;
    ///   - the start line's "kroom" (WriteStart): the room's chests ([type, name, isLocked, active]
    ///     in roomChests' order), OnAllEnemiesDefeated's handlers ("Type.Method" in order),
    ///     isRoomCleared and "locked" (its isLocked); and "heroEffects": Enemy.OnEnemyRecivesDamage's
    ///     handlers ("damage", "Type.Method" in order), MoonlighterEffectRegistry's effects
    ///     ("effects": each [type, active] and, for a WandererAmuletEffect, its probToCallWanderer
    ///     bits), DungeonManager.isWandererActive ("wandererActive"). (kamifix-3: the hero's
    ///     Wanderer amulet draws Random.value on every DealDamageToEnemy, the kamikaze's
    ///     explosion on itself included.)
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
            public string sound;
            public bool played;
        }

        public static bool Wants(string prefab) => prefab == "KamikazeGolem";

        private static int Step => EnemyOracle.StepIndex;
        private static bool Recording => active && EnemyOracle.Recording;

        public static void Begin(string prefab)
        {
            active = Wants(prefab);
            if (active) HookSounds();
            if (active) HookRandomProbe();
        }

        // ---- The draws inside the explosion and SelfDestroy ---------------------------------

        private static int window; // inside CreateExplosionToDamageEnemies or SelfDestroy (depth)
        private static bool inProbe;  // the probe's own Random.state reads and stack walks
        private static bool randomProbeHooked;

        private static string Stack()
        {
            var st = new System.Diagnostics.StackTrace(2, false);
            var names = new List<string>();
            for (int i = 0; i < st.FrameCount && names.Count < 8; i++)
            {
                var m = st.GetFrame(i).GetMethod();
                if (m == null) continue;
                var t = m.DeclaringType;
                if (t != null && t.Assembly == typeof(KamikazeOracle).Assembly) continue; // (the oracle's own)
                names.Add((t != null ? t.Name : "?") + "." + m.Name);
            }
            return string.Join(" < ", names.ToArray());
        }

        private static void HookRandomProbe()
        {
            if (randomProbeHooked) return;
            randomProbeHooked = true;
            var harmony = new Harmony("moonlighter.oracle.kamikaze.random");
            int n = 0;
            var rndPrefix = new HarmonyMethod(typeof(KamikazeOracle), nameof(RndPrefix));
            var brPrefix = new HarmonyMethod(typeof(KamikazeOracle), nameof(BrPrefix));
            var postfix = new HarmonyMethod(typeof(KamikazeOracle), nameof(ProbePostfix));
            foreach (var m in typeof(UnityEngine.Random).GetMethods(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static))
            {
                if (m.ContainsGenericParameters || m.GetMethodBody() == null || m.Name == "get_state" || m.Name == "set_state") continue;
                try { harmony.Patch(m, prefix: rndPrefix, postfix: postfix); n++; }
                catch (Exception) { }
            }
            var path = new List<MethodBase>();
            foreach (var m in typeof(Physics2D).GetMethods(BindingFlags.Public | BindingFlags.Static))
                if (m.Name == "OverlapCircleAll") path.Add(m);
            path.Add(AccessTools.Method(typeof(Enemy), "DealDamageToEnemy"));
            path.Add(AccessTools.Method(typeof(Enemy), "CalcHitDamage"));
            path.Add(AccessTools.PropertySetter(typeof(EnemyStats), "CurrentHealth"));
            path.Add(AccessTools.PropertyGetter(typeof(EnemyStats), "Invencible"));
            path.Add(AccessTools.Method(typeof(EnemyBehaviour), "IsEnemyPlayingDamagedEffect"));
            path.Add(AccessTools.PropertyGetter(typeof(QuantitytIndicatorManager), "Instance"));
            path.Add(AccessTools.PropertyGetter(typeof(HeroMerchant), "Instance"));
            path.Add(AccessTools.PropertyGetter(typeof(MoonlighterEffectRegistry), "Instance"));
            path.Add(AccessTools.Method(typeof(EnemyStats), "Die"));
            path.Add(AccessTools.Method(typeof(EnemyStats), "MarkEnemyAsDefeated"));
            path.Add(AccessTools.Method(typeof(DungeonRoom), "EnemyDefeated"));
            path.Add(AccessTools.Method(typeof(DungeonRoom), "OpenAllDoors"));
            foreach (var m in path)
            {
                if (m == null || m.GetMethodBody() == null) continue;
                try { harmony.Patch(m, prefix: brPrefix, postfix: postfix); n++; }
                catch (Exception e) { OraclePlugin.Log.LogWarning("enemy KamikazeGolem: can't hook " + m.Name + ": " + e.Message); }
            }
            OraclePlugin.Log.LogInfo("enemy KamikazeGolem: random probe, " + n + " methods hooked");
        }

        private static void RndPrefix(MethodBase __originalMethod, out Ev __state)
        {
            __state = null;
            if (window <= 0 || !Recording || inProbe) return;
            inProbe = true;
            __state = AddPlain("rnd");
            __state.other = __originalMethod.Name;
            __state.sound = Stack();
            inProbe = false;
        }

        private static void BrPrefix(MethodBase __originalMethod, out Ev __state)
        {
            __state = null;
            if (window <= 0 || !Recording || inProbe) return;
            inProbe = true;
            __state = AddPlain("br");
            __state.other = __originalMethod.DeclaringType.Name + "." + __originalMethod.Name;
            inProbe = false;
        }

        private static void ProbePostfix(Ev __state)
        {
            if (__state == null) return;
            inProbe = true;
            __state.rng1 = Rng.Get();
            inProbe = false;
        }

        private static string Handlers()
        {
            var parts = new List<string>();
            try
            {
                var f = AccessTools.Field(typeof(Enemy), "OnEnemyRecivesDamage");
                var d = f != null ? f.GetValue(null) as Delegate : null;
                if (d != null)
                    foreach (var h in d.GetInvocationList()) parts.Add("damage:" + h.Method.DeclaringType.Name + "." + h.Method.Name);
                var inst = AccessTools.Field(typeof(MoonlighterEffectRegistry), "_instance").GetValue(null);
                var reg = inst != null ? AccessTools.Field(typeof(MoonlighterEffectRegistry), "_reg").GetValue(inst) as System.Collections.IEnumerable : null;
                if (reg != null)
                    foreach (var entry in reg)
                    {
                        var eff = entry != null ? Traverse.Create(entry).Field("effect").GetValue() : null;
                        parts.Add("effect:" + (eff != null ? eff.GetType().Name : "null"));
                    }
            }
            catch (Exception e) { parts.Add("error:" + e.Message); }
            return string.Join(" ", parts.ToArray());
        }

        [HarmonyPatch(typeof(GolemKamikazeBehaviour), "CreateExplosionToDamageEnemies")]
        private static class ExplosionWindow
        {
            [HarmonyPriority(Priority.First)]
            private static void Prefix()
            {
                if (!Recording) return;
                inProbe = true;
                var e = AddPlain("window");
                e.other = "CreateExplosionToDamageEnemies " + Handlers();
                e.flag = true;
                inProbe = false;
                window++;
            }

            [HarmonyPriority(Priority.Last)]
            private static void Postfix()
            {
                if (!Recording || window <= 0) return;
                window--;
                inProbe = true;
                AddPlain("window").other = "CreateExplosionToDamageEnemies";
                inProbe = false;
            }
        }

        [HarmonyPatch(typeof(GolemKamikazeBehaviour), "SelfDestroy")]
        private static class SelfDestroyWindow
        {
            [HarmonyPriority(Priority.First)]
            private static void Prefix()
            {
                if (!Recording) return;
                inProbe = true;
                var e = AddPlain("window");
                e.other = "SelfDestroy";
                e.flag = true;
                inProbe = false;
                window++;
            }

            [HarmonyPriority(Priority.Last)]
            private static void Postfix()
            {
                if (!Recording || window <= 0) return;
                window--;
                inProbe = true;
                AddPlain("window").other = "SelfDestroy";
                inProbe = false;
            }
        }

        public static void End() => active = false;

        /// The start line's "kroom" (the room the recording is in).
        public static void WriteStart(Json j, DungeonRoom room)
        {
            if (!active || room == null) return;
            j.Key("kroom").Obj();
            j.Key("chests").Arr();
            if (room.roomChests != null)
                foreach (var c in room.roomChests)
                    if (c != null) j.Arr().Val((int)c.type).Val(c.name).Val(c.isLocked).Val(c.gameObject.activeInHierarchy).EndArr();
            j.EndArr();
            j.Key("allDefeated").Arr();
            try
            {
                var field = AccessTools.Field(typeof(DungeonRoom), "OnAllEnemiesDefeated");
                var d = field != null ? field.GetValue(room) as Delegate : null;
                if (d != null)
                    foreach (var h in d.GetInvocationList()) j.Val(h.Method.DeclaringType.Name + "." + h.Method.Name);
            }
            catch (Exception) { }
            j.EndArr();
            j.Prop("cleared", room.isRoomCleared);
            j.Prop("locked", room.isLocked);
            j.EndObj();
            j.Key("heroEffects").Obj();
            j.Key("damage").Arr();
            try
            {
                var f = AccessTools.Field(typeof(Enemy), "OnEnemyRecivesDamage");
                var d = f != null ? f.GetValue(null) as Delegate : null;
                if (d != null)
                    foreach (var h in d.GetInvocationList()) j.Val(h.Method.DeclaringType.Name + "." + h.Method.Name);
            }
            catch (Exception) { }
            j.EndArr();
            j.Key("effects").Arr();
            try
            {
                var inst = AccessTools.Field(typeof(MoonlighterEffectRegistry), "_instance").GetValue(null);
                var reg = inst != null ? AccessTools.Field(typeof(MoonlighterEffectRegistry), "_reg").GetValue(inst) as System.Collections.IEnumerable : null;
                if (reg != null)
                    foreach (var entry in reg)
                    {
                        var eff = entry != null ? Traverse.Create(entry).Field("effect").GetValue() as MoonlighterEffect : null;
                        if (eff == null) continue;
                        j.Arr().Val(eff.GetType().Name).Val(eff is AmuletMoonlighterEffect a && a.IsEffectActive());
                        if (eff is WandererAmuletEffect w) j.Bits(w.probToCallWanderer);
                        j.EndArr();
                    }
            }
            catch (Exception) { }
            j.EndArr();
            j.Prop("wandererActive", DungeonManager.Instance != null && DungeonManager.Instance.isWandererActive);
            j.EndObj();
        }

        /// The scripts on the stack (Assembly-CSharp's and MasterAudio's own components', not
        /// MasterAudio itself nor the oracle's): "Type.Method", nearest first, at most five.
        private static string Callers()
        {
            var st = new System.Diagnostics.StackTrace(2, false);
            var game = typeof(EnemyBehaviour).Assembly;
            var ma = typeof(DarkTonic.MasterAudio.MasterAudio);
            var names = new List<string>();
            for (int i = 0; i < st.FrameCount && names.Count < 5; i++)
            {
                var m = st.GetFrame(i).GetMethod();
                if (m == null || m.DeclaringType == null || m.DeclaringType == ma) continue;
                var asm = m.DeclaringType.Assembly;
                if (asm != game && asm != ma.Assembly) continue;
                names.Add(m.DeclaringType.Name + "." + m.Name);
            }
            return string.Join(" < ", names.ToArray());
        }

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
                    case "snd":
                        j.Prop("sound", e.sound).Prop("child", e.flag).Prop("played", e.played).Prop("callers", e.other);
                        Rng.WriteState(j.Key("rng0"), e.rng0);
                        Rng.WriteState(j.Key("rng1"), e.rng1 ?? e.rng0);
                        break;
                    case "unlockChests":
                    case "indicator":
                        j.Prop("flag", e.flag);
                        Rng.WriteState(j.Key("rng0"), e.rng0);
                        Rng.WriteState(j.Key("rng1"), e.rng1 ?? e.rng0);
                        break;
                    case "window":
                    case "rnd":
                    case "br":
                        j.Prop("other", e.other).Prop("start", e.flag);
                        if (e.sound != null) j.Prop("callers", e.sound);
                        Rng.WriteState(j.Key("rng0"), e.rng0);
                        Rng.WriteState(j.Key("rng1"), e.rng1 ?? e.rng0);
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

        private static Ev AddPlain(string k)
        {
            var e = new Ev { k = k, s = -1, step = Step, time = Time.time, fixedTime = Time.fixedTime, rng0 = Rng.Get() };
            events.Add(e);
            return e;
        }

        // Whose Random draws: MasterAudio's plays, the room's OnAllEnemiesDefeated chest unlock,
        // the floating damage number.
        [HarmonyPatch(typeof(DarkTonic.MasterAudio.MasterAudio), "PlaySoundAtVolume")]
        private static class SoundHook
        {
            private static void Prefix(string sType, bool triggeredAsChildGroup, out Ev __state)
            {
                __state = null;
                if (!Recording) return;
                __state = AddPlain("snd");
                __state.sound = sType;
                __state.flag = triggeredAsChildGroup;
                __state.other = Callers();
            }

            private static void Postfix(Ev __state, DarkTonic.MasterAudio.PlaySoundResult __result)
            {
                if (__state == null) return;
                __state.rng1 = Rng.Get();
                __state.played = __result != null && __result.SoundPlayed;
            }
        }

        [HarmonyPatch(typeof(DungeonRoom), "UnLockRoomChests")]
        private static class UnlockChestsHook
        {
            private static void Prefix(out Ev __state) => __state = Recording ? AddPlain("unlockChests") : null;
            private static void Postfix(Ev __state) { if (__state != null) __state.rng1 = Rng.Get(); }
        }

        [HarmonyPatch(typeof(QuantitytIndicatorManager), "InstantiateFloatingDamageIndicator")]
        private static class IndicatorHook
        {
            private static void Prefix(bool isInvencible, out Ev __state)
            {
                __state = Recording ? AddPlain("indicator") : null;
                if (__state != null) __state.flag = isInvencible;
            }

            private static void Postfix(Ev __state) { if (__state != null) __state.rng1 = Rng.Get(); }
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
