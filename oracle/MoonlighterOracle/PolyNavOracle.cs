using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using HarmonyLib;
using UnityEngine;

namespace MoonlighterOracle
{
    /// `-oracle-polynav[=<culture>]`: load the first save slot like the harvest (saving and stats
    /// blocked), take the culture's gate (Golem by default) on a fixed seed, then enter every
    /// room of the floor that has enemies the way a door does (DungeonManager.ChangeRoomTo, the
    /// hero teleported to the door's arrival point) and record the PolyNav2D map the game made
    /// for it into polynav.jsonl, one line per room:
    ///   - the PolyNav2D (position, scale, rotation, box size and offset, inflateRadius);
    ///   - the room's hierarchy (preorder, paths below the room; local transforms; each
    ///     PolyNavObstacle's fields and its object's first Collider2D) and its obstaclesGrid;
    ///   - navObstacles in order: path, fields, `points`, PolyNav2D.InflatePolygon of them, the
    ///     transform chain up to the root;
    ///   - the map: master polygons, obstacle polygons, the nodes with their links;
    ///   - probes on a 24-unit grid over the room: PointIsValid, GetCloserEdgePoint, CheckLOS
    ///     from the room's centre;
    ///   - FindPath between fixed points (relative to the centre), with the frames the result
    ///     took (0: answered inside the call).
    /// Game time is stopped (timeScale 0) once the floor is up: the rooms' EnableEnemies
    /// (an Invoke) never comes, so nothing moves or asks for paths; LateUpdate and coroutines
    /// still run. The room left behind is disabled at once (its DisableRoom, which the game
    /// invokes half a second later on game time), which regenerates the map as the game does.
    internal static class PolyNavOracle
    {
        private static StreamWriter writer;

        private static Json V2(Json j, Vector2 v) => j.Arr().Bits(v.x).Bits(v.y).EndArr();

        private static Json Flat(Json j, IEnumerable<Vector2> pts)
        {
            j.Arr();
            if (pts != null)
                foreach (var p in pts) j.Bits(p.x).Bits(p.y);
            return j.EndArr();
        }

        private static string RelPath(Transform t, Transform root)
        {
            string p = t.name;
            for (t = t.parent; t != null && t != root; t = t.parent) p = t.name + "/" + p;
            return t == root ? p : null;
        }

        private static string FullPath(Transform t)
        {
            string p = t.name;
            for (t = t.parent; t != null; t = t.parent) p = t.name + "/" + p;
            return p;
        }

        // 2D rotation in degrees as tools/extract/prefabs.py computes it from the quaternion.
        private static float Degrees(Quaternion q) => (float)(2.0 * Math.Atan2(q.z, q.w) * 180.0 / Math.PI);

        private static void WriteCollider(Json j, Collider2D c)
        {
            if (c == null) { j.Val((string)null); return; }
            j.Arr().Val(c.GetType().Name).Val(c.enabled).Val(c.isTrigger).Bits(c.offset.x).Bits(c.offset.y);
            if (c is BoxCollider2D b) j.Bits(b.size.x).Bits(b.size.y);
            else if (c is CircleCollider2D ci) j.Bits(ci.radius);
            else if (c is PolygonCollider2D pc)
            {
                j.Arr();
                for (int i = 0; i < pc.pathCount; i++) Flat(j, pc.GetPath(i));
                j.EndArr();
            }
            j.EndArr();
        }

        private static void Rows(Json j, Transform t, string path)
        {
            foreach (Transform child in t)
            {
                string p = path == "" ? child.name : path + "/" + child.name;
                var q = child.localRotation;
                j.Arr().Val(p).Val(child.gameObject.activeSelf).Bits(child.localPosition.x).Bits(child.localPosition.y)
                    .Bits(child.localScale.x).Bits(child.localScale.y).Bits(Degrees(q)).Bits(q.z).Bits(q.w);
                var o = child.GetComponent<PolyNavObstacle>();
                if (o != null)
                {
                    j.Arr().Val(o.invertPolygon).Val((int)o.shapeType).Bits(o.extraOffset).Val(o.enabled).EndArr();
                    WriteCollider(j, child.GetComponent<Collider2D>());
                }
                else j.Val((string)null).Val((string)null);
                j.EndArr();
                Rows(j, child, p);
            }
        }

        private static readonly Vector2[] Points =
        {
            new Vector2(-200f, -100f), new Vector2(200f, 100f), new Vector2(-200f, 100f), new Vector2(200f, -100f),
            new Vector2(0f, -110f), new Vector2(0f, 110f), new Vector2(-225f, 0f), new Vector2(225f, 0f),
            new Vector2(-100f, 40f), new Vector2(100f, -40f), new Vector2(-30f, -60f), new Vector2(30f, 60f),
        };
        private static readonly int[][] Pairs =
        {
            new[] { 0, 1 }, new[] { 2, 3 }, new[] { 4, 5 }, new[] { 6, 7 }, new[] { 8, 9 }, new[] { 10, 11 },
            new[] { 1, 0 }, new[] { 3, 2 }, new[] { 7, 6 }, new[] { 9, 8 },
        };

