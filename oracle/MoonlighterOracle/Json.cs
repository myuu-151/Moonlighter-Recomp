using System.Globalization;
using System.Text;

namespace MoonlighterOracle
{
    /// Minimal streaming JSON writer; output is one object per line (JSONL).
    internal sealed class Json
    {
        private readonly StringBuilder sb = new StringBuilder();
        private bool needComma;

        public Json Obj() { Sep(); sb.Append('{'); needComma = false; return this; }
        public Json EndObj() { sb.Append('}'); needComma = true; return this; }
        public Json Arr() { Sep(); sb.Append('['); needComma = false; return this; }
        public Json EndArr() { sb.Append(']'); needComma = true; return this; }

        public Json Key(string k) { Sep(); Str(k); sb.Append(':'); needComma = false; return this; }

        public Json Val(int v) { Sep(); sb.Append(v.ToString(CultureInfo.InvariantCulture)); needComma = true; return this; }
        public Json Val(uint v) { Sep(); sb.Append(v.ToString(CultureInfo.InvariantCulture)); needComma = true; return this; }
        public Json Val(bool v) { Sep(); sb.Append(v ? "true" : "false"); needComma = true; return this; }
        public Json Val(string v) { Sep(); if (v == null) sb.Append("null"); else Str(v); needComma = true; return this; }
        /// A value already written as JSON (another Json's text).
        public Json Raw(string json) { Sep(); sb.Append(json); needComma = true; return this; }

        /// Floats are written as their exact IEEE bits so comparisons are bit-for-bit.
        public Json Bits(float v) { return Val(FloatBits(v)); }

        public Json Prop(string k, int v) => Key(k).Val(v);
        public Json Prop(string k, bool v) => Key(k).Val(v);
        public Json Prop(string k, string v) => Key(k).Val(v);

        public static int FloatBits(float v)
        {
            unsafe { return *(int*)&v; }
        }

        private void Sep()
        {
            if (needComma) sb.Append(',');
        }

        private void Str(string s)
        {
            sb.Append('"');
            foreach (char c in s)
            {
                switch (c)
                {
                    case '"': sb.Append("\\\""); break;
                    case '\\': sb.Append("\\\\"); break;
                    case '\n': sb.Append("\\n"); break;
                    case '\r': sb.Append("\\r"); break;
                    case '\t': sb.Append("\\t"); break;
                    default:
                        if (c < 0x20) sb.Append("\\u").Append(((int)c).ToString("x4"));
                        else sb.Append(c);
                        break;
                }
            }
            sb.Append('"');
        }

        public override string ToString() => sb.ToString();
    }
}
