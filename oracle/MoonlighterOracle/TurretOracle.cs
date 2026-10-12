using System;
using System.Collections.Generic;
using System.Linq;
using DG.Tweening;
using HarmonyLib;
using UnityEngine;

namespace MoonlighterOracle
{
    /// The Broken turret's part of `-oracle-enemy=BrokenGolemTurret` (EnemyOracle records the
    /// enemies; this adds the turrets' looks and shots and the projectiles), written into
    /// EnemyOracle's lines:
    ///   - "enemyStart": "roomChain" (the room's transform, then its parents': local positions,
    ///     where the shots are parented), "pools" (each projectile pool of the prefab, name and
    ///     its objects' ids in the pool's order: ids are given here, pool order first, then as
    ///     new ones show up);
    ///   - each enemy ("enemy", a frame's "enemies"): "turret": patrolLapse, canShoot,
    ///     enemyDirection, the ids in _projectilesShot, the Animator's Direction* bools
    ///     (up, down, left, right) and "Shoot", and "tc": whether its transform changed between its
    ///     Update and the frame's end (the Animator's root motion: applyRootMotion is on);
    ///   - a frame's "psteps": per fixed step (as "steps"), each active projectile at the step's
    ///     start: [id, transform x, y, body x, y, velocity x, y, awake, isTrigger, [names of what
    ///     its body touches (Rigidbody2D.GetContacts)]];
    ///   - a frame's "tev": what happened in order, with the fixed step (-1: none) and Time.time:
    ///     "look" (LookToPosition: the hero's and the turret's positions, playerOffset, each
    ///     raycast it makes (the same Physics2D.Raycast repeated in the prefix: origin, direction,
    ///     distance, mask, the collider's path or null, point, fraction, distance), then canShoot
    ///     and enemyDirection), "attack" (AttackPlayer: its Invoke("ShootAnimation", shootDelay),
    ///     with Time.time and Time.fixedTime), "shootAnim" (ShootAnimation runs), "shot"
    ///     (ShootProjectile: canShoot, the projectile taken, where, its direction), "hit"
    ///     (OnCollisionEnter2D on a shot: the other's name and tag, the hero's transform and
    ///     body, the shot's transform, the Random state before and after; a crash Invokes
    ///     DestroyNow), "push" (PushPlayerBackwards: the force), "destroyNow";
    ///   - a frame's "proj": each active projectile at the frame's end: id, transform (world and
    ///     local), body (position, velocity, awake), isTrigger, its Animator (state, normalized
    ///     time), sprite, ProjectileArt's rotation z, its tween (elapsed, playing); "pooled":
    ///     each pool's ids in order.
    internal static class TurretOracle
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
            public int s = -1, p = -1, step = -1, dir = -1;
            public float time, fixedTime, a;
            public bool flag, crash;
            public string other, otherTag;
            public Vector2 hero, heroBody, heroVel, pos, offset, force;
            public uint[] rng0, rng1;
            public List<Ray> rays;
        }

        private struct Ray
        {
            public Vector2 origin, direction, point;
            public float distance, fraction, hitDistance;
            public int mask;
            public string hit;
        }

        public static bool Wants(string prefab) => prefab == "BrokenGolemTurret";

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
            foreach (var shot in UnityEngine.Object.FindObjectsOfType<BrokenGolemTurretProjectile>()) IdOf(shot.gameObject);
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

        private static string PathOf(Transform t)
        {
            string p = t.name;
            for (t = t.parent; t != null; t = t.parent) p = t.name + "/" + p;
            return p;
        }

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

        public static void WriteEnemy(Json j, EnemyBehaviour b)
        {
            if (!active || !(b is BrokenGolemTurretBehaviour turret)) return;
            var tr = Traverse.Create(turret);
            j.Key("turret").Obj().Key("lapse").Bits(tr.Field("patrolLapse").GetValue<float>())
                .Prop("canShoot", tr.Field("canShoot").GetValue<bool>()).Prop("dir", (int)turret.enemyDirection);
            j.Key("shot").Arr();
            var shot = tr.Field("_projectilesShot").GetValue<List<BrokenGolemTurretProjectile>>();
            if (shot != null) foreach (var s in shot) j.Val(s != null ? IdOf(s.gameObject) : -1);
            j.EndArr();
            var anim = turret.enemyAnimator;
            if (anim != null && anim.isActiveAndEnabled)
            {
                j.Key("bools").Arr().Val(anim.GetBool("DirectionUp")).Val(anim.GetBool("DirectionDown"))
                    .Val(anim.GetBool("DirectionLeft")).Val(anim.GetBool("DirectionRight")).EndArr();
                bool shoot = false;
                try { shoot = anim.GetBool("Shoot"); } catch (Exception) { }
                j.Prop("shoot", shoot);
            }
            j.Prop("tc", turret.transform.hasChanged);
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
                        j.Key("offset"); V2(j, e.offset);
                        j.Key("rays").Arr();
                        foreach (var r in e.rays)
                        {
                            j.Obj().Key("o"); V2(j, r.origin);
                            j.Key("d"); V2(j, r.direction);
                            j.Key("dist").Bits(r.distance).Prop("mask", r.mask).Prop("hit", r.hit);
                            j.Key("point"); V2(j, r.point);
                            j.Key("fraction").Bits(r.fraction).Key("hd").Bits(r.hitDistance).EndObj();
                        }
                        j.EndArr().Prop("canShoot", e.flag).Prop("dir", e.dir);
                        break;
                    case "attack":
                        j.Key("fixedTime").Bits(e.fixedTime).Key("delay").Bits(e.a);
                        break;
                    case "shot":
                        j.Prop("canShoot", e.flag).Prop("dir", e.dir);
                        j.Key("pos"); V2(j, e.pos);
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
                    case "push":
                        j.Key("force"); V2(j, e.force);
                        j.Key("hero"); V2(j, e.hero);
                        j.Key("pos"); V2(j, e.pos);
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
                var shot = go.GetComponent<BrokenGolemTurretProjectile>();
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
                if (shot != null && shot.projectileArt != null) j.Key("art").Bits(shot.projectileArt.transform.eulerAngles.z);
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

        [HarmonyPatch(typeof(EnemyBehaviour), "Update")]
        private static class UpdateHook
        {
            // Its transform's changes from here to the frame's end (the Animator's root motion).
            private static void Prefix(EnemyBehaviour __instance)
            {
                if (Recording && __instance is BrokenGolemTurretBehaviour) __instance.transform.hasChanged = false;
            }
        }

        [HarmonyPatch(typeof(BrokenGolemTurretBehaviour), "LookToPosition")]
        private static class LookHook
        {
            // The raycasts LookToPosition is about to make, made here first (queries: nothing changes).
            private static void Prefix(BrokenGolemTurretBehaviour __instance, out Ev __state)
            {
                __state = null;
                if (!Recording) return;
                int s = EnemyOracle.IdOf(__instance);
                if (s < 0) return;
                var tr = Traverse.Create(__instance);
                Vector3 offset = tr.Field("playerOffset").GetValue<Vector3>();
                Transform hero = HeroMerchant.Instance.transform;
                var e = new Ev { k = "look", s = s, step = Step, hero = hero.position, pos = __instance.transform.position, offset = offset, rays = new List<Ray>() };
                var dirs = tr.Field("checkDirections").GetValue<List<Vector2>>();
                LayerMask mask = (1 << LayerMask.NameToLayer("RisenObstacle")) | (1 << LayerMask.NameToLayer("LevelBoundary"));
                Vector2 item = Extensions.NormalizeOneDimensional(hero.position - __instance.transform.position + offset);
                int num = dirs.IndexOf(item);
                for (int i = 0; i < dirs.Count && num >= 0; i++)
                {
                    float dist = Constants.GetFloat("tileSize") * 2.4f;
                    RaycastHit2D hit = Physics2D.Raycast(__instance.transform.position, dirs[num], dist, mask);
                    e.rays.Add(new Ray
                    {
                        origin = __instance.transform.position, direction = dirs[num], distance = dist, mask = mask,
                        hit = hit.collider != null ? PathOf(hit.collider.transform) : null, point = hit.point, fraction = hit.fraction,
                        hitDistance = hit.distance
                    });
                    if (hit.collider == null) break;
                    num = (num + 1) % dirs.Count;
                }
                __state = e;
                Add(e);
            }

            private static void Postfix(BrokenGolemTurretBehaviour __instance, Ev __state)
            {
                if (__state == null) return;
                __state.flag = Traverse.Create(__instance).Field("canShoot").GetValue<bool>();
                __state.dir = (int)__instance.enemyDirection;
            }
        }

        [HarmonyPatch(typeof(BrokenGolemTurretBehaviour), "AttackPlayer")]
        private static class AttackHook
        {
            private static void Prefix(BrokenGolemTurretBehaviour __instance)
            {
                if (!Recording) return;
                int s = EnemyOracle.IdOf(__instance);
                if (s >= 0) Add(new Ev { k = "attack", s = s, step = Step, a = __instance.shootDelay });
            }
        }

        [HarmonyPatch(typeof(BrokenGolemTurretBehaviour), "ShootAnimation")]
        private static class ShootAnimationHook
        {
            private static void Prefix(BrokenGolemTurretBehaviour __instance)
            {
                if (!Recording) return;
                int s = EnemyOracle.IdOf(__instance);
                if (s >= 0) Add(new Ev { k = "shootAnim", s = s, step = Step });
            }
        }

        [HarmonyPatch(typeof(BrokenGolemTurretBehaviour), "ShootProjectile")]
        private static class ShootHook
        {
            private static void Prefix(BrokenGolemTurretBehaviour __instance, out Ev __state)
            {
                __state = null;
                if (!Recording) return;
                int s = EnemyOracle.IdOf(__instance);
                if (s < 0) return;
                __state = new Ev { k = "shot", s = s, step = Step, flag = Traverse.Create(__instance).Field("canShoot").GetValue<bool>(), dir = (int)__instance.enemyDirection };
                Add(__state);
            }

            private static void Postfix(BrokenGolemTurretBehaviour __instance, Ev __state)
            {
                if (__state == null || !__state.flag) return;
                var shot = Traverse.Create(__instance).Field("_projectilesShot").GetValue<List<BrokenGolemTurretProjectile>>();
                var last = shot != null && shot.Count > 0 ? shot[shot.Count - 1] : null;
                if (last == null) return;
                __state.p = IdOf(last.gameObject);
                __state.pos = last.transform.position;
            }
        }

        [HarmonyPatch(typeof(BrokenGolemTurretProjectile), "OnCollisionEnter2D")]
        private static class HitHook
        {
            private static void Prefix(BrokenGolemTurretProjectile __instance, Collision2D other, out Ev __state)
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

            private static void Postfix(Ev __state)
            {
                if (__state != null) __state.rng1 = Rng.Get();
            }
        }

        [HarmonyPatch(typeof(EnemyBehaviour), "PushPlayerBackwards", new Type[0])]
        private static class PushHook
        {
            private static void Prefix(EnemyBehaviour __instance)
            {
                if (!Recording || !(__instance is BrokenGolemTurretBehaviour)) return;
                int s = EnemyOracle.IdOf(__instance);
                if (s < 0) return;
                Vector2 hero = HeroMerchant.Instance.transform.position;
                Vector2 vector = HeroMerchant.Instance.transform.position - __instance.transform.position;
                vector.Normalize();
                Add(new Ev { k = "push", s = s, step = Step, hero = hero, pos = __instance.transform.position, force = vector * __instance.attackPushBackwardsForce });
            }
        }

        [HarmonyPatch(typeof(BrokenGolemTurretProjectile), "DestroyNow")]
        private static class DestroyHook
        {
            private static void Prefix(BrokenGolemTurretProjectile __instance)
            {
                if (!Recording || !ids.ContainsKey(__instance.gameObject)) return;
                Add(new Ev { k = "destroyNow", p = IdOf(__instance.gameObject), step = Step });
            }
        }
    }
}
