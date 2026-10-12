using System;
using System.Collections;
using System.IO;
using System.Linq;
using HarmonyLib;
using InControl;
using UnityEngine;

namespace MoonlighterOracle
{
    /// `-oracle-walk`: load the first save slot like the harvest (saving and stats blocked),
    /// take the Golem gate on a fixed seed, then drive the hero with a fixed input script and
    /// record him every frame into walk.jsonl: the frame's deltaTime, the input, his position,
    /// the Animator's state and the controller's movement fields. The first lines hold the
    /// room's colliders and the controller's settings, for the port's Physics2D queries; every
    /// frame also has his Rigidbody2D (position and velocity after the frame's physics steps,
    /// and as each step started).
    internal static partial class Walk
    {
        public static bool Active;
        private static int startFrame = -1;
        private static StreamWriter writer;

        /// One segment: frames to hold, stick x and y, buttons pressed on its first frame
        /// (1 Action1, 2 Action2, 4 Action3, 8 Action4, 16 LeftTrigger: the roll). `toward`
        /// (the rocks script's "go"): x and y are a point to steer the stick to (StickFor).
        private struct Segment
        {
            public int frames;
            public float x, y;
            public int buttons;
            public bool toward;
            public Segment(int f, float x, float y, int b = 0) { frames = f; this.x = x; this.y = y; buttons = b; toward = false; }
        }

        // Inside the floor's first room (Will starts in its middle; the doors are in the middle
        // of each wall, so the script keeps to the sides): walls, corners, a diagonal, a stick
        // under the dead zone.
        private const int Roll = 16;

        // `-oracle-walk=roll`: rolls from the middle of the room, each with the cooldown long
        // gone: from standing (facing), while walking, diagonally, and into a wall.
        private static readonly Segment[] RollScript =
        {
            new Segment(20, 0f, 0f),
            new Segment(45, 1f, 0f, Roll),    // right, rolling at once
            new Segment(60, 0f, 0f),
            new Segment(45, 0f, 1f, Roll),    // up
            new Segment(60, 0f, 0f),
            new Segment(30, -1f, 0f),         // walking left...
            new Segment(45, -1f, 0f, Roll),   // ...then rolling on
            new Segment(60, 0f, 0f),
            new Segment(45, 0f, 0f, Roll),    // standing: the way he faces
            new Segment(60, 0f, 0f),
            new Segment(45, 0.7071068f, -0.7071068f, Roll), // diagonal
            new Segment(60, 0f, 0f),
            new Segment(30, 0f, 1f),          // up...
            new Segment(150, 1f, 0f),         // ...to the east wall above the door
            new Segment(45, 1f, 0f, Roll),    // a roll into the wall
            new Segment(60, 0f, 0f),
            new Segment(45, 0f, -0.45f, Roll), // a short stick: down
            new Segment(60, 0f, 0f),
        };

        // `-oracle-walk=diag`: digital diagonals (the D-pad / keyboard: both axes full) after
        // holding one direction first: which way does he face?
        private static readonly Segment[] DiagScript =
        {
            new Segment(20, 0f, 0f),
            new Segment(30, -1f, 0f),         // left...
            new Segment(40, -1f, 1f),         // ...then up and left together
            new Segment(30, 0f, 0f),
            new Segment(30, 0f, -1f),         // down...
            new Segment(40, 1f, -1f),         // ...then down and right together
            new Segment(30, 0f, 0f),
            new Segment(30, 1f, 0f),          // right...
            new Segment(40, 1f, 1f),          // ...then up and right together
            new Segment(30, 0f, 0f),
        };

        private static Segment[] Script; // Run picks it

        // `-oracle-walk=fall`: in a room with holes (entered through a door, as DungeonDoor.
        // OnPlayerEnter does it, so the game knows the door to put him back at), from a floor
        // tile next to a hole, walking into it: the push, the fall and the respawn.
        private static Segment[] FallScript(Vector2 toward) => new[]
        {
            new Segment(20, 0f, 0f),
            new Segment(90, toward.x, toward.y), // into the hole: 0.6 s of pushing, then the fall
            new Segment(90, 0f, 0f),             // the fall (0.8 s) and back at the door
            new Segment(40, -toward.x, -toward.y),
            new Segment(20, 0f, 0f),
        };

