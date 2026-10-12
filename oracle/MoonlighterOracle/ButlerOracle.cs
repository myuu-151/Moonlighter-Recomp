using System;
using System.Collections.Generic;
using System.Linq;
using DG.Tweening;
using HarmonyLib;
using UnityEngine;

namespace MoonlighterOracle
{
    /// The GolemButler's part of `-oracle-enemy=GolemButler` (EnemyOracle records the enemies;
    /// this adds the butlers' looks, checks and shots and the projectiles), written into
    /// EnemyOracle's lines with the keys TurretOracle uses for the projectiles (one of the two is
    /// active in a run):
    ///   - "enemyStart": "roomChain" (the room's transform, then its parents': local positions,
    ///     where the shots are parented), "pools" (each projectile pool of the prefab, name and
    ///     its objects' ids in the pool's order: ids are given here, pool order first, then as
    ///     new ones show up);
    ///   - each enemy ("enemy", a frame's "enemies"): "butler": previousEnemyHealth, isWaiting,
    ///     shotEnabled, the Animator's "ShootAttack" and "Attacking" triggers (GetBool), whether
    ///     LookToPosition and RecoverShot are Invoked, the transform's z and the hero's;
    ///   - a frame's "psteps": per fixed step (as "steps"), each active projectile at the step's
    ///     start: [id, transform x, y, body x, y, velocity x, y, awake, isTrigger, [names of what
    ///     its body touches (Rigidbody2D.GetContacts)]];
    ///   - a frame's "tev": what happened in order, with the fixed step (-1: none) and Time.time:
    ///     "look" (EnemyBehaviour.LookToPosition on a butler: the hero's and its positions, its
    ///     caller (TryShootAttack, or none: the Invoke), then distanceX / distanceY), "vision" and
    ///     "back" (IsPlayerInFieldOfVision, IsPlayerInBack: the result, the state's shortNameHash,
    ///     the hero's position with z and its), "tryShoot" (TryShootAttack: shotEnabled before),
    ///     "recover" (RecoverShot runs), "wait" (the Invoke("LookToPosition", 2.5) made), "shot"
    ///     (ShootAttack: the projectile taken, where, the direction it was given (x, y, z), the
    ///     hero's position (x, y, z), the PixelRotation's angle), "hit" (OnCollisionEnter2D on a
    ///     shot: the other's name and tag, the hero's transform and body, the shot's transform, the
    ///     Random state before and after; a crash Invokes DestroyNow), "destroyNow";
    ///   - a frame's "proj": each active projectile at the frame's end: id, transform (world and
    ///     local), body (position, velocity, awake), isTrigger, its Animator (state, normalized
    ///     time), sprite, the PixelRotation's angle ("rot"), its tween (elapsed, playing);
    ///     "pooled": each pool's ids in order.
    internal static class ButlerOracle
    {
        private static bool active;
        private static string poolPrefix;
        private static readonly Dictionary<GameObject, int> ids = new Dictionary<GameObject, int>();
        private static readonly List<GameObject> objects = new List<GameObject>();
        private static readonly List<List<string>> stepRows = new List<List<string>>();
        private static readonly List<Ev> events = new List<Ev>();

        private class Ev
        {
            public string k;
            public int s = -1, p = -1, step = -1, state, angle;
            public float time, fixedTime, a, b, heroZ, posZ, dirZ;
            public bool flag, crash;
            public string other, otherTag, caller;
            public Vector2 hero, heroBody, heroVel, pos, dir;
            public uint[] rng0, rng1;
        }

        public static bool Wants(string prefab) => prefab == "GolemButler";

        private static int Step => EnemyOracle.StepIndex;

        public static void Begin(string prefab)
        {
            active = Wants(prefab);
            ids.Clear();
            objects.Clear();
            if (!active) return;
            poolPrefix = prefab + "_";
            var pm = DungeonManager.Instance.poolManager;
            foreach (var kv in pm.poolsCollection.OrderBy(kv => kv.Key, StringComparer.Ordinal))
                if (kv.Key.StartsWith(poolPrefix, StringComparison.Ordinal))
                    foreach (var go in kv.Value.pooledObjects) IdOf(go);
            foreach (var shot in UnityEngine.Object.FindObjectsOfType<GolemButlerProjectile>()) IdOf(shot.gameObject);
        }

