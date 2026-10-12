using System;
using System.Collections;
using System.Collections.Generic;
using HarmonyLib;
using UnityEngine;

namespace MoonlighterOracle
{
    /// `-oracle-enemy=FlyingRepairGolem`'s additions (game/FlyingRepairGolem and game/RoomDrops in
    /// the port): the golems' own fields, the room's dropped items they go after, and items
    /// dropped on purpose so they have something to steal.
    ///
    /// Items get ids here (ItemStack objects, in the order first seen). Recorded:
    ///   - the start ("enemyStart"): "repairItems" (every ItemStack in the room's drop list or
    ///     carried by a golem: see "items" below), "drops" (the room's _droppedItems, ids in
    ///     order), "dropSubs" (OnDropListChanged's invocation list: each handler's golem id, -1
    ///     for a subscriber that isn't one of them);
    ///   - each golem ("enemy", a frame's "enemies"): "repair": patrolLapse, moveDirection (x, y,
    ///     z), myPos, wasHit, velocityPercent, targetItem (id, -1 none, -2 a destroyed one),
    ///     stolenItems (ids), itemSprite's sprite (name or null), the transform's z, its
    ///     subscriptions (how many of the room's handlers are its OnItemDroppedInRoom);
    ///   - each frame's "items" (every item seen so far: [id, x, y, z, activeInHierarchy, enabled,
    ///     destroyed, the golem it's parented to (-1 none), its sprite's name, the master's
    ///     name]), "drops", "dropSubs" at its end, and "rev", what happened in order (with the
    ///     fixed step, -1 none):
    ///       "upd"     a golem's Update begins: its target (id, position, isActiveAndEnabled), its
    ///                 transform's z and the hero's;
    ///       "dropList" its OnItemDroppedInRoom: the list (ids), who changed it (the script frame
    ///                 calling the delegate), the Random state before and after, the target after;
    ///       "add"     the room's AddDropItems: the items (with where they are), the enemy whose
    ///                 DropItemsOnDeath it is (-1 none), golem Updates run so far this frame, the
    ///                 Random state before and after (its handlers' draws);
    ///       "remove"  RemoveDropItem (the item, its caller), "heroPick" OnHeroPickedItem;
    ///       "trigger" a golem's OnTriggerEnter2D: the other object's name and tag, its item id
    ///                 (-1: not an ItemStack), whether it's the golem's target's object;
    ///       "drop"    ItemStack.Drop(position, radius, parent): the item, where, the radius,
    ///                 whether the parent is the room, the Random state before and after.
    ///
    /// The scripted drops: at script frames 100, 330, 560 and 800 an "Iron Bar" ItemStack is
    /// created, Drop'ped at the live golem farthest from the hero (30 around, into the room) and
    /// added to the room's drop list, from a coroutine (after the frame's Updates).
    internal static partial class EnemyOracle
    {
        private static bool repairActive;
        private static readonly Dictionary<ItemStack, int> itemIds = new Dictionary<ItemStack, int>();
        private static readonly List<ItemStack> itemObjects = new List<ItemStack>();
        private static readonly List<RepairEv> repairEvents = new List<RepairEv>();
        private static int repairUpdates; // golem Updates this frame
        private static int repairDroppingEnemy = -1;
        private static GameObject repairDropper;
        private static readonly int[] RepairDropFrames = { 100, 330, 560, 800 };

        private class RepairEv
        {
            public string k, caller, other, otherTag;
            public int s = -1, step = -1, item = -1, by = -1, upd, target = -1;
            public bool flag;
            public float a, z, heroZ;
            public Vector2 pos;
            public List<int> ids;
            public List<Vector2> positions;
            public uint[] rng0, rng1;
        }

        private static bool RepairRecording => repairActive && recording;

        private static int ItemId(ItemStack item)
        {
            if (item is null) return -1;
            if (itemIds.TryGetValue(item, out int id)) return id;
            if (item == null) return -2; // (destroyed before it was seen)
            id = itemObjects.Count;
            itemIds[item] = id;
            itemObjects.Add(item);
            return id;
        }

