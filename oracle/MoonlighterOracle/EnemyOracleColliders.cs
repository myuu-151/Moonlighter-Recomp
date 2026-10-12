using System.Collections.Generic;
using HarmonyLib;
using UnityEngine;

namespace MoonlighterOracle
{
    /// `-oracle-enemy`'s additions for the HermitSlime and what it brought (game/HermitSlime,
    /// game/EnemyColliders, game/HeroDamage in the port):
    ///   - the start: the room floor (roomFloorWidth, roomFloorHeight, roomCenterYOffset), and per
    ///     enemy its Rigidbody2D's mass and centre of mass, its root box's trigger flag;
    ///   - each fixed step's start ("box" per step): each enemy's root BoxCollider2D (offset,
    ///     size: clips drive the HermitSlime's) and its body's centre of mass;
    ///   - each frame's end, per enemy: the root box, the centre of mass and mass, the
    ///     hurtboxes (children with an EnemyDamageReceiver: path, box enabled, object active,
    ///     layer, tag, offset, size, local position, whether gameObjectToAdvise is set), and a
    ///     HermitSlimeBehaviourNEW's patrol (patrolLapse, destinyPosition, roomGridOrigin,
    ///     roomGridSize) and look lock;
    ///   - events: "patrol" (ChooseNewDestination: the lapse just drawn, the destination, the
    ///     Random state before and after), "drain" (HeroMerchantInventory.DropRandomItems: how
    ///     many, how many slots could drop, the Random state before and after), "recvHit"
    ///     (EnemyDamageReceiver.DealDamageToEnemy: which child, whether it advises anyone),
    ///     "weaponHit" (Weapon.OnMainAttackHit on one of the enemies or a child: the other's name
    ///     and tag); and DealDamageToPlayer's damage type, DealDamageToEnemy's receiver (in
    ///     EnemyOracle.cs).
    internal static partial class EnemyOracle
    {
        // Each fixed step's root boxes: [id, offset x, y, size x, y, centre of mass x, y].
        private static readonly List<List<float[]>> stepBoxes = new List<List<float[]>>();

        /// The enemy (or the enemy a child belongs to) as one of those recorded.
        private static int IdInParent(GameObject o)
        {
            var b = o != null ? o.GetComponentInParent<EnemyBehaviour>() : null;
            return b != null ? slimes.IndexOf(b) : -1;
        }

        private static void StepBoxes()
        {
            var list = new List<float[]>();
            for (int i = 0; i < slimes.Count; i++)
            {
                var b = slimes[i];
                if (b == null) continue;
                var box = b.GetComponent<BoxCollider2D>();
                var rb = b.GetComponent<Rigidbody2D>();
                if (box == null || rb == null) continue;
                list.Add(new[] { i, box.offset.x, box.offset.y, box.size.x, box.size.y, rb.centerOfMass.x, rb.centerOfMass.y });
            }
            stepBoxes.Add(list);
        }

        private static void WriteStepBoxes(Json j, int k)
        {
            if (k >= stepBoxes.Count) return;
            j.Key("box").Arr();
            foreach (var s in stepBoxes[k])
            {
                j.Arr().Val((int)s[0]);
                for (int i = 1; i < s.Length; i++) j.Bits(s[i]);
                j.EndArr();
            }
            j.EndArr();
        }

        private static void WriteRoomFloor(Json j, DungeonRoom room)
        {
            j.Key("roomFloor").Arr().Val(room.roomFloorWidth).Val(room.roomFloorHeight).Val(room.roomCenterYOffset).EndArr();
        }

        /// The root box, the body's mass and centre, the hurtboxes.
        private static void WriteColliders(Json j, EnemyBehaviour b, bool setup)
        {
            var box = b.GetComponent<BoxCollider2D>();
            if (box != null)
            {
                j.Key("rootBox").Arr().Bits(box.offset.x).Bits(box.offset.y).Bits(box.size.x).Bits(box.size.y).EndArr();
                if (setup) j.Prop("rootTrigger", box.isTrigger);
            }
            var rb = b.GetComponent<Rigidbody2D>();
            if (rb != null)
            {
                j.Key("com"); V2(j, rb.centerOfMass);
                j.Key("mass").Bits(rb.mass);
            }
            j.Key("receivers").Arr();
            foreach (var r in b.GetComponentsInChildren<EnemyDamageReceiver>(true))
            {
                var rbox = r.GetComponent<BoxCollider2D>();
                j.Obj().Prop("path", r.transform == b.transform ? "" : r.name).Prop("advise", r.gameObjectToAdvise != null)
                    .Prop("active", r.gameObject.activeSelf).Prop("layer", r.gameObject.layer).Prop("tag", r.tag);
                if (rbox != null)
                {
                    j.Prop("on", rbox.enabled).Prop("trigger", rbox.isTrigger);
                    j.Key("offset"); V2(j, rbox.offset);
                    j.Key("size"); V2(j, rbox.size);
                }
                j.Key("pos"); V2(j, r.transform.localPosition);
                j.EndObj();
            }
            j.EndArr();
        }