        public static void End() => active = false;

        private static int IdOf(GameObject go)
        {
            if (go == null) return -1;
            if (ids.TryGetValue(go, out int id)) return id;
            id = objects.Count;
            ids[go] = id;
            objects.Add(go);
            return id;
        }

        private static Json V2(Json j, Vector2 v) => j.Arr().Bits(v.x).Bits(v.y).EndArr();

        // ---- EnemyOracle's lines -----------------------------------------------------------

        public static void WriteStart(Json j, DungeonRoom room)
        {
            if (!active) return;
            j.Key("roomChain").Arr();
            for (Transform p = room.transform; p != null; p = p.parent) V2(j, p.localPosition);
            j.EndArr();
            WritePools(j.Key("pools"));
        }

        private static void WritePools(Json j)
        {
            j.Arr();
            var pm = DungeonManager.Instance.poolManager;
            foreach (var kv in pm.poolsCollection.OrderBy(kv => kv.Key, StringComparer.Ordinal))
            {
                if (!kv.Key.StartsWith(poolPrefix, StringComparison.Ordinal)) continue;
                j.Obj().Prop("name", kv.Key).Key("ids").Arr();
                foreach (var go in kv.Value.pooledObjects) j.Val(IdOf(go));
                j.EndArr().EndObj();
            }
            j.EndArr();
        }

        private static bool Trigger(Animator anim, string name)
        {
            try { return anim.GetBool(name); } catch (Exception) { return false; }
        }

        public static void WriteEnemy(Json j, EnemyBehaviour b)
        {
            if (!active || !(b is GolemButlerBehaviour butler)) return;
            var tr = Traverse.Create(butler);
            j.Key("butler").Obj().Key("prevHp").Bits(tr.Field("previousEnemyHealth").GetValue<float>())
                .Prop("waiting", tr.Field("isWaiting").GetValue<bool>()).Prop("shotEnabled", tr.Field("shotEnabled").GetValue<bool>())
                .Prop("lookInvoked", butler.IsInvoking("LookToPosition")).Prop("recoverInvoked", butler.IsInvoking("RecoverShot"))
                .Key("dx").Bits(tr.Field("distanceX").GetValue<float>()).Key("dy").Bits(tr.Field("distanceY").GetValue<float>())
                .Key("z").Bits(butler.transform.position.z)
                .Key("heroZ").Bits(HeroMerchant.Instance != null ? HeroMerchant.Instance.transform.position.z : 0f);
            var anim = butler.enemyAnimator;
            if (anim != null && anim.isActiveAndEnabled)
                j.Prop("shootAttack", Trigger(anim, "ShootAttack")).Prop("attacking", Trigger(anim, "Attacking"));
            j.EndObj();
        }

