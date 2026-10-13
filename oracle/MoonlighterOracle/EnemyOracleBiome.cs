using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using HarmonyLib;
using UnityEngine;

namespace MoonlighterOracle
{
    /// `-oracle-enemy` in any dungeon, and what the other dungeons' enemies add to a recording
    /// (docs/biome_recordings.md):
    ///   - `-oracle-dungeon=<Golem|Forest|Desert|Tech|Unknown>` (default Golem; "Tesla" is Tech):
    ///     the dungeon whose floors the run takes, by HeroMerchant.LoadDungeonScene(culture) as
    ///     the Golem run always did and as the game's own debug panels do (HUDDebug.cs:484-500,
    ///     MoonlighterConsoleDebugPanel.cs:601-613). The town's WorldDungeonDoor, which locks the
    ///     dungeons past the slot's culturesDefeated (UpdateByCulturesDefeated), isn't involved;
    ///     the save isn't changed (Harvest.InstallSafety blocks every write).
    ///   - the start line: "dungeon" (the culture asked), "progress" (the slot's culturesDefeated
    ///     and maxDungeonLevelVisited: what the town's gates would have allowed),
    ///     "soundCreators" (MasterAudio's DynamicSoundGroupCreators, their paths: the permanent
    ///     ones, then those below DungeonManager's children in sibling order, which is the order
    ///     DungeonGenerator made them and so the order they made their groups: the first group of
    ///     a name wins; the creator prefab is the path's "Sounds - ..." part),
    ///     "spawnPrefabs" (what the enemies can make: the prefabs their scripts' fields reference,
    ///     two levels deep, and the pools named after them; each with the scripts tracked);
    ///   - per enemy ("enemy", "spawnEnemy" and a frame's "enemies"), "gen": the state of its game
    ///     scripts but the ones other rows have (Enemy, PolyNavAgent, the sounds...): every
    ///     instance field from its class down to MonoBehaviour (an EnemyBehaviour's own and its
    ///     base's), its Collider2Ds, its objects' active flags, its other Animators and its
    ///     SpriteRenderers. The setup line names them, and so does any later line where they
    ///     changed (an effect parented to the enemy, a child gone) ("comps": class, path, field names; "cols":
    ///     path, type, polygon paths; "nodes", "anims", "sprites": paths), every line has the
    ///     values in that order ("gv": per script the fields' values; "gc": per collider [enabled,
    ///     trigger, layer, offset x, y, size x, y (a circle: radius, 0; a polygon: paths, points),
    ///     position x, y, rotation z, lossy scale x, y]; "act": the objects' activeSelf as 0/1;
    ///     "ga": per Animator [on, state, normalized time] ([false] when off); "gs": per renderer
    ///     its sprite's name (null: none or the renderer off)). Values: floats as bits, vectors as
    ///     arrays of bits, enums as ints, a UnityEngine.Object as its name (null when gone), a
    ///     tween as [active, playing, elapsed], a collection as "#<count>", a delegate as true,
    ///     another object as "obj" or "struct";
    ///   - the objects the enemies make ("spawnPrefabs"' instances: shots, puddles, effects),
    ///     each with an id: events "spawn" (seen active: by one of its scripts' Awake/OnEnable/
    ///     Start, a fixed step's scan or the frame's end scan; "respawn" when a pooled one comes
    ///     back), "despawn" (destroyed or inactive), "olife" (Awake, OnEnable, Start, OnDisable,
    ///     OnDestroy of its scripts), "ohit" (their OnTriggerEnter2D/Exit2D,
    ///     OnCollisionEnter2D/Exit2D: the other's path, tag and layer), "ocall" (a method its
    ///     Animators' clips name, called by the Animator or a script), each with the Random state
    ///     before and after; per fixed step
    ///     "o" ([id, transform x, y, body x, y, velocity x, y, awake], or [id, x, y] without a
    ///     body); at the frame's end "objs" (id, prefab, active, world and local position,
    ///     rotation z, the body, the root Animator and sprite, and "gen" as for an enemy, its
    ///     names in the object's first line ("first": true));
    ///   - enemies the enemies make (a spawned prefab with an Enemy, seen at Enemy.Init or a
    ///     scan) join the recorded ones with the next ids: event "spawnEnemy" (its setup line in
    ///     "line", its prefab, who called), then they're in every row as the others are;
    ///   - "canim": a clip event's method on another script of an enemy's Animator's object
    ///     than its behaviour (its sounds: "anim" keeps to the behaviour's, as it always did);
    ///   - the clocks a script can read past Time.time: "rt" (Time.realtimeSinceStartup) at each
    ///     recorded Update's start (in "updates") and at the frame's end (the Slime2_0's attack
    ///     and expel timers run on it);
    ///   - the hero: "heroEffect" (HeroMerchant.AddEffect: the effect's name, who called, the
    ///     Random state before and after), "heroForce" (HeroMerchantController.AddForce: the
    ///     force, who called), DealDamageToPlayer's "src" (its damageSource) and "by" (who
    ///     called), "effect" (MoonlighterEffect.Create: the effect, its target (the hero, an enemy's
    ///     id, else the object's name), who called, the Random state before and after), and per
    ///     frame "heroFx": speedFactor, isTrapped, canMove, canAttack,
    ///     ismovedExternally, GetIntangibleMode, and his active Effects and MoonlighterEffects
    ///     ([class, name]);
    ///   - "query": each Physics2D query a recorded enemy's or a spawned object's script makes
    ///     (EnemyOracleQueries.cs).
    internal static partial class EnemyOracle
    {
        // ---- -oracle-dungeon ---------------------------------------------------------------

