using System;
using System.Collections;
using System.Collections.Generic;
using UnityEngine;

namespace MoonlighterOracle
{
    /// After `-oracle-enemy=BrokenGolemTurret`'s last frame (the recording over): the order in which
    /// Unity calls OnCollisionEnter2D when several contacts start in one fixed step, on bodies made
    /// for the probe far from the room. enemy_BrokenGolemTurret.jsonl (enemyfix-turret-2) has shots
    /// meeting shots: each contact's two calls with the shot fired later first, and, in frame 300,
    /// two such contacts and one with the hero in an order the port's contact list doesn't give
    /// (tests/turret_replay.h). Whether that order follows the objects' creation (instance ids) or
    /// Box2D's broad-phase proxies (made again when an object is switched off and on) is what the
    /// scenarios tell apart.
    ///
    /// Each scenario makes four objects in order (a Dynamic Rigidbody2D, gravity 0, a 10 x 10
    /// BoxCollider2D on the shots' layer, a recorder of its OnCollisionEnter2D), 40 apart; may switch
    /// them off and on again in a given order; then, in one frame, moves two pairs onto each other by
    /// transform (synced at the next step, so both pairs' contacts are made at its end and start
    /// touching in the one after); and records the calls over the next five fixed steps. One
    /// "contactOrderProbe" line: per scenario its name, the objects (name, instance id, made order,
    /// the order switched back on), the pairs, and the calls in order ([this, other, the fixed step's
    /// index since the move]).
    internal static class ContactOrderProbe
    {
        private class Recorder : MonoBehaviour
        {
            public List<string[]> calls;
            public Func<int> stepNow;

            private void OnCollisionEnter2D(Collision2D c)
            {
                if (calls != null) calls.Add(new[] { name, c.collider != null ? c.collider.name : "?", stepNow().ToString() });
            }
        }

        private class StepCounter : MonoBehaviour
        {
            public int steps;
            private void FixedUpdate() => steps++;
        }

        private struct Scenario
        {
            public string name;
            public int[] offOrder, onOrder; // switched off, then on again (null: not)
            public int[][] pairs;
        }

        public static IEnumerator Run(Action<Json> emit)
        {
            var scenarios = new[]
            {
                new Scenario { name = "fresh-outer-inner", pairs = new[] { new[] { 0, 3 }, new[] { 1, 2 } } },
                new Scenario { name = "fresh-adjacent", pairs = new[] { new[] { 0, 1 }, new[] { 2, 3 } } },
                new Scenario { name = "reactivated-reversed", offOrder = new[] { 0, 1, 2, 3 }, onOrder = new[] { 3, 2, 1, 0 }, pairs = new[] { new[] { 0, 3 }, new[] { 1, 2 } } },
                new Scenario { name = "reactivated-first-last", offOrder = new[] { 0 }, onOrder = new[] { 0 }, pairs = new[] { new[] { 0, 1 }, new[] { 2, 3 } } },
            };
            int layer = LayerMask.NameToLayer("EnemyProjectile");
            if (layer < 0) layer = 0;
            var counter = new GameObject("OracleContactOrderSteps").AddComponent<StepCounter>();
            var j = new Json().Obj().Prop("ev", "contactOrderProbe").Prop("layer", layer).Key("scenarios").Arr();
            for (int si = 0; si < scenarios.Length; si++)
            {
                Scenario sc = scenarios[si];
                var calls = new List<string[]>();
                var objs = new List<GameObject>();
                Vector2 origin = new Vector2(20000f + 1000f * si, 20000f);
                int moveStep = 0;
                for (int i = 0; i < 4; i++)
                {
                    var go = new GameObject("Probe" + si + "_" + i);
                    go.layer = layer;
                    go.transform.position = origin + new Vector2(40f * i, 0f);
                    var rb = go.AddComponent<Rigidbody2D>();
                    rb.gravityScale = 0f;
                    var box = go.AddComponent<BoxCollider2D>();
                    box.size = new Vector2(10f, 10f);
                    var rec = go.AddComponent<Recorder>();
                    rec.calls = calls;
                    rec.stepNow = () => counter.steps - moveStep;
                    objs.Add(go);
                }
                yield return new WaitForFixedUpdate();
                yield return new WaitForFixedUpdate();
                if (sc.offOrder != null)
                {
                    foreach (int k in sc.offOrder) objs[k].SetActive(false);
                    yield return new WaitForFixedUpdate();
                    foreach (int k in sc.onOrder) objs[k].SetActive(true);
                    yield return new WaitForFixedUpdate();
                    yield return new WaitForFixedUpdate();
                }
                // Both pairs onto each other in one frame (8 of 10 overlapping in x).
                yield return null;
                moveStep = counter.steps;
                for (int p = 0; p < sc.pairs.Length; p++)
                {
                    Vector2 at = origin + new Vector2(300f + 60f * p, 200f);
                    objs[sc.pairs[p][0]].transform.position = at;
                    objs[sc.pairs[p][1]].transform.position = at + new Vector2(2f, 0f);
                }
                for (int s = 0; s < 5; s++) yield return new WaitForFixedUpdate();
                j.Obj().Prop("name", sc.name).Key("objects").Arr();
                for (int i = 0; i < 4; i++)
                    j.Obj().Prop("name", objs[i].name).Prop("id", objs[i].GetInstanceID()).Prop("made", i)
                        .Prop("on", sc.onOrder != null ? Array.IndexOf(sc.onOrder, i) : -1).EndObj();
                j.EndArr().Key("pairs").Arr();
                foreach (var pr in sc.pairs) j.Arr().Val(pr[0]).Val(pr[1]).EndArr();
                j.EndArr().Key("calls").Arr();
                foreach (var c in calls) j.Arr().Val(c[0]).Val(c[1]).Val(int.Parse(c[2])).EndArr();
                j.EndArr().EndObj();
                foreach (var go in objs) UnityEngine.Object.Destroy(go);
                yield return new WaitForFixedUpdate();
            }
            UnityEngine.Object.Destroy(counter.gameObject);
            emit(j.EndArr());
        }
    }
}