        private static DungeonRoom RepairRoom => DungeonManager.Instance != null ? DungeonManager.Instance.currentDungeonRoom : null;

        // ---- EnemyOracle's lines ----------------------------------------------------------

        private static void RepairBegin(MonoBehaviour host)
        {
            repairActive = prefab == "FlyingRepairGolem";
            itemIds.Clear();
            itemObjects.Clear();
            repairEvents.Clear();
            if (!repairActive) return;
            var room = RepairRoom;
            if (room != null) foreach (var item in room.GetDropList()) ItemId(item);
            foreach (var b in slimes)
                if (b is FlyingRepairGolemBehaviour g)
                    foreach (var item in g.GetStolenItems()) ItemId(item);
            // SoundFlyingRepairPick.PlayFlyingRepairPick, a clip event on another component of
            // the golem's object: its draws as the behaviour's clip events'.
            try
            {
                var m = AccessTools.Method(typeof(SoundFlyingRepairPick), "PlayFlyingRepairPick");
                if (m != null && animPatched.Add(m))
                    new Harmony("moonlighter.oracle.enemy.repair").Patch(m, prefix: new HarmonyMethod(typeof(EnemyOracle), nameof(AnimPrefix)),
                        postfix: new HarmonyMethod(typeof(EnemyOracle), nameof(AnimPostfix)));
            }
            catch (Exception e)
            {
                OraclePlugin.Log.LogWarning(Tag + ": can't hook PlayFlyingRepairPick: " + e.Message);
            }
            repairDropper = new GameObject("OracleRepairDropper");
            repairDropper.AddComponent<RepairDropper>();
        }

        private static void RepairEnd()
        {
            repairActive = false;
            if (repairDropper != null) UnityEngine.Object.Destroy(repairDropper);
            repairDropper = null;
        }

        private static void RepairClear()
        {
            repairEvents.Clear();
            repairUpdates = 0;
        }

        private static void WriteItem(Json j, ItemStack item)
        {
            int id = ItemId(item);
            bool gone = item == null;
            j.Arr().Val(id);
            Vector3 p = !gone ? item.transform.position : Vector3.zero;
            j.Bits(p.x).Bits(p.y).Bits(p.z);
            j.Val(!gone && item.gameObject.activeInHierarchy).Val(!gone && item.enabled).Val(gone);
            int parent = -1;
            if (!gone && item.transform.parent != null)
            {
                var pb = item.transform.parent.GetComponent<EnemyBehaviour>();
                parent = pb != null ? slimes.IndexOf(pb) : -1;
            }
            j.Val(parent);
            j.Val(!gone && item.itemImage != null && item.itemImage.sprite != null ? item.itemImage.sprite.name : null);
            j.Val(!gone && item.master != null ? item.master.name : null);
            j.EndArr();
        }

        private static void WriteDropState(Json j, string itemsKey)
        {
            var room = RepairRoom;
            j.Key("drops").Arr();
            if (room != null) foreach (var item in room.GetDropList()) j.Val(ItemId(item));
            j.EndArr();
            j.Key("dropSubs").Arr();
            var handlers = room != null ? Traverse.Create(room).Field("OnDropListChanged").GetValue<Action<List<ItemStack>>>() : null;
            if (handlers != null)
                foreach (var d in handlers.GetInvocationList())
                    j.Val(d.Target is EnemyBehaviour b ? slimes.IndexOf(b) : -1);
            j.EndArr();
            j.Key(itemsKey).Arr();
            for (int i = 0; i < itemObjects.Count; i++) WriteItem(j, itemObjects[i]);
            j.EndArr();
        }

        private static void RepairWriteStart(Json j)
        {
            if (repairActive) WriteDropState(j, "repairItems");
        }