        private static readonly Dictionary<RuntimeAnimatorController, bool> distanceParams = new Dictionary<RuntimeAnimatorController, bool>();

        /// Whether the Animator has the DistanceX/DistanceY floats the Golem enemies' have
        /// (GetFloat on a missing one logs a warning every call).
        private static bool HasDistanceParams(Animator anim)
        {
            var ctrl = anim.runtimeAnimatorController;
            if (ctrl == null) return false;
            if (distanceParams.TryGetValue(ctrl, out bool has)) return has;
            bool x = false, y = false;
            foreach (var p in anim.parameters)
            {
                x |= p.name == "DistanceX" && p.type == AnimatorControllerParameterType.Float;
                y |= p.name == "DistanceY" && p.type == AnimatorControllerParameterType.Float;
            }
            distanceParams[ctrl] = x && y;
            return x && y;
        }

        internal static ItemMaster.Culture Dungeon = ItemMaster.Culture.Golem;

        private static readonly ItemMaster.Culture[] Dungeons =
            { ItemMaster.Culture.Golem, ItemMaster.Culture.Forest, ItemMaster.Culture.Desert, ItemMaster.Culture.Tech, ItemMaster.Culture.Unknown };

        /// The dungeon `-oracle-dungeon=` names (Golem without it); null and the reason when
        /// it names none of the five.
        private static string ParseDungeon()
        {
            Dungeon = ItemMaster.Culture.Golem;
            string arg = Array.Find(Environment.GetCommandLineArgs(), a => a.StartsWith("-oracle-dungeon="));
            if (arg == null) return null;
            string name = arg.Substring("-oracle-dungeon=".Length);
            if (string.Equals(name, "Tesla", StringComparison.OrdinalIgnoreCase)) name = "Tech";
            foreach (var c in Dungeons)
                if (string.Equals(name, c.ToString(), StringComparison.OrdinalIgnoreCase))
                {
                    Dungeon = c;
                    return null;
                }
            return "-oracle-dungeon=" + name + ": not a dungeon (Golem, Forest, Desert, Tech, Unknown)";
        }

        private static void WriteBiomeStart(Json j)
        {
            j.Prop("dungeon", Dungeon.ToString());
            var slot = GameManager.Instance != null ? GameManager.Instance.currentGameSlot : null;
            if (slot != null)
            {
                j.Key("progress").Obj().Prop("culturesDefeated", slot.culturesDefeated).Key("maxDungeonLevelVisited").Arr();
                if (slot.maxDungeonLevelVisited != null) foreach (int v in slot.maxDungeonLevelVisited) j.Val(v);
                j.EndArr().EndObj();
            }
            // (A creator prefab can hold its creators on children: "Sounds - Will/general_dungeon".)
            j.Key("soundCreators").Arr();
            var dm = DungeonManager.Instance;
            foreach (var c in UnityEngine.Object.FindObjectsOfType<DarkTonic.MasterAudio.DynamicSoundGroupCreator>().OrderBy(c => PathOf(c.transform), StringComparer.Ordinal))
                if (dm == null || !c.transform.IsChildOf(dm.transform)) j.Val(PathOf(c.transform));
            if (dm != null)
                foreach (Transform child in dm.transform)
                    foreach (var c in child.GetComponentsInChildren<DarkTonic.MasterAudio.DynamicSoundGroupCreator>(true))
                        j.Val("DungeonManager/" + PathBelow(c.transform, dm.transform));
            j.EndArr();
            j.Key("spawnPrefabs").Arr();
            foreach (var p in prefabs)
            {
                j.Obj().Prop("name", p.name).Prop("enemy", p.enemy).Prop("from", p.from).Key("scripts").Arr();
                foreach (var t in p.types) j.Val(t.Name);
                j.EndArr().EndObj();
            }
            j.EndArr();
        }

        // ---- The generic state of an object -------------------------------------------------

        private sealed class Shape
        {
            public readonly List<Component> comps = new List<Component>();
            public readonly List<FieldInfo[]> fields = new List<FieldInfo[]>();
            public Collider2D[] cols;
            public Transform[] nodes;
            public int scripts; // (MonoBehaviours below the object when made: a change remakes the shape)
            public Animator[] anims;
            public SpriteRenderer[] sprites;
        }

        private static readonly Dictionary<GameObject, Shape> shapes = new Dictionary<GameObject, Shape>();
        private static readonly Dictionary<Type, FieldInfo[]> fieldsOf = new Dictionary<Type, FieldInfo[]>();

        // Scripts other rows have, or that only present (their sounds, meshes, sorting).
        private static readonly HashSet<string> GenericSkip = new HashSet<string>
        {
            "Enemy", "EnemyStats", "PolyNavAgent", "SpriteDepthOrderRender", "EnemyAttackCollisionDetection", "EventSounds",
            "ChineseSpriteSwitcher", "PixelRotation",
        };

        private static bool GameScript(Type t) => t.Assembly == typeof(EnemyBehaviour).Assembly && !GenericSkip.Contains(t.Name) &&
                                                  !t.Name.StartsWith("Sound") && !t.Name.Contains("Mesh");

        private static FieldInfo[] FieldsOf(Type type)
        {
            if (fieldsOf.TryGetValue(type, out var f)) return f;
            var list = new List<FieldInfo>();
            for (var t = type; t != null && t != typeof(MonoBehaviour) && t != typeof(Behaviour) && t != typeof(Component); t = t.BaseType)
                foreach (var fi in t.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly))
                    list.Add(fi);
            f = list.ToArray();
            fieldsOf[type] = f;
            return f;
        }

        private static string PathBelow(Transform t, Transform root)
        {
            if (t == root) return "";
            string p = t.name;
            for (t = t.parent; t != null && t != root; t = t.parent) p = t.name + "/" + p;
            return p;
        }

