// CsParity: checks the C# compatibility layer (Cs/Cs.h) against real .NET, value for value.
// It prints exactly the lines DotNet/Program.cs prints (double/float/int/long formatting, Math.Round,
// System.Random, List.Sort, double/int TryParse) and diffs them against expected.txt, the .NET output.
//
//   CsParity.exe [expected.txt] [--print]     exit code 0 = identical, 1 = mismatches (listed), 2 = no file
//
// Built only by tools/build-core.ps1 (SPACEGOD_STANDALONE); the UE module compiles it to nothing.

#ifdef SPACEGOD_STANDALONE

#include "Cs/Cs.h"

#include <bit>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <tuple>

namespace
{
	using cs::String;

	uint64_t state = 0x853c49e6748fea9bULL;
	uint64_t Lcg() { state = state * 6364136223846793005ULL + 1442695040888963407ULL; return state; }

	String Hex(uint64_t v, int width)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%0*llX", width, static_cast<unsigned long long>(v));
		return buf;
	}
	String Bits(double v) { return Hex(std::bit_cast<uint64_t>(v), 16); }
	String Bits(float v) { return Hex(std::bit_cast<uint32_t>(v), 8); }

	std::ostringstream Out;
	void P(const String& s) { Out << s << '\n'; }

	constexpr double P10[] = { 1e-6, 1e-5, 1e-4, 1e-3, 1e-2, 1e-1, 1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9 };

	cs::List<double> Doubles()
	{
		using cs::Double::NaN; using cs::Double::PositiveInfinity; using cs::Double::NegativeInfinity;
		cs::List<double> v{
			0.0, -0.0, 0.5, 1.5, 2.5, -2.5, 0.125, 0.375, 1.005, 2.675, 1.45, 0.05, 0.15, 0.25, 0.35, 0.45, 0.55,
			1e15, 1e16, 1e14, 123456789012345.67, 1234567890123456.7, 1e-5, 1e-4, 0.0001234, 1.0 / 3, 2.0 / 3, 100, -60,
			1e21, 1e300, 5e-324, 2.2250738585072014E-308, 1.7976931348623157E+308, 0.1, 0.2, 0.3, 0.7, 9.995, 9.9999,
			99.5, 999999.5, 0.9999999999999999, 4.35, 1234.5678, -1234.5678, 0.004, 0.005, 0.006, -0.004, -0.0049,
			0.0005, 12345678.9, 1e7, 123456789, NaN, PositiveInfinity, NegativeInfinity, 0.045,
			0.0045, 1.0e-7, cs::Math::PI, -cs::Math::E, 1e23, 9007199254740993.0, 4503599627370497.5, 0.000001, 1e-10, 123.456e-12,
			-0.5, -1.5, 0.95, 0.995, 0.9995, 1.95, 19.5, 0.0625, 0.03125, 1.25, 2.25, 1.125, 1.375, -0.125, 7.5e-5,
			2.5e-6, 1e100, 1.5e-300, 255.5, 1000000, 999.9999999, 0.6666666666666666,
		};
		for (int k = 0; k < 120; k++)
		{
			double d = std::bit_cast<double>(Lcg());
			if (std::isfinite(d)) v.Add(d);
		}
		for (int k = 0; k < 160; k++)
		{
			double m = static_cast<double>(Lcg() >> 11) * (1.0 / 9007199254740992.0);
			double scale = P10[Lcg() % 16];
			double sign = (Lcg() & 1) == 0 ? 1 : -1;
			v.Add(sign * m * scale);
		}
		for (int k = 0; k < 80; k++) v.Add((static_cast<int>(Lcg() % 400) - 200) / 8.0);
		for (int k = 0; k < 80; k++) v.Add(static_cast<int>(Lcg() % 20000) / 1000.0);
		for (int k = 0; k < 40; k++) v.Add(static_cast<int>(Lcg() % 4000) / 100.0 + 0.005);
		return v;
	}

	const char* DoubleFormats[] = {
		"", "R", "G", "G6", "G17", "G3", "G1", "E", "E3", "E0", "e2", "F0", "F2", "F3", "F", "N0", "N1", "N2", "N3", "N",
		"P0", "P1", "P2", "C", "0", "0.0", "0.00", "0.000", "0.#", "0.##", "0.###", "#,0", "+#,0;-#,0;0", "#,0.00",
		"0%", "0.0%", "00.00", "#", "#.##", "0.00E+00", "0.###e0", "#,##0.0", "'pre'0.0' x'", "0.0;(0.0);zero",
		"#,#", "0,", "0.0,,", "##0.0#", "\\#0", "0.####################",
	};
	const char* IntFormats[] = {
		"", "N0", "N2", "0", "0.0", "+#,0;-#,0;0", "D5", "D", "X", "x8", "G", "G2", "E2", "P0", "#,0", "00000", "F1",
		"0.##", "N", "#", "0;neg;zero",
	};
	const char* FloatFormats[] = { "", "R", "G", "0.##", "0.0", "0", "N2", "F3", "G9", "E2", "0.########" };

	template <class F> String Safe(F f)
	{
		try { return f(); }
		catch (const std::exception& e) { return String("<throw ") + e.what() + ">"; }
	}

	void Generate()
	{
		// --- double formatting
		auto ds = Doubles();
		for (double d : ds)
			for (const char* f : DoubleFormats)
				P(cs::Cat("D ", Bits(d), " [", f, "] ", Safe([&] { return *f ? cs::Fmt(d, f) : cs::Str(d); })));

		// --- float formatting
		cs::List<float> fs{ 0.0f, -0.0f, 0.1f, 0.5f, 1.5f, 2.5f, 1e7f, 1e6f, 1234567.0f, 12345678.0f, 3.4028235E+38f, 1.401298E-45f,
			0.3f, 1.0f / 3, 16777216.0f, 0.125f, 99.95f, 1.005f, cs::Single::NaN, cs::Single::NegativeInfinity, 1e-5f, 1e-4f };
		for (int k = 0; k < 60; k++) fs.Add(static_cast<float>((static_cast<int>(Lcg() % 2000000) - 1000000) / 997.0));
		for (float x : fs)
			for (const char* f : FloatFormats)
				P(cs::Cat("F ", Bits(x), " [", f, "] ", Safe([&] { return *f ? cs::Fmt(x, f) : cs::Str(x); })));

		// --- int formatting
		cs::List<int> ints{ 0, 1, -1, 5, 15, 999, 1000, -1000, 1234567, -1234567, cs::Int::MaxValue, cs::Int::MinValue, 45, 95, 1500, 5, 50, 500 };
		for (int k = 0; k < 40; k++) ints.Add(static_cast<int>(Lcg() % 2000001) - 1000000);
		for (int i : ints)
			for (const char* f : IntFormats)
				P(cs::Cat("I ", i, " [", f, "] ", Safe([&] { return *f ? cs::Fmt(i, f) : cs::Str(i); })));
		for (long long l : { 0LL, std::numeric_limits<long long>::max(), std::numeric_limits<long long>::min(), -123456789012LL, 9876543210LL })
			for (const char* f : { "", "N0", "0", "X", "#,0" })
				P(cs::Cat("L ", l, " [", f, "] ", Safe([&] { return *f ? cs::Fmt(l, f) : cs::Str(l); })));

		// --- Math.Round
		using cs::Math::MidpointRounding;
		for (double d : ds)
		{
			String sb;
			sb += Bits(cs::Math::Round(d)) + " ";
			sb += Bits(cs::Math::Round(d, MidpointRounding::AwayFromZero)) + " ";
			for (int dg = 0; dg <= 4; dg++)
				sb += Bits(cs::Math::Round(d, dg)) + " " + Bits(cs::Math::Round(d, dg, MidpointRounding::AwayFromZero)) + " ";
			sb += Bits(cs::Math::Round(d, 15));
			P(cs::Cat("R ", Bits(d), " ", sb));
		}

		// --- System.Random
		for (int seed : { 0, 1, 42, -1, 12345, cs::Int::MinValue, cs::Int::MaxValue, 161803398, 1 << 30, -987654321 })
		{
			cs::Random r(seed);
			String sb;
			for (int k = 0; k < 20; k++) sb += cs::Str(r.Next()) + " ";
			for (int k = 0; k < 20; k++) sb += cs::Str(r.Next(100)) + " ";
			for (int k = 0; k < 20; k++) sb += cs::Str(r.Next(-50, 50)) + " ";
			for (int k = 0; k < 10; k++) sb += cs::Str(r.Next(cs::Int::MinValue, cs::Int::MaxValue)) + " ";
			for (int k = 0; k < 10; k++) sb += cs::Str(r.Next(-5, cs::Int::MaxValue)) + " ";
			for (int k = 0; k < 20; k++) sb += Bits(r.NextDouble()) + " ";
			auto bytes = cs::NewArray<uint8_t>(8);
			r.NextBytes(bytes);
			for (uint8_t b : bytes) sb += cs::Str(static_cast<int>(b)) + " ";
			int a = r.Next(0);
			int b = r.Next(7, 7);
			int c = r.Next(1);
			sb += cs::Cat(a, " ", b, " ", c);
			P(cs::Cat("N ", seed, " ", sb));
		}

		// --- List.Sort
		using Item = std::pair<int, int>; // (Key, Id)
		auto byKey = [](const Item& a, const Item& b) { return a.first < b.first ? -1 : (a.first > b.first ? 1 : 0); };
		for (int n : { 0, 1, 2, 3, 4, 5, 16, 17, 31, 50, 100, 257, 1000, 5000 })
		{
			for (int distinct : { 2, 7, 1000000 })
			{
				cs::List<Item> items;
				for (int i = 0; i < n; i++) items.Add({ static_cast<int>(Lcg() % static_cast<uint64_t>(distinct)), i });
				items.Sort(byKey);
				String sb;
				for (auto& it : items) sb += cs::Str(it.second) + ",";
				P(cs::Cat("S ", n, " ", distinct, " ", sb));
			}
		}
		{
			cs::List<Item> items;
			for (int i = 0; i < 4096; i++) items.Add({ i < 2048 ? i % 37 : (4096 - i) % 41, i });
			items.Sort(byKey);
			String sb;
			for (auto& it : items) sb += cs::Str(it.second) + ",";
			P(cs::Cat("S adversarial ", sb));
		}
		{
			using cs::Double::NaN; using cs::Double::PositiveInfinity; using cs::Double::NegativeInfinity;
			cs::List<double> dl{ 3, NaN, -0.0, 0.0, 1, NaN, -1, NegativeInfinity, 2, 0.0, -0.0, PositiveInfinity };
			for (int i = 0; i < 40; i++)
			{
				int k = static_cast<int>(Lcg() % 9) - 4;
				double z = (Lcg() & 1) == 0 ? 0.0 : -0.0;
				dl.Add(k + z);
			}
			dl.Sort();
			String sb;
			for (double d : dl) sb += Bits(d) + ",";
			P(cs::Cat("S doubles ", sb));
			cs::List<int> il;
			for (int i = 0; i < 300; i++) il.Add(static_cast<int>(Lcg() % 50));
			il.Sort();
			P(cs::Cat("S ints ", cs::Strings::Join(",", il)));
		}

		// --- TryParse
		for (const char* s : { "0", "-0", "1.5", " 12 ", "1,234.5", "1e400", "-1e-400", "NaN", "nan", "Infinity", "-Infinity", "-infinity",
			"+3.25", ".5", "5.", "1e", "abc", "", "1.7976931348623157E+308", "4.9E-324", "2.4703282292062327E-324", "0x10", "1_0",
			"123456789012345678901234567890", "0.1", "1,2,3", ",5", "1e+05", "1E-05", "  -7.25e2  ", "\xE2\x88\x9E", "1.2.3" })
		{
			double v = 0; int iv = 0;
			bool ok = cs::Double::TryParse(s, v);
			bool iok = cs::Int::TryParse(s, iv);
			P(cs::Cat("P [", s, "] ", ok, " ", Bits(v), " ", iok, " ", iv));
		}
		for (double d : ds)
		{
			String s = cs::Fmt(d, "R");
			double v = 0;
			bool ok = cs::Double::TryParse(s, v);
			P(cs::Cat("T ", Bits(d), " ", ok, " ", Bits(v)));
		}

		// --- DateTime / TimeSpan
		{
			using cs::DateTime; using cs::TimeSpan; using cs::DateTimeKind;
			DateTime j2000(2000, 1, 1, 12, 0, 0, DateTimeKind::Utc);
			P(cs::Cat("DT limits ", Bits((DateTime::MaxValue - j2000).TotalDays()), " ", Bits((DateTime::MinValue - j2000).TotalDays()), " ", j2000.ToBinary(), " ", j2000.Ticks()));
			cs::List<double> days{ 0, 0.5, -0.5, 1.0 / 3, 365.25, -36525.123456789, 2461348.0 - 2451545.0, 1e-9, 12345.678901234, -730000.5, 2900000.25, 0.1, 0.000011574074074074073 };
			for (int k = 0; k < 60; k++) days.Add((static_cast<double>(Lcg() >> 11) * (1.0 / 9007199254740992.0) - 0.5) * 1400000.0);
			const char* dfs[] = { "", "o", "s", "u", "R", "G", "d", "D", "f", "t", "T", "M", "y", "yyyyMMdd'T'HHmmss'Z'", "yyyy-MM-dd  HH:mm", "MMM d HH:mm", "yyyy-MM-dd HH:mm:ss.fffffff", "h:mm tt F FF FFF", "dddd ddd dd d yy y" };
			for (double d : days)
			{
				String r;
				try
				{
					DateTime t = j2000.AddDays(d);
					r += cs::Cat(t.Ticks(), " ", t.ToBinary(), " ", t.Year(), " ", t.DayOfYear(), " ", static_cast<int>(t.DayOfWeek()), " ");
					r += cs::Cat(Bits((t - j2000).TotalDays()), " ", TimeSpan::FromDays(d).Ticks(), " ", TimeSpan::FromDays(d).ToString(), " ");
					r += cs::Cat(t.AddHours(d).Ticks(), " ");
					r += cs::Cat(t.AddSeconds(d * 1000).Ticks(), " ");
					r += cs::Cat(t.AddMonths(7).Ticks(), " ");
					DateTime u = DateTime::SpecifyKind(t, DateTimeKind::Unspecified);
					for (const char* f : dfs) r += cs::Cat("|", *f ? u.ToString(f) : u.ToString());
				}
				catch (const cs::ArgumentOutOfRangeException&) { r += "<throw>"; }
				String sb = r;
				P(cs::Cat("DT ", Bits(d), " ", sb));
			}
		}
	}

	// Checks of the layer's own C++ mechanics (no .NET counterpart needed): List<bool>, Delegate, comparers.
	int SelfChecks()
	{
		int bad = 0;
		auto expect = [&](bool ok, const char* what) { if (!ok) { std::printf("self-check failed: %s\n", what); bad++; } };
		cs::List<bool> flags = cs::NewArray<bool>(3);
		flags[1] = true;
		bool& ref = flags[2]; ref = true;
		int n = 0; for (bool b : flags) n += b ? 1 : 0;
		for (bool& b : flags) b = !b;
		expect(n == 2 && flags[0] && !flags[1] && !flags[2] && flags.IndexOf(true) == 0, "List<bool>");
		int calls = 0;
		cs::Delegate<int> del;
		expect(!del, "Delegate empty");
		del += [&](int x) { calls += x; };
		del += [&](int x) { calls += 10 * x; };
		del(2);
		del = [&](int x) { calls += 100 * x; };
		del.Invoke(1);
		expect(calls == 122 && static_cast<bool>(del), "Delegate");
		cs::Dictionary<String, int, cs::OrdinalIgnoreCaseComparer> ci;
		ci["Alpha"] = 1; ci["ALPHA"] = 2;
		cs::HashSet<String, cs::OrdinalIgnoreCaseComparer> hs{ "a", "A", "b" };
		expect(ci.Count() == 1 && ci["alpha"] == 2 && hs.Count() == 2, "OrdinalIgnoreCase");
		cs::Dictionary<std::tuple<int, String>, int> tk; tk[{ 1, "x" }] = 5;
		expect(tk.ContainsKey({ 1, "x" }) && !tk.ContainsKey({ 2, "x" }), "tuple keys");
		expect(cs::CompareTo(cs::Double::NaN, 0.0) == -1 && cs::CompareTo(cs::Double::NaN, cs::Double::NaN) == 0 && cs::CompareTo(1.0, cs::Double::NaN) == 1, "CompareTo");
		expect(cs::Math::Max(0, 2.5) == 2.5 && cs::Math::Min(3, 2.5) == 2.5 && cs::Mod(-7.5, 2.0) == -1.5, "Math mixed");
		cs::List<int> li{ 1, 2, 3, 4, 5 };
		expect(li.GetRange(1, 3).Count() == 3 && li.GetRange(1, 3)[0] == 2, "GetRange");
		expect(cs::Format("{0:0.###} {1,4}|{2,-3}|{{x}}", 1.23456, 7, "ab") == "1.235    7|ab |{x}", "Format");
		return bad;
	}

	std::vector<String> Lines(const String& text)
	{
		std::vector<String> r;
		std::istringstream in(text);
		String line;
		while (std::getline(in, line))
		{
			if (!line.empty() && line.back() == '\r') line.pop_back();
			r.push_back(line);
		}
		return r;
	}
}