        public static IEnumerator Run(MonoBehaviour host, bool quit, string cultureArg)
        {
            var log = OraclePlugin.Log;
            var culture = ItemMaster.Culture.Golem;
            if (cultureArg != "")
                try { culture = (ItemMaster.Culture)Enum.Parse(typeof(ItemMaster.Culture), cultureArg, true); }
                catch { log.LogWarning("polynav: unknown culture " + cultureArg + ", using Golem"); }

            // The walk's first steps: the slot, the town, the gate on a fixed seed.
            yield return Harvest.LoadSlot();
            bool generated = false;
            Action done = () => generated = true;
            DungeonGenerator.OnGeneratedDungeon += done;
            UnityEngine.Random.InitState(7919);
            HeroMerchant.Instance.LoadDungeonScene(culture);
            float start = Time.realtimeSinceStartup;
            while (!generated && Time.realtimeSinceStartup - start < 120f) yield return null;
            DungeonGenerator.OnGeneratedDungeon -= done;
            if (!generated) { log.LogError("polynav: floor timed out"); yield break; }
            yield return new WaitForSecondsRealtime(3f);
            for (int tries = 0; tries < 5 && GUIManager.Instance.currentInventory != null; tries++)
            {
                GUIManager.Instance.DisableCurrentPanel();
                yield return new WaitForSecondsRealtime(0.5f);
            }
            if (GameManager.Instance.isGamePaused) GameManager.Instance.UnpauseGame();
            yield return new WaitForSecondsRealtime(2f);

            var dm = DungeonManager.Instance;
            var nav = PolyNav2D.current;
            var ctrl = HeroMerchant.Instance.heroMerchantController;
            if (nav == null) { log.LogError("polynav: no PolyNav2D"); yield break; }
            writer = new StreamWriter(Path.Combine(OraclePlugin.OutDir, "polynav.jsonl"), append: false) { AutoFlush = false };
            writer.WriteLine(new Json().Obj().Prop("ev", "polynavStart").Prop("session", Trace.Session).Prop("floor", Trace.Floor)
                .Prop("culture", (int)culture).Prop("level", dm.level).EndObj().ToString());
            writer.Flush();

            Time.timeScale = 0f;
            var rooms = dm.dungeonRooms.Where(r => r != null && r.roomEnemies != null && r.roomEnemies.Count > 0).ToList();
            int recorded = 0;
            foreach (var room in rooms)
            {
                // (Entering the room it's in changes nothing: the map keeps the floor start's master.)
                if (room == dm.currentDungeonRoom) { log.LogInfo($"polynav: {room.name} is the current room, skipped"); continue; }
                DungeonDoor door = null;
                foreach (var other in dm.dungeonRooms)
                    if (other != null && other != room && door == null)
                        foreach (var d in other.roomDoors)
                            if (d != null && d.destinationRoom == room && door == null) door = d;
                if (door == null) { log.LogInfo($"polynav: no door into {room.name}"); continue; }

                // DungeonDoor.OnPlayerEnter: through the door into the room.
                var from = dm.currentDungeonRoom;
                ctrl.StopMovement();
                ctrl.DisableMoveInput();
                dm.ChangeRoomTo(room, door);
                ctrl.TeleportTo(door.playerEnterDestinyRoomPoint.position);
                yield return null;
                yield return null;
                if (from != null && from != room)
                {
                    from.CancelInvoke("DisableRoom");
                    AccessTools.Method(typeof(DungeonRoom), "DisableRoom").Invoke(from, null);
                }
                for (int f = 0; f < 120 && (nav.regenerateFlag || nav.pendingRequest); f++) yield return null;
                yield return null;

                Json j = null;
                try { j = RoomRecord(nav, room); }
                catch (Exception e) { log.LogError("polynav: record failed in " + room.name + ": " + e); }
                if (j == null) continue;

                // FindPath between fixed points (and into / out of a hole).
                Vector2 c = nav.transform.position;
                var requests = Pairs.Select(p => new[] { c + Points[p[0]], c + Points[p[1]] }).ToList();
                int w = room.gridSlicesX;
                if (room.obstaclesGrid != null)
                    for (int t = 0; t < room.obstaclesGrid.Length; t++)
                        if (room.obstaclesGrid[t] == 2)
                        {
                            // The tile's middle on the holes' grid (24 per tile from (-240, -132)).
                            Vector2 hole = (Vector2)room.transform.position + new Vector2(24f * (t % w) - 228f, 24f * (t / w) - 120f);
                            requests.Add(new[] { hole, c + Points[1] });
                            requests.Add(new[] { c + Points[0], hole });
                            break;
                        }
                j.Key("paths").Arr();
                foreach (var req in requests)
                {
                    bool got = false;
                    Vector2[] result = null;
                    int f0 = Time.frameCount;
                    nav.FindPath(req[0], req[1], p => { got = true; result = p; });
                    while (!got && Time.frameCount - f0 < 120) yield return null;
                    j.Obj().Key("start"); V2(j, req[0]);
                    j.Key("end"); V2(j, req[1]);
                    j.Prop("got", got).Prop("frames", Time.frameCount - f0).Key("path");
                    if (result == null) j.Val((string)null); else Flat(j, result);
                    j.EndObj();
                }
                j.EndArr();
                writer.WriteLine(j.EndObj().ToString());
                writer.Flush();
                recorded++;
                log.LogInfo($"polynav: {room.name} recorded ({nav.navObstacles.Count} obstacles, {nav.nodesCount} nodes)");
            }
            Time.timeScale = 1f;
            writer.Close();
            File.WriteAllText(Path.Combine(OraclePlugin.OutDir, "POLYNAV_DONE"), DateTime.Now.ToString("o") + " rooms " + recorded);
            log.LogInfo($"polynav: {recorded} rooms recorded");
            if (quit) Application.Quit();
        }