        /// The object's shape; `changed`: made again because its objects or scripts changed (an
        /// effect parented to it, a child destroyed), or made the first time.
        private static Shape ShapeOf(GameObject go, out bool changed)
        {
            changed = false;
            int nodes = go.GetComponentsInChildren<Transform>(true).Length, scripts = go.GetComponentsInChildren<MonoBehaviour>(true).Length;
            if (shapes.TryGetValue(go, out var s) && s.nodes.Length == nodes && s.scripts == scripts) return s;
            changed = true;
            s = new Shape { scripts = scripts };
            foreach (var mb in go.GetComponentsInChildren<MonoBehaviour>(true))
                if (mb != null && GameScript(mb.GetType()))
                {
                    s.comps.Add(mb);
                    s.fields.Add(FieldsOf(mb.GetType()));
                }
            s.cols = go.GetComponentsInChildren<Collider2D>(true);
            s.nodes = go.GetComponentsInChildren<Transform>(true);
            s.anims = go.GetComponentsInChildren<Animator>(true).Where(a => a.gameObject != go).ToArray();
            s.sprites = go.GetComponentsInChildren<SpriteRenderer>(true);
            shapes[go] = s;
            return s;
        }

        private static void WriteValue(Json j, object v)
        {
            switch (v)
            {
                case null: j.Val((string)null); return;
                case float f: j.Bits(f); return;
                case double d: j.Val("d" + BitConverter.DoubleToInt64Bits(d)); return;
                case bool b: j.Val(b); return;
                case int i: j.Val(i); return;
                case uint u: j.Val(u); return;
                case short sh: j.Val((int)sh); return;
                case ushort us: j.Val((int)us); return;
                case byte by: j.Val((int)by); return;
                case sbyte sb: j.Val((int)sb); return;
                case char ch: j.Val((int)ch); return;
                case long l: j.Val("l" + l); return;
                case ulong ul: j.Val("l" + ul); return;
                case string str: j.Val(str); return;
                case Enum e:
                    {
                        long n = Convert.ToInt64(e);
                        if (n >= int.MinValue && n <= int.MaxValue) j.Val((int)n); else j.Val("l" + n);
                        return;
                    }
                case Vector2 v2: j.Arr().Bits(v2.x).Bits(v2.y).EndArr(); return;
                case Vector3 v3: j.Arr().Bits(v3.x).Bits(v3.y).Bits(v3.z).EndArr(); return;
                case Vector4 v4: j.Arr().Bits(v4.x).Bits(v4.y).Bits(v4.z).Bits(v4.w).EndArr(); return;
                case Quaternion q: j.Arr().Bits(q.x).Bits(q.y).Bits(q.z).Bits(q.w).EndArr(); return;
                case Color c: j.Arr().Bits(c.r).Bits(c.g).Bits(c.b).Bits(c.a).EndArr(); return;
                case Rect r: j.Arr().Bits(r.x).Bits(r.y).Bits(r.width).Bits(r.height).EndArr(); return;
                case Vector2Int vi: j.Arr().Val(vi.x).Val(vi.y).EndArr(); return;
                case LayerMask m: j.Val(m.value); return;
                case UnityEngine.Object o: j.Val(o == null ? null : o.name); return;
                case DG.Tweening.Tween tw:
                    {
                        bool on = DG.Tweening.TweenExtensions.IsActive(tw);
                        j.Arr().Val(on).Val(on && DG.Tweening.TweenExtensions.IsPlaying(tw)).Bits(on ? DG.Tweening.TweenExtensions.Elapsed(tw, false) : 0f).EndArr();
                        return;
                    }
                case Delegate _: j.Val(true); return;
                case ICollection col: j.Val("#" + col.Count); return;
                default: j.Val(v.GetType().IsValueType ? "struct" : "obj"); return;
            }
        }

        private static void ColliderShape(Json j, Collider2D c)
        {
            switch (c)
            {
                case BoxCollider2D b: j.Bits(b.size.x).Bits(b.size.y); break;
                case CircleCollider2D r: j.Bits(r.radius).Bits(0f); break;
                case CapsuleCollider2D p: j.Bits(p.size.x).Bits(p.size.y); break;
                case PolygonCollider2D g:
                    {
                        int points = 0;
                        for (int i = 0; i < g.pathCount; i++) points += g.GetPath(i).Length;
                        j.Val(g.pathCount).Val(points);
                        break;
                    }
                case EdgeCollider2D e: j.Val(e.pointCount).Bits(e.edgeRadius); break;
                default: j.Val(0).Val(0); break;
            }
        }

