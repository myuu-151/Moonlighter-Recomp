using System;
using System.Collections;
using System.Collections.Generic;
using HarmonyLib;
using Moonlighter.DungeonGeneration;
using UnityEngine;

namespace MoonlighterOracle
{
    /// Inputs and results of ChestGeneration (random chest contents), for porting it:
    ///  - chestLists: _weightItemLists exactly as built by RefreshWeightLists (order + weights;
    ///    the lists' sort is unstable with a non-transitive comparer, so they're dumped, not rebuilt);
    ///  - effectLists: ItemEffectManager's per-chest-type effect tables and each effect's
    ///    action data (random directions, fixed stack, random-select), plus the trick effect;
    ///  - chestGen: per GenerateChestContent call, its arguments, the amulet state, owned
    ///    amulets, RNG state before/after and the generated contents.
    internal static class ChestGenOracle
    {
        private static object F(object o, string name) =>
            o == null ? null : AccessTools.Field(o.GetType(), name)?.GetValue(o);

        /// WeightedList<T> internals: (_items as (item, weight)) and _maxWeight.
        private static IEnumerable<(object item, float weight)> Items(object weightedList)
        {
            if (F(weightedList, "_items") is IList list)
                foreach (object it in list) yield return (F(it, "item"), (float)F(it, "weight"));
        }

        private static float MaxWeight(object weightedList) => weightedList == null ? 0f : (float)F(weightedList, "_maxWeight");

        private static void Item(Json j, ItemMaster m)
        {
            if (m == null) { j.Val((string)null); return; }
            j.Obj().Prop("name", m.name).Prop("id", System.Runtime.CompilerServices.RuntimeHelpers.GetHashCode(m)).Prop("maxStack", m.maxStack)
                .Prop("fixedChestStack", m.fixedChestStack).Prop("minChestStack", m.minChestStack).Prop("maxChestStack", m.maxChestStack)
                .Prop("destroyedOnRunEnded", m.isDestroyedOnRunEnded).Prop("culture", Convert.ToInt32(F(m, "culture") ?? 0))
                .Prop("amulet", m is AmuletEquipmentMaster).EndObj();
        }

        private static void EffectData(Json j, Effect e)
        {
            if (e == null) { j.Val((string)null); return; }
            var action = e.GetComponent<EffectAction>();
            j.Obj().Prop("name", e.name).Prop("action", action != null ? action.GetType().Name : null);
            if (action is InventorySlotAction isa)
                j.Prop("randomDirections", (int)(F(isa, "_maxRandomDirections") ?? 0)).Prop("fixedStack", isa.fixedStack);
            if (action is PreferedPositionAction ppa) j.Prop("selectRandom", ppa.selectRandom);
            j.EndObj();
        }

        [HarmonyPatch(typeof(ChestGeneration), nameof(ChestGeneration.RefreshWeightLists))]
        private static class ListsBuilt
        {
            private static void Postfix()
            {
                if (CaveOracle.inMirror) return;
                Trace.Safe("chestLists", () =>
                {
                    var all = AccessTools.Field(typeof(ChestGeneration), "_weightItemLists").GetValue(null) as IDictionary;
                    if (all == null) return;
                    var j = Trace.Ev("chestLists").Key("types").Arr();
                    foreach (DictionaryEntry byType in all)
                    {
                        j.Obj().Prop("type", Convert.ToInt32(byType.Key)).Key("cultures").Arr();
                        foreach (DictionaryEntry byCulture in (IDictionary)byType.Value)
                        {
                            object outer = byCulture.Value;
                            j.Obj().Prop("culture", Convert.ToInt32(byCulture.Key)).Key("maxWeight").Bits(MaxWeight(outer)).Key("tiers").Arr();
                            foreach (var (inner, w) in Items(outer))
                            {
                                j.Obj().Key("weight").Bits(w).Key("maxWeight").Bits(MaxWeight(inner)).Key("items").Arr();
                                foreach (var (item, iw) in Items(inner))
                                {
                                    j.Obj().Key("weight").Bits(iw).Key("item");
                                    Item(j, item as ItemMaster);
                                    j.EndObj();
                                }
                                j.EndArr().EndObj();
                            }
                            j.EndArr().EndObj();
                        }
                        j.EndArr().EndObj();
                    }
                    j.EndArr();
                    Trace.Emit(Trace.State(j));
                    DumpEffects();
                });
            }
        }

