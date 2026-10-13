using System.Collections.Generic;
using UnityEngine;

namespace MoonlighterOracle
{
    /// The room's critters for `-oracle-enemy` (every prefab; EnemyOracle calls this): the Buggies
    /// (Wormy, Beetly, ...) that DungeonRoom.FillRoomWithCreatures scatters. Each crawls about by
    /// its own DOMove (Buggies.Move: insideUnitCircle, a raycast, a DOMove with its ease, a
    /// DOVirtual.DelayedCall of Range(min, max) between moves), on a Dynamic Rigidbody2D with a
    /// trigger BoxCollider2D (layer Enemy), and an enemy's attack trigger enters that box
    /// (enemy_FlyingKamikazeGolem.jsonl, frame 466: OnTriggerEnter2D on Wormy(Clone)). The replay
    /// doesn't move them (tests/enemy_replay_critters.h): it places their boxes where they were.
    ///   - the start's "critters": each Buggies below the room (GetComponentsInChildren order,
    ///     inactive ones too): its path, its Rigidbody2D's mass, its own BoxCollider2D's layer,
    ///     isTrigger, enabled, offset and size, the transform's position, lossyScale and z
    ///     rotation (degrees);
    ///   - a frame's "csteps": per fixed step (as "steps"), each critter at the step's start (the
    ///     transform the step's sync gives its body): [index, x, y, activeInHierarchy, the box
    ///     enabled, z rotation, lossyScale x]; a destroyed one: [index, 0, 0, false, false, 0, 1].
    internal static class CritterOracle
    {
        private static bool active;
        private static readonly List<Buggies> critters = new List<Buggies>();
        private static readonly List<List<string>> stepRows = new List<List<string>>();

        private static Json V2(Json j, Vector2 v) => j.Arr().Bits(v.x).Bits(v.y).EndArr();

        private static string PathOf(Transform t)
        {
            string p = t.name;
            for (t = t.parent; t != null; t = t.parent) p = t.name + "/" + p;
            return p;
        }

        public static void Begin(DungeonRoom room)
        {
            critters.Clear();
            stepRows.Clear();
            active = room != null;
            if (!active) return;
            critters.AddRange(room.GetComponentsInChildren<Buggies>(true));
        }

        public static void End() => active = false;

        public static void Clear() => stepRows.Clear();

        /// The start line's "critters".
        public static void WriteStart(Json j)
        {
            if (!active) return;
            j.Key("critters").Arr();
            foreach (var c in critters)
            {
                j.Obj();
                if (c == null) { j.Prop("gone", true).EndObj(); continue; }
                var t = c.transform;
                var rb = c.GetComponent<Rigidbody2D>();
                var box = c.GetComponent<BoxCollider2D>();
                j.Prop("path", PathOf(t)).Key("mass").Bits(rb != null ? rb.mass : 0f)
                    .Prop("body", rb != null ? rb.bodyType.ToString() : "none")
                    .Prop("layer", box != null ? box.gameObject.layer : c.gameObject.layer)
                    .Prop("trigger", box != null && box.isTrigger).Prop("enabled", box != null && box.enabled)
                    .Prop("active", c.gameObject.activeInHierarchy);
                j.Key("offset"); V2(j, box != null ? box.offset : Vector2.zero);
                j.Key("size"); V2(j, box != null ? box.size : Vector2.zero);
                j.Key("pos"); V2(j, t.position);
                j.Key("scale"); V2(j, t.lossyScale);
                j.Key("rot").Bits(t.eulerAngles.z);
                j.EndObj();
            }
            j.EndArr();
        }

        /// A fixed step's start (EnemyOracle's Recorder.FixedUpdate).
        public static void OnStep()
        {
            if (!active || !EnemyOracle.Recording) return;
            var rows = new List<string>();
            for (int i = 0; i < critters.Count; i++)
            {
                var c = critters[i];
                var j = new Json().Arr().Val(i);
                if (c == null)
                {
                    j.Bits(0f).Bits(0f).Val(false).Val(false).Bits(0f).Bits(1f).EndArr();
                    rows.Add(j.ToString());
                    continue;
                }
                var t = c.transform;
                var box = c.GetComponent<BoxCollider2D>();
                Vector2 p = t.position;
                j.Bits(p.x).Bits(p.y).Val(c.gameObject.activeInHierarchy).Val(box != null && box.enabled).Bits(t.eulerAngles.z)
                    .Bits(t.lossyScale.x).EndArr();
                rows.Add(j.ToString());
            }
            stepRows.Add(rows);
        }

        /// A frame's "csteps".
        public static void WriteFrame(Json j)
        {
            if (!active || critters.Count == 0) return;
            j.Key("csteps").Arr();
            foreach (var rows in stepRows)
            {
                j.Arr();
                foreach (var r in rows) j.Raw(r);
                j.EndArr();
            }
            j.EndArr();
        }
    }
}