        /// "gen" (setup: the names) and the values, as the header comment says.
        private static void WriteGeneric(Json j, GameObject go, bool setup)
        {
            var s = ShapeOf(go, out bool changed);
            var root = go.transform;
            if (setup || changed)
            {
                j.Key("gen").Obj().Key("comps").Arr();
                for (int i = 0; i < s.comps.Count; i++)
                {
                    var c = s.comps[i];
                    j.Obj().Prop("c", c != null ? c.GetType().Name : null).Prop("p", c != null ? PathBelow(c.transform, root) : null).Key("f").Arr();
                    foreach (var f in s.fields[i]) j.Val(f.DeclaringType.Name + "." + f.Name);
                    j.EndArr().EndObj();
                }
                j.EndArr().Key("cols").Arr();
                foreach (var c in s.cols)
                {
                    j.Obj().Prop("p", c != null ? PathBelow(c.transform, root) : null).Prop("t", c != null ? c.GetType().Name : null);
                    if (c is PolygonCollider2D pc)
                    {
                        j.Key("paths").Arr();
                        for (int i = 0; i < pc.pathCount; i++) Flat(j, pc.GetPath(i));
                        j.EndArr();
                    }
                    j.EndObj();
                }
                j.EndArr().Key("nodes").Arr();
                foreach (var n in s.nodes) j.Val(n != null ? PathBelow(n, root) : null);
                j.EndArr().Key("anims").Arr();
                foreach (var a in s.anims) j.Val(a != null ? PathBelow(a.transform, root) : null);
                j.EndArr().Key("sprites").Arr();
                foreach (var r in s.sprites) j.Val(r != null ? PathBelow(r.transform, root) : null);
                j.EndArr().EndObj();
            }
            j.Key("gv").Arr();
            for (int i = 0; i < s.comps.Count; i++)
            {
                var c = s.comps[i];
                j.Arr();
                if (c != null)
                    foreach (var f in s.fields[i])
                    {
                        object v;
                        try { v = f.GetValue(c); }
                        catch (Exception) { v = "?"; }
                        WriteValue(j, v);
                    }
                j.EndArr();
            }
            j.EndArr().Key("gc").Arr();
            foreach (var c in s.cols)
            {
                j.Arr();
                if (c != null)
                {
                    j.Val(c.enabled).Val(c.isTrigger).Val(c.gameObject.layer).Bits(c.offset.x).Bits(c.offset.y);
                    ColliderShape(j, c);
                    var t = c.transform;
                    j.Bits(t.position.x).Bits(t.position.y).Bits(t.eulerAngles.z).Bits(t.lossyScale.x).Bits(t.lossyScale.y);
                }
                j.EndArr();
            }
            j.EndArr();
            var act = new System.Text.StringBuilder(s.nodes.Length);
            foreach (var n in s.nodes) act.Append(n != null && n.gameObject.activeSelf ? '1' : '0');
            j.Prop("act", act.ToString());
            j.Key("ga").Arr();
            foreach (var a in s.anims)
            {
                j.Arr();
                bool on = a != null && a.isActiveAndEnabled && a.runtimeAnimatorController != null;
                j.Val(on);
                if (on)
                {
                    var info = a.GetCurrentAnimatorStateInfo(0);
                    j.Val(info.shortNameHash).Bits(info.normalizedTime);
                }
                j.EndArr();
            }
            j.EndArr().Key("gs").Arr();
            foreach (var r in s.sprites) j.Val(r != null && r.enabled && r.sprite != null ? r.sprite.name : null);
            j.EndArr();
        }

        // ---- What the enemies make -----------------------------------------------------------

        private sealed class SpawnPrefab
        {
            public string name, from;
            public bool enemy;
            public readonly List<Type> types = new List<Type>();
        }

        private sealed class Tracked
        {
            public int id;
            public GameObject go;
            public string prefab;
            public bool on, gone, named, physical; // (physical: a Collider2D or Rigidbody2D below it)
        }

        private static readonly List<SpawnPrefab> prefabs = new List<SpawnPrefab>();
        private static readonly Dictionary<string, SpawnPrefab> prefabByName = new Dictionary<string, SpawnPrefab>();
        private static readonly List<Tracked> tracked = new List<Tracked>();
        private static readonly Dictionary<GameObject, Tracked> trackedOf = new Dictionary<GameObject, Tracked>();
        private static readonly List<List<float[]>> stepObjects = new List<List<float[]>>();
        private static readonly HashSet<MethodBase> spawnPatched = new HashSet<MethodBase>();
        private static bool biome;            // the generic rows are on (every -oracle-enemy recording)
        private static bool animRehook;       // an enemy joined: its clip events to hook at the frame's end

        private static string PrefabName(string name) => name.EndsWith("(Clone)") ? name.Substring(0, name.Length - "(Clone)".Length) : name;

        /// The prefabs a script's fields reference (assets, not scene objects).
        private static IEnumerable<GameObject> Referenced(MonoBehaviour mb)
        {
            foreach (var f in FieldsOf(mb.GetType()))
            {
                object v;
                try { v = f.GetValue(mb); }
                catch (Exception) { continue; }
                if (v is UnityEngine.Object o)
                {
                    var g = AsPrefab(o);
                    if (g != null) yield return g;
                }
                else if (v is IEnumerable list && !(v is string))
                    foreach (object x in list)
                        if (x is UnityEngine.Object xo)
                        {
                            var g = AsPrefab(xo);
                            if (g != null) yield return g;
                        }
            }
        }

        private static GameObject AsPrefab(UnityEngine.Object o)
        {
            if (o == null) return null;
            var g = o as GameObject ?? (o as Component)?.gameObject;
            if (g == null || g.scene.IsValid()) return null; // (a scene object: not a prefab)
            return g;
        }

        private static void AddPrefab(GameObject g, string from, int depth)
        {
            if (g == null || prefabByName.ContainsKey(g.name)) return;
            if (g.GetComponent<RectTransform>() != null || g.name == "HealthBar") return; // (UI: the HUD's)
            var p = new SpawnPrefab { name = g.name, from = from, enemy = g.GetComponent<Enemy>() != null };
            prefabs.Add(p);
            prefabByName[g.name] = p;
            if (!p.enemy)
            {
                foreach (var mb in g.GetComponents<MonoBehaviour>())
                    if (mb != null && GameScript(mb.GetType()) && !p.types.Contains(mb.GetType())) p.types.Add(mb.GetType());
                if (p.types.Count == 0)
                    foreach (var mb in g.GetComponentsInChildren<MonoBehaviour>(true))
                        if (mb != null && GameScript(mb.GetType()) && !p.types.Contains(mb.GetType())) p.types.Add(mb.GetType());
            }
            if (depth >= 2) return;
            foreach (var mb in g.GetComponentsInChildren<MonoBehaviour>(true))
                if (mb != null && mb.GetType().Assembly == typeof(EnemyBehaviour).Assembly)
                    foreach (var r in Referenced(mb)) AddPrefab(r, g.name, depth + 1);
        }

