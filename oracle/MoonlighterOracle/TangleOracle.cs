using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using HarmonyLib;
using InControl;
using UnityEngine;

namespace MoonlighterOracle
{
    /// `-oracle-tangle[=<room name>]`: the Tangles against the hero, frame by frame, into
    /// tangle.jsonl. SlimeOracle's recording (its helpers copied here, to be unified with the
    /// generic enemy oracle later) for TangleBehaviour, plus what its LateUpdate does.
    ///
    /// Loads the first save slot, takes the Golem gate on seed 7919 and goes through a door into
    /// the room with the most Tangles (ties: the fewest other enemies; or the room named). Its
    /// other enemies leave roomEnemies and are switched off before EnableEnemies, the familiars
    /// are switched off.
    ///
    /// The lines, as slime.jsonl's (slime_test reads those; tangle_test reads these):
    ///   - "tangleStart": the room, the stats' context, the hero's body and collider (and
    ///     GetColliderOffsset()), PolyNavAgent.allAgents (tangle ids), EnableEnemies' time, the
    ///     targeting delay, Time.fixedDeltaTime, the weave's raycast layer mask and tileSize,
    ///     "enableClock" (EnableClockProbe), "attackEnters" (EnemyOracle's);
    ///   - "collider": every Collider2D in the scene (PolygonProbe on the polygons); "navMap": the
    ///     PolyNav2D map; "rngProbe";
    ///   - "tangle": each Tangle as it is (slime.jsonl's "slime" fields, plus "weave": the
    ///     amplitudes as Init's draws left them, omegas, index).
    /// Each frame ("frame"), as slime.jsonl's (clocks, Random state, input, hero, fixed steps
    /// with their "fx", events with "attackEnter", "updates"), with "tangles" (each at the end, "weave" included) and "late": every
    /// LateUpdate of a Tangle or of its PolyNavAgent, in the order the game ran them (the
    /// script order between the two), each:
    ///   {"k": "a", "s", "v0", "v1"}: the agent's LateUpdate, its velocity before and after;
    ///   {"k": "t", "s", "target", "taken", "hero", "pos", "i0", "i1", "ax", "ay", "v0", "v1"
    ///    [, "d", "hit", "point", "normal", "dist", "frac"]}: TangleBehaviour.LateUpdate: whether
    ///    it targets the hero and whether the weave's branch ran (the game's own condition,
    ///    computed before it), where the hero and the Tangle were, index before and after, the
    ///    amplitudes after, the agent's velocity before and after; if the branch ran, the
    ///    raycast's direction new Vector2(x, y) and result (the collider's path or null, the
    ///    point, normal, distance, fraction), cast again just after with the same arguments
    ///    (queries don't change the world).
    ///
    /// The hero's script is SlimeOracle's (standing, walking at the nearest Tangle, attacks).
    internal static class TangleOracle
    {
        public static bool Active;
        private static bool recording;
        private static int startFrame = -1;
        private static StreamWriter writer;
        private static readonly List<TangleBehaviour> tangles = new List<TangleBehaviour>();
        private static DungeonRoom targetRoom;
        private static float enableTime = -1f;
        private static int enableFrame = -1;
        private static bool mapRegenerated;

        private class Ev
        {
            public string k;
            public int s = -1, step = -1, type;
            public float a, hp0, hp1;
            public bool player;
            public string other, caller;
            public Vector2 hero, pos;
        }

        private class Upd
        {
            public int s;
            public uint[] rng0, rng1;
            public Vector2 hero;
        }

        private class Late
        {
            public string k;
            public int s;
            public Vector2 v0, v1;
            // The Tangle's.
            public bool target, taken;
            public Vector2 hero, pos;
            public float i0, i1, ax, ay;
            public Vector2 d, point, normal;
            public string hit;
            public float dist, frac;
        }

        private static readonly List<Ev> events = new List<Ev>();
        private static readonly List<Upd> updates = new List<Upd>();
        private static readonly List<Late> late = new List<Late>();
        private static readonly List<float[]> stepHero = new List<float[]>();
        private static readonly List<List<float[]>> stepTangles = new List<List<float[]>>();
        private static readonly List<float> stepTime = new List<float>();
        private static uint[] frameStartRng;

        private static float dotweenUdt = -1f;
        private static System.Reflection.FieldInfo dotweenUdtField;

        private static void DOTweenUpdatePostfix(object __instance)
        {
            if (dotweenUdtField != null) dotweenUdt = (float)dotweenUdtField.GetValue(__instance);
        }

        private static void HookDOTween()
        {
            try
            {
                var type = AccessTools.TypeByName("DG.Tweening.Core.DOTweenComponent");
                var update = type != null ? AccessTools.Method(type, "Update") : null;
                dotweenUdtField = type != null ? AccessTools.Field(type, "_unscaledDeltaTime") : null;
                if (update == null || dotweenUdtField == null)
                {
                    OraclePlugin.Log.LogWarning("tangle: no DOTweenComponent.Update / _unscaledDeltaTime: DOTween's clock not recorded");
                    return;
                }
                new Harmony("moonlighter.oracle.tangle").Patch(update, postfix: new HarmonyMethod(typeof(TangleOracle), nameof(DOTweenUpdatePostfix)));
            }
            catch (Exception e)
            {
                OraclePlugin.Log.LogWarning("tangle: DOTween hook failed: " + e.Message);
            }
        }

        private static Vector2 lastStick;

        // ---- The hero's script (SlimeOracle's) ----------------------------------------------

        private enum Kind { Stand, Toward, Attack }

        private struct Segment
        {
            public int frames;
            public Kind kind;
            public Segment(int f, Kind k) { frames = f; kind = k; }
        }

        private static readonly Segment[] Script = BuildScript();

