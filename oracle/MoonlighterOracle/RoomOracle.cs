using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using HarmonyLib;
using UnityEngine;

namespace MoonlighterOracle
{
    /// Hooks that trace real dungeon generation room by room (see Trace for the format):
    ///  - the room database as the game loaded it (roomdb_asset.json, once per session);
    ///  - every GetRandomRoom / GetMinibossRoom call with its inputs, weights and result;
    ///  - every room: grid and spawn pack, enemies, and the RNG state at each fill stage,
    ///    so each stage can be checked on its own and hidden RNG consumers show up as gaps;
    ///  - post-loop steps, and a full object snapshot of the floor at the end.
    internal static class RoomOracle
    {
        private static bool dbDumped;
        private static int pendingX, pendingY;

        [HarmonyPatch(typeof(CaveGenerator), nameof(CaveGenerator.Init))]
        private static class FloorBegin
        {
            private static void Prefix()
            {
                if (CaveOracle.inMirror) return;
                Trace.BeginFloor();
                Trace.Safe("floorBegin", () =>
                {
                    var j = Trace.Ev("floorBegin");
                    var g = UnityEngine.Object.FindObjectOfType<DungeonGenerator>();
                    if (g != null) j.Prop("level", g.level).Prop("culture", (int)g.culture);
                    if (TimeManagerV2.Instance != null) j.Prop("dayTime", TimeManagerV2.Instance.CurrrentDayTime.ToString());
                    Trace.Emit(Trace.State(j));
                });
            }
        }

        [HarmonyPatch(typeof(DungeonRoomDatabase), nameof(DungeonRoomDatabase.LoadFromJSON))]
        private static class DbLoaded
        {
            private static void Postfix(DungeonRoomDatabase __instance)
            {
                if (dbDumped) return;
                dbDumped = true;
                Trace.Safe("roomdb dump", () =>
                    File.WriteAllText(Path.Combine(OraclePlugin.OutDir, "roomdb_asset.json"), JsonUtility.ToJson(__instance)));
            }
        }

        private static Json Candidates(Json j, List<DungeonRoomInfo> rooms)
        {
            j.Key("rooms").Arr();
            foreach (DungeonRoomInfo r in rooms) j.Arr().Val(r.name).Val((int)r.culture).Bits(r.weight).EndArr();
            return j.EndArr();
        }

        [HarmonyPatch(typeof(DungeonGenerator), "GetRandomRoom",
            new[] { typeof(List<DungeonRoomInfo>), typeof(List<DungeonRoomInfo.Doors>), typeof(List<DungeonRoomInfo>) })]
        private static class GetRandomRoomByDoors
        {
            private static void Prefix(List<DungeonRoomInfo> rooms, List<DungeonRoomInfo.Doors> desiredDoorIndex, List<DungeonRoomInfo> ignore, out Json __state)
            {
                Json j = null;
                Trace.Safe("getRandomRoom", () =>
                {
                    j = Trace.Ev("getRandomRoom");
                    Candidates(j, rooms);
                    j.Key("doors").Arr();
                    foreach (var d in desiredDoorIndex) j.Val((int)d);
                    j.EndArr();
                    Trace.Names(j, "ignore", ignore, r => r.name);
                    j.Prop("ignoreIsNull", ignore == null);
                    Trace.State(j, "before");
                });
                __state = j;
            }

            private static void Postfix(DungeonRoomInfo __result, Json __state)
            {
                if (__state == null) return;
                Trace.Safe("getRandomRoom post", () =>
                {
                    __state.Prop("result", __result?.name);
                    if (__result != null) __state.Key("resultWeightAfter").Bits(__result.weight);
                    Trace.Emit(Trace.State(__state, "after"));
                });
            }
        }

        [HarmonyPatch(typeof(DungeonGenerator), "GetRandomRoom", new[] { typeof(List<DungeonRoomInfo>), typeof(int) })]
        private static class GetRandomRoomByIndex
        {
            private static void Prefix(List<DungeonRoomInfo> rooms, int desiredDoorIndex, out Json __state)
            {
                Json j = null;
                Trace.Safe("getRandomRoomIdx", () =>
                {
                    j = Trace.Ev("getRandomRoomIdx");
                    Candidates(j, rooms).Prop("door", desiredDoorIndex);
                    Trace.State(j, "before");
                });
                __state = j;
            }