        /// Before the recording (the frames before its start): the prefabs the enemies can make,
        /// their scripts hooked, the Physics2D queries hooked.
        private static void BiomePrepare()
        {
            biome = true;
            prefabs.Clear();
            prefabByName.Clear();
            animRehook = false;
            foreach (var b in slimes.Where(b => b != null))
                foreach (var mb in b.GetComponentsInChildren<MonoBehaviour>(true))
                    if (mb != null && mb.GetType().Assembly == typeof(EnemyBehaviour).Assembly)
                        foreach (var r in Referenced(mb)) AddPrefab(r, b.name, 1);
            // The pools named after the enemy (PoolManager: "<enemy>_<prefab>"), their objects' prefabs.
            var pm = DungeonManager.Instance != null ? DungeonManager.Instance.poolManager : null;
            if (pm != null && pm.poolsCollection != null)
                foreach (var kv in pm.poolsCollection)
                    if (kv.Key.StartsWith(prefab + "_", StringComparison.Ordinal) && kv.Value != null && kv.Value.pooledObjects != null)
                    {
                        var first = kv.Value.pooledObjects.FirstOrDefault(o => o != null);
                        if (first == null || prefabByName.ContainsKey(PrefabName(first.name))) continue;
                        // (PoolManager names its objects after the pool: "<enemy>_<prefab>".)
                        var p = new SpawnPrefab { name = PrefabName(first.name), from = "pool of " + kv.Key.Substring(prefab.Length + 1), enemy = first.GetComponent<Enemy>() != null };
                        foreach (var mb in first.GetComponents<MonoBehaviour>())
                            if (mb != null && GameScript(mb.GetType()) && !p.types.Contains(mb.GetType())) p.types.Add(mb.GetType());
                        prefabs.Add(p);
                        prefabByName[p.name] = p;
                    }
            HookSpawnScripts();
            WatchTypes(); // (EnemyOracleQueries.cs)
            HookQueries();
        }

        /// The recording starts (its start line next): what the enemies made so far.
        private static void BiomeBegin()
        {
            tracked.Clear();
            trackedOf.Clear();
            stepObjects.Clear();
            shapes.Clear();
            ScanSpawned(-2);
        }

        private static void BiomeEnd() => biome = false;

        private static void BiomeClear() => stepObjects.Clear();

        /// The spawnable scripts' lifecycle, physics messages and clip-event methods, hooked.
        private static void HookSpawnScripts()
        {
            var harmony = new Harmony("moonlighter.oracle.enemy.spawn");
            var life = new HarmonyMethod(typeof(EnemyOracle), nameof(SpawnLifePrefix));
            var lifePost = new HarmonyMethod(typeof(EnemyOracle), nameof(SpawnLifePostfix));
            string[] lifeNames = { "Awake", "OnEnable", "Start", "OnDisable", "OnDestroy", "OnTriggerEnter2D", "OnTriggerExit2D", "OnCollisionEnter2D", "OnCollisionExit2D" };
            foreach (var p in prefabs)
            {
                if (p.enemy) continue;
                foreach (var type in p.types)
                {
                    var names = new HashSet<string>(lifeNames);
                    names.UnionWith(ClipEventsOf(type, p.name));
                    for (var t = type; t != null && t != typeof(MonoBehaviour); t = t.BaseType)
                        foreach (var m in t.GetMethods(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly))
                        {
                            if (!names.Contains(m.Name) || m.IsAbstract || m.ContainsGenericParameters || !spawnPatched.Add(m)) continue;
                            try { harmony.Patch(m, prefix: life, postfix: lifePost); }
                            catch (Exception e) { OraclePlugin.Log.LogWarning(Tag + ": can't hook " + t.Name + "." + m.Name + ": " + e.Message); }
                        }
                }
            }
            OraclePlugin.Log.LogInfo(Tag + ": spawnable prefabs: " + string.Join(", ", prefabs.Select(p => p.name + (p.enemy ? " (enemy)" : "")).ToArray()));
        }

        /// The clip events of the Animators on a prefab's objects that carry `type`.
        private static IEnumerable<string> ClipEventsOf(Type type, string prefabName)
        {
            var names = new HashSet<string>();
            var pool = UnityEngine.Resources.FindObjectsOfTypeAll(type).OfType<Component>().Where(c => c != null && PrefabName(c.transform.root.name) == prefabName);
            foreach (var c in pool.Take(4))
            {
                var anim = c.GetComponent<Animator>();
                var ctrl = anim != null ? anim.runtimeAnimatorController : null;
                if (ctrl == null) continue;
                foreach (var clip in ctrl.animationClips)
                    foreach (var ev in clip.events) names.Add(ev.functionName);
            }
            return names;
        }

        private static readonly HashSet<string> LifeNames = new HashSet<string>
            { "Awake", "OnEnable", "Start", "OnDisable", "OnDestroy", "OnTriggerEnter2D", "OnTriggerExit2D", "OnCollisionEnter2D", "OnCollisionExit2D" };

        /// The root a spawned script belongs to: its object or the nearest parent named after a spawnable prefab.
        private static GameObject SpawnRoot(Component c, out SpawnPrefab p)
        {
            p = null;
            for (var t = c.transform; t != null; t = t.parent)
                if (prefabByName.TryGetValue(PrefabName(t.name), out p)) return t.gameObject;
            return null;
        }

