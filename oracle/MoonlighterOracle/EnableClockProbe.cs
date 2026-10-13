using System.Collections.Generic;
using HarmonyLib;
using UnityEngine;

namespace MoonlighterOracle
{
    /// What the enemy recordings' replays can't tell from "enableTime" alone: which clock the
    /// targeting's Invoke (EnableEnemies -> StartTargetingPlayerAfterTime) counted from. In the
    /// recordings of 2026-10-12 22:00, EnableEnemies read a Time.time between fixed steps (it ran
    /// in a frame), yet the targeting started a second after the fixed step before it
    /// (enemy_GolemSoldier.jsonl, tangle.jsonl; recomp/tests/enable_clock.h).
    ///
    /// Written into the start line as "enableClock" (enemy_*.jsonl, slime.jsonl, tangle.jsonl):
    ///   - "enter": DungeonRoom.EnterRoom's clocks (it Invokes EnableEnemies "appearDelay" later):
    ///     Time.time, Time.fixedTime, Time.inFixedTimeStep, Time.frameCount;
    ///   - "enable": the same at EnableEnemies, and Time.unscaledTime;
    ///   - "probes": Invokes made right after EnableEnemies, from the same pass, on a probe
    ///     object: each one's delay and, when it ran (before the recording starts), the same
    ///     clocks. The delays: 0; and fixedDeltaTime minus half the gap from Time.fixedTime to
    ///     Time.time (counted from the fixed step it runs in the next step's pass; counted from
    ///     Time.time it can't). The targeting itself is the frame's "target" event.
    /// The probe objects are the oracle's, not the game's: nothing of the game draws Random or
    /// runs differently for them.
    internal static class EnableClockProbe
    {
        internal static DungeonRoom Target; // the room the oracle goes into
        private static float[] enter, enable;
        private static readonly List<Probe> probes = new List<Probe>();

        private class Probe
        {
            public float delay;
            public float[] fired; // time, fixedTime, inFixedTimeStep, frameCount
        }

        private class ProbeBehaviour : MonoBehaviour
        {
            public Probe probe;
            private void Fire() { probe.fired = Clocks(); }
        }

        private static float[] Clocks() => new[] { Time.time, Time.fixedTime, Time.inFixedTimeStep ? 1f : 0f, Time.frameCount, Time.unscaledTime };

        internal static void Begin(DungeonRoom room)
        {
            Target = room;
            enter = enable = null;
            probes.Clear();
        }

        [HarmonyPatch(typeof(DungeonRoom), "EnterRoom")]
        private static class EnterHook
        {
            private static void Postfix(DungeonRoom __instance)
            {
                if (__instance == Target && Target != null) enter = Clocks();
            }
        }

        // From the oracles' EnableEnemies postfix (the target room's).
        internal static void OnEnable(DungeonRoom room)
        {
            if (room != Target || enable != null) return;
            enable = Clocks();
            float gap = Time.time - Time.fixedTime;
            var delays = new List<float> { 0f, Time.fixedDeltaTime - 0.5f * gap };
            foreach (float d in delays)
            {
                var go = new GameObject("OracleInvokeProbe");
                Object.DontDestroyOnLoad(go);
                var p = new Probe { delay = d };
                var b = go.AddComponent<ProbeBehaviour>();
                b.probe = p;
                probes.Add(p);
                b.Invoke("Fire", d);
            }
        }

        internal static void Write(Json j)
        {
            j.Key("enableClock").Obj();
            Clock(j, "enter", enter);
            Clock(j, "enable", enable);
            j.Key("probes").Arr();
            foreach (var p in probes)
            {
                j.Obj().Key("delay").Bits(p.delay);
                Clock(j, "fired", p.fired);
                j.EndObj();
            }
            j.EndArr().EndObj();
        }

        private static void Clock(Json j, string key, float[] c)
        {
            j.Key(key);
            if (c == null) { j.Val((string)null); return; }
            j.Obj().Key("time").Bits(c[0]).Key("fixedTime").Bits(c[1]).Prop("inFixedStep", c[2] != 0f).Prop("frame", (int)c[3]).Key("unscaledTime").Bits(c[4]).EndObj();
        }
    }
}