        /// A room with holes, the door into it, and a floor tile beside a hole: where he stands
        /// and which way the hole is. False if the floor has none.
        private static bool FindHoleEdge(out DungeonRoom room, out DungeonDoor door, out Vector2 stand, out Vector2 toward)
        {
            room = null;
            door = null;
            stand = toward = Vector2.zero;
            var dm = DungeonManager.Instance;
            foreach (var r in dm.dungeonRooms)
            {
                if (r == null || r.obstaclesGrid == null || r.isInit || r.secretRoom != null) continue; // (a secret room: its holes lead there)
                DungeonDoor into = null;
                foreach (var other in dm.dungeonRooms)
                    if (other != null && other != r)
                        foreach (var d in other.roomDoors)
                            if (d != null && d.destinationRoom == r && into == null) into = d;
                if (into == null) continue;
                int w = r.gridSlicesX, h = r.obstaclesGrid.Length / w;
                // Prefer a hole to the side (a horizontal push), then above or below.
                int[][] steps = { new[] { 1, 0 }, new[] { -1, 0 }, new[] { 0, 1 }, new[] { 0, -1 } };
                foreach (var st in steps)
                    for (int j = 1; j < h - 1; j++)
                        for (int i = 1; i < w - 1; i++)
                        {
                            int t = i + j * w, n = (i + st[0]) + (j + st[1]) * w;
                            if (r.obstaclesGrid[t] != 0 || r.obstaclesGrid[n] != 2) continue;
                            room = r;
                            door = into;
                            // The tile's middle on the holes' grid (GenerateCollidersFromGrid: 24 per
                            // tile from (-240, -132), no centre offset).
                            stand = (Vector2)r.transform.position + new Vector2(24f * i - 228f, 24f * j - 120f);
                            toward = new Vector2(st[0], st[1]);
                            return true;
                        }
            }
            return false;
        }

        private static readonly Segment[] WalkScript =
        {
            new Segment(20, 0f, 0f),
            new Segment(25, 0f, 1f),          // up a bit
            new Segment(100, 1f, 0f),         // right, into the east wall above its door
            new Segment(45, 0f, 1f),          // up, into the north-east corner
            new Segment(30, 0f, 0f),
            new Segment(60, -1f, 0f),         // left along the north wall
            new Segment(30, -0.7071068f, -0.7071068f), // down-left
            new Segment(40, 0.2f, 0f),        // under the dead zone
            new Segment(50, 1f, 0.4f),        // mostly right
            new Segment(45, 0f, -1f),         // down
            new Segment(20, 0f, 0f),
            new Segment(150, -1f, 0f),        // left, into the west wall below its door
            new Segment(30, 0f, -1f),         // down, into the south-west corner
            new Segment(30, 0f, 0f),
        };

        private static int ScriptFrames => Script.Aggregate(0, (n, s) => n + s.frames);

        /// The script's input for this frame (frame 0 is the first scripted one).
        private static bool Current(out Segment seg, out bool first)
        {
            seg = default;
            first = false;
            if (!Active || startFrame < 0) return false;
            int f = Time.frameCount - startFrame;
            if (f < 0 || f >= ScriptFrames) return false;
            foreach (var s in Script)
            {
                if (f < s.frames)
                {
                    seg = s;
                    first = f == 0;
                    return true;
                }
                f -= s.frames;
            }
            return false;
        }

        [HarmonyPatch(typeof(TwoAxisInputControl), "UpdateWithAxes")]
        private static class StickHook
        {
            private static void Prefix(TwoAxisInputControl __instance, ref float x, ref float y)
            {
                if (!Active || startFrame < 0) return;
                var actions = InputActionsManager.Instance != null ? InputActionsManager.Instance.actions : null;
                if (actions == null || __instance != actions.LeftStick) return;
                if (Current(out Segment s, out _))
                {
                    lastStick = StickFor(s);
                    x = lastStick.x;
                    y = lastStick.y;
                }
                else { x = 0f; y = 0f; }
            }
        }

        [HarmonyPatch(typeof(PlayerAction), "Update")]
        private static class ButtonHook
        {
            private static bool Prefix(PlayerAction __instance, ulong updateTick, float deltaTime)
            {
                if (!Active || startFrame < 0) return true;
                var a = InputActionsManager.Instance != null ? InputActionsManager.Instance.actions : null;
                if (a == null) return true;
                int bit = __instance == a.Action1 ? 1 : __instance == a.Action2 ? 2 : __instance == a.Action3 ? 4 : __instance == a.Action4 ? 8
                    : __instance == a.LeftTrigger ? Roll : 0;
                if (bit == 0) return true;
                bool down = Current(out Segment s, out bool first) && first && (s.buttons & bit) != 0;
                __instance.CommitWithValue(down ? 1f : 0f, updateTick, deltaTime);
                return false;
            }
        }

