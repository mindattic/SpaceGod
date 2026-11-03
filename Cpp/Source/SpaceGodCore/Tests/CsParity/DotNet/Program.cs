// Prints the reference values Tests/CsParity checks the C++ layer (Cs/Cs.h) against, from real .NET.
// Regenerate with:  dotnet run -c Release --project Cpp/Source/SpaceGodCore/Tests/CsParity/DotNet > Cpp/Source/SpaceGodCore/Tests/CsParity/expected.txt
// CsParity.cpp prints the same lines in the same order; keep the two in step.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

static class Program
{
    static ulong state = 0x853c49e6748fea9bUL;
    static ulong Lcg() { state = unchecked(state * 6364136223846793005UL + 1442695040888963407UL); return state; }
    static string Bits(double v) => BitConverter.DoubleToInt64Bits(v).ToString("X16");
    static readonly CultureInfo Inv = CultureInfo.InvariantCulture;
    static readonly StringBuilder Out = new StringBuilder();
    static void P(string s) => Out.Append(s).Append('\n');

    static readonly double[] P10 = { 1e-6, 1e-5, 1e-4, 1e-3, 1e-2, 1e-1, 1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9 };

    static List<double> Doubles()
    {
        var v = new List<double> {
            0.0, -0.0, 0.5, 1.5, 2.5, -2.5, 0.125, 0.375, 1.005, 2.675, 1.45, 0.05, 0.15, 0.25, 0.35, 0.45, 0.55,
            1e15, 1e16, 1e14, 123456789012345.67, 1234567890123456.7, 1e-5, 1e-4, 0.0001234, 1.0 / 3, 2.0 / 3, 100, -60,
            1e21, 1e300, 5e-324, 2.2250738585072014E-308, 1.7976931348623157E+308, 0.1, 0.2, 0.3, 0.7, 9.995, 9.9999,
            99.5, 999999.5, 0.9999999999999999, 4.35, 1234.5678, -1234.5678, 0.004, 0.005, 0.006, -0.004, -0.0049,
            0.0005, 12345678.9, 1e7, 123456789, double.NaN, double.PositiveInfinity, double.NegativeInfinity, 0.045,
            0.0045, 1.0e-7, Math.PI, -Math.E, 1e23, 9007199254740993, 4503599627370497.5, 0.000001, 1e-10, 123.456e-12,
            -0.5, -1.5, 0.95, 0.995, 0.9995, 1.95, 19.5, 0.0625, 0.03125, 1.25, 2.25, 1.125, 1.375, -0.125, 7.5e-5,
            2.5e-6, 1e100, 1.5e-300, 255.5, 1000000, 999.9999999, 0.6666666666666666,
        };
        for (int k = 0; k < 120; k++)
        {
            double d = BitConverter.Int64BitsToDouble((long)Lcg());
            if (double.IsFinite(d)) v.Add(d);
        }
        for (int k = 0; k < 160; k++)
        {
            double m = (Lcg() >> 11) * (1.0 / 9007199254740992.0);
            double scale = P10[(int)(Lcg() % 16)];
            v.Add(((Lcg() & 1) == 0 ? 1 : -1) * m * scale);
        }
        for (int k = 0; k < 80; k++) v.Add(((int)(Lcg() % 400) - 200) / 8.0);
        for (int k = 0; k < 80; k++) v.Add((int)(Lcg() % 20000) / 1000.0);
        for (int k = 0; k < 40; k++) v.Add((int)(Lcg() % 4000) / 100.0 + 0.005);
        return v;
    }

    static readonly string[] DoubleFormats = {
        "", "R", "G", "G6", "G17", "G3", "G1", "E", "E3", "E0", "e2", "F0", "F2", "F3", "F", "N0", "N1", "N2", "N3", "N",
        "P0", "P1", "P2", "C", "0", "0.0", "0.00", "0.000", "0.#", "0.##", "0.###", "#,0", "+#,0;-#,0;0", "#,0.00",
        "0%", "0.0%", "00.00", "#", "#.##", "0.00E+00", "0.###e0", "#,##0.0", "'pre'0.0' x'", "0.0;(0.0);zero",
        "#,#", "0,", "0.0,,", "##0.0#", "\\#0", "0.####################",
    };

    static readonly string[] IntFormats = {
        "", "N0", "N2", "0", "0.0", "+#,0;-#,0;0", "D5", "D", "X", "x8", "G", "G2", "E2", "P0", "#,0", "00000", "F1",
        "0.##", "N", "#", "0;neg;zero",
    };

    static readonly string[] FloatFormats = { "", "R", "G", "0.##", "0.0", "0", "N2", "F3", "G9", "E2", "0.########" };