        private static Segment[] BuildScript()
        {
            var s = new List<Segment> { new Segment(150, Kind.Stand), new Segment(90, Kind.Toward) };
            for (int i = 0; i < 8; i++)
            {
                s.Add(new Segment(15, Kind.Toward));
                s.Add(new Segment(1, Kind.Attack));
                s.Add(new Segment(30, Kind.Stand));
            }
            s.Add(new Segment(120, Kind.Stand));
            return s.ToArray();
        }

        private static int ScriptFrames => Script.Aggregate(0, (n, s) => n + s.frames);

        private static bool Current(out Segment seg, out bool first)
        {
            seg = default;
            first = false;
            if (!Active || startFrame < 0) return false;
            int f = Time.frameCount - startFrame;
            if (f < 0 || f >= ScriptFrames) return false;
            foreach (var s in Script)
            {
                if (f < s.frames)
                {
                    seg = s;
                    first = f == 0;
                    return true;
                }
                f -= s.frames;
            }
            return false;
        }

        /// Toward the nearest live Tangle from his collider's centre; let go within 12.
        private static Vector2 StickFor(Segment s)
        {
            if (s.kind == Kind.Stand) return Vector2.zero;
            var ctrl = HeroMerchant.Instance.heroMerchantController;
            Vector2 h = (Vector2)ctrl.transform.position + ctrl.characterCollider.offset;
            float best = float.MaxValue;
            Vector2 to = Vector2.zero;
            foreach (var b in tangles)
            {
                if (b == null || b.enemy == null || !b.enemy.enemyStats.isAlive) continue;
                Vector2 d = (Vector2)b.transform.position - h;
                if (d.magnitude < best)
                {
                    best = d.magnitude;
                    to = d;
                }
            }
            if (best == float.MaxValue || (s.kind == Kind.Toward && best < 12f)) return Vector2.zero;
            return to.normalized;
        }

        [HarmonyPatch(typeof(TwoAxisInputControl), "UpdateWithAxes")]
        private static class StickHook
        {
            private static void Prefix(TwoAxisInputControl __instance, ref float x, ref float y)
            {
                if (!Active || startFrame < 0) return;
                var actions = InputActionsManager.Instance != null ? InputActionsManager.Instance.actions : null;
                if (actions == null || __instance != actions.LeftStick) return;
                lastStick = Current(out Segment s, out _) ? StickFor(s) : Vector2.zero;
                x = lastStick.x;
                y = lastStick.y;
            }
        }

        [HarmonyPatch(typeof(PlayerAction), "Update")]
        private static class ButtonHook
        {
            private static bool Prefix(PlayerAction __instance, ulong updateTick, float deltaTime)
            {
                if (!Active || startFrame < 0) return true;
                var a = InputActionsManager.Instance != null ? InputActionsManager.Instance.actions : null;
                if (a == null) return true;
                if (__instance != a.Action1 && __instance != a.Action2 && __instance != a.Action3 && __instance != a.Action4 && __instance != a.LeftTrigger)
                    return true;
                bool down = __instance == a.Action1 && Current(out Segment s, out bool first) && first && s.kind == Kind.Attack;
                __instance.CommitWithValue(down ? 1f : 0f, updateTick, deltaTime);
                return false;
            }
        }

        // ---- What happens to the Tangles ----------------------------------------------------

        private static bool IsTangle(EnemyBehaviour b) => b != null && b.GetType() == typeof(TangleBehaviour);

        private static int Id(object o)
        {
            if (o is TangleBehaviour b) return tangles.IndexOf(b);
            if (o is Enemy e) return e.enemyBehaviour is TangleBehaviour eb ? tangles.IndexOf(eb) : -1;
            if (o is Component c) return c.GetComponent<TangleBehaviour>() is TangleBehaviour cb ? tangles.IndexOf(cb) : -1;
            return -1;
        }

        private static int Step => Time.inFixedTimeStep ? stepTime.Count - 1 : -1;

        private static Vector2 HeroPos => HeroMerchant.Instance != null ? (Vector2)HeroMerchant.Instance.transform.position : Vector2.zero;

        private static void Add(Ev e)
        {
            if (recording) events.Add(e);
        }

        [HarmonyPatch(typeof(EnemyBehaviour), "Update")]
        private static class UpdateHook
        {
            private static void Prefix(EnemyBehaviour __instance, out Upd __state)
            {
                __state = null;
                if (!recording) return;
                int id = Id(__instance);
                if (id < 0) return;
                __state = new Upd { s = id, rng0 = Rng.Get(), hero = HeroPos };
                updates.Add(__state);
            }

            private static void Postfix(Upd __state)
            {
                if (__state != null) __state.rng1 = Rng.Get();
            }
        }

        [HarmonyPatch(typeof(PolyNavAgent), "LateUpdate")]
        private static class AgentLateHook
        {
            private static void Prefix(PolyNavAgent __instance, out Late __state)
            {
                __state = null;
                if (!recording) return;
                int id = Id(__instance);
                if (id < 0) return;
                __state = new Late { k = "a", s = id, v0 = __instance.velocity };
                late.Add(__state);
            }

            private static void Postfix(PolyNavAgent __instance, Late __state)
            {
                if (__state != null) __state.v1 = __instance.velocity;
            }
        }

