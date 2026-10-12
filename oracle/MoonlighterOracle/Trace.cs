using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.CompilerServices;

namespace MoonlighterOracle
{
    /// Event log for real dungeon generations: one JSON object per line in trace.jsonl.
    /// Every event carries the floor number (per session) and, where relevant, a room id
    /// (order the room was added on that floor) and the RNG state at that point.
    internal static class Trace
    {
        public static readonly string Session = DateTime.Now.ToString("yyyyMMdd-HHmmss");
        public static int Floor;
        private static StreamWriter writer;
        private static readonly ConditionalWeakTable<object, object> roomIds = new ConditionalWeakTable<object, object>();
        private static int nextRoomId;

        public static void BeginFloor()
        {
            Floor++;
            nextRoomId = 0;
        }

        public static int RoomId(object room)
        {
            if (room == null) return -1;
            return roomIds.TryGetValue(room, out object id) ? (int)id : -1;
        }

        public static int AssignRoomId(object room)
        {
            int id = nextRoomId++;
            roomIds.Remove(room);
            roomIds.Add(room, id);
            return id;
        }

        /// Starts an event with the common header; finish with Emit().
        public static Json Ev(string type)
        {
            return new Json().Obj().Prop("session", Session).Prop("floor", Floor).Prop("ev", type);
        }

        public static Json State(Json j, string key = "state")
        {
            j.Key(key);
            return Rng.WriteState(j, Rng.Get());
        }

        public static void Emit(Json j)
        {
            if (writer == null)
            {
                writer = new StreamWriter(Path.Combine(OraclePlugin.OutDir, "trace.jsonl"), append: true);
                writer.AutoFlush = true;
            }
            writer.WriteLine(j.EndObj().ToString());
        }

        public static void Safe(string where, Action a)
        {
            try { a(); }
            catch (Exception e) { OraclePlugin.Log.LogError(where + ": " + e); }
        }

        public static Json Names<T>(Json j, string key, IEnumerable<T> items, Func<T, string> name)
        {
            j.Key(key).Arr();
            if (items != null) foreach (T x in items) j.Val(name(x));
            return j.EndArr();
        }
    }
}