        private static void Emit(Json j)
        {
            writer.WriteLine(j.EndObj().ToString());
        }

        private static string PathOf(Transform t)
        {
            string p = t.name;
            for (t = t.parent; t != null; t = t.parent) p = t.name + "/" + p;
            return p;
        }

        private static Json V2(Json j, string key, Vector2 v) => j.Key(key).Arr().Bits(v.x).Bits(v.y).EndArr();

        private static void WriteColliders()
        {
            foreach (var c in UnityEngine.Object.FindObjectsOfType<Collider2D>())
            {
                var j = new Json().Obj().Prop("ev", "collider").Prop("type", c.GetType().Name).Prop("path", PathOf(c.transform))
                    .Prop("layer", c.gameObject.layer).Prop("tag", c.tag).Prop("enabled", c.enabled).Prop("trigger", c.isTrigger)
                    .Prop("body", c.attachedRigidbody != null ? c.attachedRigidbody.bodyType.ToString() : "none");
                V2(j, "pos", c.transform.position);
                V2(j, "scale", c.transform.lossyScale);
                V2(j, "offset", c.offset);
                V2(j, "min", c.bounds.min);
                V2(j, "max", c.bounds.max);
                if (c is BoxCollider2D b) { V2(j, "size", b.size); j.Key("edgeRadius").Bits(b.edgeRadius); }
                if (c is CircleCollider2D ci) j.Key("radius").Bits(ci.radius);
                if (c is PolygonCollider2D pc)
                {
                    j.Key("paths").Arr();
                    for (int i = 0; i < pc.pathCount; i++)
                    {
                        j.Arr();
                        foreach (var p in pc.GetPath(i)) j.Bits(p.x).Bits(p.y);
                        j.EndArr();
                    }
                    j.EndArr();
                }
                Emit(j);
            }
        }