        private static Tracked Track(GameObject go, SpawnPrefab p, int step, string how)
        {
            if (trackedOf.TryGetValue(go, out var t))
            {
                if (!t.on && go.activeInHierarchy)
                {
                    t.on = true;
                    Add(new Ev { k = "spawn", type = t.id, step = step, other = t.prefab, caller = how, player = true, pos = go.transform.position });
                }
                return t;
            }
            t = new Tracked { id = tracked.Count, go = go, prefab = p.name, on = go.activeInHierarchy,
                              physical = go.GetComponentInChildren<Collider2D>(true) != null || go.GetComponentInChildren<Rigidbody2D>(true) != null };
            tracked.Add(t);
            trackedOf[go] = t;
            Add(new Ev { k = "spawn", type = t.id, step = step, other = t.prefab, caller = how, pos = go.transform.position, raw = go.transform.parent != null ? PathOf(go.transform.parent) : "" });
            return t;
        }

        private static void SpawnLifePrefix(object __instance, MethodBase __originalMethod, object[] __args, out Ev __state)
        {
            __state = null;
            if (!recording || !(__instance is Component c) || c == null) return;
            var root = SpawnRoot(c, out var p);
            if (root == null || p == null || p.enemy) return;
            string fn = __originalMethod.Name;
            var t = Track(root, p, Step, fn);
            bool hit = fn.StartsWith("OnTrigger") || fn.StartsWith("OnCollision");
            string kind = LifeNames.Contains(fn) ? (hit ? "ohit" : "olife") : "ocall";
            __state = new Ev { k = kind, type = t.id, step = Step, other = fn, rng0 = Rng.Get(), caller = c.GetType().Name };
            if (hit && __args != null && __args.Length > 0)
            {
                GameObject o = __args[0] is Collider2D col && col != null ? col.gameObject : __args[0] is Collision2D cl && cl != null && cl.collider != null ? cl.collider.gameObject : null;
                if (o != null)
                {
                    __state.raw = PathOf(o.transform);
                    __state.player = o.CompareTag("Player");
                    __state.hp0 = o.layer;
                    __state.s = Id(o.GetComponentInParent<EnemyBehaviour>());
                    __state.pos = o.transform.position;
                    __state.hero = new Vector2(0f, 0f);
                    __state.caller = c.GetType().Name + "|" + o.tag;
                }
            }
            Add(__state);
        }

        private static void SpawnLifePostfix(Ev __state)
        {
            if (__state != null) __state.rng1 = Rng.Get();
        }

        /// Spawned objects seen now (step: the fixed step's index, -1 the frame's end, -2 the start).
        private static void ScanSpawned(int step)
        {
            foreach (var type in prefabs.Where(p => !p.enemy).SelectMany(p => p.types).Distinct())
                foreach (var o in UnityEngine.Object.FindObjectsOfType(type))
                    if (o is Component c && c != null)
                    {
                        var root = SpawnRoot(c, out var rp);
                        if (root != null && rp != null && !rp.enemy && root.activeInHierarchy) Track(root, rp, step, step == -2 ? "start" : "scan");
                    }
            // Enemies made by the enemies (spawned prefabs with an Enemy), not seen at Init.
            if (prefabs.Any(p => p.enemy))
                foreach (var e in UnityEngine.Object.FindObjectsOfType<Enemy>())
                    if (e != null && e.enemyBehaviour != null && !slimes.Contains(e.enemyBehaviour) && prefabByName.TryGetValue(PrefabName(e.name), out var ep) && ep.enemy)
                        JoinEnemy(e.enemyBehaviour, step == -2 ? "start" : "scan");
            // Gone: destroyed, or inactive (a pooled one put back).
            foreach (var t in tracked)
            {
                if (t.gone) continue;
                if (t.go == null)
                {
                    t.gone = true;
                    t.on = false;
                    Add(new Ev { k = "despawn", type = t.id, step = step, other = "destroyed" });
                }
                else if (t.on && !t.go.activeInHierarchy)
                {
                    t.on = false;
                    Add(new Ev { k = "despawn", type = t.id, step = step, other = "inactive" });
                }
            }
        }

        /// A spawned enemy joins the recorded ones (the next id).
        private static void JoinEnemy(EnemyBehaviour b, string how)
        {
            if (b == null || slimes.Contains(b)) return;
            slimes.Add(b);
            int id = slimes.Count - 1;
            var j = new Json().Obj();
            try { WriteSlime(j, id, b, true); }
            catch (Exception e) { OraclePlugin.Log.LogWarning(Tag + ": spawned enemy " + b.name + ": " + e.Message); }
            j.EndObj();
            Add(new Ev { k = "spawnEnemy", s = id, step = Step, other = PrefabName(b.name), caller = how, raw = j.ToString() });
            animRehook = true;
            OraclePlugin.Log.LogInfo(Tag + ": " + b.name + " joined as enemy " + id + " (" + how + ")");
        }

        [HarmonyPatch(typeof(Enemy), "Init")]
        private static class SpawnedEnemyInitHook
        {
            private static void Postfix(Enemy __instance)
            {
                if (!recording || !biome || __instance == null || __instance.enemyBehaviour == null) return;
                if (slimes.Contains(__instance.enemyBehaviour)) return;
                JoinEnemy(__instance.enemyBehaviour, ScriptCaller("Init") ?? "Init");
            }
        }