        [HarmonyPatch(typeof(TangleBehaviour), "LateUpdate")]
        private static class TangleLateHook
        {
            private static void Prefix(TangleBehaviour __instance, out Late __state)
            {
                __state = null;
                if (!recording) return;
                int id = Id(__instance);
                if (id < 0) return;
                var tr = Traverse.Create(__instance);
                bool target = tr.Field("targetPlayer").GetValue<bool>();
                Transform heroT = __instance.heroMerchantTransform;
                // TangleBehaviour.LateUpdate's condition, as it has it.
                bool taken = target && heroT != null &&
                    ((Vector3)((Vector2)heroT.position + HeroMerchant.Instance.heroMerchantController.GetColliderOffsset())).DiamondDistance(__instance.transform.position) > Constants.GetFloat("tileSize") * 1.5f;
                __state = new Late
                {
                    k = "t", s = id, target = target, taken = taken, hero = heroT != null ? (Vector2)heroT.position : Vector2.zero,
                    pos = __instance.transform.position, i0 = tr.Field("index").GetValue<float>(),
                    v0 = __instance.enemy != null && __instance.enemy.navAgent != null ? __instance.enemy.navAgent.velocity : Vector2.zero,
                };
                late.Add(__state);
            }

            private static void Postfix(TangleBehaviour __instance, Late __state)
            {
                if (__state == null) return;
                var tr = Traverse.Create(__instance);
                float index = tr.Field("index").GetValue<float>();
                float amplitudeX = tr.Field("amplitudeX").GetValue<float>(), amplitudeY = tr.Field("amplitudeY").GetValue<float>();
                float omegaX = tr.Field("omegaX").GetValue<float>(), omegaY = tr.Field("omegaY").GetValue<float>();
                __state.i1 = index;
                __state.ax = amplitudeX;
                __state.ay = amplitudeY;
                __state.v1 = __instance.enemy != null && __instance.enemy.navAgent != null ? __instance.enemy.navAgent.velocity : Vector2.zero;
                if (!__state.taken) return;
                // The weave's raycast again, from the same values (the position doesn't change in it).
                float x = amplitudeX * Mathf.Cos(omegaX * index);
                float y = amplitudeY * Mathf.Cos(omegaY * index);
                LayerMask layerMask = (1 << LayerMask.NameToLayer("LevelBoundary")) | (1 << LayerMask.NameToLayer("EnemyBoundary")) | (1 << LayerMask.NameToLayer("RisenObstacle"));
                RaycastHit2D hit = Physics2D.Raycast(__instance.transform.position, new Vector2(x, y), 30f, layerMask);
                __state.d = new Vector2(x, y);
                __state.hit = hit.collider != null ? PathOf(hit.collider.transform) : null;
                __state.point = hit.point;
                __state.normal = hit.normal;
                __state.dist = hit.distance;
                __state.frac = hit.fraction;
            }
        }

        [HarmonyPatch(typeof(EnemyBehaviour), "AttackHit", new[] { typeof(GameObject) })]
        private static class AttackHitHook
        {
            private static void Prefix(EnemyBehaviour __instance, GameObject other)
            {
                int id = Id(__instance);
                if (id >= 0) Add(new Ev { k = "attackHit", s = id, step = Step, player = other != null && other.CompareTag("Player"), other = other != null ? other.name : null });
            }
        }

        // The attack collider's own callbacks (EnemyAttackCollisionDetection: what it tells the
        // behaviour it advises, AttackHit), "attackEnter" ("how": "trigger" or "collision"; in
        // Ev.caller). "attackHit" above misses a behaviour whose AttackHit override doesn't call
        // EnemyBehaviour's (HermitSlimeBehaviourNEW) and has the projectiles' calls too; the start
        // line's "attackEnters" says a recording has these.
        [HarmonyPatch(typeof(EnemyAttackCollisionDetection), "OnTriggerEnter2D")]
        private static class AttackTriggerHook
        {
            private static void Prefix(EnemyAttackCollisionDetection __instance, Collider2D other) =>
                AttackEnter(__instance, other != null ? other.gameObject : null, "trigger");
        }

        [HarmonyPatch(typeof(EnemyAttackCollisionDetection), "OnCollisionEnter2D")]
        private static class AttackCollisionHook
        {
            private static void Prefix(EnemyAttackCollisionDetection __instance, Collision2D collision) =>
                AttackEnter(__instance, collision != null ? collision.gameObject : null, "collision");
        }

        private static void AttackEnter(EnemyAttackCollisionDetection d, GameObject o, string how)
        {
            if (d.gameObjectToAdvise == null || d.gameObjectToAdvise.name.Contains("SlimeTrap")) return;
            int id = Id(d.gameObjectToAdvise.GetComponent<EnemyBehaviour>());
            if (id >= 0)
                Add(new Ev { k = "attackEnter", s = id, step = Step, player = o != null && o.CompareTag("Player"), other = o != null ? o.name : null, caller = how });
        }

        [HarmonyPatch(typeof(HeroMerchant), "DealDamageToPlayer")]
        private static class DamageToPlayerHook
        {
            private static void Prefix(float attackStrength, Enemy enemySource)
            {
                Add(new Ev { k = "dmgToPlayer", s = enemySource != null ? Id(enemySource) : -1, step = Step, a = attackStrength });
            }

            private static void Postfix(HeroMerchant __instance)
            {
                if (Active || recording) __instance.heroMerchantStats.RecoverAllHealth();
            }
        }

        [HarmonyPatch(typeof(Enemy), "DealDamageToEnemy")]
        private static class DamageToEnemyHook
        {
            private static void Prefix(Enemy __instance, float attackStrength, Enemy.EnemyDamageReceivedType enemyDamageReceivedType, out Ev __state)
            {
                __state = null;
                int id = Id(__instance);
                if (id < 0 || !recording) return;
                __state = new Ev { k = "dmgToEnemy", s = id, step = Step, a = attackStrength, type = (int)enemyDamageReceivedType, hp0 = __instance.enemyStats.CurrentHealth, hero = HeroPos };
                Add(__state);
            }

            private static void Postfix(Enemy __instance, Ev __state)
            {
                if (__state != null) __state.hp1 = __instance.enemyStats.CurrentHealth;
            }
        }

