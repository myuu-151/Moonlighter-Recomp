using BepInEx.Logging;
using UnityEngine;

namespace MoonlighterOracle
{
    /// `-oracle-walk=rocks`: a rock beside Will in the floor's first room (a copy of one of the
    /// floor's own GolemRocks, Kinematic Rigidbody2D and 24x24 BoxCollider2D on RisenObstacle,
    /// on a tile of the room's grid as DungeonRoomGenerator.GenerateRockObjects places them),
    /// then walks and rolls into it: straight at each face (his casts stop him), diagonally into
    /// each corner and grazing them (where the casts let him in and Box2D pushes him out), and a
    /// long push. The first room has no enemies to interfere; the rock's own room would.
    ///
    /// The approaches start from points around the rock: "go" segments steer the stick there
    /// (around the rock, never through it) and let go on arrival; what the stick did is
    /// recorded like any other frame's.
    internal static partial class Walk
    {
        private static Vector2 rockAt;           // the rock's centre (its transform)
        private static string rockFrom = "";     // the GolemRock copied, or "synthetic"
        private static Vector2 lastStick;        // the stick the script gave this frame

        private const float D = 0.7071068f;

        private static Segment Go(float dx, float dy, int frames = 80) => new Segment(frames, dx, dy) { toward = true };

        // His collider's centre relative to the rock's.
        private static readonly Segment[] RocksScript =
        {
            new Segment(20, 0f, 0f),
            Go(-50f, 0f), new Segment(10, 0f, 0f), new Segment(30, 1f, 0f),      // the left face, straight on
            Go(-45f, 45f), new Segment(10, 0f, 0f), new Segment(40, D, -D),      // the top-left corner, diagonally
            Go(0f, 50f), new Segment(10, 0f, 0f), new Segment(30, 0f, -1f),      // the top face
            Go(45f, 45f), new Segment(10, 0f, 0f), new Segment(40, -D, -D),      // the top-right corner
            Go(50f, 0f), new Segment(10, 0f, 0f), new Segment(30, -1f, 0f),      // the right face
            Go(45f, -45f), new Segment(10, 0f, 0f), new Segment(40, -D, D),      // the bottom-right corner
            Go(0f, -50f), new Segment(10, 0f, 0f), new Segment(30, 0f, 1f),      // the bottom face
            Go(-45f, -45f), new Segment(10, 0f, 0f), new Segment(40, D, D),      // the bottom-left corner
            Go(-50f, -14f), new Segment(10, 0f, 0f), new Segment(40, 1f, 0.4f),  // mostly right, low on the face
            Go(-45f, 40f), new Segment(10, 0f, 0f), new Segment(40, D, -D),      // diagonals off the corner
            Go(-40f, 45f), new Segment(10, 0f, 0f), new Segment(40, D, -D),
            Go(-60f, 0f), new Segment(20, 0f, 0f), new Segment(50, 1f, 0f, Roll), // rolls: straight on
            Go(-50f, 50f), new Segment(20, 0f, 0f), new Segment(50, D, -D, Roll), // into a corner
            Go(0f, 60f), new Segment(20, 0f, 0f), new Segment(50, 0f, -1f, Roll),
            Go(55f, -55f), new Segment(20, 0f, 0f), new Segment(50, -D, D, Roll),
            Go(-50f, 0f), new Segment(10, 0f, 0f), new Segment(90, 1f, 0f),      // a long push,
            new Segment(90, 0f, 0f),                                             // then standing against it
        };

        /// The stick for a segment: its own, or toward the rock's centre + (x, y) for "go".
        private static Vector2 StickFor(Segment s)
        {
            if (!s.toward) return new Vector2(s.x, s.y);
            var ctrl = HeroMerchant.Instance.heroMerchantController;
            Vector2 h = (Vector2)ctrl.transform.position + ctrl.characterCollider.offset;
            Vector2 target = rockAt + new Vector2(s.x, s.y);
            if ((target - h).magnitude < 2.5f) return Vector2.zero;
            return (Waypoint(h - rockAt, target - rockAt) + rockAt - h).normalized;
        }

        // Clear of the rock: its half size, his and a margin.
        private const float Keep = 12f + 6f + 4f, Corner = Keep + 8f;