        public static IEnumerator Run(MonoBehaviour host, bool quit, string script = "")
        {
            Script = script == "roll" ? RollScript : script == "diag" ? DiagScript : script == "rocks" ? RocksScript : WalkScript;
            var log = OraclePlugin.Log;
            // The harvest's first steps: the slot, the town, the Golem gate on a fixed seed.
            yield return Harvest.LoadSlot();
            bool generated = false;
            Action done = () => generated = true;
            DungeonGenerator.OnGeneratedDungeon += done;
            UnityEngine.Random.InitState(7919);
            HeroMerchant.Instance.LoadDungeonScene(ItemMaster.Culture.Golem);
            float start = Time.realtimeSinceStartup;
            while (!generated && Time.realtimeSinceStartup - start < 120f) yield return null;
            DungeonGenerator.OnGeneratedDungeon -= done;
            if (!generated) { log.LogError("walk: floor timed out"); yield break; }
            yield return new WaitForSecondsRealtime(3f);
            for (int tries = 0; tries < 5 && GUIManager.Instance.currentInventory != null; tries++)
            {
                GUIManager.Instance.DisableCurrentPanel();
                yield return new WaitForSecondsRealtime(0.5f);
            }
            if (GameManager.Instance.isGamePaused) GameManager.Instance.UnpauseGame();
            // The floor's loading panel can stay in GUIManager's slot after it closed (see the
            // harvest); with it there the roll's CanUseAbility is false. Real play has it empty.
            var stale = GUIManager.Instance.currentInventory;
            if (stale != null && !stale.IsActive()) GUIManager.Instance.currentInventory = null;
            yield return new WaitForSecondsRealtime(2f);

            var hero = HeroMerchant.Instance;
            var ctrl = hero.heroMerchantController;
            var tr = Traverse.Create(ctrl);
            if (script == "fall")
            {
                if (!FindHoleEdge(out DungeonRoom holes, out DungeonDoor door, out Vector2 stand, out Vector2 toward))
                {
                    log.LogError("walk: no room with holes on this floor");
                    yield break;
                }
                // DungeonDoor.OnPlayerEnter: through the door into the room.
                ctrl.StopMovement();
                ctrl.DisableMoveInput();
                DungeonManager.Instance.ChangeRoomTo(holes, door);
                ctrl.TeleportTo(door.playerEnterDestinyRoomPoint.position);
                yield return new WaitForSecondsRealtime(2.5f);
                ctrl.TeleportTo(stand);
                yield return new WaitForSecondsRealtime(1f);
                Script = FallScript(toward);
                log.LogInfo($"walk: fall in {holes.name} from {stand} toward {toward}, back at {door.playerEnterDestinyRoomPoint.position}");
            }
            if (script == "rocks")
            {
                if (!PlaceRock(log)) yield break;
                yield return new WaitForSecondsRealtime(1f);
            }
            writer = new StreamWriter(Path.Combine(OraclePlugin.OutDir, script == "" ? "walk.jsonl" : "walk_" + script + ".jsonl"), append: false) { AutoFlush = false };
            var setup = new Json().Obj().Prop("ev", "walkStart").Prop("session", Trace.Session).Prop("floor", Trace.Floor)
                .Prop("collisionLayers", ctrl.collisionDetectionLayers.value)
                .Prop("slideLayers", ctrl.slideCollisionDetectionLayers.value);
            V2(setup, "colliderSize", ctrl.characterCollider.size);
            V2(setup, "colliderOffset", ctrl.characterCollider.offset);
            setup.Key("moveSpeed").Bits(ctrl.currentPlayerMoveSpeed).Key("speedFactor").Bits(ctrl.speedFactor);
            setup.Prop("godModeLayers", ctrl.godModeCollisionDetectionLayers.value).Prop("script", script);
            setup.Prop("holesMask", tr.Field("holesMask").GetValue<LayerMask>().value);
            var cur = DungeonManager.Instance.currentDungeonRoom;
            setup.Prop("room", cur != null ? cur.name : "");
            V2(setup, "roomPos", cur != null ? (Vector2)cur.transform.position : Vector2.zero);
            var lastDoor = Traverse.Create(DungeonManager.Instance).Field("lastDoorUsed").GetValue<DungeonDoor>();
            if (lastDoor != null) V2(setup, "respawn", lastDoor.playerEnterDestinyRoomPoint.position);
            var ability = ctrl.currentEquipedAbility;
            setup.Key("cooldown").Bits(ability != null ? ability.coolDownTime : -1f);
            setup.Key("statSpeed").Bits(hero.heroMerchantStats.speed.Value);
            var rb = hero.GetComponent<Rigidbody2D>();
            setup.Prop("body", rb != null ? rb.bodyType.ToString() : "none");
            if (rb != null)
            {
                // His Rigidbody2D's settings, as the physics world has them.
                setup.Key("rbMass").Bits(rb.mass).Prop("rbAutoMass", rb.useAutoMass);
                V2(setup, "rbCenterOfMass", rb.centerOfMass);
                setup.Key("rbDrag").Bits(rb.drag).Key("rbAngularDrag").Bits(rb.angularDrag).Key("rbGravityScale").Bits(rb.gravityScale);
                setup.Key("rbInertia").Bits(rb.inertia);
                setup.Prop("rbConstraints", (int)rb.constraints).Prop("rbDetection", rb.collisionDetectionMode.ToString())
                    .Prop("rbSleep", rb.sleepMode.ToString()).Prop("rbInterpolation", rb.interpolation.ToString())
                    .Prop("rbFullKinematicContacts", rb.useFullKinematicContacts).Prop("rbSimulated", rb.simulated);
                setup.Key("fixedDeltaTime").Bits(Time.fixedDeltaTime);
            }
            if (script == "rocks")
            {
                V2(setup, "rock", rockAt);
                setup.Prop("rockFrom", rockFrom);
            }
            Emit(setup);
            WriteColliders();
            writer.Flush();

            // Steady 60 fps for the script (the harvest's floors stay uncapped).
            QualitySettings.vSyncCount = 0;
            Application.targetFrameRate = 60;
            var rec = new GameObject("OracleWalkRecorder").AddComponent<Recorder>();
            rec.ctrl = ctrl;
            rec.tr = tr;
            rec.rb = rb;
            startFrame = Time.frameCount + 1;
            Active = true;
            while (Time.frameCount - startFrame < ScriptFrames + 2) yield return null;
            Active = false;
            UnityEngine.Object.Destroy(rec.gameObject);
            writer.Flush();
            writer.Close();
            File.WriteAllText(Path.Combine(OraclePlugin.OutDir, "WALK_DONE"), DateTime.Now.ToString("o"));
            log.LogInfo($"walk: {ScriptFrames} frames recorded");
            if (quit) Application.Quit();
        }