int main(int argc, char** argv)
{
	String expectedPath;
	bool print = false;
	for (int i = 1; i < argc; i++)
	{
		String a = argv[i];
		if (a == "--print") print = true;
		else expectedPath = a;
	}

	Generate();
	String actual = Out.str();
	if (print) { std::fwrite(actual.data(), 1, actual.size(), stdout); return 0; }
	if (expectedPath.empty()) expectedPath = "expected.txt";

	std::ifstream f(expectedPath, std::ios::binary);
	if (!f) { std::fprintf(stderr, "CsParity: can't read %s\n", expectedPath.c_str()); return 2; }
	std::stringstream ss; ss << f.rdbuf();
	auto want = Lines(ss.str());
	auto got = Lines(actual);

	int bad = 0;
	size_t n = std::max(want.size(), got.size());
	for (size_t i = 0; i < n; i++)
	{
		const String& w = i < want.size() ? want[i] : String("<missing>");
		const String& g = i < got.size() ? got[i] : String("<missing>");
		if (w != g)
		{
			if (bad < 200) std::printf("line %zu\n  .NET: %s\n  C++ : %s\n", i + 1, w.c_str(), g.c_str());
			bad++;
		}
	}
	int selfBad = SelfChecks();
	std::printf("CsParity: %zu lines, %d mismatch%s; %d self-check failure%s\n", n, bad, bad == 1 ? "" : "es", selfBad, selfBad == 1 ? "" : "s");
	return bad == 0 && selfBad == 0 ? 0 : 1;
}

#endif
