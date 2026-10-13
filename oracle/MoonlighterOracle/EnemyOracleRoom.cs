using HarmonyLib;

namespace MoonlighterOracle
{
    /// `-oracle-enemy`'s record of the room's DungeonRoom.DisableEnemies (game/EnemyRoom.h in the
    /// port; tests/enemy_replay_room.h replays it): ExitRoom runs it when the hero goes through a
    /// door or falls into the hole over a secret room (enemy_BrokenGolemTurret.jsonl, frame 208),
    /// and every enemy still in roomEnemies is made inactive.
    ///   - events: "disableEnemies" (the recorded room's DisableEnemies, before it runs): its fixed
    ///     step (-1: the frame's part) and "updates", how many of the frame's recorded enemy
    ///     Updates ran before it.
    internal static partial class EnemyOracle
    {
        [HarmonyPatch(typeof(DungeonRoom), "DisableEnemies")]
        private static class DisableEnemiesHook
        {
            private static void Prefix(DungeonRoom __instance)
            {
                if (__instance == null || __instance != targetRoom) return;
                Add(new Ev { k = "disableEnemies", step = Step, type = updates.Count });
            }
        }

        /// The events of this file's hooks (false: not one of them).
        private static bool WriteRoomEvent(Json j, Ev e)
        {
            switch (e.k)
            {
                case "disableEnemies":
                    j.Prop("updates", e.type);
                    return true;
            }
            return false;
        }
    }
}