    static void Main()
    {
        // --- double formatting
        var ds = Doubles();
        foreach (double d in ds)
            foreach (string f in DoubleFormats)
                P($"D {Bits(d)} [{f}] {(f.Length == 0 ? d.ToString(Inv) : d.ToString(f, Inv))}");

        // --- float formatting
        var fs = new List<float> { 0f, -0f, 0.1f, 0.5f, 1.5f, 2.5f, 1e7f, 1e6f, 1234567f, 12345678f, 3.4028235E+38f, 1.401298E-45f,
            0.3f, 1f / 3, 16777216f, 0.125f, 99.95f, 1.005f, float.NaN, float.NegativeInfinity, 1e-5f, 1e-4f };
        for (int k = 0; k < 60; k++) fs.Add((float)(((int)(Lcg() % 2000000) - 1000000) / 997.0));
        foreach (float x in fs)
            foreach (string f in FloatFormats)
                P($"F {BitConverter.SingleToInt32Bits(x):X8} [{f}] {(f.Length == 0 ? x.ToString(Inv) : x.ToString(f, Inv))}");

        // --- int formatting
        var ints = new List<int> { 0, 1, -1, 5, 15, 999, 1000, -1000, 1234567, -1234567, int.MaxValue, int.MinValue, 45, 95, 1500, 5, 50, 500 };
        for (int k = 0; k < 40; k++) ints.Add((int)(Lcg() % 2000001) - 1000000);
        foreach (int i in ints)
            foreach (string f in IntFormats)
                P($"I {i} [{f}] {(f.Length == 0 ? i.ToString(Inv) : i.ToString(f, Inv))}");
        foreach (long l in new long[] { 0, long.MaxValue, long.MinValue, -123456789012L, 9876543210L })
            foreach (string f in new[] { "", "N0", "0", "X", "#,0" })
                P($"L {l} [{f}] {(f.Length == 0 ? l.ToString(Inv) : l.ToString(f, Inv))}");

        // --- Math.Round
        foreach (double d in ds)
        {
            var sb = new StringBuilder();
            sb.Append(Bits(Math.Round(d))).Append(' ');
            sb.Append(Bits(Math.Round(d, MidpointRounding.AwayFromZero))).Append(' ');
            for (int dg = 0; dg <= 4; dg++)
                sb.Append(Bits(Math.Round(d, dg))).Append(' ').Append(Bits(Math.Round(d, dg, MidpointRounding.AwayFromZero))).Append(' ');
            sb.Append(Bits(Math.Round(d, 15)));
            P($"R {Bits(d)} {sb}");
        }

        // --- System.Random
        foreach (int seed in new[] { 0, 1, 42, -1, 12345, int.MinValue, int.MaxValue, 161803398, 1 << 30, -987654321 })
        {
            var r = new Random(seed);
            var sb = new StringBuilder();
            for (int k = 0; k < 20; k++) sb.Append(r.Next()).Append(' ');
            for (int k = 0; k < 20; k++) sb.Append(r.Next(100)).Append(' ');
            for (int k = 0; k < 20; k++) sb.Append(r.Next(-50, 50)).Append(' ');
            for (int k = 0; k < 10; k++) sb.Append(r.Next(int.MinValue, int.MaxValue)).Append(' ');
            for (int k = 0; k < 10; k++) sb.Append(r.Next(-5, int.MaxValue)).Append(' ');
            for (int k = 0; k < 20; k++) sb.Append(Bits(r.NextDouble())).Append(' ');
            var bytes = new byte[8]; r.NextBytes(bytes);
            foreach (byte b in bytes) sb.Append(b).Append(' ');
            sb.Append(r.Next(0)).Append(' ').Append(r.Next(7, 7)).Append(' ').Append(r.Next(1));
            P($"N {seed} {sb}");
        }

        // --- List.Sort (Comparison, unstable) and List.Sort() (default comparer)
        foreach (int n in new[] { 0, 1, 2, 3, 4, 5, 16, 17, 31, 50, 100, 257, 1000, 5000 })
        {
            foreach (int distinct in new[] { 2, 7, 1000000 })
            {
                var items = new List<(int Key, int Id)>();
                for (int i = 0; i < n; i++) items.Add(((int)(Lcg() % (ulong)distinct), i));
                items.Sort((a, b) => a.Key.CompareTo(b.Key));
                var sb = new StringBuilder();
                foreach (var it in items) sb.Append(it.Id).Append(',');
                P($"S {n} {distinct} {sb}");
            }
        }
        {
            // An adversarial (organ-pipe + sawtooth) input, long enough to reach the heapsort fallback.
            var items = new List<(int Key, int Id)>();
            for (int i = 0; i < 4096; i++) items.Add((i < 2048 ? i % 37 : (4096 - i) % 41, i));
            items.Sort((a, b) => a.Key.CompareTo(b.Key));
            var sb = new StringBuilder();
            foreach (var it in items) sb.Append(it.Id).Append(',');
            P($"S adversarial {sb}");
        }
        {
            var dl = new List<double> { 3, double.NaN, -0.0, 0.0, 1, double.NaN, -1, double.NegativeInfinity, 2, 0.0, -0.0, double.PositiveInfinity };
            for (int i = 0; i < 40; i++) dl.Add((int)(Lcg() % 9) - 4 + ((Lcg() & 1) == 0 ? 0.0 : -0.0));
            dl.Sort();
            var sb = new StringBuilder();
            foreach (double d in dl) sb.Append(Bits(d)).Append(',');
            P($"S doubles {sb}");
            var il = new List<int>();
            for (int i = 0; i < 300; i++) il.Add((int)(Lcg() % 50));
            il.Sort();
            P($"S ints {string.Join(",", il)}");
        }

        // --- double.TryParse / int.TryParse (invariant)
        foreach (string s in new[] { "0", "-0", "1.5", " 12 ", "1,234.5", "1e400", "-1e-400", "NaN", "nan", "Infinity", "-Infinity", "-infinity",
            "+3.25", ".5", "5.", "1e", "abc", "", "1.7976931348623157E+308", "4.9E-324", "2.4703282292062327E-324", "0x10", "1_0",
            "123456789012345678901234567890", "0.1", "1,2,3", ",5", "1e+05", "1E-05", "  -7.25e2  ", "∞", "1.2.3" })
        {
            bool ok = double.TryParse(s, NumberStyles.Float | NumberStyles.AllowThousands, Inv, out double v);
            bool iok = int.TryParse(s, NumberStyles.Integer, Inv, out int iv);
            P($"P [{s}] {ok} {Bits(v)} {iok} {iv}");
        }
        foreach (double d in ds)
        {
            string s = d.ToString("R", Inv);
            bool ok = double.TryParse(s, NumberStyles.Float | NumberStyles.AllowThousands, Inv, out double v);
            P($"T {Bits(d)} {ok} {Bits(v)}");
        }

        // --- DateTime / TimeSpan (UTC and unspecified only: local time depends on the machine's zone)
        {
            var j2000 = new DateTime(2000, 1, 1, 12, 0, 0, DateTimeKind.Utc);
            P($"DT limits {Bits((DateTime.MaxValue - j2000).TotalDays)} {Bits((DateTime.MinValue - j2000).TotalDays)} {j2000.ToBinary()} {j2000.Ticks}");
            var days = new List<double> { 0, 0.5, -0.5, 1.0 / 3, 365.25, -36525.123456789, 2461348.0 - 2451545.0, 1e-9, 12345.678901234, -730000.5, 2900000.25, 0.1, 0.000011574074074074073 };
            for (int k = 0; k < 60; k++) days.Add(((Lcg() >> 11) * (1.0 / 9007199254740992.0) - 0.5) * 1400000.0);
            string[] dfs = { "", "o", "s", "u", "R", "G", "d", "D", "f", "t", "T", "M", "y", "yyyyMMdd'T'HHmmss'Z'", "yyyy-MM-dd  HH:mm", "MMM d HH:mm", "yyyy-MM-dd HH:mm:ss.fffffff", "h:mm tt F FF FFF", "dddd ddd dd d yy y" };
            foreach (double d in days)
            {
                var sb = new StringBuilder();
                try
                {
                var t = j2000.AddDays(d);
                sb.Append(t.Ticks).Append(' ').Append(t.ToBinary()).Append(' ').Append(t.Year).Append(' ').Append(t.DayOfYear).Append(' ').Append((int)t.DayOfWeek).Append(' ');
                sb.Append(Bits((t - j2000).TotalDays)).Append(' ').Append(TimeSpan.FromDays(d).Ticks).Append(' ').Append(TimeSpan.FromDays(d).ToString()).Append(' ');
                sb.Append(t.AddHours(d).Ticks).Append(' ');
                sb.Append(t.AddSeconds(d * 1000).Ticks).Append(' ');
                sb.Append(t.AddMonths(7).Ticks).Append(' ');
                var u = DateTime.SpecifyKind(t, DateTimeKind.Unspecified);
                foreach (string f in dfs) sb.Append('|').Append(f.Length == 0 ? u.ToString(Inv) : u.ToString(f, Inv));
                }
                catch (ArgumentOutOfRangeException) { sb.Append("<throw>"); }
                P($"DT {Bits(d)} {sb}");
            }
        }

        var o = Console.OpenStandardOutput();
        var bytesOut = new UTF8Encoding(false).GetBytes(Out.ToString());
        o.Write(bytesOut, 0, bytesOut.Length);
    }
}
