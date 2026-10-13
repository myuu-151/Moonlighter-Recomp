using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using HarmonyLib;
using UnityEngine;

namespace MoonlighterOracle
{
    /// `-oracle-enemy`'s "query" events (EnemyOracleBiome.cs): each Physics2D query (Raycast,
    /// BoxCast, CircleCast, Linecast, the Overlap* ones and their *All forms; every public
    /// overload of Physics2D and PhysicsScene2D, the outermost call only) that a script of a
    /// recorded enemy or of what the enemies make calls while recording: "fn", "by" (the
    /// script's Type.Method), "q": {"a": the arguments (vectors and floats as bits, a
    /// ContactFilter2D as [layer mask or -1, useTriggers]), "r": the result (a RaycastHit2D as
    /// {"c": the collider's path, "pt", "n", "d", "fr"}, or null; a Collider2D as its path; an
    /// array as the paths)}. The replays' worlds must answer the same: these say what the game's
    /// answered. A query the Mono JIT inlined into the script (a short overload) isn't seen.
    internal static partial class EnemyOracle
    {
        private static readonly HashSet<Type> watched = new HashSet<Type>();
        private static bool queriesHooked;
        [ThreadStatic] private static int queryDepth;

        private static readonly string[] QueryNames =
        {
            "Raycast", "BoxCast", "CircleCast", "CapsuleCast", "Linecast", "OverlapBox", "OverlapCircle", "OverlapPoint", "OverlapArea",
            "OverlapCapsule", "RaycastAll", "BoxCastAll", "CircleCastAll", "LinecastAll", "OverlapBoxAll", "OverlapCircleAll", "OverlapPointAll",
            "OverlapAreaAll",
        };

        /// The scripts whose queries count: the recorded enemies' (and their children's), the
        /// spawnable prefabs' tracked scripts and the scripts of the enemy prefabs they make.
        private static void WatchTypes()
        {
            void Watch(Type type)
            {
                for (var t = type; t != null && t != typeof(MonoBehaviour); t = t.BaseType) watched.Add(t); // (a base's method: EnemyBehaviour.Update's hole check)
            }
            foreach (var b in slimes)
                if (b != null)
                    foreach (var mb in b.GetComponentsInChildren<MonoBehaviour>(true))
                        if (mb != null) Watch(mb.GetType());
            foreach (var p in prefabs)
                foreach (var t in p.types) Watch(t);
        }

        private static void HookQueries()
        {
            if (queriesHooked) return;
            queriesHooked = true;
            var harmony = new Harmony("moonlighter.oracle.enemy.query");
            var prefix = new HarmonyMethod(typeof(EnemyOracle), nameof(QueryPrefix));
            var finalizer = new HarmonyMethod(typeof(EnemyOracle), nameof(QueryFinalizer));
            var posts = new Dictionary<Type, HarmonyMethod>
            {
                { typeof(RaycastHit2D), new HarmonyMethod(typeof(EnemyOracle), nameof(QueryHitPostfix)) },
                { typeof(Collider2D), new HarmonyMethod(typeof(EnemyOracle), nameof(QueryColliderPostfix)) },
                { typeof(RaycastHit2D[]), new HarmonyMethod(typeof(EnemyOracle), nameof(QueryHitsPostfix)) },
                { typeof(Collider2D[]), new HarmonyMethod(typeof(EnemyOracle), nameof(QueryCollidersPostfix)) },
            };
            int n = 0;
            foreach (var type in new[] { typeof(Physics2D), typeof(PhysicsScene2D) })
                foreach (var m in type.GetMethods(BindingFlags.Public | BindingFlags.Static | BindingFlags.Instance | BindingFlags.DeclaredOnly))
                {
                    if (Array.IndexOf(QueryNames, m.Name) < 0 || m.ContainsGenericParameters || !posts.TryGetValue(m.ReturnType, out var post)) continue;
                    if (m.GetParameters().Any(p => p.ParameterType.IsByRef || p.ParameterType.IsArray || p.ParameterType.IsGenericType)) continue;
                    try
                    {
                        harmony.Patch(m, prefix: prefix, postfix: post, finalizer: finalizer);
                        n++;
                    }
                    catch (Exception e) { OraclePlugin.Log.LogWarning(Tag + ": can't hook " + type.Name + "." + m.Name + ": " + e.Message); }
                }
            OraclePlugin.Log.LogInfo(Tag + ": " + n + " Physics2D query overloads hooked");
        }