            private static void Postfix(DungeonRoomInfo __result, Json __state)
            {
                if (__state == null) return;
                Trace.Safe("getRandomRoomIdx post", () => Trace.Emit(Trace.State(__state.Prop("result", __result?.name), "after")));
            }
        }

        [HarmonyPatch(typeof(DungeonGenerator), "GetMinibossRoom")]
        private static class MinibossRoom
        {
            private static void Postfix(DungeonRoomInfo __result) =>
                Trace.Safe("miniboss", () => Trace.Emit(Trace.State(Trace.Ev("minibossRoom").Prop("result", __result?.name))));
        }

        [HarmonyPatch(typeof(DungeonGenerator), nameof(DungeonGenerator.AddSingleDungeonRoom))]
        private static class AddRoom
        {
            private static void Prefix(DungeonRoomInfo roomInfo, int x, int y)
            {
                pendingX = x;
                pendingY = y;
            }
        }

        [HarmonyPatch(typeof(DungeonRoom), "InitializeRoom", new[] { typeof(DungeonRoomInfo) })]
        private static class RoomInit
        {
            private static void Prefix(DungeonRoom __instance, DungeonRoomInfo roomInfo)
            {
                Trace.Safe("roomInit", () =>
                {
                    int id = Trace.AssignRoomId(__instance);
                    var j = Trace.Ev("room").Prop("room", id).Prop("x", pendingX).Prop("y", pendingY)
                        .Prop("name", roomInfo?.name).Prop("culture", roomInfo != null ? (int)roomInfo.culture : -1)
                        .Prop("gridSlicesX", __instance.gridSlicesX).Prop("gridSlicesY", __instance.gridSlicesY)
                        .Prop("type", __instance.GetType().Name)
                        // Room names aren't unique; the database index identifies the design.
                        .Prop("dbIndex", DungeonGenerator.roomDatabase?.database.rooms?.IndexOf(roomInfo) ?? -1);
                    Trace.Emit(Trace.State(j));
                });
            }
        }

        /// RNG state at the start and end of every fill stage and post-loop step.
        [HarmonyPatch]
        private static class Stages
        {
            private static readonly string[] RoomMethods =
            {
                "LoadGridFromInfo", "RoomPlaced", "SpawnEnemies", "DisableEnemies", "SpawnChests", "SpawnRoomPrefabs",
                "SpawnDetails", "SetTilesZones", "AddSpecialInitDoor", "ReplaceSpecialDoor", "ReplaceNormalDoor",
                "SpawnEnemy", "SpawnChest", "SpawnMimic", "GetChestNameByFloor", "GetWandererChestNameByFloor",
            };

            private static readonly string[] OtherMethods = { "Chest.Awake", "BreakableObjectBehaviour.TransformToSkeleton" };

            private static readonly string[] GeneratorMethods =
            {
                "AddLightShadow", "GetMiddleDetailZonesFromGrid", "GenerateRocksPrefab", "GenerateHolesPrefab", "SetBackgroundImages",
                "GenerateSlimePrefab", "GenerateAcidPrefab", "GenerateLavaPrefab", "GenerateElectricPrefab", "GenerateFallingFloorPrefab",
            };

            private static readonly string[] DungeonGeneratorMethods =
            {
                "SpawnSpecialRoomsAfterGeneration", "PlaceCorruptedRoomDoors", "UpdateMinimapOnGeneratedDungeon", "UpdateChestCount",
                "AddNextLevelDoor", "AddSecretRoom", "AddBossRoom", "ConnectRoomsByDirection", "SpawnSpecialUnlockAtInit",
            };

            private static IEnumerable<MethodBase> TargetMethods()
            {
                IEnumerable<MethodBase> Of(Type t, string[] names) =>
                    AccessTools.GetDeclaredMethods(t).Where(m => names.Contains(m.Name) && !m.IsAbstract && !m.ContainsGenericParameters).Cast<MethodBase>();
                var others = OtherMethods.Select(s => s.Split('.'))
                    .Select(p => (MethodBase)AccessTools.DeclaredMethod(AccessTools.TypeByName(p[0]), p[1]))
                    .Where(m => m != null);
                return Of(typeof(DungeonRoom), RoomMethods)
                    .Concat(Of(typeof(DungeonRoomGenerator), GeneratorMethods))
                    .Concat(Of(typeof(DungeonGenerator), DungeonGeneratorMethods))
                    .Concat(others);
            }

