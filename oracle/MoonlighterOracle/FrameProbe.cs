using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using HarmonyLib;
using UnityEngine;

namespace MoonlighterOracle
{
    /// Who draws from UnityEngine.Random during the frames generation yields to (window
    /// "generation": before the first room and between rooms; "bossStart": the boss room's
    /// Start frame on): every Awake/Start/OnEnable/Update/... of the game's MonoBehaviours and
    /// StateMachineBehaviours is wrapped, and while a window is open each call that moved the
    /// RNG is logged ("frameDraw": type, method, object, draws, the nesting depth, so nested
    /// calls aren't counted twice, and "gen": made inside the generation's own coroutine).
    /// Random's own methods are engine internals Harmony can't wrap; this finds their callers
    /// instead.
    internal static class FrameProbe
    {
        private static readonly string[] Methods =
        {
            "Awake", "Start", "OnEnable", "Update", "LateUpdate", "FixedUpdate",
            "OnStateEnter", "OnStateUpdate", "OnStateExit", "OnStateMachineEnter",
        };

        public static string Window; // the open window's name, or null
        private static int depth;

        public static void Install(Harmony harmony)
        {
            var prefix = new HarmonyMethod(typeof(FrameProbe), nameof(Prefix));
            var postfix = new HarmonyMethod(typeof(FrameProbe), nameof(Postfix));
            int patched = 0, failed = 0;
            Type[] types;
            try { types = typeof(DungeonGenerator).Assembly.GetTypes(); }
            catch (ReflectionTypeLoadException e) { types = e.Types.Where(t => t != null).ToArray(); }
            foreach (Type t in types)
            {
                if (t.IsGenericTypeDefinition || !(typeof(MonoBehaviour).IsAssignableFrom(t) || typeof(StateMachineBehaviour).IsAssignableFrom(t)))
                    continue;
                foreach (MethodInfo m in t.GetMethods(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly))
                {
                    if (!Methods.Contains(m.Name) || m.IsAbstract || m.ContainsGenericParameters || m.GetMethodBody() == null) continue;
                    try
                    {
                        harmony.Patch(m, prefix, postfix);
                        patched++;
                    }
                    catch (Exception)
                    {
                        failed++;
                    }
                }
            }
            OraclePlugin.Log.LogInfo($"frame probe: {patched} methods wrapped ({failed} could not be)");
            HookGenerator(harmony);
        }

        private static void Prefix(out uint[] __state)
        {
            __state = null;
            if (Window == null) return;
            __state = Rng.Get();
            depth++;
        }

        private static void Postfix(object __instance, MethodBase __originalMethod, uint[] __state)
        {
            if (__state == null) return;
            depth--;
            uint[] after = Rng.Get();
            if (after[0] == __state[0] && after[1] == __state[1] && after[2] == __state[2] && after[3] == __state[3]) return;
            Trace.Safe("frameDraw", () =>
            {
                var j = Trace.Ev("frameDraw").Prop("window", Window).Prop("type", __originalMethod.DeclaringType?.FullName)
                    .Prop("method", __originalMethod.Name).Prop("depth", depth).Prop("gen", inGenerator > 0).Prop("object", PathOf(__instance))
                    .Prop("draws", DrawsBetween(__state, after, 100000));
                Rng.WriteState(j.Key("before"), __state);
                Trace.Emit(Trace.State(j, "after"));
            });
        }

        private static string PathOf(object instance)
        {
            Transform t = (instance as Component)?.transform;
            if (t == null) return instance is StateMachineBehaviour ? "(state machine)" : null;
            var parts = new List<string>();
            for (int i = 0; t != null && i < 6; i++, t = t.parent) parts.Insert(0, t.name);
            return string.Join("/", parts);
        }

        /// xorshift128 steps from one state to another (UnityEngine.Random), or -1.
        public static int DrawsBetween(uint[] from, uint[] to, int max)
        {
            uint x = from[0], y = from[1], z = from[2], w = from[3];
            for (int n = 0; n <= max; n++)
            {
                if (x == to[0] && y == to[1] && z == to[2] && w == to[3]) return n;
                uint t = x ^ (x << 11);
                x = y; y = z; z = w;
                w = w ^ (w >> 19) ^ t ^ (t >> 8);
            }
            return -1;
        }

        // The frames the generation yields to, from the cave's end (GeneratChestRooms, where
        // cave_game.jsonl takes "stateAfter") to the boss room's window or the end: the bundles'
        // loading before the first room and a corrupted room's, and each room's
        // `yield return new WaitForEndOfFrame()`. Other scripts' Updates run there and can draw
        // (trace.jsonl, session 20261012-215722, the old walk_fall with the town's scene still
        // loaded: 104 draws before the first room, then some between rooms; floor_sync_test's
        // TraceEngine::FramePasses). The generation's own draws in those windows (an object's
        // Awake or OnEnable in an Instantiate it makes) come from inside its coroutine: "gen".
        // Opening it writes "frameWindow" (window "generation") into the floor's trace: a trace
        // with it has every other script's draw in those frames, and the test takes no resync there.
        [HarmonyPatch(typeof(DungeonGenerator), "GeneratChestRooms")]
        private static class GenerationOpens
        {
            private static void Postfix()
            {
                if (Window != null || CaveOracle.inMirror) return;
                Window = "generation";
                Trace.Safe("frameWindow", () => Trace.Emit(Trace.Ev("frameWindow").Prop("window", Window)));
            }
        }

        // Inside one of DungeonGenerator's coroutines (their MoveNext: GenerateDungeonCoroutine,
        // the bundles' waits it starts are other classes'): the draws there are the generation's.
        private static int inGenerator;
        private static void GeneratorPrefix() => inGenerator++;
        private static void GeneratorFinalizer() => inGenerator--;

        private static void HookGenerator(Harmony harmony)
        {
            int n = 0;
            foreach (Type t in typeof(DungeonGenerator).GetNestedTypes(BindingFlags.NonPublic | BindingFlags.Public))
            {
                if (!typeof(System.Collections.IEnumerator).IsAssignableFrom(t)) continue;
                MethodInfo m = t.GetMethod("MoveNext", BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
                if (m == null) continue;
                try
                {
                    harmony.Patch(m, prefix: new HarmonyMethod(typeof(FrameProbe), nameof(GeneratorPrefix)),
                                  finalizer: new HarmonyMethod(typeof(FrameProbe), nameof(GeneratorFinalizer)));
                    n++;
                }
                catch (Exception e)
                {
                    OraclePlugin.Log.LogWarning($"frame probe: can't hook {t.Name}.MoveNext: {e.Message}");
                }
            }
            OraclePlugin.Log.LogInfo($"frame probe: {n} generator coroutines hooked");
        }

        // The boss room's Start frame: from AddBossRoom to SpawnSpecialRoomsAfterGeneration.
        [HarmonyPatch(typeof(DungeonGenerator), "AddBossRoom")]
        private static class BossRoomOpens
        {
            private static void Postfix() => Window = "bossStart";
        }

        [HarmonyPatch(typeof(DungeonGenerator), "SpawnSpecialRoomsAfterGeneration")]
        private static class BossRoomCloses
        {
            private static void Prefix() => Window = null;
        }
    }
}