        /// The next point on the way from `h` to `t` (both relative to the rock) not crossing it:
        /// straight, or through one or two of the corners around it; off it first if he's at it.
        private static Vector2 Waypoint(Vector2 h, Vector2 t)
        {
            if (Mathf.Abs(h.x) < Keep && Mathf.Abs(h.y) < Keep)
                return Mathf.Abs(h.x) >= Mathf.Abs(h.y) ? new Vector2(Mathf.Sign(h.x) * Corner, h.y) : new Vector2(h.x, Mathf.Sign(h.y) * Corner);
            if (!Crosses(h, t)) return t;
            Vector2[] corners = { new Vector2(-Corner, -Corner), new Vector2(Corner, -Corner), new Vector2(Corner, Corner), new Vector2(-Corner, Corner) };
            Vector2 best = t;
            float bestLength = float.MaxValue;
            for (int i = 0; i < 4; i++)
            {
                Vector2 a = corners[i];
                if (Crosses(h, a)) continue;
                if (!Crosses(a, t) && (h - a).magnitude + (a - t).magnitude < bestLength)
                {
                    bestLength = (h - a).magnitude + (a - t).magnitude;
                    best = a;
                }
                foreach (int k in new[] { (i + 1) % 4, (i + 3) % 4 })
                {
                    Vector2 b = corners[k];
                    float length = (h - a).magnitude + (a - b).magnitude + (b - t).magnitude;
                    if (!Crosses(b, t) && length < bestLength)
                    {
                        bestLength = length;
                        best = a;
                    }
                }
            }
            return best;
        }

        /// Whether the segment a-b passes through the rock's keep-out box.
        private static bool Crosses(Vector2 a, Vector2 b)
        {
            float t0 = 0f, t1 = 1f;
            Vector2 d = b - a;
            for (int k = 0; k < 2; k++)
            {
                float o = k == 0 ? a.x : a.y, v = k == 0 ? d.x : d.y;
                if (Mathf.Abs(v) < 1e-6f)
                {
                    if (o <= -Keep || o >= Keep) return false;
                    continue;
                }
                float lo = (-Keep - o) / v, hi = (Keep - o) / v;
                if (lo > hi) { float s = lo; lo = hi; hi = s; }
                t0 = Mathf.Max(t0, lo);
                t1 = Mathf.Min(t1, hi);
                if (t0 >= t1) return false;
            }
            return true;
        }

        /// The rock: on the first free tile of the room's grid about three tiles right (else
        /// left) of Will, with room to walk around it. False if there's no such tile.
        private static bool PlaceRock(ManualLogSource log)
        {
            var hero = HeroMerchant.Instance;
            var ctrl = hero.heroMerchantController;
            var room = DungeonManager.Instance.currentDungeonRoom;
            Vector2 h = (Vector2)ctrl.transform.position + ctrl.characterCollider.offset;
            // GenerateRockObjects' grid (room-local tile corners at num2 + i * tile, num3 + j * tile).
            int tile = Constants.GetInt("tileSize");
            int x0 = -((room.roomFloorWidth - room.roomFloorWidth % tile - tile) / 2);
            int y0 = -((room.roomFloorHeight - room.roomFloorHeight % tile - tile) / 2);
            Vector2 origin = room.transform.position;
            var heroBody = hero.GetComponent<Rigidbody2D>();
            Vector2 local = Vector2.zero;
            bool found = false;
            foreach (float dx in new[] { 72f, -72f, 96f, -96f })
            {
                int i = Mathf.RoundToInt((h.x + dx - origin.x - x0) / tile), j = Mathf.RoundToInt((h.y - origin.y - y0) / tile);
                if (i < 0 || j < 0 || i >= room.gridSlicesX || j >= room.gridSlicesY) continue;
                local = new Vector2(x0 + i * tile, y0 + j * tile);
                bool clear = true;
                foreach (var c in Physics2D.OverlapBoxAll(origin + local, new Vector2(24f + 2f * 66f, 24f + 2f * 66f), 0f))
                    if (!c.isTrigger && (heroBody == null || c.attachedRigidbody != heroBody)) clear = false;
                if (clear)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                log.LogError("walk: no clear tile for the rock beside Will");
                return false;
            }

            GameObject template = null;
            foreach (var r in Resources.FindObjectsOfTypeAll<RockRandomSprite>())
            {
                var go = r.gameObject;
                var box = go.GetComponent<BoxCollider2D>();
                var body = go.GetComponent<Rigidbody2D>();
                if (!go.scene.IsValid() || go.layer != 8 || box == null || body == null || body.bodyType != RigidbodyType2D.Kinematic) continue;
                if (box.size != new Vector2(24f, 24f) || box.offset != Vector2.zero) continue;
                template = go;
                break;
            }
            GameObject rock;
            if (template != null)
            {
                rock = Object.Instantiate(template);
                rockFrom = template.name;
            }
            else
            {
                rock = new GameObject();
                rock.layer = 8;
                var body = rock.AddComponent<Rigidbody2D>();
                body.bodyType = RigidbodyType2D.Kinematic;
                body.sleepMode = RigidbodySleepMode2D.StartAwake;
                rock.AddComponent<BoxCollider2D>().size = new Vector2(24f, 24f);
                rockFrom = "synthetic";
            }
            rock.name = "OracleRock";
            rock.transform.parent = room.transform;
            rock.transform.localPosition = local;
            rock.transform.localRotation = Quaternion.identity;
            rock.transform.localScale = Vector3.one;
            rock.SetActive(true); // (its body follows the transform at the next physics step)
            rockAt = rock.transform.position;
            log.LogInfo($"walk: rock ({rockFrom}) at {rockAt}, Will's collider at {h}");
            return true;
        }
    }
}
