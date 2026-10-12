using System;
using System.IO;
using System.Reflection;
using Random = UnityEngine.Random;

namespace MoonlighterOracle
{
    /// Reads/writes UnityEngine.Random's xorshift128 state and records reference
    /// outputs so the port's RNG can be checked bit-for-bit.
    internal static class Rng
    {
        private static readonly FieldInfo[] S =
        {
            typeof(Random.State).GetField("s0", BindingFlags.NonPublic | BindingFlags.Instance),
            typeof(Random.State).GetField("s1", BindingFlags.NonPublic | BindingFlags.Instance),
            typeof(Random.State).GetField("s2", BindingFlags.NonPublic | BindingFlags.Instance),
            typeof(Random.State).GetField("s3", BindingFlags.NonPublic | BindingFlags.Instance),
        };

        public static uint[] Get()
        {
            object st = Random.state;
            var r = new uint[4];
            for (int i = 0; i < 4; i++) r[i] = unchecked((uint)(int)S[i].GetValue(st));
            return r;
        }

        public static void Set(uint[] s)
        {
            object st = new Random.State();
            for (int i = 0; i < 4; i++) S[i].SetValue(st, unchecked((int)s[i]));
            Random.state = (Random.State)st;
        }

        public static Json WriteState(Json j, uint[] s)
        {
            j.Arr();
            foreach (uint v in s) j.Val(v);
            return j.EndArr();
        }

        private static readonly int[] Seeds = { 0, 1, 2, 42, 12345, -1, int.MaxValue, int.MinValue, 987654321, 1812433253 };

        private struct Op
        {
            public string name;
            public float a, b;
            public Op(string n, float a = 0, float b = 0) { name = n; this.a = a; this.b = b; }
        }

        private static readonly Op[] Ops =
        {
            new Op("value"),
            new Op("int", 0, 4), new Op("int", 0, 3), new Op("int", 0, 1), new Op("int", 5, 5),
            new Op("int", 5, 2), new Op("int", -3, 3), new Op("int", -1000000, 2000000),
            new Op("float", 0, 1), new Op("float", -2.5f, 7.25f), new Op("float", 3, 1), new Op("float", 4, 4),
        };

        public static void Probe(string path, int callsPerOp)
        {
            using (var w = new StreamWriter(path))
            {
                foreach (int seed in Seeds)
                {
                    Random.InitState(seed);
                    var j = new Json().Obj().Prop("seed", seed).Key("init");
                    WriteState(j, Get());
                    w.WriteLine(j.EndObj());

                    foreach (Op op in Ops)
                    {
                        Random.InitState(seed);
                        j = new Json().Obj().Prop("seed", seed).Prop("op", op.name).Key("a").Bits(op.a).Key("b").Bits(op.b).Key("out").Arr();
                        for (int i = 0; i < callsPerOp; i++)
                        {
                            switch (op.name)
                            {
                                case "value": j.Bits(Random.value); break;
                                case "int": j.Val(Random.Range((int)op.a, (int)op.b)); break;
                                case "float": j.Bits(Random.Range(op.a, op.b)); break;
                            }
                        }
                        j.EndArr().Key("end");
                        WriteState(j, Get());
                        w.WriteLine(j.EndObj());
                    }
                }
            }
        }
    }
}
