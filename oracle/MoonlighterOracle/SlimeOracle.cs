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
    /// `-oracle-slime[=<room name>]`: the BabySlimes against the hero, frame by frame, into
    /// slime.jsonl. Loads the first save slot like the harvest (saving and stats blocked), takes
    /// the Golem gate on a fixed seed (7919, as the walks; generation isn't fully seeded, so the
    /// floor can differ between sessions), and goes through a door (DungeonManager.ChangeRoomTo, as
    /// the PolyNav oracle) into the room with the most BabySlimes (ties: the fewest other enemies;
    /// or the room named). Its other enemies leave roomEnemies and are switched off before
    /// EnableEnemies (half a second later) can wake them, and the hero's familiars are switched
    /// off (their agents and attacks would join in), so the room holds the slimes and him.
    ///
    /// Once the slimes are up (EnableEnemies ran, the room left behind was disabled and the map
    /// regenerated) and before they start targeting him (a second after EnableEnemies), the
    /// recording starts at a frame's end:
    ///   - "slimeStart": the room, the stats' context (level, night, difficulty, NG+, the
    ///     autoscale row), the hero's body and collider, PolyNavAgent.allAgents (slime ids), when
    ///     EnableEnemies ran and the targeting delay, Time.fixedDeltaTime;
    ///   - "collider": every Collider2D in the scene (as the walk records them);
    ///   - "navMap": the PolyNav2D map (master and obstacle polygons, inflateRadius); again with
    ///     the frame if it's regenerated while recording;
    ///   - "rngProbe": Random.insideUnitCircle from the state then (put back after);
    ///   - "slime": each slime as it is (the start of the replay).
    /// Then each frame ("frame", at its end, after rendering): the clocks (deltaTime, Time.time,
    /// timeScale), the Random state at its start (the last frame's end), the input given, the
    /// hero (position, layer, body); each fixed step's start (Time.fixedTime, the hero's
    /// transform, body position and velocity, layer, awake; each slime's transform and body);
    /// what happened in order ("events": the attack trigger's AttackHit, DealDamageToPlayer,
    /// DealDamageToEnemy and Weapon.PushEnemyBackwards with the step they landed in (-1: not in
    /// physics), StartTargetingPlayer, OnDeath, OnDestroy, the dead sprite); the slimes' Updates
    /// in the order the game ran them with the Random state before and after each and where each
    /// saw the hero; the agents' LateUpdate order; and each slime at the end.
    ///
    /// The hero's script: 150 frames standing (the slimes come at him), 90 walking at the
    /// nearest slime, then 8 times: 15 frames toward the nearest slime, an attack (Action1) with
    /// the stick on it, 30 frames standing; 120 frames standing. He doesn't die: his health is
    /// refilled after every hit (RecoverAllHealth, after DealDamageToPlayer has run).
    ///
    /// Not deterministic: the frame times (60 fps asked, real time measured: deltaTime is
    /// recorded), and so how many fixed steps each frame has (recorded too); the hit-stop
    /// GameManager.SetGameSpeedForTime puts on weapon hits (timeScale, recorded through dt).
    internal static class SlimeOracle
    {
        public static bool Active;     // the hero's script is driving the input
        private static bool recording; // between the setup line and the last frame
        private static int startFrame = -1;
        private static StreamWriter writer;
        private static readonly List<BabySlimeBehaviour> slimes = new List<BabySlimeBehaviour>();
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
            public string other;
            public Vector2 hero, pos;
        }

        private class Upd
        {
            public int s;
            public uint[] rng0, rng1;
            public Vector2 hero;
        }

        private static readonly List<Ev> events = new List<Ev>();
        private static readonly List<Upd> updates = new List<Upd>();
        private static readonly List<int> late = new List<int>();
        private static readonly List<float[]> stepHero = new List<float[]>();
        private static readonly List<List<float[]>> stepSlimes = new List<List<float[]>>();
        private static readonly List<float> stepTime = new List<float>();
        private static uint[] frameStartRng;
        private static Vector2 lastStick;

        // ---- The hero's script --------------------------------------------------------------

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

        /// Toward the nearest live slime from his collider's centre; let go within 12.
        private static Vector2 StickFor(Segment s)
        {
            if (s.kind == Kind.Stand) return Vector2.zero;
            var ctrl = HeroMerchant.Instance.heroMerchantController;
            Vector2 h = (Vector2)ctrl.transform.position + ctrl.characterCollider.offset;
            float best = float.MaxValue;
            Vector2 to = Vector2.zero;
            foreach (var b in slimes)
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

        // ---- What happens to the slimes -----------------------------------------------------

        private static int Id(object o)
        {
            if (o is BabySlimeBehaviour b) return slimes.IndexOf(b);
            if (o is Enemy e) return e.enemyBehaviour is BabySlimeBehaviour eb ? slimes.IndexOf(eb) : -1;
            if (o is Component c) return c.GetComponent<BabySlimeBehaviour>() is BabySlimeBehaviour cb ? slimes.IndexOf(cb) : -1;
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
        private static class LateHook
        {
            private static void Prefix(PolyNavAgent __instance)
            {
                if (!recording) return;
                int id = Id(__instance);
                if (id >= 0) late.Add(id);
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

        [HarmonyPatch(typeof(HeroMerchant), "DealDamageToPlayer")]
        private static class DamageToPlayerHook
        {
            private static void Prefix(float attackStrength, Enemy enemySource)
            {
                Add(new Ev { k = "dmgToPlayer", s = enemySource != null ? Id(enemySource) : -1, step = Step, a = attackStrength });
            }

            // God mode the plain way: his health back after the hit (the hit itself as in play).
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
                if (__instance == targetRoom) Add(new Ev { k = "deadSprite", step = Step, other = s != null ? s.name : null, pos = position });
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
                }
                Emit(j);
            }
        }

        private static void WriteSlime(Json j, int id, BabySlimeBehaviour b, bool setup)
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
            var j = new Json().Obj().Prop("ev", "slimeStart").Prop("session", Trace.Session).Prop("floor", Trace.Floor)
                .Prop("room", room.name).Prop("roomInfo", room.roomInfo != null ? room.roomInfo.name : "").Prop("enemy", "BabySlime");
            j.Key("roomPos"); V2(j, room.transform.position);
            // EnemyStats.Init's inputs.
            var info = dm.intermediateInfo;
            var culture = info.dungeonType == DungeonManager.DungeonType.Intermediate ? info.intermediateCulture : dm.culture;
            var slot = GameManager.Instance != null ? GameManager.Instance.currentGameSlot : null;
            j.Key("stats").Obj().Prop("level", dm.level)
                .Prop("afternoon", TimeManagerV2.Instance != null && TimeManagerV2.Instance.CurrrentDayTime == DayTime.Afternoon)
                .Prop("difficulty", OptionsManager.Get("GAME", "Difficulty", -1))
                .Prop("ngp", slot != null && slot.gameType == GameSlot.GameType.NewGamePlus).Prop("dlc", slot != null && slot.wasDlcCompleted)
                .Prop("dungeonCulture", (int)culture).EndObj();
            // The hero's body.
            j.Key("hero").Obj().Key("mass").Bits(rb.mass).Key("drag").Bits(rb.drag).Key("angularDrag").Bits(rb.angularDrag)
                .Key("gravityScale").Bits(rb.gravityScale).Prop("constraints", (int)rb.constraints)
                .Prop("continuous", rb.collisionDetectionMode == CollisionDetectionMode2D.Continuous);
            j.Key("centerOfMass"); V2(j, rb.centerOfMass);
            j.Key("pos"); V2(j, hero.transform.position);
            j.Prop("layer", hero.gameObject.layer).Prop("tag", hero.tag);
            j.Key("colliderOffset"); V2(j, ctrl.characterCollider.offset);
            j.Key("colliderSize"); V2(j, ctrl.characterCollider.size);
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
            j.Key("targetDelay").Bits(Traverse.Create(room).Field("enemyTimeToStartTargetingPlayer").GetValue<float>());
            j.Key("fixedDeltaTime").Bits(Time.fixedDeltaTime).Key("time").Bits(Time.time);
            j.Prop("dotweenEase", DG.Tweening.DOTween.defaultEaseType.ToString());
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
            stepSlimes.Clear();
            stepTime.Clear();
        }

        private static void WriteFrame(int f)
        {
            var hero = HeroMerchant.Instance;
            var rb = hero.GetComponent<Rigidbody2D>();
            var j = new Json().Obj().Prop("ev", "frame").Prop("f", f).Prop("fc", Time.frameCount);
            j.Key("dt").Bits(Time.deltaTime).Key("time").Bits(Time.time).Key("ts").Bits(Time.timeScale).Key("udt").Bits(Time.unscaledDeltaTime);
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
                foreach (var s in stepSlimes[k])
                {
                    j.Arr().Val((int)s[0]);
                    for (int i = 1; i < 7; i++) j.Bits(s[i]);
                    j.Val(s[7] != 0f).EndArr();
                }
                j.EndArr().EndObj();
            }
            j.EndArr();
            j.Key("events").Arr();
            foreach (var e in events)
            {
                j.Obj().Prop("k", e.k).Prop("s", e.s).Prop("step", e.step);
                switch (e.k)
                {
                    case "attackHit": j.Prop("player", e.player).Prop("other", e.other); break;
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
            foreach (int id in late) j.Val(id);
            j.EndArr();
            j.Key("slimes").Arr();
            for (int i = 0; i < slimes.Count; i++)
            {
                j.Obj();
                WriteSlime(j, i, slimes[i], false);
                j.EndObj();
            }
            j.EndArr();
            Emit(j);
        }

        /// Each fixed step's start (all FixedUpdates run before the step's simulation).
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
                for (int i = 0; i < slimes.Count; i++)
                {
                    var b = slimes[i];
                    if (b == null) continue;
                    var srb = b.GetComponent<Rigidbody2D>();
                    Vector2 st = b.transform.position;
                    list.Add(new[] { i, st.x, st.y, srb.position.x, srb.position.y, srb.velocity.x, srb.velocity.y, srb.IsAwake() ? 1f : 0f });
                }
                stepSlimes.Add(list);
            }
        }

        /// The room with the most BabySlimes (ties: fewest other enemies), not the current one,
        /// with a door into it; or the one named.
        private static bool PickRoom(string name, out DungeonRoom room, out DungeonDoor door)
        {
            room = null;
            door = null;
            var dm = DungeonManager.Instance;
            int bestSlimes = 0, bestOthers = int.MaxValue;
            foreach (var r in dm.dungeonRooms)
            {
                if (r == null || r == dm.currentDungeonRoom || r.roomEnemies == null) continue;
                if (name != "" && r.name != name && (r.roomInfo == null || r.roomInfo.name != name)) continue;
                int n = r.roomEnemies.Count(e => e != null && e.enemyBehaviour is BabySlimeBehaviour);
                int others = r.roomEnemies.Count(e => e != null) - n;
                if (n == 0) continue;
                DungeonDoor into = null;
                foreach (var other in dm.dungeonRooms)
                    if (other != null && other != r)
                        foreach (var d in other.roomDoors)
                            if (d != null && d.destinationRoom == r && into == null) into = d;
                if (into == null) continue;
                if (n > bestSlimes || (n == bestSlimes && others < bestOthers))
                {
                    bestSlimes = n;
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
            if (!generated) { log.LogError("slime: floor timed out"); yield break; }
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
                log.LogError("slime: no room with BabySlimes and a door into it on this floor" + (roomArg != "" ? " named " + roomArg : ""));
                yield break;
            }
            // The familiars off (their agents would join allAgents; they attack enemies).
            foreach (var fam in UnityEngine.Object.FindObjectsOfType<FamiliarBase>())
                fam.gameObject.SetActive(false);
            QualitySettings.vSyncCount = 0;
            Application.targetFrameRate = 60;

            // DungeonDoor.OnPlayerEnter: through the door into the room.
            targetRoom = room;
            enableTime = -1f;
            mapRegenerated = false;
            ctrl.StopMovement();
            ctrl.DisableMoveInput();
            dm.ChangeRoomTo(room, door);
            ctrl.TeleportTo(door.playerEnterDestinyRoomPoint.position);
            // The others out before EnableEnemies (an Invoke half a second away) wakes them.
            foreach (var e in room.roomEnemies.ToList())
                if (e != null && !(e.enemyBehaviour is BabySlimeBehaviour))
                {
                    room.roomEnemies.Remove(e);
                    e.gameObject.SetActive(false);
                }
            slimes.Clear();
            foreach (var e in room.roomEnemies)
                if (e != null && e.enemyBehaviour is BabySlimeBehaviour b) slimes.Add(b);
            log.LogInfo($"slime: {room.name} ({(room.roomInfo != null ? room.roomInfo.name : "")}), {slimes.Count} BabySlimes");
            float wait = Time.realtimeSinceStartup;
            while ((enableTime < 0f || !mapRegenerated) && Time.realtimeSinceStartup - wait < 10f) yield return null;
            if (enableTime < 0f) { log.LogError("slime: EnableEnemies never came"); yield break; }
            yield return null;
            yield return null;

            var rec = new GameObject("OracleSlimeRecorder").AddComponent<Recorder>();
            writer = new StreamWriter(Path.Combine(OraclePlugin.OutDir, "slime.jsonl"), append: false) { AutoFlush = false };
            // The start, at a frame's end.
            yield return new WaitForEndOfFrame();
            WriteStart(room);
            WriteColliders();
            WriteMap(PolyNav2D.current, -1);
            WriteProbes();
            for (int i = 0; i < slimes.Count; i++)
            {
                var j = new Json().Obj().Prop("ev", "slime");
                WriteSlime(j, i, slimes[i], true);
                Emit(j);
            }
            writer.Flush();
            ClearFrame();
            frameStartRng = Rng.Get();
            startFrame = Time.frameCount + 1;
            recording = true;
            Active = true;
            int frames = ScriptFrames;
            for (int f = 0; f < frames; f++)
            {
                yield return new WaitForEndOfFrame();
                try { WriteFrame(f); }
                catch (Exception e) { log.LogError("slime: frame " + f + ": " + e); }
                ClearFrame();
                frameStartRng = Rng.Get();
                if (f % 60 == 0) writer.Flush();
            }
            recording = false;
            Active = false;
            UnityEngine.Object.Destroy(rec.gameObject);
            writer.Flush();
            writer.Close();
            File.WriteAllText(Path.Combine(OraclePlugin.OutDir, "SLIME_DONE"), DateTime.Now.ToString("o") + " " + room.name + " " + frames + " frames");
            log.LogInfo($"slime: {frames} frames recorded in {room.name}");
            if (quit) Application.Quit();
        }
    }
}