        [HarmonyPatch(typeof(Weapon), "PushEnemyBackwards")]
        private static class PushHook
        {
            private static void Prefix(GameObject enemy, float attackPushForce)
            {
                int id = enemy != null ? Id(enemy.transform) : -1;
                if (id >= 0) Add(new Ev { k = "push", s = id, step = Step, a = attackPushForce, hero = HeroPos, pos = enemy.transform.position });
            }
        }

        [HarmonyPatch(typeof(EnemyBehaviour), "StartTargetingPlayer")]
        private static class TargetHook
        {
            private static void Prefix(EnemyBehaviour __instance)
            {
                int id = Id(__instance);
                if (id >= 0) Add(new Ev { k = "target", s = id, step = Step });
            }
        }

        [HarmonyPatch(typeof(EnemyBehaviour), "OnDeath")]
        private static class DeathHook
        {
            private static void Prefix(EnemyBehaviour __instance)
            {
                int id = Id(__instance);
                if (id >= 0) Add(new Ev { k = "death", s = id, step = Step, hero = HeroPos });
            }
        }

        [HarmonyPatch(typeof(Enemy), "OnDestroy")]
        private static class DestroyHook
        {
            private static void Prefix(Enemy __instance)
            {
                int id = Id(__instance);
                if (id >= 0) Add(new Ev { k = "destroy", s = id, step = Step });
            }
        }

        [HarmonyPatch(typeof(DungeonRoom), "SpawnEnemyDeadSprite")]
        private static class DeadSpriteHook
        {
            private static void Prefix(DungeonRoom __instance, Sprite s, Vector3 position)
            {
                if (__instance == targetRoom && recording) Add(new Ev { k = "deadSprite", step = Step, other = s != null ? s.name : null, pos = position });
            }
        }

        [HarmonyPatch(typeof(DungeonRoom), "EnableEnemies")]
        private static class EnableHook
        {
            private static void Postfix(DungeonRoom __instance)
            {
                if (__instance != targetRoom || targetRoom == null) return;
                enableTime = Time.time;
                enableFrame = Time.frameCount;
                EnableClockProbe.OnEnable(__instance);
            }
        }

        [HarmonyPatch(typeof(PolyNav2D), "GenerateMap")]
        private static class MapHook
        {
            private static void Postfix(PolyNav2D __instance)
            {
                if (targetRoom == null) return;
                mapRegenerated = true;
                if (recording) WriteMap(__instance, Time.frameCount - startFrame);
            }
        }

        // ---- Recording ----------------------------------------------------------------------

        private static Json V2(Json j, Vector2 v) => j.Arr().Bits(v.x).Bits(v.y).EndArr();

        private static Json Flat(Json j, IEnumerable<Vector2> pts)
        {
            j.Arr();
            if (pts != null)
                foreach (var p in pts) j.Bits(p.x).Bits(p.y);
            return j.EndArr();
        }

        private static string PathOf(Transform t)
        {
            string p = t.name;
            for (t = t.parent; t != null; t = t.parent) p = t.name + "/" + p;
            return p;
        }

        private static void Emit(Json j) => writer.WriteLine(j.EndObj().ToString());

        private static void WriteMap(PolyNav2D nav, int frame)
        {
            var j = new Json().Obj().Prop("ev", "navMap").Prop("f", frame).Key("inflate").Bits(nav.inflateRadius);
            j.Key("master").Arr();
            if (nav.map.masterPolygons != null) foreach (var p in nav.map.masterPolygons) Flat(j, p.points);
            j.EndArr().Key("polys").Arr();
            if (nav.map.obstaclePolygons != null) foreach (var p in nav.map.obstaclePolygons) Flat(j, p.points);
            j.EndArr();
            Emit(j);
        }

        private static void WriteColliders()
        {
            foreach (var c in UnityEngine.Object.FindObjectsOfType<Collider2D>())
            {
                var j = new Json().Obj().Prop("ev", "collider").Prop("type", c.GetType().Name).Prop("path", PathOf(c.transform))
                    .Prop("layer", c.gameObject.layer).Prop("tag", c.tag).Prop("enabled", c.enabled).Prop("trigger", c.isTrigger)
                    .Prop("body", c.attachedRigidbody != null ? c.attachedRigidbody.bodyType.ToString() : "none");
                j.Key("pos"); V2(j, c.transform.position);
                j.Key("scale"); V2(j, c.transform.lossyScale);
                j.Key("offset"); V2(j, c.offset);
                j.Key("min"); V2(j, c.bounds.min);
                j.Key("max"); V2(j, c.bounds.max);
                if (c is BoxCollider2D b) { j.Key("size"); V2(j, b.size); }
                if (c is CircleCollider2D ci) j.Key("radius").Bits(ci.radius);
                if (c is PolygonCollider2D pc)
                {
                    j.Key("paths").Arr();
                    for (int i = 0; i < pc.pathCount; i++) Flat(j, pc.GetPath(i));
                    j.EndArr();
                    PolygonProbe.Write(j, pc);
                }
                Emit(j);
            }
        }

        private static void WriteWeave(Json j, TangleBehaviour b, bool setup)
        {
            var tr = Traverse.Create(b);
            j.Key("weave").Obj().Key("oax").Bits(tr.Field("originalAmplitudeX").GetValue<float>())
                .Key("oay").Bits(tr.Field("originalAmplitudeY").GetValue<float>())
                .Key("ax").Bits(tr.Field("amplitudeX").GetValue<float>()).Key("ay").Bits(tr.Field("amplitudeY").GetValue<float>())
                .Key("idx").Bits(tr.Field("index").GetValue<float>());
            if (setup)
                j.Key("ox").Bits(tr.Field("omegaX").GetValue<float>()).Key("oy").Bits(tr.Field("omegaY").GetValue<float>())
                    .Prop("culture", (int)b.tangleCulture);
            j.EndObj();
        }

