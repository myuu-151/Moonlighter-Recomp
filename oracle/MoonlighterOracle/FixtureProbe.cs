using System.Collections.Generic;
using UnityEngine;

namespace MoonlighterOracle
{
    /// Which colliders Unity made fixtures for since the last fixed step: Box2D's e_newFixture,
    /// which has the next step pair every proxy that moved (b2World::Step's FindNewContacts)
    /// before it collides, not after it solves. The replays only hold the room's enemies, the
    /// hero and the room's colliders; anything else that gets a fixture (an item's PickOnWalk
    /// trigger switched on when its drop lands, a breakable's pieces, a weapon's hitbox) changes
    /// when the others' new contacts start. tangle.jsonl, frame 406: the hero back on Default
    /// (from IntangibleHero, in frame 405's Update) is pushed apart from Tangle 2 in the step
    /// that follows, as the replay gets only with a FindNewContacts before that step; in frames
    /// 224, 282, 343 the same layer change pushes nothing (no fixture was made).
    ///
    /// Each recorded fixed step gets "fx": [made, reshaped, staticMoved, childMoved, trigger,
    /// body], counts of colliders that, since the step before (this step's recorder FixedUpdate
    /// against the last one's: what other scripts' FixedUpdates change after it lands a step
    /// late; one made and gone again between two of them isn't seen), outside the recorded
    /// enemies' hierarchies (what the replays don't hold), and "fxOwn", the same counts for the
    /// recorded enemies' own colliders (the replays make those themselves, as the port does: they
    /// are only for checking):
    ///   made         are active and enabled and weren't (new, enabled, their object activated);
    ///   reshaped     changed their shape (box size/offset/edge radius, circle radius, capsule
    ///                size/direction, polygon points, edge points, offset);
    ///   staticMoved  have no Rigidbody2D and their transform moved, turned or scaled (on the
    ///                static ground body, their shapes are in world space);
    ///   childMoved   sit below their Rigidbody2D's object and moved relative to it;
    ///   trigger      switched isTrigger;
    ///   body         belong to a Rigidbody2D whose bodyType or simulated changed.
    /// Which of these Unity makes new fixtures for isn't known for all of them ("made" surely);
    /// they're counted apart so the recordings can tell.
    internal static class FixtureProbe
    {
        private struct State
        {
            public bool on, trigger, hasBody, simulated, own;
            public int bodyType;
            public string shape, place;
        }

        private static Dictionary<int, State> last;
        private static readonly List<int[]> steps = new List<int[]>();
        private static System.Func<IEnumerable<Transform>> recorded; // the recorded enemies' roots

        /// A recording starts: the colliders now are what its first step is compared with.
        /// `enemies`: the recorded enemies' root transforms (their colliders count in "fxOwn").
        internal static void Reset(System.Func<IEnumerable<Transform>> enemies)
        {
            recorded = enemies;
            last = TakeAll();
            steps.Clear();
        }

        internal static void Clear() => steps.Clear();

        /// From a recorder's FixedUpdate, once per fixed step.
        internal static void OnStep()
        {
            Dictionary<int, State> now = TakeAll();
            var fx = new int[12]; // [0, 6): the others', [6, 12): the recorded enemies' own
            if (last != null)
                foreach (var kv in now)
                {
                    State s = kv.Value;
                    if (!s.on) continue;
                    int o = s.own ? 6 : 0;
                    if (!last.TryGetValue(kv.Key, out State p) || !p.on)
                    {
                        fx[o]++;
                        continue;
                    }
                    if (s.shape != p.shape) fx[o + 1]++;
                    if (!s.hasBody && s.place != p.place) fx[o + 2]++;
                    if (s.hasBody && s.place != p.place) fx[o + 3]++;
                    if (s.trigger != p.trigger) fx[o + 4]++;
                    if (s.hasBody != p.hasBody || s.bodyType != p.bodyType || s.simulated != p.simulated) fx[o + 5]++;
                }
            last = now;
            steps.Add(fx);
        }

        /// Step k's "fx" into its object in a frame's "steps".
        internal static void WriteStep(Json j, int k)
        {
            if (k < 0 || k >= steps.Count) return;
            j.Key("fx").Arr();
            for (int n = 0; n < 6; n++) j.Val(steps[k][n]);
            j.EndArr();
            j.Key("fxOwn").Arr();
            for (int n = 6; n < 12; n++) j.Val(steps[k][n]);
            j.EndArr();
        }

        private static Dictionary<int, State> TakeAll()
        {
            var all = new Dictionary<int, State>();
            var roots = new List<Transform>();
            if (recorded != null)
                foreach (Transform r in recorded())
                    if (r != null) roots.Add(r);
            foreach (Collider2D c in Object.FindObjectsOfType<Collider2D>())
                if (c != null)
                {
                    State s = Snapshot(c);
                    foreach (Transform r in roots) s.own |= c.transform.IsChildOf(r);
                    all[c.GetInstanceID()] = s;
                }
            return all;
        }

        private static State Snapshot(Collider2D c)
        {
            var s = new State { on = c.isActiveAndEnabled, trigger = c.isTrigger };
            Rigidbody2D rb = c.attachedRigidbody;
            s.hasBody = rb != null;
            if (rb != null)
            {
                s.bodyType = (int)rb.bodyType;
                s.simulated = rb.simulated;
            }
            s.shape = Shape(c);
            s.place = rb == null ? World(c.transform) : Relative(c.transform, rb.transform);
            return s;
        }

        private static string F(float f) => System.BitConverter.ToInt32(System.BitConverter.GetBytes(f), 0).ToString();
        private static string V(Vector2 v) => F(v.x) + "," + F(v.y);
        private static string V(Vector3 v) => F(v.x) + "," + F(v.y) + "," + F(v.z);
        private static string Q(Quaternion q) => F(q.x) + "," + F(q.y) + "," + F(q.z) + "," + F(q.w);

        private static string Shape(Collider2D c)
        {
            string s = V(c.offset);
            switch (c)
            {
                case BoxCollider2D b: return s + "|b" + V(b.size) + "," + F(b.edgeRadius);
                case CircleCollider2D r: return s + "|c" + F(r.radius);
                case CapsuleCollider2D p: return s + "|p" + V(p.size) + "," + (int)p.direction;
                case PolygonCollider2D g:
                    {
                        var sb = new System.Text.StringBuilder(s + "|g" + g.pathCount);
                        for (int i = 0; i < g.pathCount; i++)
                            foreach (Vector2 v in g.GetPath(i)) sb.Append(';').Append(V(v));
                        return sb.ToString();
                    }
                case EdgeCollider2D e:
                    {
                        var sb = new System.Text.StringBuilder(s + "|e" + F(e.edgeRadius));
                        foreach (Vector2 v in e.points) sb.Append(';').Append(V(v));
                        return sb.ToString();
                    }
                default: return s + "|" + c.GetType().Name;
            }
        }

        private static string World(Transform t) => V(t.position) + "|" + Q(t.rotation) + "|" + V(t.lossyScale);

        // The local transforms from the collider's object up to (not including) its body's.
        private static string Relative(Transform t, Transform body)
        {
            if (t == body) return "";
            var sb = new System.Text.StringBuilder();
            for (Transform x = t; x != null && x != body; x = x.parent)
                sb.Append(V(x.localPosition)).Append('|').Append(Q(x.localRotation)).Append('|').Append(V(x.localScale)).Append(';');
            return sb.ToString();
        }
    }
}
