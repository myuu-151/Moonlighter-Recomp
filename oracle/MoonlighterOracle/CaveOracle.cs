using System;
using System.Collections.Generic;
using System.IO;
using HarmonyLib;
using UnityEngine;
using Random = UnityEngine.Random;

namespace MoonlighterOracle
{
    /// Runs the game's own dungeon layout code (CaveGenerator, miners and the
    /// DungeonGenerator special-room pickers) for many seeds and records the result.
    ///
    /// The driver below mirrors DungeonGenerator.GenerateDungeonCoroutine from
    /// `lastDoorDirectionIndex = ...` through `chestCaveRooms = GeneratChestRooms()`;
    /// everything it calls is the original game code. Real in-game generations are
    /// also captured by the hooks at the bottom, to check the mirror against the game.
    internal static class CaveOracle
    {
        public static bool inMirror;

        private static readonly string[] SpecialFields =
        {
            "initCaveRoom", "bossCaveRoom", "onsenCaveRoom", "homeChestCaveRoom", "wavesCaveRoom",
            "wandererNestCaveRoom", "abandonedItemsCaveRoom", "crazyPeteCaveRoom", "specialDungeonCaveRoom",
            "corruptedCaveRoom",
        };

        private static T Call<T>(DungeonGenerator g, string method) =>
            (T)AccessTools.Method(typeof(DungeonGenerator), method).Invoke(g, null);

        private static void SetF(DungeonGenerator g, string field, object v) =>
            AccessTools.Field(typeof(DungeonGenerator), field).SetValue(g, v);

        private static T GetF<T>(DungeonGenerator g, string field) =>
            (T)AccessTools.Field(typeof(DungeonGenerator), field).GetValue(g);

        public static void Run(string path, int[] levels, int seeds, ItemMaster.Culture culture)
        {
            var host = new GameObject("@OracleDungeonGenerator");
            var gen = host.AddComponent<DungeonGenerator>();
            var savedFloorInfo = DungeonGenerator.floorGenerationSaveInfo;
            inMirror = true;
            try
            {
                using (var w = new StreamWriter(path))
                {
                    foreach (int level in levels)
                    {
                        for (int seed = 1; seed <= seeds; seed++)
                        {
                            w.WriteLine(RunOne(gen, culture, level, seed));
                        }
                    }
                }
            }
            finally
            {
                inMirror = false;
                DungeonGenerator.floorGenerationSaveInfo = savedFloorInfo;
                UnityEngine.Object.Destroy(host);
            }
        }

        private static string RunOne(DungeonGenerator gen, ItemMaster.Culture culture, int level, int seed)
        {
            var j = new Json().Obj().Prop("src", "mirror").Prop("seed", seed).Prop("level", level).Prop("culture", (int)culture);

            // Inputs: ResetValues() from the seed, plus a door index the previous floor would have saved.
            Random.InitState(seed);
            var fg = new DungeonGenerator.FloorGenerationSaveInfo();
            fg.ResetValues();
            fg.lastDoorDirectionIndex = level == 0 ? 0 : seed & 3;
            DungeonGenerator.floorGenerationSaveInfo = fg;
            WriteFloorInfo(j, fg);

            // Fresh generator state (fields the coroutine relies on).
            gen.level = level;
            gen.culture = culture;
            SetF(gen, "usedSpecialCaveRooms", new List<CaveRoom>());
            foreach (string f in SpecialFields) SetF(gen, f, null);
            SetF(gen, "timedChestCaveRooms", null);
            SetF(gen, "chestCaveRooms", null);

            j.Key("stateAtLayout");
            Rng.WriteState(j, Rng.Get());
            try
            {
                Layout(gen, level, fg, specialRoomAvailable: false);
                j.Prop("exception", (string)null);
            }
            catch (Exception e)
            {
                Exception inner = e.InnerException ?? e;
                j.Prop("exception", inner.GetType().Name);
            }
            j.Key("stateAfter");
            Rng.WriteState(j, Rng.Get());
            WriteLayout(j, gen);
            return j.EndObj().ToString();
        }