        private static void WriteTangle(Json j, int id, TangleBehaviour b, bool setup)
        {
            j.Prop("s", id);
            bool gone = b == null;
            if (!setup) j.Prop("gone", gone);
            if (gone) return;
            var e = b.enemy;
            var t = b.transform;
            var tr = Traverse.Create(b);
            j.Key("pos"); V2(j, t.position);
            j.Key("local"); V2(j, t.localPosition);
            if (setup)
            {
                j.Prop("name", PathOf(t));
                j.Key("chain").Arr();
                for (Transform p = t; p != null; p = p.parent) V2(j, p.localPosition);
                j.EndArr();
                j.Key("stats").Obj().Key("maxHealth").Bits(e.enemyStats.maxHealth).Key("strength").Bits(e.enemyStats.strength)
                    .Key("speed").Bits(e.enemyStats.speed).Key("defense").Bits(e.enemyStats.defense).Key("weight").Bits(e.enemyStats.weight)
                    .Prop("champion", e.enemyStats.isChampion).EndObj();
                j.Prop("deadSprite", e.EnemyDeadSprite != null ? e.EnemyDeadSprite.name : null);
                var box = b.GetComponent<BoxCollider2D>();
                j.Key("box").Arr().Bits(box.offset.x).Bits(box.offset.y).Bits(box.size.x).Bits(box.size.y).EndArr();
                var ac = t.Find("AttackCollider");
                var abox = ac != null ? ac.GetComponent<BoxCollider2D>() : null;
                j.Key("attackBox").Arr();
                if (abox != null) j.Bits(abox.offset.x).Bits(abox.offset.y).Bits(abox.size.x).Bits(abox.size.y);
                j.EndArr().Prop("attackLayer", ac != null ? ac.gameObject.layer : -1);
            }
            j.Key("hp").Bits(e.enemyStats.CurrentHealth).Prop("alive", e.enemyStats.isAlive).Prop("inv", e.enemyStats.Invencible)
                .Prop("layer", b.gameObject.layer);
            var rb = b.GetComponent<Rigidbody2D>();
            if (setup)
            {
                j.Key("rb").Obj().Key("mass").Bits(rb.mass).Key("drag").Bits(rb.drag).Key("angularDrag").Bits(rb.angularDrag)
                    .Prop("constraints", (int)rb.constraints).Prop("sleep", rb.sleepMode.ToString()).Prop("detection", rb.collisionDetectionMode.ToString())
                    .Key("pos"); V2(j, rb.position);
                j.Key("vel"); V2(j, rb.velocity);
                j.Prop("awake", rb.IsAwake()).EndObj();
            }
            else
            {
                j.Key("rb").Arr().Bits(rb.position.x).Bits(rb.position.y).Bits(rb.velocity.x).Bits(rb.velocity.y).EndArr().Prop("awake", rb.IsAwake());
            }
            var a = e.navAgent;
            var at = Traverse.Create(a);
            j.Key("agent").Obj();
            if (setup)
            {
                j.Key("maxSpeed").Bits(a.maxSpeed).Key("mass").Bits(a.mass).Key("stoppingDistance").Bits(a.stoppingDistance)
                    .Key("slowingDistance").Bits(a.slowingDistance).Key("accelerationRate").Bits(a.accelerationRate)
                    .Key("decelerationRate").Bits(a.decelerationRate).Key("avoidRadius").Bits(a.avoidRadius)
                    .Key("lookAheadDistance").Bits(a.lookAheadDistance).Prop("repath", a.repath).Prop("restrict", a.restrict)
                    .Prop("closerPointOnInvalid", a.closerPointOnInvalid).Prop("roundCorners", a.roundCorners);
            }
            j.Key("vel"); V2(j, a.velocity);
            j.Key("goal"); V2(j, a.primeGoal);
            j.Key("path"); Flat(j, a.activePath);
            j.Prop("req", at.Field("requests").GetValue<int>()).Key("acc").Bits(at.Field("accelerationValue").GetValue<float>()).EndObj();
            var anim = b.enemyAnimator;
            var info = anim.GetCurrentAnimatorStateInfo(0);
            var sr = e.enemySprite;
            j.Key("anim").Obj().Prop("state", info.shortNameHash).Key("t").Bits(info.normalizedTime).Key("speed").Bits(anim.speed)
                .Prop("on", anim.enabled).Prop("trans", anim.isActiveAndEnabled && anim.IsInTransition(0))
                .Prop("sprite", sr != null && sr.sprite != null ? sr.sprite.name : null);
            if (anim.isActiveAndEnabled) j.Key("dx").Bits(anim.GetFloat("DistanceX")).Key("dy").Bits(anim.GetFloat("DistanceY"));
            j.EndObj();
            j.Key("behaviour").Obj().Prop("es", (int)b.enemyState).Prop("target", tr.Field("targetPlayer").GetValue<bool>())
                .Prop("canMove", b.canMove).Key("lapse").Bits(tr.Field("_lapse").GetValue<float>())
                .Prop("dmgFx", b.IsEnemyPlayingDamagedEffect()).Prop("fallInHoles", b.fallInHoles)
                .Prop("deadForce", tr.Field("deadForce").GetValue<int>()).Prop("enabled", b.enabled);
            j.Key("playerOffset"); V2(j, tr.Field("playerOffset").GetValue<Vector3>());
            j.EndObj();
            WriteWeave(j, b, setup);
            var mat = sr != null ? sr.material : null;
            j.Key("flash").Bits(mat != null && mat.HasProperty("_FlashAmount") ? mat.GetFloat("_FlashAmount") : 0f);
            Color col = mat != null && mat.HasProperty("_Color") ? mat.GetColor("_Color") : Color.white;
            j.Key("color").Arr().Bits(col.r).Bits(col.g).Bits(col.b).Bits(col.a).EndArr();
        }

