using UnityEngine;

namespace MoonlighterOracle
{
    /// How Unity cut a PolygonCollider2D into Box2D's convex polygons, which no API of Unity
    /// 2019.2 hands out (Collider2D.GetShapes is 2021.2's). Added to the collider's "collider"
    /// line:
    ///   "shapeCount": Collider2D.shapeCount, the number of pieces;
    ///   "rays": from points inside it (a 6-unit grid from its bounds' corner + 3, those
    ///     OverlapPoint finds in it), in 8 directions (0: +x, then 45 degrees counterclockwise
    ///     each), as far as its bounds' diagonal, Physics2D.RaycastNonAlloc with
    ///     queriesStartInColliders off, on its layer: the hits on this collider, each ray as
    ///     [origin x, y, direction x, y, distance, [fraction, point x, y, normal x, y], ...] (rays
    ///     without one are left out). A ray starting inside a piece doesn't hit that piece; if
    ///     Unity drops only that piece's hit (per fixture), the hits are where the ray enters the
    ///     collider's other pieces: the cuts between pieces, which the port's cut
    ///     (recomp/src/unity/PolygonCollider2D.h) must reproduce (recomp/tests/polygon_probe.h).
    ///     If Unity drops the whole collider, there are none and only shapeCount tells.
    /// Physics2D.queriesStartInColliders and queriesHitTriggers are put back right after; no game
    /// script runs in between.
    internal static class PolygonProbe
    {
        internal static void Write(Json j, PolygonCollider2D pc)
        {
            j.Prop("shapeCount", pc.shapeCount);
            if (!pc.isActiveAndEnabled) return;
            bool startIn = Physics2D.queriesStartInColliders, hitTriggers = Physics2D.queriesHitTriggers;
            Physics2D.queriesStartInColliders = false;
            Physics2D.queriesHitTriggers = true;
            try
            {
                Bounds b = pc.bounds;
                float reach = Vector2.Distance(b.min, b.max);
                int mask = 1 << pc.gameObject.layer;
                var hits = new RaycastHit2D[64];
                var dirs = new Vector2[8];
                for (int d = 0; d < 8; d++)
                {
                    float a = d * Mathf.PI / 4f;
                    dirs[d] = new Vector2(Mathf.Cos(a), Mathf.Sin(a));
                }
                dirs[0] = Vector2.right; dirs[2] = Vector2.up; dirs[4] = Vector2.left; dirs[6] = Vector2.down;
                j.Key("rays").Arr();
                for (float y = b.min.y + 3f; y < b.max.y; y += 6f)
                    for (float x = b.min.x + 3f; x < b.max.x; x += 6f)
                    {
                        var o = new Vector2(x, y);
                        if (!pc.OverlapPoint(o)) continue;
                        for (int d = 0; d < 8; d++)
                        {
                            int n = Physics2D.RaycastNonAlloc(o, dirs[d], hits, reach, mask);
                            bool any = false;
                            for (int i = 0; i < n; i++)
                            {
                                if (hits[i].collider != pc) continue;
                                if (!any) j.Arr().Bits(o.x).Bits(o.y).Bits(dirs[d].x).Bits(dirs[d].y).Bits(reach);
                                any = true;
                                j.Arr().Bits(hits[i].fraction).Bits(hits[i].point.x).Bits(hits[i].point.y).Bits(hits[i].normal.x).Bits(hits[i].normal.y).EndArr();
                            }
                            if (any) j.EndArr();
                        }
                    }
                j.EndArr();
            }
            finally
            {
                Physics2D.queriesStartInColliders = startIn;
                Physics2D.queriesHitTriggers = hitTriggers;
            }
        }
    }
}