        /// A fixed step's start (from the Recorder): the objects seen, then their bodies.
        private static void BiomeOnStep()
        {
            if (!biome) return;
            int step = stepTime.Count - 1;
            ScanSpawned(step);
            var rows = new List<float[]>();
            foreach (var t in tracked)
            {
                if (!t.on || t.go == null || !t.physical) continue;
                Vector2 p = t.go.transform.position;
                var rb = t.go.GetComponent<Rigidbody2D>();
                rows.Add(rb != null
                    ? new[] { t.id, p.x, p.y, rb.position.x, rb.position.y, rb.velocity.x, rb.velocity.y, rb.IsAwake() ? 1f : 0f }
                    : new[] { t.id, p.x, p.y });
            }
            stepObjects.Add(rows);
        }

        private static void WriteBiomeStep(Json j, int k)
        {
            if (!biome || k >= stepObjects.Count || stepObjects[k].Count == 0) return;
            j.Key("o").Arr();
            foreach (var r in stepObjects[k])
            {
                j.Arr().Val((int)r[0]);
                for (int i = 1; i < r.Length && i < 7; i++) j.Bits(r[i]);
                if (r.Length > 7) j.Val(r[7] != 0f);
                j.EndArr();
            }
            j.EndArr();
        }

        /// The frame's end: the spawned objects, the hero's effects.
        private static void WriteBiomeFrame(Json j)
        {
            if (!biome) return;
            j.Key("objs").Arr();
            foreach (var t in tracked)
            {
                if (t.gone && t.named) continue;
                j.Obj().Prop("o", t.id);
                if (!t.named) j.Prop("prefab", t.prefab).Prop("first", true);
                bool alive = t.go != null;
                j.Prop("on", alive && t.go.activeInHierarchy);
                if (alive && (t.go.activeInHierarchy || !t.named))
                {
                    var tr = t.go.transform;
                    j.Key("pos"); V2(j, tr.position);
                    j.Key("local"); V2(j, tr.localPosition);
                    j.Key("rot").Bits(tr.eulerAngles.z);
                    var rb = t.go.GetComponent<Rigidbody2D>();
                    if (rb != null)
                    {
                        j.Key("rb").Arr().Bits(rb.position.x).Bits(rb.position.y).Bits(rb.velocity.x).Bits(rb.velocity.y).EndArr().Prop("awake", rb.IsAwake())
                            .Prop("bodyType", (int)rb.bodyType).Prop("simulated", rb.simulated);
                    }
                    var anim = t.go.GetComponent<Animator>();
                    if (anim != null && anim.isActiveAndEnabled && anim.runtimeAnimatorController != null)
                    {
                        var info = anim.GetCurrentAnimatorStateInfo(0);
                        j.Key("anim").Arr().Val(info.shortNameHash).Bits(info.normalizedTime).Bits(anim.speed).EndArr();
                    }
                    var sr = t.go.GetComponent<SpriteRenderer>();
                    if (sr != null) j.Prop("sprite", sr.enabled && sr.sprite != null ? sr.sprite.name : null);
                    try { WriteGeneric(j, t.go, !t.named); }
                    catch (Exception e) { j.Prop("genError", e.GetType().Name); }
                }
                t.named = true;
                j.EndObj();
            }
            j.EndArr();
            // The hero.
            var hero = HeroMerchant.Instance;
            if (hero == null) return;
            var ctrl = hero.heroMerchantController;
            j.Key("heroFx").Obj().Key("speed").Bits(ctrl != null ? ctrl.speedFactor : 0f);
            if (ctrl != null)
            {
                var tr = Traverse.Create(ctrl);
                j.Prop("trapped", ctrl.isTrapped).Prop("canMove", ctrl.canMove).Prop("canAttack", ctrl.canAttack)
                    .Prop("movedExternally", tr.Field("ismovedExternally").GetValue<bool>()).Prop("intangible", ctrl.GetIntangibleMode());
            }
            j.Key("effects").Arr();
            foreach (var e in hero.GetComponentsInChildren<MonoBehaviour>(false))
                if (e is Effect || e is MoonlighterEffect || e is DamageOverTimeEffect)
                    j.Arr().Val(e.GetType().Name).Val(e.name).EndArr();
            j.EndArr().EndObj();
        }

        /// This file's events (false: not one of them).
        private static bool WriteBiomeEvent(Json j, Ev e)
        {
            switch (e.k)
            {
                case "spawn":
                    j.Prop("o", e.type).Prop("prefab", e.other).Prop("how", e.caller);
                    if (e.player) j.Prop("respawn", true);
                    if (e.raw != null) j.Prop("parent", e.raw);
                    j.Key("pos"); V2(j, e.pos);
                    return true;
                case "despawn":
                    j.Prop("o", e.type).Prop("why", e.other);
                    return true;
                case "olife":
                case "ocall":
                    j.Prop("o", e.type).Prop("fn", e.other).Prop("script", e.caller);
                    Rng.WriteState(j.Key("rng0"), e.rng0);
                    Rng.WriteState(j.Key("rng1"), e.rng1 ?? e.rng0);
                    return true;
                case "ohit":
                    j.Prop("o", e.type).Prop("fn", e.other).Prop("script", e.caller).Prop("other", e.raw).Prop("player", e.player).Prop("layer", (int)e.hp0);
                    Rng.WriteState(j.Key("rng0"), e.rng0);
                    Rng.WriteState(j.Key("rng1"), e.rng1 ?? e.rng0);
                    return true;
                case "spawnEnemy":
                    j.Prop("prefab", e.other).Prop("how", e.caller).Key("line").Raw(e.raw);
                    return true;
                case "canim":
                    j.Prop("fn", e.other).Prop("script", e.caller);
                    Rng.WriteState(j.Key("rng0"), e.rng0);
                    Rng.WriteState(j.Key("rng1"), e.rng1 ?? e.rng0);
                    return true;
                case "heroEffect":
                    j.Prop("effect", e.other).Prop("by", e.caller);
                    Rng.WriteState(j.Key("rng0"), e.rng0);
                    Rng.WriteState(j.Key("rng1"), e.rng1 ?? e.rng0);
                    return true;
                case "effect":
                    j.Prop("effect", e.other).Prop("target", e.raw).Prop("by", e.caller);
                    Rng.WriteState(j.Key("rng0"), e.rng0);
                    Rng.WriteState(j.Key("rng1"), e.rng1 ?? e.rng0);
                    return true;
                case "query":
                    j.Prop("fn", e.other).Prop("by", e.caller).Key("q").Raw(e.raw);
                    return true;
                case "heroForce":
                    j.Key("force"); V2(j, e.pos);
                    j.Prop("by", e.caller);
                    return true;
            }
            return false;
        }