        private static void WriteStart(DungeonRoom room)
        {
            var dm = DungeonManager.Instance;
            var hero = HeroMerchant.Instance;
            var ctrl = hero.heroMerchantController;
            var rb = hero.GetComponent<Rigidbody2D>();
            var j = new Json().Obj().Prop("ev", "tangleStart").Prop("session", Trace.Session).Prop("floor", Trace.Floor)
                .Prop("room", room.name).Prop("roomInfo", room.roomInfo != null ? room.roomInfo.name : "").Prop("enemy", "Tangle");
            j.Key("roomPos"); V2(j, room.transform.position);
            var info = dm.intermediateInfo;
            var culture = info.dungeonType == DungeonManager.DungeonType.Intermediate ? info.intermediateCulture : dm.culture;
            var slot = GameManager.Instance != null ? GameManager.Instance.currentGameSlot : null;
            j.Key("stats").Obj().Prop("level", dm.level)
                .Prop("afternoon", TimeManagerV2.Instance != null && TimeManagerV2.Instance.CurrrentDayTime == DayTime.Afternoon)
                .Prop("difficulty", OptionsManager.Get("GAME", "Difficulty", -1))
                .Prop("ngp", slot != null && slot.gameType == GameSlot.GameType.NewGamePlus).Prop("dlc", slot != null && slot.wasDlcCompleted)
                .Prop("dungeonCulture", (int)culture).EndObj();
            j.Key("hero").Obj().Key("mass").Bits(rb.mass).Key("drag").Bits(rb.drag).Key("angularDrag").Bits(rb.angularDrag)
                .Key("gravityScale").Bits(rb.gravityScale).Prop("constraints", (int)rb.constraints)
                .Prop("continuous", rb.collisionDetectionMode == CollisionDetectionMode2D.Continuous);
            j.Key("centerOfMass"); V2(j, rb.centerOfMass);
            j.Key("pos"); V2(j, hero.transform.position);
            j.Prop("layer", hero.gameObject.layer).Prop("tag", hero.tag);
            j.Key("colliderOffset"); V2(j, ctrl.characterCollider.offset);
            j.Key("colliderSize"); V2(j, ctrl.characterCollider.size);
            j.Key("getColliderOffset"); V2(j, ctrl.GetColliderOffsset());
            j.EndObj();
            var all = Traverse.Create(typeof(PolyNavAgent)).Field("allAgents").GetValue<List<PolyNavAgent>>();
            j.Key("agents").Arr();
            if (all != null)
                foreach (var a in all)
                {
                    int id = a != null ? Id(a) : -1;
                    if (id >= 0) j.Val(id); else j.Val(a != null ? PathOf(a.transform) : null);
                }
            j.EndArr();
            j.Key("enableTime").Bits(enableTime).Prop("enableFrame", enableFrame - Time.frameCount);
            EnableClockProbe.Write(j);
            j.Prop("attackEnters", true);
            j.Key("targetDelay").Bits(Traverse.Create(room).Field("enemyTimeToStartTargetingPlayer").GetValue<float>());
            j.Key("fixedDeltaTime").Bits(Time.fixedDeltaTime).Key("time").Bits(Time.time);
            j.Prop("dotweenEase", DG.Tweening.DOTween.defaultEaseType.ToString());
            var independent = AccessTools.Field(typeof(DG.Tweening.DOTween), "defaultTimeScaleIndependent");
            if (independent != null) j.Prop("dotweenIndependent", (bool)independent.GetValue(null));
            j.Key("dotweenTimeScale").Bits(DG.Tweening.DOTween.timeScale);
            // The weave's raycast: its layer mask, the settings it runs under, tileSize.
            j.Prop("weaveMask", (1 << LayerMask.NameToLayer("LevelBoundary")) | (1 << LayerMask.NameToLayer("EnemyBoundary")) | (1 << LayerMask.NameToLayer("RisenObstacle")));
            j.Prop("queriesHitTriggers", Physics2D.queriesHitTriggers).Prop("queriesStartInColliders", Physics2D.queriesStartInColliders)
                .Prop("autoSyncTransforms", Physics2D.autoSyncTransforms);
            j.Key("tileSize").Bits(Constants.GetFloat("tileSize"));
            Emit(j);
        }

        private static void WriteProbes()
        {
            uint[] saved = Rng.Get();
            for (int i = 0; i < 16; i++)
            {
                uint[] before = Rng.Get();
                Vector2 v = UnityEngine.Random.insideUnitCircle;
                var j = new Json().Obj().Prop("ev", "rngProbe");
                Rng.WriteState(j.Key("state"), before);
                j.Key("out"); V2(j, v);
                Rng.WriteState(j.Key("after"), Rng.Get());
                Emit(j);
            }
            Rng.Set(saved);
        }

        private static void ClearFrame()
        {
            events.Clear();
            updates.Clear();
            late.Clear();
            stepHero.Clear();
            stepTangles.Clear();
            stepTime.Clear();
            FixtureProbe.Clear();
        }