        // The outermost query call (an overload that calls another is one query).
        private static void QueryPrefix(out bool __state)
        {
            __state = queryDepth == 0;
            queryDepth++;
        }

        private static Exception QueryFinalizer(Exception __exception)
        {
            if (queryDepth > 0) queryDepth--;
            return __exception;
        }

        /// The script that asked, if it's watched (and not the oracle itself repeating a query).
        private static string QueryCaller()
        {
            var st = new System.Diagnostics.StackTrace(2, false);
            var game = typeof(EnemyBehaviour).Assembly;
            var me = typeof(EnemyOracle).Assembly;
            int i = 0;
            // (The postfix's own frames first; then the patched query, Unity's own frames.)
            for (; i < st.FrameCount; i++)
            {
                var m = st.GetFrame(i).GetMethod();
                if (m == null || m.DeclaringType == null || m.DeclaringType.Assembly != me) break;
            }
            for (; i < st.FrameCount; i++)
            {
                var m = st.GetFrame(i).GetMethod();
                var t = m != null ? m.DeclaringType : null;
                if (t == null) continue;
                if (t.Assembly == me) return null; // (the oracle's own query: TurretOracle's repeated raycasts)
                if (t.Assembly != game) continue;
                var outer = t;
                while (outer.DeclaringType != null && (outer.Name.StartsWith("<") || outer.IsNested && outer.Name.Contains("<"))) outer = outer.DeclaringType;
                return watched.Contains(outer) ? outer.Name + "." + m.Name : null;
            }
            return null;
        }

        private static void WriteArgs(Json j, object[] args)
        {
            j.Key("a").Arr();
            if (args != null)
                foreach (var a in args)
                    switch (a)
                    {
                        case Vector2 v: j.Arr().Bits(v.x).Bits(v.y).EndArr(); break;
                        case Vector3 v3: j.Arr().Bits(v3.x).Bits(v3.y).EndArr(); break;
                        case float f: j.Bits(f); break;
                        case int i: j.Val(i); break;
                        case ContactFilter2D cf: j.Arr().Val(cf.useLayerMask ? cf.layerMask.value : -1).Val(cf.useTriggers).EndArr(); break;
                        case Enum e: j.Val(Convert.ToInt32(e)); break;
                        default: j.Val(a == null ? null : a.GetType().Name); break;
                    }
            j.EndArr();
        }

        private static void WriteHit(Json j, RaycastHit2D h)
        {
            if (h.collider == null)
            {
                j.Val((string)null);
                return;
            }
            j.Obj().Prop("c", PathOf(h.collider.transform)).Key("pt");
            V2(j, h.point);
            j.Key("n");
            V2(j, h.normal);
            j.Key("d").Bits(h.distance).Key("fr").Bits(h.fraction).EndObj();
        }

        private static void AddQuery(MethodBase m, object[] args, Action<Json> result)
        {
            string by = QueryCaller();
            if (by == null) return;
            var j = new Json().Obj();
            WriteArgs(j, args);
            j.Key("r");
            result(j);
            j.EndObj();
            Add(new Ev { k = "query", step = Step, other = m.DeclaringType.Name + "." + m.Name, caller = by, raw = j.ToString() });
        }

        private static bool Querying(bool outermost) => outermost && recording && biome;

        private static void QueryHitPostfix(bool __state, MethodBase __originalMethod, object[] __args, RaycastHit2D __result)
        {
            if (Querying(__state)) AddQuery(__originalMethod, __args, j => WriteHit(j, __result));
        }

        private static void QueryColliderPostfix(bool __state, MethodBase __originalMethod, object[] __args, Collider2D __result)
        {
            if (Querying(__state)) AddQuery(__originalMethod, __args, j => j.Val(__result != null ? PathOf(__result.transform) : null));
        }

        private static void QueryHitsPostfix(bool __state, MethodBase __originalMethod, object[] __args, RaycastHit2D[] __result)
        {
            if (!Querying(__state)) return;
            AddQuery(__originalMethod, __args, j =>
            {
                j.Arr();
                if (__result != null) foreach (var h in __result) WriteHit(j, h);
                j.EndArr();
            });
        }

        private static void QueryCollidersPostfix(bool __state, MethodBase __originalMethod, object[] __args, Collider2D[] __result)
        {
            if (!Querying(__state)) return;
            AddQuery(__originalMethod, __args, j =>
            {
                j.Arr();
                if (__result != null) foreach (var c in __result) j.Val(c != null ? PathOf(c.transform) : null);
                j.EndArr();
            });
        }
    }
}