            /// Primitive arguments (ints, floats, strings, enums), plus names of Unity objects.
            private static void Args(Json j, object[] args)
            {
                j.Key("args").Arr();
                if (args != null)
                    foreach (object a in args)
                    {
                        switch (a)
                        {
                            case null: j.Val((string)null); break;
                            case int i: j.Val(i); break;
                            case bool b: j.Val(b); break;
                            case float f: j.Bits(f); break;
                            case string s: j.Val(s); break;
                            case Enum e: j.Val(Convert.ToInt32(e)); break;
                            case UnityEngine.Object o: j.Val(o ? o.name : null); break;
                            case DungeonRoomInfo.EnemySpawn es: j.Val(es.enemy?.name + "@" + es.index); break;
                            default: j.Val(a.GetType().Name); break;
                        }
                    }
                j.EndArr();
            }

            private static int RoomOf(object instance, object[] args)
            {
                if (instance is DungeonRoom r) return Trace.RoomId(r);
                if (args != null) foreach (object a in args) if (a is DungeonRoom ar) return Trace.RoomId(ar);
                return -1;
            }

            private static void Prefix(MethodBase __originalMethod, object __instance, object[] __args)
            {
                if (CaveOracle.inMirror) return;
                Trace.Safe("stage", () =>
                {
                    if (__originalMethod.Name == "RoomPlaced" && __instance is DungeonRoom dr && Trace.RoomId(dr) < 0)
                        Trace.Emit(Trace.State(Trace.Ev("room").Prop("room", Trace.AssignRoomId(dr)).Prop("name", dr.roomInfo?.name)
                            .Prop("type", dr.GetType().Name)));
                    var j = Trace.Ev("stage").Prop("name", __originalMethod.DeclaringType.Name + "." + __originalMethod.Name)
                        .Prop("phase", "pre").Prop("room", RoomOf(__instance, __args));
                    Args(j, __args);
                    if (__instance is Component c && !(__instance is DungeonRoom))
                    {
                        j.Prop("object", c.name);
                        Vec(j, "pos", c.transform.position);
                    }
                    Trace.Emit(Trace.State(j));
                });
            }

            private static void Postfix(MethodBase __originalMethod, object __instance, object[] __args)
            {
                if (CaveOracle.inMirror) return;
                Trace.Safe("stage post", () =>
                {
                    var j = Trace.Ev("stage").Prop("name", __originalMethod.DeclaringType.Name + "." + __originalMethod.Name)
                        .Prop("phase", "post").Prop("room", RoomOf(__instance, __args));
                    if (__originalMethod.Name == "LoadGridFromInfo" && __instance is DungeonRoom g)
                    {
                        j.Prop("currentSpawnPack", g.currentSpawnPack).Key("grid").Arr();
                        if (g.obstaclesGrid != null) foreach (int t in g.obstaclesGrid) j.Val(t);
                        j.EndArr();
                    }
                    if (__originalMethod.Name == "SpawnEnemies" && __instance is DungeonRoom e) WriteEnemies(j, e);
                    if (__originalMethod.Name == "SpawnChests" && __instance is DungeonRoom c) WriteChests(j, c);
                    if (__originalMethod.Name == "AddNextLevelDoor")
                        j.Prop("savedLastDoorDirectionIndex", DungeonGenerator.floorGenerationSaveInfo?.lastDoorDirectionIndex ?? -1);
                    Trace.Emit(Trace.State(j));
                });
            }
        }

        private static readonly HashSet<string> detailsDumped = new HashSet<string>();

        private static void SpriteSizes(Json j, string key, List<Sprite> sprites)
        {
            j.Key(key).Arr();
            if (sprites != null)
                foreach (Sprite s in sprites)
                {
                    if (s == null || s.texture == null) { j.Val((string)null); continue; }
                    j.Arr().Val(s.texture.width).Val(s.texture.height).EndArr();
                }
            j.EndArr();
        }

        /// Probability images: size and red channel (GetPixel(i, j).r is byte/255), base64.
        private static void ProbabilityImages(Json j, string key, List<Sprite> sprites)
        {
            j.Key(key).Arr();
            if (sprites != null)
                foreach (Sprite s in sprites)
                {
                    if (s == null || s.texture == null) { j.Val((string)null); continue; }
                    var px = s.texture.GetPixels32();
                    var red = new byte[px.Length];
                    for (int i = 0; i < px.Length; i++) red[i] = px[i].r;
                    j.Obj().Prop("w", s.texture.width).Prop("h", s.texture.height).Prop("red", Convert.ToBase64String(red)).EndObj();
                }
            j.EndArr();
        }