        private static void WriteLate(Json j, Late l)
        {
            j.Obj().Prop("k", l.k).Prop("s", l.s);
            j.Key("v0"); V2(j, l.v0);
            j.Key("v1"); V2(j, l.v1);
            if (l.k == "t")
            {
                j.Prop("target", l.target).Prop("taken", l.taken);
                j.Key("hero"); V2(j, l.hero);
                j.Key("pos"); V2(j, l.pos);
                j.Key("i0").Bits(l.i0).Key("i1").Bits(l.i1).Key("ax").Bits(l.ax).Key("ay").Bits(l.ay);
                if (l.taken)
                {
                    j.Key("d"); V2(j, l.d);
                    j.Prop("hit", l.hit);
                    j.Key("point"); V2(j, l.point);
                    j.Key("normal"); V2(j, l.normal);
                    j.Key("dist").Bits(l.dist).Key("frac").Bits(l.frac);
                }
            }
            j.EndObj();
        }

        private static void WriteFrame(int f)
        {
            var hero = HeroMerchant.Instance;
            var rb = hero.GetComponent<Rigidbody2D>();
            var j = new Json().Obj().Prop("ev", "frame").Prop("f", f).Prop("fc", Time.frameCount);
            j.Key("dt").Bits(Time.deltaTime).Key("time").Bits(Time.time).Key("ts").Bits(Time.timeScale).Key("udt").Bits(Time.unscaledDeltaTime);
            if (dotweenUdt >= 0f) j.Key("dotweenUdt").Bits(dotweenUdt);
            dotweenUdt = -1f;
            Rng.WriteState(j.Key("rng"), frameStartRng);
            Current(out Segment seg, out bool first);
            j.Key("in").Arr().Bits(lastStick.x).Bits(lastStick.y).Val(first && seg.kind == Kind.Attack ? 1 : 0).EndArr();
            j.Key("hero").Obj().Key("pos"); V2(j, hero.transform.position);
            j.Prop("layer", hero.gameObject.layer).Key("rb").Arr().Bits(rb.position.x).Bits(rb.position.y).Bits(rb.velocity.x).Bits(rb.velocity.y).EndArr().EndObj();
            j.Key("steps").Arr();
            for (int k = 0; k < stepTime.Count; k++)
            {
                j.Obj().Key("ft").Bits(stepTime[k]).Key("hero").Arr();
                float[] h = stepHero[k];
                for (int i = 0; i < 6; i++) j.Bits(h[i]);
                j.Val((int)h[6]).Val(h[7] != 0f).EndArr();
                j.Key("s").Arr();
                foreach (var s in stepTangles[k])
                {
                    j.Arr().Val((int)s[0]);
                    for (int i = 1; i < 7; i++) j.Bits(s[i]);
                    j.Val(s[7] != 0f).EndArr();
                }
                j.EndArr();
                FixtureProbe.WriteStep(j, k);
                j.EndObj();
            }
            j.EndArr();
            j.Key("events").Arr();
            foreach (var e in events)
            {
                j.Obj().Prop("k", e.k).Prop("s", e.s).Prop("step", e.step);
                switch (e.k)
                {
                    case "attackHit": j.Prop("player", e.player).Prop("other", e.other); break;
                    case "attackEnter": j.Prop("player", e.player).Prop("other", e.other).Prop("how", e.caller); break;
                    case "dmgToPlayer": j.Key("str").Bits(e.a); break;
                    case "dmgToEnemy":
                        j.Key("str").Bits(e.a).Prop("type", e.type).Key("hp0").Bits(e.hp0).Key("hp1").Bits(e.hp1);
                        j.Key("hero"); V2(j, e.hero);
                        break;
                    case "push":
                        j.Key("force").Bits(e.a);
                        j.Key("hero"); V2(j, e.hero);
                        j.Key("pos"); V2(j, e.pos);
                        break;
                    case "death": j.Key("hero"); V2(j, e.hero); break;
                    case "deadSprite": j.Prop("sprite", e.other).Key("pos"); V2(j, e.pos); break;
                }
                j.EndObj();
            }
            j.EndArr();
            j.Key("updates").Arr();
            foreach (var u in updates)
            {
                j.Obj().Prop("s", u.s);
                Rng.WriteState(j.Key("rng0"), u.rng0);
                Rng.WriteState(j.Key("rng1"), u.rng1 ?? u.rng0);
                j.Key("hero"); V2(j, u.hero);
                j.EndObj();
            }
            j.EndArr();
            j.Key("late").Arr();
            foreach (var l in late) WriteLate(j, l);
            j.EndArr();
            j.Key("tangles").Arr();
            for (int i = 0; i < tangles.Count; i++)
            {
                j.Obj();
                WriteTangle(j, i, tangles[i], false);
                j.EndObj();
            }
            j.EndArr();
            Emit(j);
        }

        private class Recorder : MonoBehaviour
        {
            private void FixedUpdate()
            {
                if (!recording) return;
                var hero = HeroMerchant.Instance;
                var rb = hero.GetComponent<Rigidbody2D>();
                Vector2 t = hero.transform.position;
                stepTime.Add(Time.fixedTime);
                stepHero.Add(new[] { t.x, t.y, rb.position.x, rb.position.y, rb.velocity.x, rb.velocity.y, hero.gameObject.layer, rb.IsAwake() ? 1f : 0f });
                var list = new List<float[]>();
                for (int i = 0; i < tangles.Count; i++)
                {
                    var b = tangles[i];
                    if (b == null) continue;
                    var srb = b.GetComponent<Rigidbody2D>();
                    Vector2 st = b.transform.position;
                    list.Add(new[] { i, st.x, st.y, srb.position.x, srb.position.y, srb.velocity.x, srb.velocity.y, srb.IsAwake() ? 1f : 0f });
                }
                stepTangles.Add(list);
                FixtureProbe.OnStep();
            }
        }