        /// Mirror of GenerateDungeonCoroutine lines `if (level == 0) lastDoorDirectionIndex = 1` .. `GeneratChestRooms()`.
        private static void Layout(DungeonGenerator gen, int level, DungeonGenerator.FloorGenerationSaveInfo fg, bool specialRoomAvailable)
        {
            int lastDoor = level == 0 ? 1 : (int)AccessTools.Method(typeof(DungeonGenerator), "GetComplementaryDoorIndex").Invoke(null, new object[] { fg.lastDoorDirectionIndex });
            SetF(gen, "lastDoorDirectionIndex", lastDoor);
            int num = level != 0 ? (level != 1 ? Constants.GetInt("roomsInFloor3") : Constants.GetInt("roomsInFloor2")) : Constants.GetInt("roomsInFloor1");

            var cave = new CaveGenerator();
            cave.Init();
            SetF(gen, "cave", cave);
            for (int i = 0; i < 5; i++) cave.CreateMiner(new CommonRoomMiner());

            var init = new SingleCaveRoom();
            switch (lastDoor)
            {
                case 0: init.units[0].blockedNorth = true; break;
                case 1: init.units[0].blockedSouth = true; break;
                case 2: init.units[0].blockedWest = true; break;
                case 3: init.units[0].blockedEast = true; break;
            }
            SetF(gen, "initCaveRoom", init);
            cave.AddRoom(init);
            int num3 = 100;
            while (cave.rooms.Count < num && cave.areMinersAlive && num3 > 0)
            {
                cave.IterateMinners();
                num3--;
            }
            cave.KillAllMiners();

            CaveRoom boss;
            if (level == 2)
            {
                cave.CreateMiner(new BossRoomMiner());
                boss = Call<CaveRoom>(gen, "GenerateBossRoomCaveRoom");
            }
            else
            {
                boss = Call<CaveRoom>(gen, "GenerateNextLevelCaveRoom");
            }
            SetF(gen, "bossCaveRoom", boss);
            GetF<List<CaveRoom>>(gen, "usedSpecialCaveRooms").Add(init);
            SetF(gen, "timedChestCaveRooms", new List<CaveRoom>());
            SetF(gen, "chestCaveRooms", new List<CaveRoom>());
            SetF(gen, "onsenCaveRoom", Call<CaveRoom>(gen, "GenerateCaveOnsen"));
            if (level == fg.dungeonToHomeFloor) SetF(gen, "homeChestCaveRoom", Call<CaveRoom>(gen, "GenerateHomeChestCaveRoom"));
            if (level == fg.wavesRoomFloor) SetF(gen, "wavesCaveRoom", Call<CaveRoom>(gen, "GenerateWavesCaveRoom"));
            if (level == fg.abandonedItemsFloor) SetF(gen, "abandonedItemsCaveRoom", Call<CaveRoom>(gen, "GenerateAbandonedItemsCaveRoom"));
            if (level == fg.wandererNestRoomFloor) SetF(gen, "wandererNestCaveRoom", Call<CaveRoom>(gen, "GenerateWandererCaveRoom"));
            SetF(gen, "timedChestCaveRooms", Call<List<CaveRoom>>(gen, "GenerateTimedChestCaveRooms"));
            SetF(gen, "crazyPeteCaveRoom", Call<CaveRoom>(gen, "GenerateCrazyPeteCaveRoom"));
            if (level == fg.specialDungeonRoom && specialRoomAvailable) SetF(gen, "specialDungeonCaveRoom", Call<CaveRoom>(gen, "GenerateCrazyPeteCaveRoom"));
            if (level == fg.corruptedRoomFloor) SetF(gen, "corruptedCaveRoom", Call<CaveRoom>(gen, "GenerateCorruptedCaveRoom"));
            SetF(gen, "chestCaveRooms", Call<List<CaveRoom>>(gen, "GeneratChestRooms"));
        }

        private static void WriteFloorInfo(Json j, DungeonGenerator.FloorGenerationSaveInfo fg)
        {
            j.Key("floorInfo").Obj()
                .Prop("lastDoorDirectionIndex", fg.lastDoorDirectionIndex)
                .Prop("wavesRoomFloor", fg.wavesRoomFloor)
                .Prop("wandererNestRoomFloor", fg.wandererNestRoomFloor)
                .Prop("abandonedItemsFloor", fg.abandonedItemsFloor)
                .Prop("dungeonToHomeFloor", fg.dungeonToHomeFloor)
                .Prop("corruptedRoomFloor", fg.corruptedRoomFloor)
                .Prop("crazyPeteRoomFloor", fg.crazyPeteRoomFloor)
                .Prop("specialDungeonRoom", fg.specialDungeonRoom)
                .Prop("desertHardEnemiesRoom", fg.desertHardEnemiesRoom)
                .EndObj();
        }