        /// SpawnDetails inputs: per room the spawner count, zones and cache state; once per
        /// culture/level the detail lists' sizes and the probability images' pixels.
        [HarmonyPatch(typeof(DungeonRoom), nameof(DungeonRoom.SpawnDetails), new Type[0])]
        private static class DetailsInputs
        {
            [HarmonyPriority(Priority.First)]
            private static void Prefix(DungeonRoom __instance)
            {
                if (CaveOracle.inMirror) return;
                Trace.Safe("detailsInputs", () =>
                {
                    var info = __instance.roomInfo;
                    if (info == null) return;
                    var dm = DungeonManager.Instance;
                    int level = dm != null ? dm.level : 0;
                    var culture = info.culture;
                    string key = culture + "/" + level;
                    if (detailsDumped.Add(key))
                    {
                        var j = Trace.Ev("detailsData").Prop("culture", (int)culture).Prop("level", level)
                            .Prop("breakables", DetailsPrefabRegister.GetBreakableDetailsByCulture(culture, level).Count)
                            .Prop("particles", DetailsPrefabRegister.GetParticleDetailsByCulture(culture, level).Count)
                            .Prop("creatures", DetailsPrefabRegister.GetCreatureDetailsByCulture(culture, level).Count)
                            .Prop("walls", DetailsPrefabRegister.GetWallDetailsByCulture(culture, level).Count);
                        SpriteSizes(j, "lower", DetailsPrefabRegister.GetLowerLayerDetailsByCulture(culture, level));
                        SpriteSizes(j, "lower2", DetailsPrefabRegister.GetLower2LayerDetailsByCulture(culture, level));
                        SpriteSizes(j, "middle", DetailsPrefabRegister.GetMiddleLayerDetailsByCulture(culture, level));
                        ProbabilityImages(j, "lowerProb", DetailsPrefabRegister.GetLower1GenerationProbabilitiesByCulture(culture, level));
                        ProbabilityImages(j, "lower2Prob", DetailsPrefabRegister.GetLower2GenerationProbabilitiesByCulture(culture, level));
                        Trace.Emit(j);
                    }
                    var r = Trace.Ev("detailsRoom").Prop("room", Trace.RoomId(__instance)).Prop("culture", (int)culture).Prop("level", level)
                        .Prop("floorCulture", dm != null ? (int)dm.culture : -1)
                        .Prop("spawners", __instance.detailSpawners != null ? __instance.detailSpawners.childCount : -1)
                        .Prop("roomFloorWidth", __instance.roomFloorWidth).Prop("roomFloorHeight", __instance.roomFloorHeight)
                        .Prop("lowerDetailsIndex", (int)AccessTools.Field(typeof(DungeonRoom), "lowerDetailsIndex").GetValue(null))
                        .Prop("lower2DetailsIndex", (int)AccessTools.Field(typeof(DungeonRoom), "lower2DetailsIndex").GetValue(null));
                    var d = info.detailGenerationInfo;
                    r.Key("zones").Arr();
                    foreach (Rect z in d.middleDetailZones) r.Arr().Bits(z.x).Bits(z.y).Bits(z.width).Bits(z.height).EndArr();
                    r.EndArr();
                    r.Key("probs").Arr().Bits(d.wallDetailGenerationProb).Bits(d.middleDetailGenerationProb).Bits(d.particlesDetailGenerationProb)
                        .Bits(d.breakablesProbability).Bits(d.creaturesDetailGenerationProb).EndArr();
                    r.Prop("middlePerZone", d.middleDetailsPerZone).Prop("breakablesPerZone", d.breakablesPerZone).Prop("creaturesPerZone", d.creaturesPerZone);
                    Trace.Emit(Trace.State(r));
                });
            }
        }