        private static void RepairWriteEnemy(Json j, EnemyBehaviour b)
        {
            if (!repairActive || !(b is FlyingRepairGolemBehaviour g)) return;
            var tr = Traverse.Create(g);
            Vector3 md = tr.Field("moveDirection").GetValue<Vector3>();
            Vector2 myPos = tr.Field("myPos").GetValue<Vector2>();
            j.Key("repair").Obj().Key("lapse").Bits(tr.Field("patrolLapse").GetValue<float>());
            j.Key("md").Arr().Bits(md.x).Bits(md.y).Bits(md.z).EndArr();
            j.Key("myPos"); V2(j, myPos);
            j.Prop("wasHit", tr.Field("wasHit").GetValue<bool>()).Key("vp").Bits(tr.Field("velocityPercent").GetValue<float>());
            j.Prop("target", ItemId(tr.Field("targetItem").GetValue<ItemStack>()));
            j.Key("stolen").Arr();
            foreach (var item in g.GetStolenItems()) j.Val(ItemId(item));
            j.EndArr();
            j.Prop("itemSprite", g.itemSprite != null && g.itemSprite.sprite != null ? g.itemSprite.sprite.name : null);
            j.Key("z").Bits(g.transform.position.z);
            int subs = 0;
            var room = RepairRoom;
            var handlers = room != null ? Traverse.Create(room).Field("OnDropListChanged").GetValue<Action<List<ItemStack>>>() : null;
            if (handlers != null)
                foreach (var d in handlers.GetInvocationList())
                    if (ReferenceEquals(d.Target, g)) subs++;
            j.Prop("subs", subs).EndObj();
        }

        private static void RepairWriteFrame(Json j)
        {
            if (!repairActive) return;
            WriteDropState(j, "items");
            j.Key("rev").Arr();
            foreach (var e in repairEvents)
            {
                j.Obj().Prop("k", e.k).Prop("s", e.s).Prop("step", e.step);
                switch (e.k)
                {
                    case "upd":
                        j.Prop("target", e.target).Key("pos"); V2(j, e.pos);
                        j.Prop("on", e.flag).Key("z").Bits(e.z).Key("heroZ").Bits(e.heroZ);
                        break;
                    case "dropList":
                        j.Key("ids").Arr();
                        foreach (int id in e.ids) j.Val(id);
                        j.EndArr().Prop("caller", e.caller).Prop("target", e.target).Prop("alive", e.flag);
                        Rng.WriteState(j.Key("rng0"), e.rng0);
                        Rng.WriteState(j.Key("rng1"), e.rng1 ?? e.rng0);
                        break;
                    case "add":
                        j.Key("ids").Arr();
                        foreach (int id in e.ids) j.Val(id);
                        j.EndArr().Key("pos").Arr();
                        foreach (var p in e.positions) V2(j, p);
                        j.EndArr().Prop("by", e.by).Prop("upd", e.upd);
                        Rng.WriteState(j.Key("rng0"), e.rng0);
                        Rng.WriteState(j.Key("rng1"), e.rng1 ?? e.rng0);
                        break;
                    case "remove":
                    case "heroPick":
                        j.Prop("item", e.item).Prop("caller", e.caller).Prop("upd", e.upd);
                        break;
                    case "trigger":
                        j.Prop("other", e.other).Prop("tag", e.otherTag).Prop("item", e.item).Prop("isTarget", e.flag);
                        break;
                    case "drop":
                        j.Prop("item", e.item).Key("pos"); V2(j, e.pos);
                        j.Key("radius").Bits(e.a).Prop("inRoom", e.flag);
                        Rng.WriteState(j.Key("rng0"), e.rng0);
                        Rng.WriteState(j.Key("rng1"), e.rng1 ?? e.rng0);
                        break;
                }
                j.EndObj();
            }
            j.EndArr();
        }

        // ---- Hooks ------------------------------------------------------------------------

        private static void RepairAdd(RepairEv e)
        {
            if (RepairRecording) repairEvents.Add(e);
        }