        private static Json RoomRecord(PolyNav2D nav, DungeonRoom room)
        {
            var j = new Json().Obj().Prop("ev", "polynavRoom").Prop("room", room.name)
                .Prop("roomInfo", room.roomInfo != null ? room.roomInfo.name : "");
            j.Key("roomPos"); V2(j, room.transform.position);
            j.Prop("gridX", room.gridSlicesX).Key("grid").Arr();
            if (room.obstaclesGrid != null) foreach (int v in room.obstaclesGrid) j.Val(v);
            j.EndArr();

            var box = nav.GetComponent<BoxCollider2D>();
            j.Key("nav").Obj().Key("pos"); V2(j, nav.transform.position);
            j.Key("scale"); V2(j, nav.transform.lossyScale);
            j.Key("rot").Arr().Bits(nav.transform.rotation.z).Bits(nav.transform.rotation.w).EndArr();
            j.Key("size"); V2(j, box != null ? box.size : Vector2.zero);
            j.Key("offset"); V2(j, box != null ? box.offset : Vector2.zero);
            j.Key("inflate").Bits(nav.inflateRadius).Prop("pending", nav.pendingRequest).Prop("regen", nav.regenerateFlag).EndObj();

            j.Key("rows").Arr();
            Rows(j, room.transform, "");
            j.EndArr();

            j.Key("obstacles").Arr();
            foreach (var o in nav.navObstacles)
            {
                if (!o) { j.Val((string)null); continue; }
                string rel = RelPath(o.transform, room.transform);
                j.Obj().Prop("path", rel ?? FullPath(o.transform)).Prop("inRoom", rel != null).Prop("enabled", o.enabled)
                    .Prop("invert", o.invertPolygon).Prop("shape", (int)o.shapeType);
                j.Key("extra").Bits(o.extraOffset);
                var pts = o.points;
                j.Key("points"); Flat(j, pts);
                j.Key("inflated");
                if (pts != null) Flat(j, PolyNav2D.InflatePolygon(pts, Mathf.Max(0.01f, nav.inflateRadius + o.extraOffset)));
                else j.Val((string)null);
                j.Key("chain").Arr();
                for (Transform t = o.transform; t != null; t = t.parent)
                {
                    var q = t.localRotation;
                    j.Arr().Bits(t.localPosition.x).Bits(t.localPosition.y).Bits(t.localScale.x).Bits(t.localScale.y)
                        .Bits(q.x).Bits(q.y).Bits(q.z).Bits(q.w).EndArr();
                }
                j.EndArr().EndObj();
            }
            j.EndArr();

            j.Key("master").Arr();
            foreach (var p in nav.map.masterPolygons) Flat(j, p.points);
            j.EndArr();
            j.Key("polys").Arr();
            foreach (var p in nav.map.obstaclePolygons) Flat(j, p.points);
            j.EndArr();

            var nodes = Traverse.Create(nav).Field("nodes").GetValue<List<PolyNav2D.PathNode>>();
            j.Key("nodes").Arr();
            foreach (var n in nodes)
            {
                j.Arr().Bits(n.pos.x).Bits(n.pos.y).Arr();
                foreach (int l in n.links) j.Val(l);
                j.EndArr().EndArr();
            }
            j.EndArr();

            // Probes over the room on a 24-unit grid around the map's centre.
            Vector2 c = nav.transform.position;
            j.Key("probes").Arr();
            for (int yi = 0; yi < 12; yi++)
                for (int xi = 0; xi < 21; xi++)
                {
                    Vector2 p = c + new Vector2(-240f + 24f * xi, -132f + 24f * yi);
                    Vector2 closer = nav.GetCloserEdgePoint(p);
                    j.Arr().Bits(p.x).Bits(p.y).Val(nav.PointIsValid(p)).Bits(closer.x).Bits(closer.y).Val(nav.CheckLOS(c, p)).EndArr();
                }
            j.EndArr();
            return j;
        }
    }
}