        // ---- Clip events on an enemy's other scripts ("canim") -------------------------------

        private static readonly HashSet<MethodBase> canimPatched = new HashSet<MethodBase>();

        /// The methods the enemies' Animators' clips name on the scripts of the Animator's object
        /// other than the behaviour's chain (which "anim" has).
        private static void HookOtherClipEvents()
        {
            var harmony = new Harmony("moonlighter.oracle.enemy.canim");
            var prefix = new HarmonyMethod(typeof(EnemyOracle), nameof(CanimPrefix));
            var postfix = new HarmonyMethod(typeof(EnemyOracle), nameof(AnimPostfix));
            foreach (var b in slimes.ToList())
            {
                if (b == null) continue;
                var behaviourChain = new HashSet<Type>();
                for (var t = b.GetType(); t != null && t != typeof(MonoBehaviour); t = t.BaseType) behaviourChain.Add(t);
                foreach (var anim in b.GetComponentsInChildren<Animator>(true))
                {
                    var ctrl = anim.runtimeAnimatorController;
                    if (ctrl == null) continue;
                    var names = new HashSet<string>();
                    foreach (var clip in ctrl.animationClips)
                        foreach (var ev in clip.events) names.Add(ev.functionName);
                    foreach (var mb in anim.GetComponents<MonoBehaviour>())
                    {
                        if (mb == null) continue;
                        for (var t = mb.GetType(); t != null && t != typeof(MonoBehaviour); t = t.BaseType)
                        {
                            if (anim.gameObject == b.gameObject && behaviourChain.Contains(t)) continue; // ("anim")
                            foreach (var m in t.GetMethods(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly))
                            {
                                if (!names.Contains(m.Name) || m.IsAbstract || m.ContainsGenericParameters || animPatched.Contains(m) || !canimPatched.Add(m)) continue;
                                try { harmony.Patch(m, prefix: prefix, postfix: postfix); }
                                catch (Exception e) { OraclePlugin.Log.LogWarning(Tag + ": can't hook " + t.Name + "." + m.Name + ": " + e.Message); }
                            }
                        }
                    }
                }
            }
        }

        private static void CanimPrefix(object __instance, MethodBase __originalMethod, out Ev __state)
        {
            __state = null;
            if (!recording || !(__instance is Component c) || c == null) return;
            int id = IdInParent(c.gameObject);
            if (id < 0) return;
            __state = new Ev { k = "canim", s = id, step = Step, other = __originalMethod.Name, caller = __instance.GetType().Name, rng0 = Rng.Get() };
            Add(__state);
        }

        /// After a frame: the clip events of enemies that joined it.
        private static void BiomeAfterFrame()
        {
            if (!animRehook) return;
            animRehook = false;
            HookAnimationEvents();
            HookOtherClipEvents();
            WatchTypes();
        }

        // ---- The hero ------------------------------------------------------------------------

        [HarmonyPatch(typeof(HeroMerchant), "AddEffect")]
        private static class HeroEffectHook
        {
            private static void Prefix(Effect effect, out Ev __state)
            {
                __state = null;
                if (!recording) return;
                __state = new Ev { k = "heroEffect", step = Step, other = effect != null ? effect.name : null, caller = ScriptCaller("AddEffect"), rng0 = Rng.Get() };
                Add(__state);
            }

            private static void Postfix(Ev __state)
            {
                if (__state != null) __state.rng1 = Rng.Get();
            }
        }

        [HarmonyPatch]
        private static class EffectCreateHook
        {
            private static IEnumerable<MethodBase> TargetMethods() =>
                typeof(MoonlighterEffect).GetMethods(BindingFlags.Public | BindingFlags.Static).Where(m => m.Name == "Create" && !m.ContainsGenericParameters).Cast<MethodBase>();

            private static void Prefix(MoonlighterEffect prefab, GameObject target, out Ev __state)
            {
                __state = null;
                if (!recording || !biome) return;
                string to = target == null ? null : HeroMerchant.Instance != null && target == HeroMerchant.Instance.gameObject ? "hero" : null;
                if (to == null && target != null)
                {
                    int id = IdInParent(target);
                    to = id >= 0 ? "enemy " + id : target.name;
                }
                __state = new Ev { k = "effect", step = Step, other = prefab != null ? prefab.name : null, raw = to, caller = ScriptCaller("Create"), rng0 = Rng.Get() };
                Add(__state);
            }

            private static void Postfix(Ev __state)
            {
                if (__state != null) __state.rng1 = Rng.Get();
            }
        }

        [HarmonyPatch(typeof(HeroMerchantController), "AddForce")]
        private static class HeroForceHook
        {
            private static void Prefix(Vector2 force)
            {
                if (recording) Add(new Ev { k = "heroForce", step = Step, pos = force, caller = ScriptCaller("AddForce") });
            }
        }
    }
}