        private static void Vec(Json j, Vector2 v) => j.Arr().Bits(v.x).Bits(v.y).EndArr();

        /// Writes the cave graph. Rooms are referenced by their index in cave.rooms.
        public static void WriteLayout(Json j, DungeonGenerator gen)
        {
            var cave = GetF<CaveGenerator>(gen, "cave");
            if (cave == null) { j.Key("layout").Val((string)null); return; }
            var index = new Dictionary<CaveRoom, int>();
            for (int i = 0; i < cave.rooms.Count; i++) index[cave.rooms[i]] = i;
            int Idx(CaveRoom r) => r == null ? -1 : index.TryGetValue(r, out int k) ? k : -2;

            j.Key("layout").Obj();
            j.Key("minPos"); Vec(j, cave.minPos);
            j.Key("maxPos"); Vec(j, cave.maxPos);
            j.Prop("miners", cave.miners.Count);
            j.Key("rooms").Arr();
            foreach (CaveRoom r in cave.rooms)
            {
                j.Obj().Prop("t", r is BossCaveRoom ? "B" : r is SingleCaveRoom ? "S" : r.GetType().Name);
                j.Key("p"); Vec(j, r.position);
                j.Key("u").Arr();
                foreach (CaveRoomUnit u in r.units)
                {
                    j.Obj().Key("l"); Vec(j, u.localPos);
                    j.Prop("n", Idx(u.connectedNorth)).Prop("s", Idx(u.connectedSouth)).Prop("e", Idx(u.connectedEast)).Prop("w", Idx(u.connectedWest));
                    j.Prop("b", (u.blockedNorth ? 1 : 0) | (u.blockedSouth ? 2 : 0) | (u.blockedEast ? 4 : 0) | (u.blockedWest ? 8 : 0));
                    j.EndObj();
                }
                j.EndArr();
                j.Key("path").Arr();
                foreach (Vector2 v in r.pathFromInit) Vec(j, v);
                j.EndArr().EndObj();
            }
            j.EndArr();
            j.Key("special").Obj();
            foreach (string f in SpecialFields) j.Prop(f, Idx(GetF<CaveRoom>(gen, f)));
            foreach (string f in new[] { "timedChestCaveRooms", "chestCaveRooms" })
            {
                j.Key(f).Arr();
                var list = GetF<List<CaveRoom>>(gen, f);
                if (list != null) foreach (CaveRoom r in list) j.Val(Idx(r));
                j.EndArr();
            }
            j.Key("used").Arr();
            foreach (CaveRoom r in GetF<List<CaveRoom>>(gen, "usedSpecialCaveRooms")) j.Val(Idx(r));
            j.EndArr();
            j.EndObj().EndObj();
        }

        // ---- Hooks on real in-game generation ------------------------------------------------

        private static uint[] stateAtCaveInit;

        [HarmonyPatch(typeof(CaveGenerator), nameof(CaveGenerator.Init))]
        private static class CaveInitHook
        {
            private static void Prefix() { if (!inMirror) stateAtCaveInit = Rng.Get(); }
        }

        [HarmonyPatch(typeof(DungeonGenerator), "GeneratChestRooms")]
        private static class ChestRoomsHook
        {
            private static void Postfix(DungeonGenerator __instance, List<CaveRoom> __result)
            {
                if (inMirror) return;
                try
                {
                    var j = new Json().Obj().Prop("src", "game").Prop("level", __instance.level).Prop("culture", (int)__instance.culture)
                        .Prop("time", DateTime.Now.ToString("o"));
                    WriteFloorInfo(j, DungeonGenerator.floorGenerationSaveInfo);
                    j.Prop("lastDoorDirectionIndex", GetF<int>(__instance, "lastDoorDirectionIndex"));
                    j.Key("stateAtCaveInit");
                    Rng.WriteState(j, stateAtCaveInit ?? new uint[4]);
                    j.Key("stateAfter");
                    Rng.WriteState(j, Rng.Get());
                    // chestCaveRooms is assigned by the caller after this returns; record it from __result.
                    SetF(__instance, "chestCaveRooms", __result);
                    WriteLayout(j, __instance);
                    File.AppendAllText(Path.Combine(OraclePlugin.OutDir, "cave_game.jsonl"), j.EndObj() + "\n");
                }
                catch (Exception e)
                {
                    OraclePlugin.Log.LogError("cave hook: " + e);
                }
            }
        }
    }
}