        [HarmonyPatch(typeof(EnemyBehaviour), "Update")]
        private static class RepairUpdateHook
        {
            private static void Prefix(EnemyBehaviour __instance)
            {
                if (!RepairRecording || !(__instance is FlyingRepairGolemBehaviour g)) return;
                int s = Id(g);
                if (s < 0) return;
                var target = Traverse.Create(g).Field("targetItem").GetValue<ItemStack>();
                var hero = HeroMerchant.Instance;
                var e = new RepairEv { k = "upd", s = s, step = Step, target = ItemId(target), z = g.transform.position.z, heroZ = hero != null ? hero.transform.position.z : 0f };
                if (target != null)
                {
                    e.pos = target.transform.position;
                    e.flag = target.isActiveAndEnabled;
                }
                RepairAdd(e);
                repairUpdates++;
            }
        }

        [HarmonyPatch(typeof(FlyingRepairGolemBehaviour), "OnItemDroppedInRoom")]
        private static class DropListHook
        {
            private static void Prefix(FlyingRepairGolemBehaviour __instance, List<ItemStack> objectsInRoom, out RepairEv __state)
            {
                __state = null;
                if (!RepairRecording) return;
                int s = Id(__instance);
                if (s < 0) return;
                __state = new RepairEv { k = "dropList", s = s, step = Step, ids = new List<int>(), caller = RepairCaller("OnItemDroppedInRoom"), rng0 = Rng.Get(), flag = __instance.enemy.enemyStats.isAlive };
                if (objectsInRoom != null) foreach (var item in objectsInRoom) __state.ids.Add(ItemId(item));
                RepairAdd(__state);
            }

            private static void Postfix(FlyingRepairGolemBehaviour __instance, RepairEv __state)
            {
                if (__state == null) return;
                __state.rng1 = Rng.Get();
                __state.target = ItemId(Traverse.Create(__instance).Field("targetItem").GetValue<ItemStack>());
            }
        }

        /// The first script frame below the hooked method (and Harmony's replacement of it): for
        /// a handler call, who invoked the delegate (DungeonRoom.AddDropItems, RemoveDropItem,
        /// OnHeroPickedItem, CleanDropList's MoveNext); for RemoveDropItem, who called it.
        private static string RepairCaller(string hooked)
        {
            var st = new System.Diagnostics.StackTrace(2, false);
            var game = typeof(EnemyBehaviour).Assembly;
            for (int i = 0; i < st.FrameCount; i++)
            {
                var m = st.GetFrame(i).GetMethod();
                if (m == null || m.DeclaringType == null || m.Name.Contains(hooked)) continue;
                if (m.DeclaringType.Assembly == game) return m.DeclaringType.Name + "." + m.Name;
            }
            return null;
        }

        [HarmonyPatch(typeof(DungeonRoom), "AddDropItems")]
        private static class AddDropHook
        {
            private static void Prefix(DungeonRoom __instance, List<ItemStack> items, out RepairEv __state)
            {
                __state = null;
                if (!RepairRecording || __instance != RepairRoom) return;
                __state = new RepairEv { k = "add", step = Step, ids = new List<int>(), positions = new List<Vector2>(), by = repairDroppingEnemy, upd = repairUpdates, rng0 = Rng.Get() };
                if (items != null)
                    foreach (var item in items)
                    {
                        __state.ids.Add(ItemId(item));
                        __state.positions.Add(item != null ? (Vector2)item.transform.position : Vector2.zero);
                    }
                RepairAdd(__state);
            }

            private static void Postfix(RepairEv __state)
            {
                if (__state != null) __state.rng1 = Rng.Get();
            }
        }

        [HarmonyPatch(typeof(DungeonRoom), "RemoveDropItem")]
        private static class RemoveDropHook
        {
            private static void Prefix(DungeonRoom __instance, ItemStack item)
            {
                if (!RepairRecording || __instance != RepairRoom) return;
                RepairAdd(new RepairEv { k = "remove", step = Step, item = ItemId(item), caller = RepairCaller("RemoveDropItem"), upd = repairUpdates });
            }
        }