        /// A fixed step's start (EnemyOracle's Recorder.FixedUpdate).
        public static void OnStep()
        {
            if (!active) return;
            var rows = new List<string>();
            var contacts = new ContactPoint2D[16];
            for (int id = 0; id < objects.Count; id++)
            {
                var go = objects[id];
                if (go == null || !go.activeInHierarchy) continue;
                var rb = go.GetComponent<Rigidbody2D>();
                var box = go.GetComponent<BoxCollider2D>();
                Vector2 t = go.transform.position;
                var j = new Json().Arr().Val(id).Bits(t.x).Bits(t.y).Bits(rb.position.x).Bits(rb.position.y).Bits(rb.velocity.x).Bits(rb.velocity.y)
                    .Val(rb.IsAwake()).Val(box != null && box.isTrigger);
                j.Arr();
                int n = rb.GetContacts(contacts);
                for (int i = 0; i < n && i < contacts.Length; i++)
                {
                    var other = contacts[i].collider == box ? contacts[i].otherCollider : contacts[i].collider;
                    j.Val(other != null ? other.name : null);
                }
                j.EndArr().EndArr();
                rows.Add(j.ToString());
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
            j.Key("psteps").Arr();
            foreach (var rows in stepRows)
            {
                j.Arr();
                foreach (var r in rows) j.Raw(r);
                j.EndArr();
            }
            j.EndArr();
            j.Key("tev").Arr();
            foreach (var e in events)
            {
                j.Obj().Prop("k", e.k).Prop("s", e.s).Prop("p", e.p).Prop("step", e.step).Key("time").Bits(e.time);
                switch (e.k)
                {
                    case "look":
                        j.Key("hero"); V2(j, e.hero);
                        j.Key("pos"); V2(j, e.pos);
                        j.Prop("caller", e.caller).Key("dx").Bits(e.a).Key("dy").Bits(e.b);
                        break;
                    case "vision":
                    case "back":
                        j.Prop("result", e.flag).Prop("state", e.state);
                        j.Key("hero"); V2(j, e.hero);
                        j.Key("pos"); V2(j, e.pos);
                        j.Key("heroZ").Bits(e.heroZ).Key("posZ").Bits(e.posZ);
                        break;
                    case "tryShoot":
                        j.Prop("shotEnabled", e.flag);
                        break;
                    case "wait":
                        j.Key("fixedTime").Bits(e.fixedTime);
                        break;
                    case "shot":
                        j.Key("pos"); V2(j, e.pos);
                        j.Key("dir"); V2(j, e.dir);
                        j.Key("dirZ").Bits(e.dirZ);
                        j.Key("hero"); V2(j, e.hero);
                        j.Key("heroZ").Bits(e.heroZ).Prop("angle", e.angle);
                        break;
                    case "hit":
                        j.Prop("other", e.other).Prop("tag", e.otherTag).Prop("crash", e.crash).Key("fixedTime").Bits(e.fixedTime).Key("delay").Bits(e.a);
                        j.Key("hero"); V2(j, e.hero);
                        j.Key("heroBody"); V2(j, e.heroBody);
                        j.Key("heroVel"); V2(j, e.heroVel);
                        j.Key("pos"); V2(j, e.pos);
                        Rng.WriteState(j.Key("rng0"), e.rng0);
                        Rng.WriteState(j.Key("rng1"), e.rng1 ?? e.rng0);
                        break;
                }
                j.EndObj();
            }
            j.EndArr();
            j.Key("proj").Arr();
            for (int id = 0; id < objects.Count; id++)
            {
                var go = objects[id];
                if (go == null || !go.activeInHierarchy) continue;
                var rb = go.GetComponent<Rigidbody2D>();
                var box = go.GetComponent<BoxCollider2D>();
                var shot = go.GetComponent<GolemButlerProjectile>();
                j.Obj().Prop("p", id);
                j.Key("pos"); V2(j, go.transform.position);
                j.Key("local"); V2(j, go.transform.localPosition);
                j.Key("rb").Arr().Bits(rb.position.x).Bits(rb.position.y).Bits(rb.velocity.x).Bits(rb.velocity.y).EndArr();
                j.Prop("awake", rb.IsAwake()).Prop("trigger", box != null && box.isTrigger);
                var anim = go.GetComponent<Animator>();
                if (anim != null && anim.isActiveAndEnabled)
                {
                    var info = anim.GetCurrentAnimatorStateInfo(0);
                    j.Key("anim").Obj().Prop("state", info.shortNameHash).Key("t").Bits(info.normalizedTime).EndObj();
                }
                var sr = go.GetComponentInChildren<SpriteRenderer>();
                j.Prop("sprite", sr != null && sr.sprite != null ? sr.sprite.name : null);
                if (shot != null && shot.rotation != null) j.Prop("rot", shot.rotation.Angle);
                var tween = shot != null ? Traverse.Create(shot).Field("flyingMove").GetValue<Tweener>() : null;
                if (tween != null && tween.IsActive()) j.Key("tw").Arr().Bits(tween.Elapsed(false)).Val(tween.IsPlaying()).EndArr();
                j.EndObj();
            }
            j.EndArr();
            WritePools(j.Key("pooled"));
        }

        // ---- Hooks ------------------------------------------------------------------------

        private static bool Recording => active && EnemyOracle.Recording;

        private static void Add(Ev e)
        {
            e.time = Time.time;
            e.fixedTime = Time.fixedTime;
            events.Add(e);
        }

        private static Vector3 HeroPos => HeroMerchant.Instance != null ? HeroMerchant.Instance.transform.position : Vector3.zero;

        /// The script that called (the first Assembly-CSharp frame below the hook that isn't the
        /// hooked method's; none: the engine did: an Invoke, an animation event).
        private static string Caller(string hooked)
        {
            var st = new System.Diagnostics.StackTrace(2, false);
            var game = typeof(EnemyBehaviour).Assembly;
            for (int i = 0; i < st.FrameCount; i++)
            {
                var m = st.GetFrame(i).GetMethod();
                if (m == null || m.DeclaringType == null || m.Name.Contains(hooked)) continue;
                if (m.DeclaringType.Assembly == game) return m.DeclaringType.Name + "." + m.Name;
            }
            return null;
        }

        [HarmonyPatch(typeof(EnemyBehaviour), "LookToPosition")]
        private static class LookHook
        {
            private static void Prefix(EnemyBehaviour __instance, out Ev __state)
            {
                __state = null;
                if (!Recording || !(__instance is GolemButlerBehaviour)) return;
                int s = EnemyOracle.IdOf(__instance);
                if (s < 0) return;
                __state = new Ev { k = "look", s = s, step = Step, hero = HeroPos, pos = __instance.transform.position, caller = Caller("LookToPosition") };
                Add(__state);
            }

            private static void Postfix(EnemyBehaviour __instance, Ev __state)
            {
                if (__state == null) return;
                var tr = Traverse.Create(__instance);
                __state.a = tr.Field("distanceX").GetValue<float>();
                __state.b = tr.Field("distanceY").GetValue<float>();
            }
        }

        private static Ev Check(string k, GolemButlerBehaviour b)
        {
            if (!Recording) return null;
            int s = EnemyOracle.IdOf(b);
            if (s < 0) return null;
            Vector3 h = HeroPos;
            var anim = b.enemyAnimator;
            var e = new Ev
            {
                k = k, s = s, step = Step, hero = h, heroZ = h.z, pos = b.transform.position, posZ = b.transform.position.z,
                state = anim != null ? anim.GetCurrentAnimatorStateInfo(0).shortNameHash : 0
            };
            Add(e);
            return e;
        }

        [HarmonyPatch(typeof(GolemButlerBehaviour), "IsPlayerInFieldOfVision")]
        private static class VisionHook
        {
            private static void Prefix(GolemButlerBehaviour __instance, out Ev __state) => __state = Check("vision", __instance);
            private static void Postfix(bool __result, Ev __state) { if (__state != null) __state.flag = __result; }
        }

        [HarmonyPatch(typeof(GolemButlerBehaviour), "IsPlayerInBack")]
        private static class BackHook
        {
            private static void Prefix(GolemButlerBehaviour __instance, out Ev __state) => __state = Check("back", __instance);
            private static void Postfix(bool __result, Ev __state) { if (__state != null) __state.flag = __result; }
        }

        [HarmonyPatch(typeof(GolemButlerBehaviour), "TryShootAttack")]
        private static class TryShootHook
        {
            private static void Prefix(GolemButlerBehaviour __instance)
            {
                if (!Recording) return;
                int s = EnemyOracle.IdOf(__instance);
                if (s >= 0) Add(new Ev { k = "tryShoot", s = s, step = Step, flag = Traverse.Create(__instance).Field("shotEnabled").GetValue<bool>() });
            }
        }

        [HarmonyPatch(typeof(GolemButlerBehaviour), "RecoverShot")]
        private static class RecoverHook
        {
            private static void Prefix(GolemButlerBehaviour __instance)
            {
                if (!Recording) return;
                int s = EnemyOracle.IdOf(__instance);
                if (s >= 0) Add(new Ev { k = "recover", s = s, step = Step });
            }
        }

        // The wait's Invoke("LookToPosition", waitWhileWillIsNearTime), seen as isWaiting turns on.
        [HarmonyPatch(typeof(GolemButlerBehaviour), "ChooseAndApplyAction")]
        private static class WaitHook
        {
            private static void Prefix(GolemButlerBehaviour __instance, out bool __state)
            {
                __state = Recording && Traverse.Create(__instance).Field("isWaiting").GetValue<bool>();
            }

            private static void Postfix(GolemButlerBehaviour __instance, bool __state)
            {
                if (!Recording || __state || !Traverse.Create(__instance).Field("isWaiting").GetValue<bool>()) return;
                int s = EnemyOracle.IdOf(__instance);
                if (s >= 0) Add(new Ev { k = "wait", s = s, step = Step });
            }
        }

        [HarmonyPatch(typeof(GolemButlerBehaviour), "ShootAttack")]
        private static class ShootHook
        {
            private class Shot
            {
                public Ev e;
                public GameObject first; // the pool's first (GetObject takes it), null if it's empty
            }

            private static void Prefix(GolemButlerBehaviour __instance, out Shot __state)
            {
                __state = null;
                if (!Recording) return;
                int s = EnemyOracle.IdOf(__instance);
                if (s < 0) return;
                Vector3 h = HeroPos;
                __state = new Shot { e = new Ev { k = "shot", s = s, step = Step, hero = h, heroZ = h.z } };
                Add(__state.e);
                string name = Traverse.Create(__instance).Property("projectilePooledName").GetValue<string>();
                var pm = DungeonManager.Instance.poolManager;
                if (name != null && pm.poolsCollection.TryGetValue(name, out var pool) && pool.pooledObjects.Count > 0) __state.first = pool.pooledObjects[0];
            }

            /// The shot it took: the pool's first; else (the pool was empty: a new one) the one of
            /// this butler whose tween is new.
            private static void Postfix(GolemButlerBehaviour __instance, Shot __state)
            {
                if (__state == null) return;
                GolemButlerProjectile taken = __state.first != null ? __state.first.GetComponent<GolemButlerProjectile>() : null;
                if (taken == null)
                    foreach (var shot in UnityEngine.Object.FindObjectsOfType<GolemButlerProjectile>())
                    {
                        if (shot.projectileOwner != __instance.gameObject) continue;
                        var tween = Traverse.Create(shot).Field("flyingMove").GetValue<Tweener>();
                        if (tween != null && tween.IsActive() && tween.Elapsed(false) == 0f && !tween.IsComplete()) taken = shot;
                    }
                if (taken == null) return;
                var e = __state.e;
                e.p = IdOf(taken.gameObject);
                e.pos = taken.transform.position;
                e.dir = taken.projectileDirection;
                e.dirZ = taken.projectileDirection.z;
                e.angle = taken.rotation != null ? taken.rotation.Angle : 0;
            }
        }

        [HarmonyPatch(typeof(GolemButlerProjectile), "OnCollisionEnter2D")]
        private static class HitHook
        {
            private static void Prefix(GolemButlerProjectile __instance, Collision2D other, out Ev __state)
            {
                __state = null;
                if (!Recording || !ids.ContainsKey(__instance.gameObject)) return;
                var hero = HeroMerchant.Instance;
                var hrb = hero.GetComponent<Rigidbody2D>();
                var go = other.gameObject;
                bool crash = go.CompareTag("LevelBoundary") || go.CompareTag("Rock") || go.CompareTag("Player") || go.CompareTag("Shield");
                __state = new Ev
                {
                    k = "hit", p = IdOf(__instance.gameObject), step = Step, other = go.name, otherTag = go.tag, crash = crash,
                    a = __instance.timeToDestroyAfterCrash, hero = hero.transform.position, heroBody = hrb.position, heroVel = hrb.velocity,
                    pos = __instance.transform.position, rng0 = Rng.Get()
                };
                var owner = __instance.projectileOwner;
                if (owner != null) __state.s = EnemyOracle.IdOf(owner.GetComponent<EnemyBehaviour>());
                Add(__state);
            }

            // (Not run when the owner's GetComponent throws, once it's destroyed: rng1 is then rng0.)
            private static void Postfix(Ev __state)
            {
                if (__state != null) __state.rng1 = Rng.Get();
            }
        }

        [HarmonyPatch(typeof(GolemButlerProjectile), "DestroyNow")]
        private static class DestroyHook
        {
            private static void Prefix(GolemButlerProjectile __instance)
            {
                if (!Recording || !ids.ContainsKey(__instance.gameObject)) return;
                Add(new Ev { k = "destroyNow", p = IdOf(__instance.gameObject), step = Step });
            }
        }
    }
}