        private static void WriteHermit(Json j, EnemyBehaviour b)
        {
            if (!(b is HermitSlimeBehaviourNEW h)) return;
            var tr = Traverse.Create(h);
            j.Prop("locked", h.iDirectionLocked);
            j.Key("patrol").Bits(tr.Field("patrolLapse").GetValue<float>());
            j.Key("dest"); V2(j, tr.Field("destinyPosition").GetValue<Vector3>());
            j.Key("gridOrigin"); V2(j, tr.Field("roomGridOrigin").GetValue<Vector2>());
            j.Key("gridSize"); V2(j, tr.Field("roomGridSize").GetValue<Vector2>());
        }

        /// The events of this file's hooks (false: not one of them).
        private static bool WriteEventExtra(Json j, Ev e)
        {
            switch (e.k)
            {
                case "patrol":
                    j.Key("lapse").Bits(e.a).Key("dest"); V2(j, e.pos);
                    Rng.WriteState(j.Key("rng0"), e.rng0);
                    Rng.WriteState(j.Key("rng1"), e.rng1 ?? e.rng0);
                    return true;
                case "drain":
                    j.Prop("number", (int)e.a).Prop("eligible", e.type);
                    Rng.WriteState(j.Key("rng0"), e.rng0);
                    Rng.WriteState(j.Key("rng1"), e.rng1 ?? e.rng0);
                    return true;
                case "recvHit":
                    j.Prop("path", e.other).Prop("advise", e.player).Key("str").Bits(e.a);
                    return true;
                case "weaponHit":
                    j.Prop("other", e.other).Prop("tag", e.caller);
                    return true;
            }
            return false;
        }

        [HarmonyPatch(typeof(HermitSlimeBehaviourNEW), "ChooseNewDestination")]
        private static class PatrolHook
        {
            private static void Prefix(HermitSlimeBehaviourNEW __instance, out Ev __state)
            {
                __state = null;
                if (!recording) return;
                int id = Id(__instance);
                if (id < 0) return;
                // (patrolLapse's Random.Range(2, 6) came just before, in MoveEnemy.)
                __state = new Ev { k = "patrol", s = id, step = Step, a = Traverse.Create(__instance).Field("patrolLapse").GetValue<float>(), rng0 = Rng.Get() };
                Add(__state);
            }

            private static void Postfix(HermitSlimeBehaviourNEW __instance, Ev __state)
            {
                if (__state == null) return;
                __state.rng1 = Rng.Get();
                __state.pos = Traverse.Create(__instance).Field("destinyPosition").GetValue<Vector3>();
            }
        }

        [HarmonyPatch(typeof(HeroMerchantInventory), "DropRandomItems")]
        private static class DrainHook
        {
            private static void Prefix(HeroMerchantInventory __instance, int number, bool mustDropCursedItems, out Ev __state)
            {
                __state = null;
                if (!recording) return;
                int eligible = 0;
                foreach (var slot in __instance.slots)
                    if (slot.item != null && (mustDropCursedItems || slot.item.effect == null)) eligible++;
                __state = new Ev { k = "drain", step = Step, a = number, type = eligible, rng0 = Rng.Get() };
                Add(__state);
            }

            private static void Postfix(Ev __state)
            {
                if (__state != null) __state.rng1 = Rng.Get();
            }
        }

        [HarmonyPatch(typeof(EnemyDamageReceiver), "DealDamageToEnemy")]
        private static class ReceiverHook
        {
            private static void Prefix(EnemyDamageReceiver __instance, float damage)
            {
                int id = IdInParent(__instance.gameObject);
                if (id >= 0) Add(new Ev { k = "recvHit", s = id, step = Step, a = damage, other = __instance.name, player = __instance.gameObjectToAdvise != null });
            }
        }

        [HarmonyPatch(typeof(Weapon), "OnMainAttackHit")]
        private static class WeaponHitHook
        {
            private static void Prefix(GameObject other)
            {
                int id = IdInParent(other);
                if (id >= 0) Add(new Ev { k = "weaponHit", s = id, step = Step, other = other.name, caller = other.tag });
            }
        }
    }
}