        /// Skeleton pick: the floor's count, the skin list size, and the array the game picked from.
        [HarmonyPatch(typeof(BreakableObjectBehaviour), nameof(BreakableObjectBehaviour.TransformToSkeleton))]
        private static class SkeletonInputs
        {
            [HarmonyPriority(Priority.First)]
            private static void Prefix(BreakableObjectBehaviour __instance)
            {
                if (CaveOracle.inMirror) return;
                Trace.Safe("skeleton", () =>
                {
                    var dm = DungeonManager.Instance;
                    var j = Trace.Ev("skeleton").Prop("chosen", __instance.GetInstanceID())
                        .Prop("deployed", dm != null ? dm.sleketonsDeployed : -1)
                        .Prop("skins", dm != null && dm.skeletonBreakableSkins != null ? dm.skeletonBreakableSkins.Count : -1);
                    j.Key("candidates").Arr();
                    foreach (var b in UnityEngine.Object.FindObjectsOfType<BreakableObjectBehaviour>())
                    {
                        j.Arr().Val(b.GetInstanceID()).Val(b.name).Val(Trace.RoomId(b.GetComponentInParent<DungeonRoom>()));
                        var p = b.transform.position;
                        j.Bits(p.x).Bits(p.y).EndArr();
                    }
                    j.EndArr();
                    Trace.Emit(Trace.State(j));
                });
            }
        }

        /// Chest configuration at Awake (before GenerateFixedDrop's draws), with the save state
        /// that changes the draw count: NG+ and, per fixed drop, whether it's an owned amulet.
        [HarmonyPatch(typeof(Chest), "Awake")]
        private static class ChestConfig
        {
            [HarmonyPriority(Priority.First)]
            private static void Prefix(Chest __instance)
            {
                if (CaveOracle.inMirror) return;
                Trace.Safe("chestConfig", () =>
                {
                    var slot = GameManager.Instance?.currentGameSlot;
                    bool ngp = slot != null && slot.gameType == GameSlot.GameType.NewGamePlus;
                    int ngpLevel = GameManager.Instance != null ? GameManager.Instance.GetCurrentGamePlusLevel() : 0;
                    var j = Trace.Ev("chestConfig").Prop("object", __instance.name).Prop("chestType", (int)__instance.type).Prop("capacity", __instance.capacity)
                        .Prop("generateRandomContentOnInit", __instance.generateRandomContentOnInit).Prop("ngp", ngp);
                    j.Key("dropIncrement").Bits(__instance.dropIncrement);
                    j.Key("fixedDrop").Arr();
                    foreach (var d in __instance.fixedDrop)
                    {
                        var item = ItemDatabase.GetItemByName(d.itemName, ngpLevel);
                        bool ownedAmulet = item is AmuletEquipmentMaster am && AmuletUtility.CheckIfAmuletOwned(am);
                        j.Obj().Prop("item", d.itemName).Prop("min", d.minQuantity).Prop("max", d.maxQuantity).Prop("ngpOnly", d.ngpOnly)
                            .Prop("ownedAmulet", ownedAmulet).Key("prob").Bits(d.prob).EndObj();
                    }
                    j.EndArr();
                    Trace.Emit(Trace.State(j));
                });
            }
        }

        /// Return values of the room methods that produce one (chest name picks, spawned objects).
        [HarmonyPatch]
        private static class Results
        {
            private static IEnumerable<MethodBase> TargetMethods() =>
                AccessTools.GetDeclaredMethods(typeof(DungeonRoom))
                    .Where(m => new[] { "SpawnEnemy", "SpawnChest", "GetChestNameByFloor", "GetWandererChestNameByFloor" }.Contains(m.Name))
                    .Cast<MethodBase>();

            private static void Postfix(MethodBase __originalMethod, object __instance, object __result)
            {
                if (CaveOracle.inMirror) return;
                Trace.Safe("result", () =>
                {
                    var j = Trace.Ev("result").Prop("name", "DungeonRoom." + __originalMethod.Name).Prop("room", Trace.RoomId(__instance));
                    switch (__result)
                    {
                        case string s: j.Prop("value", s); break;
                        case Component c:
                            j.Prop("value", c ? c.name : null);
                            if (c) Vec(j, "pos", c.transform.localPosition);
                            break;
                        case GameObject g:
                            j.Prop("value", g ? g.name : null);
                            if (g) Vec(j, "pos", g.transform.localPosition);
                            break;
                        default: j.Prop("value", __result?.ToString()); break;
                    }
                    Trace.Emit(Trace.State(j));
                });
            }
        }

        private static void Vec(Json j, string key, Vector3 v) => j.Key(key).Arr().Bits(v.x).Bits(v.y).EndArr();