        [HarmonyPatch(typeof(DungeonRoom), "OnHeroPickedItem")]
        private static class HeroPickHook
        {
            private static void Prefix(DungeonRoom __instance, ItemStack arg1)
            {
                if (!RepairRecording || __instance != RepairRoom) return;
                RepairAdd(new RepairEv { k = "heroPick", step = Step, item = ItemId(arg1), caller = RepairCaller("OnHeroPickedItem"), upd = repairUpdates });
            }
        }

        [HarmonyPatch(typeof(FlyingRepairGolemBehaviour), "OnTriggerEnter2D")]
        private static class RepairTriggerHook
        {
            private static void Prefix(FlyingRepairGolemBehaviour __instance, Collider2D collision)
            {
                if (!RepairRecording || collision == null) return;
                int s = Id(__instance);
                if (s < 0) return;
                var go = collision.gameObject;
                var item = go.GetComponent<ItemStack>();
                var target = Traverse.Create(__instance).Field("targetItem").GetValue<ItemStack>();
                RepairAdd(new RepairEv
                {
                    k = "trigger", s = s, step = Step, other = go.name, otherTag = go.tag, item = item != null ? ItemId(item) : -1,
                    flag = target != null && target.gameObject == go
                });
            }
        }

        [HarmonyPatch(typeof(ItemStack), "Drop", new[] { typeof(Vector2), typeof(float), typeof(Transform) })]
        private static class ItemDropHook
        {
            private static void Prefix(ItemStack __instance, Vector2 position, float radius, Transform parent, out RepairEv __state)
            {
                __state = null;
                if (!RepairRecording) return;
                __state = new RepairEv { k = "drop", step = Step, item = ItemId(__instance), pos = position, a = radius, flag = parent != null && parent == (RepairRoom != null ? RepairRoom.transform : null), rng0 = Rng.Get() };
                RepairAdd(__state);
            }

            private static void Postfix(RepairEv __state)
            {
                if (__state != null) __state.rng1 = Rng.Get();
            }
        }

        [HarmonyPatch(typeof(Enemy), "DropItemsOnDeath")]
        private static class DropOnDeathHook
        {
            private static void Prefix(Enemy __instance) => repairDroppingEnemy = Id(__instance);
            private static void Postfix() => repairDroppingEnemy = -1;
        }

        /// The scripted drops (see above), from a coroutine: after the frame's Updates.
        private class RepairDropper : MonoBehaviour
        {
            private void Start() => StartCoroutine(Run());

            private IEnumerator Run()
            {
                int frame = 0, next = 0;
                while (repairActive)
                {
                    yield return null;
                    if (!recording) continue;
                    if (next < RepairDropFrames.Length && frame == RepairDropFrames[next])
                    {
                        next++;
                        try { DropOne(); }
                        catch (Exception e) { OraclePlugin.Log.LogWarning(Tag + ": scripted drop failed: " + e.Message); }
                    }
                    frame++;
                }
            }

            private static void DropOne()
            {
                var room = RepairRoom;
                var hero = HeroMerchant.Instance;
                if (room == null || hero == null) return;
                Vector2 h = hero.transform.position;
                EnemyBehaviour far = null;
                float best = -1f;
                foreach (var b in slimes)
                {
                    if (b == null || !b.enemy.enemyStats.isAlive) continue;
                    float d = ((Vector2)b.transform.position - h).magnitude;
                    if (d > best)
                    {
                        best = d;
                        far = b;
                    }
                }
                if (far == null) return;
                var stack = ItemStack.Create(ItemDatabase.GetItemByName("Iron Bar"));
                if (stack == null) return;
                ItemId(stack);
                stack.Drop(far.transform.position, 30f, room.transform);
                room.AddDropItems(new List<ItemStack> { stack });
            }
        }
    }
}
