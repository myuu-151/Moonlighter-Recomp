using System;
using System.Collections;
using System.Diagnostics;
using System.IO;
using BepInEx;
using BepInEx.Logging;
using HarmonyLib;
using UnityEngine;

namespace MoonlighterOracle
{
    /// Reference-data recorder for the Moonlighter port.
    ///
    /// Always on: hooks that log real in-game dungeon generations to data/oracle/cave_game.jsonl.
    /// With `-oracle-run` on the command line: once the game has booted, records RNG and
    /// dungeon-layout reference data, then quits if `-oracle-quit` is also given.
    [BepInPlugin("moonlighter.oracle", "Moonlighter Oracle", "0.1.0")]
    public class OraclePlugin : BaseUnityPlugin
    {
        public static string OutDir;
        public static ManualLogSource Log;

        private void Awake()
        {
            Log = Logger;
            OutDir = Path.GetFullPath(Path.Combine(Paths.GameRootPath, "..", "data", "oracle"));
            // -oracle-out=<dir>: write there instead. The recording queue (tools/recq) gives each
            // run its own folder this way, so queued runs never touch main's data/oracle.
            string outArg = Array.Find(Environment.GetCommandLineArgs(), a => a.StartsWith("-oracle-out="));
            if (outArg != null && outArg.Length > "-oracle-out=".Length) OutDir = Path.GetFullPath(outArg.Substring("-oracle-out=".Length));
            Directory.CreateDirectory(OutDir);
            var harmony = new Harmony("moonlighter.oracle");
            harmony.PatchAll(typeof(OraclePlugin).Assembly);
            FrameProbe.Install(harmony);
            DungeonGenerator.OnGeneratedDungeon += RoomOracle.OnFloorGenerated;
            Log.LogInfo("Oracle output: " + OutDir);

            string[] args = Environment.GetCommandLineArgs();
            bool quit = Array.IndexOf(args, "-oracle-quit") >= 0;
            if (Unobtrusive.Wanted(args)) StartCoroutine(Unobtrusive.Keep());
            if (Array.IndexOf(args, "-oracle-run") >= 0)
                StartCoroutine(RunAll(quit));
            string harvestArg = Array.Find(args, a => a.StartsWith("-oracle-harvest"));
            if (harvestArg != null)
            {
                int runs = harvestArg.Contains("=") && int.TryParse(harvestArg.Substring(harvestArg.IndexOf('=') + 1), out int r) ? r : 3;
                Harvest.InstallSafety(harmony);
                StartCoroutine(Harvest.Run(this, runs, quit));
            }
            string walkArg = Array.Find(args, a => a.StartsWith("-oracle-walk"));
            if (walkArg != null)
            {
                Harvest.InstallSafety(harmony);
                StartCoroutine(Walk.Run(this, quit, walkArg.Contains("=") ? walkArg.Substring(walkArg.IndexOf('=') + 1) : ""));
            }
            string navArg = Array.Find(args, a => a.StartsWith("-oracle-polynav"));
            if (navArg != null)
            {
                Harvest.InstallSafety(harmony);
                StartCoroutine(PolyNavOracle.Run(this, quit, navArg.Contains("=") ? navArg.Substring(navArg.IndexOf('=') + 1) : ""));
            }
            string slimeArg = Array.Find(args, a => a.StartsWith("-oracle-slime"));
            if (slimeArg != null)
            {
                Harvest.InstallSafety(harmony);
                StartCoroutine(EnemyOracle.Run(this, quit, slimeArg.Contains("=") ? slimeArg.Substring(slimeArg.IndexOf('=') + 1) : "", "BabySlime", true));
            }
            // -oracle-enemy=<prefab>[:<room name>] (e.g. -oracle-enemy=GolemSoldier): enemy_<prefab>.jsonl.
            string enemyArg = Array.Find(args, a => a.StartsWith("-oracle-enemy="));
            if (enemyArg != null)
            {
                string spec = enemyArg.Substring(enemyArg.IndexOf('=') + 1);
                int colon = spec.IndexOf(':');
                string enemy = colon >= 0 ? spec.Substring(0, colon) : spec;
                string room = colon >= 0 ? spec.Substring(colon + 1) : "";
                if (enemy == "") Log.LogError("-oracle-enemy needs a prefab name (-oracle-enemy=GolemSoldier)");
                else
                {
                    Harvest.InstallSafety(harmony);
                    StartCoroutine(EnemyOracle.Run(this, quit, room, enemy, false));
                }
            }
            // The Tangle lane (TangleOracle.cs; to join the generic enemy oracle).
            string tangleArg = Array.Find(args, a => a.StartsWith("-oracle-tangle"));
            if (tangleArg != null)
            {
                Harvest.InstallSafety(harmony);
                StartCoroutine(TangleOracle.Run(this, quit, tangleArg.Contains("=") ? tangleArg.Substring(tangleArg.IndexOf('=') + 1) : ""));
            }
        }

        private IEnumerator RunAll(bool quit)
        {
            // Wait for GameManager to load the constants table.
            while (GameManager.Instance == null || Constants.GetInt("roomsInFloor1") == 0)
                yield return null;
            yield return new WaitForSeconds(1f);

            uint[] saved = Rng.Get();
            var sw = Stopwatch.StartNew();
            try
            {
                File.WriteAllText(Path.Combine(OutDir, "constants.json"), Resources.Load<TextAsset>("constants").text);

                Rng.Probe(Path.Combine(OutDir, "rng.jsonl"), 64);
                Log.LogInfo($"rng probe done ({sw.ElapsedMilliseconds} ms)");

                CaveOracle.Run(Path.Combine(OutDir, "cave_mirror.jsonl"), new[] { 0, 1, 2 }, 1000, ItemMaster.Culture.Forest);
                Log.LogInfo($"cave mirror done ({sw.ElapsedMilliseconds} ms)");

                File.WriteAllText(Path.Combine(OutDir, "DONE"), DateTime.Now.ToString("o"));
            }
            catch (Exception e)
            {
                Log.LogError("oracle run failed: " + e);
                File.WriteAllText(Path.Combine(OutDir, "FAILED"), e.ToString());
            }
            finally
            {
                Rng.Set(saved);
            }
            if (quit) Application.Quit();
        }
    }
}