        private static void WriteEnemies(Json j, DungeonRoom room)
        {
            j.Key("enemies").Arr();
            foreach (Enemy en in room.roomEnemies)
            {
                if (en == null) { j.Val((string)null); continue; }
                j.Obj().Prop("name", en.gameObject.name);
                Vec(j, "pos", en.transform.localPosition);
                var champ = AccessTools.Field(en.GetType(), "isChampion");
                if (champ != null && champ.FieldType == typeof(bool)) j.Prop("champion", (bool)champ.GetValue(en));
                j.EndObj();
            }
            j.EndArr();
        }

        private static void WriteChests(Json j, DungeonRoom room)
        {
            j.Key("chests").Arr();
            foreach (Chest ch in room.roomChests)
            {
                if (ch == null) { j.Val((string)null); continue; }
                j.Obj().Prop("name", ch.gameObject.name);
                Vec(j, "pos", ch.transform.localPosition);
                j.EndObj();
            }
            j.EndArr();
        }

        /// Floor finished: RNG state and a snapshot of everything under the DungeonManager.
        public static void OnFloorGenerated()
        {
            Trace.Safe("floorEnd", () =>
            {
                var j = Trace.Ev("floorEnd");
                Trace.State(j);
                var dm = DungeonManager.Instance;
                if (dm != null)
                {
                    j.Key("objects").Arr();
                    Snapshot(j, dm.transform, "");
                    j.EndArr();
                }
                Trace.Emit(j);
            });
        }

        private static uint[] crcTable;

        /// CRC-32 (IEEE) over the pixels as RGBA bytes, row by row from the bottom (GetPixels32's order).
        private static uint Crc32(Color32[] pixels)
        {
            if (crcTable == null)
            {
                crcTable = new uint[256];
                for (uint n = 0; n < 256; n++)
                {
                    uint c = n;
                    for (int k = 0; k < 8; k++) c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                    crcTable[n] = c;
                }
            }
            uint crc = 0xFFFFFFFFu;
            void Add(byte b) => crc = crcTable[(crc ^ b) & 0xFF] ^ (crc >> 8);
            foreach (Color32 p in pixels)
            {
                Add(p.r); Add(p.g); Add(p.b); Add(p.a);
            }
            return crc ^ 0xFFFFFFFFu;
        }

        private static void Snapshot(Json j, Transform t, string path)
        {
            for (int i = 0; i < t.childCount; i++)
            {
                Transform c = t.GetChild(i);
                string p = path.Length == 0 ? c.name : path + "/" + c.name;
                j.Arr().Val(p).Val(c.gameObject.activeSelf).Bits(c.localPosition.x).Bits(c.localPosition.y);
                var sr = c.GetComponent<SpriteRenderer>();
                j.Val(sr != null && sr.sprite != null ? sr.sprite.name : null).Val(sr != null ? sr.sortingOrder : 0);
                j.Val(sr != null && sr.flipX);
                // [7] polygon collider paths (float bits), [8] a runtime-made sprite's texture:
                // [w, h, crc32 of GetPixels32, rect x, y, w, h, pivot x, y] (bits for floats),
                // [9] local scale x, y, [10] sorting layer name.
                var poly = c.GetComponent<PolygonCollider2D>();
                if (poly != null)
                {
                    j.Arr();
                    for (int k = 0; k < poly.pathCount; k++)
                    {
                        j.Arr();
                        foreach (Vector2 v in poly.GetPath(k)) j.Bits(v.x).Bits(v.y);
                        j.EndArr();
                    }
                    j.EndArr();
                }
                else j.Val((string)null);
                Texture2D tex = sr != null && sr.sprite != null ? sr.sprite.texture : null;
                if (tex != null && tex.isReadable)
                {
                    uint crc = 0;
                    try { crc = Crc32(tex.GetPixels32()); } catch (Exception) { }
                    Rect r = sr.sprite.rect;
                    j.Arr().Val(tex.width).Val(tex.height).Val(crc).Bits(r.x).Bits(r.y).Bits(r.width).Bits(r.height)
                        .Bits(sr.sprite.pivot.x).Bits(sr.sprite.pivot.y).EndArr();
                }
                else j.Val((string)null);
                j.Arr().Bits(c.localScale.x).Bits(c.localScale.y).EndArr();
                j.Val(sr != null ? sr.sortingLayerName : null);
                j.EndArr();
                Snapshot(j, c, p);
            }
        }
    }
}