        /// The room with the most Tangles (ties: fewest other enemies), not the current one,
        /// with a door into it; or the one named.
        private static bool PickRoom(string name, out DungeonRoom room, out DungeonDoor door)
        {
            room = null;
            door = null;
            var dm = DungeonManager.Instance;
            int best = 0, bestOthers = int.MaxValue;
            foreach (var r in dm.dungeonRooms)
            {
                if (r == null || r == dm.currentDungeonRoom || r.roomEnemies == null) continue;
                if (name != "" && r.name != name && (r.roomInfo == null || r.roomInfo.name != name)) continue;
                int n = r.roomEnemies.Count(e => e != null && IsTangle(e.enemyBehaviour));
                int others = r.roomEnemies.Count(e => e != null) - n;
                if (n == 0) continue;
                DungeonDoor into = null;
                foreach (var other in dm.dungeonRooms)
                    if (other != null && other != r)
                        foreach (var d in other.roomDoors)
                            if (d != null && d.destinationRoom == r && into == null) into = d;
                if (into == null) continue;
                if (n > best || (n == best && others < bestOthers))
                {
                    best = n;
                    bestOthers = others;
                    room = r;
                    door = into;
                }
            }
            return room != null;
        }

        public static IEnumerator Run(MonoBehaviour host, bool quit, string roomArg)
        {
            var log = OraclePlugin.Log;
            yield return Harvest.LoadSlot();
            bool generated = false;
            Action done = () => generated = true;
            DungeonGenerator.OnGeneratedDungeon += done;
            UnityEngine.Random.InitState(7919);
            HeroMerchant.Instance.LoadDungeonScene(ItemMaster.Culture.Golem);
            float start = Time.realtimeSinceStartup;
            while (!generated && Time.realtimeSinceStartup - start < 120f) yield return null;
            DungeonGenerator.OnGeneratedDungeon -= done;
            if (!generated) { log.LogError("tangle: floor timed out"); yield break; }
            yield return new WaitForSecondsRealtime(3f);
            for (int tries = 0; tries < 5 && GUIManager.Instance.currentInventory != null; tries++)
            {
                GUIManager.Instance.DisableCurrentPanel();
                yield return new WaitForSecondsRealtime(0.5f);
            }
            if (GameManager.Instance.isGamePaused) GameManager.Instance.UnpauseGame();
            var stale = GUIManager.Instance.currentInventory;
            if (stale != null && !stale.IsActive()) GUIManager.Instance.currentInventory = null;
            yield return new WaitForSecondsRealtime(2f);

            var dm = DungeonManager.Instance;
            var ctrl = HeroMerchant.Instance.heroMerchantController;
            if (!PickRoom(roomArg, out DungeonRoom room, out DungeonDoor door))
            {
                log.LogError("tangle: no room with Tangles and a door into it on this floor" + (roomArg != "" ? " named " + roomArg : ""));
                yield break;
            }
            foreach (var fam in UnityEngine.Object.FindObjectsOfType<FamiliarBase>())
                fam.gameObject.SetActive(false);
            QualitySettings.vSyncCount = 0;
            Application.targetFrameRate = 60;

            targetRoom = room;
            EnableClockProbe.Begin(room);
            enableTime = -1f;
            mapRegenerated = false;
            ctrl.StopMovement();
            ctrl.DisableMoveInput();
            dm.ChangeRoomTo(room, door);
            ctrl.TeleportTo(door.playerEnterDestinyRoomPoint.position);
            foreach (var e in room.roomEnemies.ToList())
                if (e != null && !IsTangle(e.enemyBehaviour))
                {
                    room.roomEnemies.Remove(e);
                    e.gameObject.SetActive(false);
                }
            tangles.Clear();
            foreach (var e in room.roomEnemies)
                if (e != null && IsTangle(e.enemyBehaviour)) tangles.Add((TangleBehaviour)e.enemyBehaviour);
            log.LogInfo($"tangle: {room.name} ({(room.roomInfo != null ? room.roomInfo.name : "")}), {tangles.Count} Tangles");
            float wait = Time.realtimeSinceStartup;
            while ((enableTime < 0f || !mapRegenerated) && Time.realtimeSinceStartup - wait < 10f) yield return null;
            if (enableTime < 0f) { log.LogError("tangle: EnableEnemies never came"); yield break; }
            yield return null;
            yield return null;

            HookDOTween();
            var rec = new GameObject("OracleTangleRecorder").AddComponent<Recorder>();
            writer = new StreamWriter(Path.Combine(OraclePlugin.OutDir, "tangle.jsonl"), append: false) { AutoFlush = false };
            yield return new WaitForEndOfFrame();
            WriteStart(room);
            WriteColliders();
            WriteMap(PolyNav2D.current, -1);
            WriteProbes();
            for (int i = 0; i < tangles.Count; i++)
            {
                var j = new Json().Obj().Prop("ev", "tangle");
                WriteTangle(j, i, tangles[i], true);
                Emit(j);
            }
            writer.Flush();
            ClearFrame();
            FixtureProbe.Reset(() => tangles.Where(b => b != null).Select(b => b.transform));
            frameStartRng = Rng.Get();
            startFrame = Time.frameCount + 1;
            recording = true;
            Active = true;
            int frames = ScriptFrames;
            for (int f = 0; f < frames; f++)
            {
                yield return new WaitForEndOfFrame();
                try { WriteFrame(f); }
                catch (Exception e) { log.LogError("tangle: frame " + f + ": " + e); }
                ClearFrame();
                frameStartRng = Rng.Get();
                if (f % 60 == 0) writer.Flush();
            }
            recording = false;
            Active = false;
            UnityEngine.Object.Destroy(rec.gameObject);
            writer.Flush();
            writer.Close();
            File.WriteAllText(Path.Combine(OraclePlugin.OutDir, "TANGLE_DONE"), DateTime.Now.ToString("o") + " " + room.name + " " + frames + " frames");
            log.LogInfo($"tangle: {frames} frames recorded in {room.name}");
            if (quit) Application.Quit();
        }
    }
}