        private static bool effectsDumped;

        private static void DumpEffects()
        {
            if (effectsDumped || ItemEffectManager.Instance == null) return;
            effectsDumped = true;
            var mgr = ItemEffectManager.Instance;
            var j = Trace.Ev("effectLists").Key("trick");
            EffectData(j, mgr.trickEffectPrefab);
            j.Key("types").Arr();
            if (AccessTools.Field(typeof(ItemEffectManager), "_prefabsByChestAndEvilness").GetValue(mgr) is IList byType)
                foreach (object outer in byType)
                {
                    j.Obj().Key("maxWeight").Bits(MaxWeight(outer)).Key("levels").Arr();
                    foreach (var (inner, w) in Items(outer))
                    {
                        j.Obj().Key("weight").Bits(w).Key("effects").Arr();
                        if (inner is IList effects) foreach (object e in effects) EffectData(j, e as Effect);
                        j.EndArr().EndObj();
                    }
                    j.EndArr().EndObj();
                }
            j.EndArr();
            Trace.Emit(j);
        }

        [HarmonyPatch(typeof(ChestGeneration), nameof(ChestGeneration.GenerateChestContent))]
        private static class Generate
        {
            private static void Prefix(ItemMaster.Culture culture, Chest.chestType chestType, int capacity, int maxNumberOfDifferentItems,
                                       int maxItemQuantityPerStack, float dropIncrement, out Json __state)
            {
                Json j = null;
                Trace.Safe("chestGen", () =>
                {
                    DumpEffects();
                    j = Trace.Ev("chestGen").Prop("culture", (int)culture).Prop("type", (int)chestType).Prop("capacity", capacity)
                        .Prop("maxDifferent", maxNumberOfDifferentItems).Prop("maxStack", maxItemQuantityPerStack);
                    j.Key("dropIncrement").Bits(dropIncrement);
                    var dm = DungeonManager.Instance;
                    if (dm != null)
                        j.Prop("intermediate", dm.intermediateInfo.dungeonType == DungeonManager.DungeonType.Intermediate)
                            .Prop("intermediateCulture", (int)dm.intermediateInfo.intermediateCulture);
                    var au = AmuletUtility.Instance;
                    j.Prop("amuletInit", (bool)(F(au, "isInitialized") ?? false))
                        .Prop("amuletsRemaining", (int)(F(au, "currentDungeonAmuletsRemainingToGenerate") ?? -1));
                    j.Key("amulets").Arr();
                    if (au != null)
                        foreach (var (a, w) in Items(au.weightedAmuletsProb))
                        {
                            var am = a as AmuletEquipmentMaster;
                            j.Arr().Val(am != null ? am.name : null).Bits(w).Val(am != null ? Convert.ToInt32(F(am, "culture") ?? 0) : -1)
                                .Val(am != null && AmuletUtility.CheckIfAmuletOwned(am)).EndArr();
                        }
                    j.EndArr();
                    // Owned flags for every amulet in this chest's candidate lists.
                    j.Key("ownedAmulets").Arr();
                    foreach (var m in ItemDatabase.GetItems(x => x is AmuletEquipmentMaster))
                        if (AmuletUtility.CheckIfAmuletOwned((AmuletEquipmentMaster)m)) j.Val(m.name);
                    j.EndArr();
                    Trace.State(j, "before");
                });
                __state = j;
            }

            private static void Postfix(List<ItemStack> __result, Json __state)
            {
                if (__state == null) return;
                Trace.Safe("chestGen post", () =>
                {
                    __state.Key("content").Arr();
                    if (__result != null)
                        foreach (var s in __result)
                        {
                            if (s == null) { __state.Val((string)null); continue; }
                            __state.Arr().Val(s.master != null ? s.master.name : null).Val(s.Quantity).Val(s.effect != null ? s.effect.name : null).EndArr();
                        }
                    __state.EndArr();
                    Trace.Emit(Trace.State(__state, "after"));
                });
            }
        }
    }
}
