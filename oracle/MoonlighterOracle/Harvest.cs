using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using DigitalSunGames.Platforms;
using HarmonyLib;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace MoonlighterOracle
{
    /// `-oracle-harvest[=N]`: load the first save slot like the main menu's Continue, then
    /// generate N runs of floors 1-3 in each dungeon the way DungeonGenerationTester does,
    /// with every room traced. Saving and Steam stats/achievements are blocked for the whole
    /// session, so the player's save and Steam profile are never touched.
    internal static class Harvest
    {
        public static bool Active;
        private static bool floorDone;

        private static readonly ItemMaster.Culture[] Cultures =
            { ItemMaster.Culture.Golem, ItemMaster.Culture.Forest, ItemMaster.Culture.Desert, ItemMaster.Culture.Tech };

        public static void InstallSafety(Harmony h)
        {
            Active = true;
            var skip = new HarmonyMethod(typeof(Harvest), nameof(SkipVoid));
            var skipTrue = new HarmonyMethod(typeof(Harvest), nameof(SkipReturnTrue));
            var skipAsync = new HarmonyMethod(typeof(Harvest), nameof(SkipWriteAsync));
            int n = 0;

            void Patch(MethodBase m, HarmonyMethod prefix)
            {
                if (m == null || m.IsAbstract) return;
                h.Patch(m, prefix: prefix);
                n++;
            }

            foreach (Type t in typeof(GameManager).Assembly.GetTypes())
            {
                if (t.IsInterface || t.IsAbstract) continue;
                if (typeof(IPersistenFilesController).IsAssignableFrom(t))
                {
                    Patch(AccessTools.DeclaredMethod(t, "WriteBytes"), skipTrue);
                    Patch(AccessTools.DeclaredMethod(t, "WriteBytesAsync"), skipAsync);
                }
                if (typeof(IAchievementController).IsAssignableFrom(t))
                {
                    foreach (string name in new[] { "SetStatValue", "AddToStatValue", "UnlockAchievement", "ResetAchievementsAndStats" })
                        Patch(AccessTools.DeclaredMethod(t, name), skip);
                }
            }
            Patch(AccessTools.Method(typeof(GameManager), nameof(GameManager.SerializeGameSlot)), skip);
            OraclePlugin.Log.LogInfo($"harvest safety: {n} save/stat methods blocked");

            // In harvest sessions the DLC fallback inside Get(Common)ObstacleByName throws (the
            // DLC controller/bundle lookup hits null) where real play gets a clean null. Turn
            // that exception into the null real play sees; the lookup draws no RNG.
            var swallow = new HarmonyMethod(typeof(Harvest), nameof(ObstacleLookupFinalizer));
            h.Patch(AccessTools.Method(typeof(ObstaclePrefabRegister), nameof(ObstaclePrefabRegister.GetObstacleByName)), finalizer: swallow);
            h.Patch(AccessTools.Method(typeof(ObstaclePrefabRegister), nameof(ObstaclePrefabRegister.GetCommonObstacleByName)), finalizer: swallow);
        }

        private static int swallowed;

        private static Exception ObstacleLookupFinalizer(Exception __exception, MethodBase __originalMethod, object[] __args, ref GameObject __result)
        {
            if (__exception is NullReferenceException)
            {
                __result = null;
                if (swallowed++ == 0)
                {
                    var gm = GameManager.Instance;
                    var handles = Platform.DLC.GetByCategory("DUNGEON");
                    OraclePlugin.Log.LogWarning($"harvest: DLC obstacle lookup threw; controllers={gm?.dlcControllers?.Count}, " +
                        $"DUNGEON handles={handles?.Length}, matched={(handles != null && handles.Length > 0 ? (gm.dlcControllers.Find(x => x.CanManageDLC(handles[0])) != null).ToString() : "-")}");
                }
                Trace.Safe("swallow", () => Trace.Emit(Trace.State(Trace.Ev("harvestLookupNull").Prop("method", __originalMethod.Name)
                    .Prop("asset", __args.Length > 1 ? __args[1] as string : __args[0] as string))));
                return null;
            }
            return __exception;
        }

        private static bool SkipVoid() => false;

        private static bool SkipReturnTrue(ref bool __result)
        {
            __result = true;
            return false;
        }

        private static bool SkipWriteAsync(Action callback)
        {
            callback?.Invoke();
            return false;
        }

        public static IEnumerator Run(MonoBehaviour host, int runs, bool quit)
        {
            var log = OraclePlugin.Log;

            // Stay light on the user's machine: Idle priority on two cores. (No frame-rate cap:
            // it shifts the timing of the floor transitions' panels and stalls them.)
            Trace.Safe("throttle", () =>
            {
                var proc = System.Diagnostics.Process.GetCurrentProcess();
                proc.PriorityClass = System.Diagnostics.ProcessPriorityClass.Idle;
                proc.ProcessorAffinity = (IntPtr)0x3;
            });

            // 1. Main menu: wait for the save data, then continue the first used slot.
            float t0 = Time.realtimeSinceStartup;
            GameSlot slot = null;
            while (slot == null)
            {
                var gm = GameManager.Instance;
                if (gm != null && gm.gameSaveData != null && gm.gameSaveData.gameSlots != null && GUIManager.Instance != null)
                    slot = gm.gameSaveData.gameSlots.FirstOrDefault(s => s != null && s.seed != 0);
                if (Time.realtimeSinceStartup - t0 > 120f)
                {
                    log.LogError("harvest: no save slot found");
                    yield break;
                }
                yield return new WaitForSecondsRealtime(1f);
            }
            // The main menu calls CheckDLCIntegrity when it opens; that registers the DLC
            // controllers (Wanderer DLC hooks, DLC asset lookups). Wait for it like a player would.
            float menuWait = Time.realtimeSinceStartup;
            while (!dlcChecked && Time.realtimeSinceStartup - menuWait < 120f)
            {
                // The main menu can show an "important news" popup (e.g. Moonlighter 2 launch)
                // that holds GUIManager's panel slot. Answer it with "Later" (its decline button
                // just calls Disable); never "Read More", which opens a browser/overlay.
                var panel = GUIManager.Instance != null ? GUIManager.Instance.currentInventory : null;
                if (panel is DigitalSun.Moonlighter.InGameNewsPopup news && news.IsActive())
                {
                    log.LogInfo("harvest: dismissing the main menu news popup (Later)");
                    news.Disable();
                }
                yield return new WaitForSecondsRealtime(1f);
            }
            if (!dlcChecked)
            {
                log.LogWarning("harvest: main menu never checked DLC; calling CheckDLCIntegrity directly");
                GameManager.Instance.CheckDLCIntegrity();
            }
            yield return new WaitForSecondsRealtime(3f);
            log.LogInfo($"harvest: loading slot {slot.seed} (DLC controllers: {GameManager.Instance.dlcControllers.Count})");
            GameManager.Instance.currentGameSlot = slot;
            GUIManager.Instance.GoToDungeonLoadingPanel();
            GameManager.Instance.LoadSlot(slot);

            // 2. Wait for the hero to exist and the town to settle.
            while (HeroMerchant.Instance == null) yield return null;
            yield return new WaitForSecondsRealtime(15f);

            // 3. Generate floors through the game's own transition (GameManager.LoadDungeonScene),
            //    as the dungeon gate and next-floor doors do: it loads DLC dungeon bundles, the
            //    Dungeon scene and the loading panel, which starts generation.
            DungeonGenerator.OnGeneratedDungeon += OnGenerated;
            int floors = 0;
            for (int run = 0; run < runs; run++)
            {
                foreach (var culture in Cultures)
                {
                    int seed = 7919 * (run + 1) + 31 * (int)culture;
                    UnityEngine.Random.InitState(seed);
                    for (int level = 0; level < 3; level++)
                    {
                        var visited = slot.maxDungeonLevelVisited.ToArray();
                        Trace.Emit(Trace.State(Trace.Ev("harvestFloor").Prop("run", run).Prop("seed", seed)
                            .Prop("culture", (int)culture).Prop("level", level)));
                        // Like a player: dismiss the floor's welcome scroll / open panel (it pauses
                        // the game, which stalls the transition's fade), then take the gate (floor 1)
                        // or the next-floor door (HeroMerchant does exactly this for both).
                        for (int tries = 0; tries < 5 && GUIManager.Instance.currentInventory != null; tries++)
                        {
                            GUIManager.Instance.DisableCurrentPanel();
                            yield return new WaitForSecondsRealtime(0.5f);
                        }
                        if (GameManager.Instance.isGamePaused) GameManager.Instance.UnpauseGame();
                        floorDone = false;
                        float start = Time.realtimeSinceStartup;
                        if (level == 0) HeroMerchant.Instance.LoadDungeonScene(culture);
                        else HeroMerchant.Instance.LoadNextLevelDungeonScene(culture);
                        while (!floorDone && Time.realtimeSinceStartup - start < 120f) yield return null;
                        if (!floorDone) log.LogError($"harvest: floor timed out ({culture} {level})");
                        // Give the floor's welcome scroll time to appear so the dismissal catches it.
                        yield return new WaitForSecondsRealtime(3f);
                        var col = HeroMerchant.Instance != null ? HeroMerchant.Instance.GetComponentInChildren<Collider2D>() : null;
                        if (col != null) col.enabled = false;

                        for (int i = 0; i < visited.Length && i < slot.maxDungeonLevelVisited.Length; i++)
                            slot.maxDungeonLevelVisited[i] = visited[i];
                        floors++;
                        log.LogInfo($"harvest: {floors} floors ({culture} floor {level + 1}, run {run + 1}/{runs})");
                    }
                }
            }
            DungeonGenerator.OnGeneratedDungeon -= OnGenerated;
            System.IO.File.WriteAllText(System.IO.Path.Combine(OraclePlugin.OutDir, "HARVEST_DONE"), $"{floors} floors {DateTime.Now:o}");
            log.LogInfo("harvest: done");
            if (quit) Application.Quit();
        }

        private static void OnGenerated() => floorDone = true;

        /// GoToDungeonLoadingPanel only works when no panel is open. A floor's welcome scroll can
        /// open between the harvest's dismissal and the transition's call, which then silently
        /// skips the loading panel (and generation). In harvest sessions, close it right there,
        /// as a player would before taking the door; also clear a stale inactive panel, which
        /// DisableCurrentPanel ignores.
        [HarmonyPatch(typeof(GUIManager), nameof(GUIManager.GoToDungeonLoadingPanel),
            new[] { typeof(bool), typeof(ItemMaster.Culture), typeof(int), typeof(bool) })]
        private static class ClearPanelBeforeLoading
        {
            private static void Prefix(GUIManager __instance)
            {
                if (!Active) return;
                var panel = __instance.currentInventory;
                if (panel == null) return;
                // Includes the previous floor's loading panel: it should have closed itself when
                // that floor finished, but DisableCurrentPanel ignores panels it sees as inactive,
                // leaving it in the slot.
                OraclePlugin.Log.LogInfo($"harvest: closing {panel.GetType().Name} (active={panel.IsActive()}) before the loading panel");
                if (panel.IsActive()) __instance.DisableCurrentPanel();
                if (__instance.currentInventory == panel) __instance.currentInventory = null;
            }
        }

        private static bool dlcChecked;

        [HarmonyPatch(typeof(GameManager), nameof(GameManager.CheckDLCIntegrity))]
        private static class DlcCheckHook
        {
            private static void Postfix() => dlcChecked = true;
        }
    }
}