        /// Records after the frame's Update and Animator pass.
        private class Recorder : MonoBehaviour
        {
            public HeroMerchantController ctrl;
            public Traverse tr;
            public Rigidbody2D rb;
            // This frame's physics steps, as each started: his transform (before the sync) and
            // his body (the last step's result) with its velocity.
            private readonly System.Collections.Generic.List<float[]> steps = new System.Collections.Generic.List<float[]>();

            private void FixedUpdate()
            {
                int f = Time.frameCount - startFrame;
                if (f < 0 || f >= ScriptFrames || rb == null) return;
                Vector2 t = ctrl.transform.position, p = rb.position, v = rb.velocity;
                steps.Add(new[] { t.x, t.y, p.x, p.y, v.x, v.y });
            }

            private void LateUpdate()
            {
                int f = Time.frameCount - startFrame;
                if (f < 0 || f >= ScriptFrames) return;
                Current(out Segment s, out bool first);
                var a = InputActionsManager.Instance.actions;
                var anim = ctrl.characterAnimator;
                var info = anim.GetCurrentAnimatorStateInfo(0);
                var j = new Json().Obj().Prop("ev", "walk").Prop("f", f);
                j.Key("dt").Bits(Time.deltaTime);
                Vector2 given = s.toward ? lastStick : new Vector2(s.x, s.y);
                j.Key("in").Arr().Bits(given.x).Bits(given.y).Val(first ? s.buttons : 0).EndArr();
                j.Key("stick").Arr().Bits(a.LeftStick.X).Bits(a.LeftStick.Y).EndArr();
                V2(j, "pos", ctrl.transform.position);
                j.Key("state").Val(info.shortNameHash).Key("t").Bits(info.normalizedTime);
                bool inTransition = anim.IsInTransition(0);
                j.Prop("trans", inTransition);
                if (inTransition)
                {
                    j.Key("next").Val(anim.GetNextAnimatorStateInfo(0).shortNameHash)
                        .Key("nextT").Bits(anim.GetNextAnimatorStateInfo(0).normalizedTime)
                        .Key("transT").Bits(anim.GetAnimatorTransitionInfo(0).normalizedTime);
                }
                j.Key("vel").Arr().Bits(tr.Field("velX").GetValue<float>()).Bits(tr.Field("velY").GetValue<float>()).EndArr();
                j.Prop("canMove", ctrl._canMove).Prop("moveInput", tr.Field("_isMoveInputEnabled").GetValue<bool>())
                    .Prop("dir", (int)ctrl.heroDirection);
                j.Key("speedFactor").Bits(ctrl.speedFactor);
                j.Key("animSpeed").Bits(anim.speed);
                j.Prop("intangible", tr.Field("isIntangible").GetValue<bool>());
                j.Prop("falling", tr.Field("isFalling").GetValue<bool>());
                var ab = ctrl.currentEquipedAbility;
                j.Prop("abilityInUse", ab != null && ab.isInUse);
                j.Prop("abilityAvailable", ab != null && Traverse.Create(ab).Field("abilityAvailable").GetValue<bool>());
                // The roll's gate: the trigger as the controller reads it, and the conditions.
                j.Key("lt").Arr().Val(a.LeftTrigger.IsPressed).Val(a.LeftTrigger.WasPressed).EndArr();
                j.Prop("canAttack", ctrl.canAttack).Prop("canUse", ab != null && ab.CanUseAbility())
                    .Prop("panel", GUIManager.Instance.currentInventory != null ? GUIManager.Instance.currentInventory.GetType().Name : "");
                var art = ctrl.transform.Find("HeroMerchantArt/Will");
                var sr = art != null ? art.GetComponent<SpriteRenderer>() : null;
                j.Prop("sprite", sr != null && sr.sprite != null ? sr.sprite.name : null);
                // His Rigidbody2D after the frame's physics steps: position, velocity; awake; layer.
                if (rb != null)
                {
                    j.Key("rb").Arr().Bits(rb.position.x).Bits(rb.position.y).Bits(rb.velocity.x).Bits(rb.velocity.y).EndArr();
                    j.Prop("awake", rb.IsAwake()).Prop("layer", ctrl.gameObject.layer).Prop("touching", rb.IsTouchingLayers());
                    j.Key("fixed").Arr();
                    foreach (var st in steps)
                    {
                        j.Arr();
                        foreach (float x in st) j.Bits(x);
                        j.EndArr();
                    }
                    j.EndArr();
                }
                steps.Clear();
                Emit(j);
            }
        }
    }
}
