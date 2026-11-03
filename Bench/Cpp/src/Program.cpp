// Bench.Cpp: runs one named scenario of Bench/scenarios.json over the C++ port of Unity.SpaceGod's Core
// (Cpp/Source/SpaceGodCore), headless, and writes its digest and timings to Bench/results/cpp-<scenario>.json
// (+ .digest.txt). A port of Bench/CSharp/src/Program.cs: same arguments, same startup, same scenarios, same digest.
//
//   Bench.Cpp --list
//   Bench.Cpp --scenario sol-idle [--scenarios path] [--out dir] [--ticks N] [--trace N] [--dump-at T]
//
// --trace N writes cpp-<scenario>.trace.txt: every N ticks (and at tick 0, after the setup) one line of FNV-1a hashes
// of the digest's head and of each system's block (Digest::TraceLine); --dump-at T also writes the full digest at tick T
// to cpp-<scenario>.t<T>.digest.txt. Bench.CSharp takes the same two flags, so the first divergence can be found.

#include "Digest.h"
#include "Simulation.h"

#include "SimClock.h"
#include "World.h"
#include "Ships.h"
#include "Fleet.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Combat/Battle.h"
#include "Systems/Galaxy.h"
#include "Shared/Json/JsonObject.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <sstream>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <dbghelp.h>

// ---------------------------------------------------------------- allocation counter (C#'s GC.GetTotalAllocatedBytes)
namespace
{
	std::atomic<long long> g_allocated{ 0 };
}

void* operator new(std::size_t n)
{
	g_allocated.fetch_add(static_cast<long long>(n), std::memory_order_relaxed);
	if (void* p = std::malloc(n ? n : 1)) return p;
	throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace Bench
{
	using namespace SpaceGod::Core;
	namespace Json = SpaceGod::Shared::Json;
	namespace fs = std::filesystem;

	struct ScenarioSetup
	{
		std::string Kind, Body, Aggressor, Defender, To;
		std::vector<std::string> Classes;
		cs::Nullable<bool> Military;
	};

	struct Scenario
	{
		std::string Name, Description, Until;
		int Warp = 0, Fps = 0, Ticks = 0, ThenWarp = -1;
		double SimDays = 0;
		std::shared_ptr<ScenarioSetup> Setup;
	};

	struct ScenarioFile
	{
		double StartJd = 0;
		int Seed = 0;
		std::vector<Scenario> Scenarios;
		std::string RawStartJd;

		Scenario* Find(const std::string& name)
		{
			for (auto& s : Scenarios) if (s.Name == name) return &s;
			return nullptr;
		}

		static std::string Str(const cs::Ref<Json::JsonObject>& o, const char* k)
		{
			auto v = (*o)[k];
			auto s = v.AsString();
			return s ? *s : std::string();
		}
		static double Num(const cs::Ref<Json::JsonObject>& o, const char* k) { return (*o)[k].AsNumber()->Value(); }
		static bool Has(const cs::Ref<Json::JsonObject>& o, const char* k) { return o->ContainsKey(k) && !(*o)[k].IsNull(); }

		static ScenarioFile Load(const std::string& path)
		{
			std::ifstream in(path, std::ios::binary);
			std::stringstream ss;
			ss << in.rdbuf();
			auto root = Json::JsonObject::Parse(ss.str());
			ScenarioFile f;
			f.StartJd = Num(root, "startJd");
			f.RawStartJd = (*root)["startJd"].AsNumber()->Raw;
			f.Seed = static_cast<int>(Num(root, "seed"));
			auto arr = (*root)["scenarios"].AsArray();
			for (int i = 0; i < arr->Count(); i++)
			{
				auto o = (*arr)[i].AsObject();
				Scenario s;
				s.Name = Str(o, "name");
				s.Description = Str(o, "description");
				s.Warp = static_cast<int>(Num(o, "warp"));
				s.Fps = static_cast<int>(Num(o, "fps"));
				s.Ticks = static_cast<int>(Num(o, "ticks"));
				s.SimDays = Has(o, "simDays") ? Num(o, "simDays") : 0;
				s.Until = Str(o, "until");
				s.ThenWarp = Has(o, "thenWarp") ? static_cast<int>(Num(o, "thenWarp")) : -1;
				if (auto su = (*o)["setup"].AsObject())
				{
					s.Setup = std::make_shared<ScenarioSetup>();
					s.Setup->Kind = Str(su, "kind");
					s.Setup->Body = Str(su, "body");
					s.Setup->Aggressor = Str(su, "aggressor");
					s.Setup->Defender = Str(su, "defender");
					s.Setup->To = Str(su, "to");
					if (Has(su, "military")) s.Setup->Military = *(*su)["military"].AsBool();
					if (auto ca = (*su)["classes"].AsArray())
						for (int k = 0; k < ca->Count(); k++) s.Setup->Classes.push_back(*(*ca)[k].AsString());
				}
				f.Scenarios.push_back(std::move(s));
			}
			return f;
		}
	};

	// ---------------------------------------------------------------- JSON writing (System.Text.Json's indented shape)
	std::string JsonEscape(const std::string& s)
	{
		std::string r = "\"";
		for (unsigned char c : s)
		{
			switch (c)
			{
			case '"': r += "\\u0022"; break;
			case '\\': r += "\\\\"; break;
			case '\n': r += "\\n"; break;
			case '\r': r += "\\r"; break;
			case '\t': r += "\\t"; break;
			case '\'': r += "\\u0027"; break;
			case '<': r += "\\u003C"; break;
			case '>': r += "\\u003E"; break;
			case '&': r += "\\u0026"; break;
			case '+': r += "\\u002B"; break;
			default:
				if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04X", c); r += b; }
				else r += static_cast<char>(c);
			}
		}
		return r + "\"";
	}
	std::string JsonNum(double d) { return cs::Str(d); }

	struct JsonWriter
	{
		std::string out;
		int depth = 0;
		bool first = true;
		void Indent() { out += std::string(static_cast<size_t>(depth) * 2, ' '); }
		void Key(const char* k)
		{
			if (!first) out += ",";
			out += "\n";
			first = false;
			Indent();
			out += JsonEscape(k) + ": ";
		}
		void Raw(const char* k, const std::string& v) { Key(k); out += v; }
		void Str(const char* k, const std::string& v) { Raw(k, JsonEscape(v)); }
		void Num(const char* k, double v) { Raw(k, JsonNum(v)); }
		void Int(const char* k, long long v) { Raw(k, std::to_string(v)); }
		void Begin(const char* k) { if (k) Key(k); out += "{"; depth++; first = true; }
		void End() { depth--; out += "\n"; Indent(); out += "}"; first = false; }
	};

	// ---------------------------------------------------------------- the run
	struct RunResult
	{
		std::string Json, DigestText, Summary;
	};

	cs::Ref<Faction> FactionOf(const cs::Ref<World>& w, const std::string& name)
	{
		if (name == "Sol") return w->Sol;
		if (name == "Belt") return w->Belt;
		if (name == "Reach") return w->Reach;
		if (name == "Independents") return w->Independents;
		if (name == "Pirates") return w->Pirates;
		throw std::runtime_error("unknown faction " + name);
	}

	ShipClass ParseShipClass(const std::string& name)
	{
		for (int i = 0; i < 64; i++)
			if (name == ToString(static_cast<ShipClass>(i))) return static_cast<ShipClass>(i);
		throw std::runtime_error("unknown ShipClass " + name);
	}

	void Setup(Simulation& sim, const Scenario& sc)
	{
		auto w = sim.World();
		std::string kind = sc.Setup ? sc.Setup->Kind : "none";
		if (kind == "none") return;
		if (kind == "skirmish")
		{
			int body = (*sim.ActiveSystem->Catalog)[sc.Setup->Body]->Index;
			cs::List<ShipClass> classes;
			for (const auto& c : sc.Setup->Classes) classes.Add(ParseShipClass(c));
			auto battle = sim.TriggerSkirmish(body, FactionOf(w, sc.Setup->Aggressor), FactionOf(w, sc.Setup->Defender), classes);
			if (battle == nullptr) throw std::runtime_error("the skirmish did not start");
			sim.Skirmish = battle;
			sim.Clock->SetPaused(false);
			sim.WatchBattle(battle);
			return;
		}
		if (kind == "crew-course")
		{
			auto to = sim.Galaxy->Find(sc.Setup->To);
			if (to == nullptr) throw std::runtime_error("no system " + sc.Setup->To);
			// The first ship (by Id) in the active system that its crew can take there: the one the player follows.
			bool sent = false;
			auto ships = w->Fleet->Ships(); // C# foreach over the live list; StartCrewCourse breaks out on success
			for (const auto& ship : ships)
			{
				if (sc.Setup->Military.has_value() && ship->Spec != nullptr && ship->Spec->IsMilitary != *sc.Setup->Military) continue;
				if (sim.StartCrewCourse(ship, to)) { sent = true; break; }
			}
			if (!sent) throw std::runtime_error("no ship could be sent to " + sc.Setup->To);
			return;
		}
		throw std::runtime_error("unknown setup kind " + kind);
	}

	bool Done(Simulation& sim, const Scenario& sc, double startJd, std::string& reason)
	{
		reason.clear();
		if (sc.SimDays > 0 && sim.Clock->Jd - startJd >= sc.SimDays) { reason = "simDays"; return true; }
		std::string until = sc.Until.empty() ? "ticks" : sc.Until;
		if (until == "battle-over")
		{
			if (sim.Skirmish != nullptr && sim.Skirmish->Over) { reason = "battle-over"; return true; }
		}
		else if (until == "course-ended")
		{
			if (sim.CourseEnded) { reason = sim.CourseArrived ? "course-arrived" : "course-ended"; return true; }
		}
		return false;
	}

	double Percentile(const std::vector<double>& sorted, double p)
	{
		if (sorted.empty()) return 0;
		int i = static_cast<int>(std::ceil(p * static_cast<double>(sorted.size()))) - 1;
		return sorted[static_cast<size_t>(std::max(0, std::min(static_cast<int>(sorted.size()) - 1, i)))];
	}

	double Round4(double ms) { return cs::Math::Round(ms, 4); }

	long long PeakWorkingSet()
	{
		PROCESS_MEMORY_COUNTERS pmc{};
		if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc)) return static_cast<long long>(pmc.PeakWorkingSetSize);
		return 0;
	}

	using Clock = std::chrono::steady_clock;
	double Ms(Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }

	struct TraceOptions
	{
		int Every = 0;
		int DumpAt = -1;
		std::string Dir, Prefix;
	};

	RunResult Run(const ScenarioFile& file, const Scenario& sc, const TraceOptions& trace)
	{
		long long alloc0 = g_allocated.load();
		Clock::duration total{};
		auto t0 = Clock::now();

		auto sim = Simulation::Build(file.StartJd, file.Seed);
		auto t1 = Clock::now();
		double buildMs = Ms(t1 - t0);
		long long alloc1 = g_allocated.load();
		total += t1 - t0;

		std::string buildDigest = Digest::Write(*sim, sc.Name);
		auto t2 = Clock::now();

		// The scenario's warp, then its setup (which may change it, as the game's own calls do).
		sim->Clock->SetWarp(sc.Warp);
		Setup(*sim, sc);
		// The player's own '.' / ',' after the setup (e.g. speeding battle time back up): -1 leaves it.
		if (sc.ThenWarp >= 0) sim->Clock->SetWarp(sc.ThenWarp);

		// UnitySpaceApp.Update: dt = Mathf.Min(Time.unscaledDeltaTime, 0.1f), a float; here a fixed frame time.
		double dt = static_cast<double>(std::min(1.0f / static_cast<float>(sc.Fps), 0.1f));
		double startJd = sim->Clock->Jd;
		std::vector<double> tickMs;
		tickMs.reserve(static_cast<size_t>(std::min(sc.Ticks, 1 << 20)));
		std::string stop = "ticks";

		std::ofstream traceOut;
		auto pauseTotal = [&](auto fn) { auto a = Clock::now(); fn(); return Clock::now() - a; };
		Clock::duration untimed{};
		auto traceAt = [&](int t) {
			if (trace.Every > 0 && t % trace.Every == 0)
			{
				untimed += pauseTotal([&] { traceOut << Digest::TraceLine(t, Digest::Write(*sim, sc.Name)) << "\n"; traceOut.flush(); });
			}
			if (t == trace.DumpAt)
			{
				untimed += pauseTotal([&] {
					std::ofstream d(fs::path(trace.Dir) / (trace.Prefix + sc.Name + ".t" + std::to_string(t) + ".digest.txt"), std::ios::binary);
					d << Digest::Write(*sim, sc.Name);
				});
			}
		};
		if (trace.Every > 0) traceOut.open(fs::path(trace.Dir) / (trace.Prefix + sc.Name + ".trace.txt"), std::ios::binary);

		traceAt(0);
		for (int t = 0; t < sc.Ticks; t++)
		{
			if (Done(*sim, sc, startJd, stop)) break;
			auto a = Clock::now();
			sim->Tick(dt);
			auto b = Clock::now();
			tickMs.push_back(Ms(b - a));
			stop = "ticks";
			traceAt(t + 1);
		}
		std::string late;
		if (stop == "ticks" && Done(*sim, sc, startJd, late)) stop = late;
		auto t3 = Clock::now();
		total += (t3 - t2) - untimed;
		long long alloc2 = g_allocated.load();

		std::string digest = Digest::Write(*sim, sc.Name);
		uint64_t hash = Digest::Fnv1a64(digest);

		auto sorted = tickMs;
		std::sort(sorted.begin(), sorted.end());
		double sum = 0;
		for (double x : sorted) sum += x;

		char runtime[128];
		std::snprintf(runtime, sizeof runtime, "MSVC %d.%d C++20 /O2 /fp:precise (SpaceGodCore standalone)", _MSC_VER / 100, _MSC_VER % 100);

		JsonWriter j;
		j.Begin(nullptr);
		j.Str("track", "cpp");
		j.Str("scenario", sc.Name);
		j.Str("description", sc.Description);
		j.Str("runtime", runtime);
		j.Str("digestVersion", Digest::Version);
		j.Raw("startJd", file.RawStartJd);
		j.Int("seed", file.Seed);
		j.Int("warp", sc.Warp);
		j.Int("thenWarp", sc.ThenWarp);
		j.Int("fps", sc.Fps);
		j.Num("dtSeconds", dt);
		j.Int("ticksRun", static_cast<long long>(tickMs.size()));
		j.Str("stopReason", stop);
		j.Num("endJd", sim->Clock->Jd);
		j.Num("simDaysAdvanced", sim->Clock->Jd - startJd);
		j.Int("endWarp", sim->Clock->WarpIndex);
		j.Str("buildDigestHash", Digest::Hex(Digest::Fnv1a64(buildDigest)));
		j.Str("digestHash", Digest::Hex(hash));
		j.Int("digestBytes", static_cast<long long>(digest.size()));
		j.Str("digestFile", "cpp-" + sc.Name + ".digest.txt");
		double tickMean = sorted.empty() ? 0 : sum / static_cast<double>(sorted.size());
		double tickMax = sorted.empty() ? 0 : sorted.back();
		double totalMs = Ms(total);
		j.Begin("timings");
		j.Num("buildMs", Round4(buildMs));
		j.Num("tickMeanMs", Round4(tickMean));
		j.Num("tickP50Ms", Round4(Percentile(sorted, 0.50)));
		j.Num("tickP95Ms", Round4(Percentile(sorted, 0.95)));
		j.Num("tickP99Ms", Round4(Percentile(sorted, 0.99)));
		j.Num("tickMaxMs", Round4(tickMax));
		j.Num("ticksTotalMs", Round4(sum));
		j.Num("totalMs", Round4(totalMs));
		j.End();
		long long peak = PeakWorkingSet();
		j.Begin("memory");
		j.Int("allocatedBuildBytes", alloc1 - alloc0);
		j.Int("allocatedRunBytes", alloc2 - alloc1);
		j.Int("peakWorkingSetBytes", peak);
		j.End();
		j.End();

		char summary[512];
		std::snprintf(summary, sizeof summary,
			"%-14s ticks=%6d stop=%-14s days=%9s build=%8.1fms tick mean=%.3f p50=%.3f p95=%.3f max=%.3fms total=%.1fms alloc=%.1fMB peakWS=%.1fMB digest=%s",
			sc.Name.c_str(), static_cast<int>(tickMs.size()), stop.c_str(), cs::Fmt(sim->Clock->Jd - startJd, "0.###").c_str(), Round4(buildMs), Round4(tickMean),
			Round4(Percentile(sorted, 0.50)), Round4(Percentile(sorted, 0.95)), Round4(tickMax), Round4(totalMs),
			static_cast<double>(alloc2 - alloc0) / 1048576.0, static_cast<double>(peak) / 1048576.0, Digest::Hex(hash).c_str());

		return RunResult{ j.out + "\n", digest, summary };
	}

	std::string FindScenarios(const char* argv0)
	{
		std::error_code ec;
		fs::path dir = fs::absolute(fs::path(argv0), ec).parent_path();
		for (; !dir.empty(); dir = dir.parent_path())
		{
			if (fs::exists(dir / "scenarios.json") && dir.filename() == "Bench") return (dir / "scenarios.json").string();
			if (fs::exists(dir / "Bench" / "scenarios.json")) return (dir / "Bench" / "scenarios.json").string();
			if (dir == dir.parent_path()) break;
		}
		return {};
	}

	int Main(int argc, char** argv)
	{
		std::string scenariosPath, outDir, name;
		bool list = false;
		int ticksOverride = -1;
		TraceOptions trace;
		for (int i = 1; i < argc; i++)
		{
			std::string a = argv[i];
			auto next = [&]() -> std::string { if (i + 1 >= argc) throw std::runtime_error("missing value after " + a); return argv[++i]; };
			if (a == "--scenarios") scenariosPath = next();
			else if (a == "--out") outDir = next();
			else if (a == "--scenario") name = next();
			else if (a == "--ticks") ticksOverride = std::stoi(next());
			else if (a == "--trace") trace.Every = std::stoi(next());
			else if (a == "--dump-at") trace.DumpAt = std::stoi(next());
			else if (a == "--list") list = true;
			else { std::cerr << "unknown argument " << a << "\n"; return 2; }
		}
		if (scenariosPath.empty()) scenariosPath = FindScenarios(argv[0]);
		if (scenariosPath.empty()) { std::cerr << "Bench/scenarios.json not found; pass --scenarios\n"; return 2; }
		if (outDir.empty()) outDir = (fs::absolute(scenariosPath).parent_path() / "results").string();

		auto file = ScenarioFile::Load(scenariosPath);
		if (list)
		{
			for (const auto& s : file.Scenarios) std::cout << s.Name << "\n";
			return 0;
		}
		auto scenario = file.Find(name);
		if (scenario == nullptr) { std::cerr << "no scenario '" << name << "' in " << scenariosPath << " (--list names them)\n"; return 2; }
		if (ticksOverride >= 0) scenario->Ticks = ticksOverride;

		fs::create_directories(outDir);
		trace.Dir = outDir;
		trace.Prefix = "cpp-";
		auto result = Run(file, *scenario, trace);
		{
			std::ofstream d(fs::path(outDir) / ("cpp-" + scenario->Name + ".digest.txt"), std::ios::binary);
			d << result.DigestText;
		}
		{
			std::ofstream o(fs::path(outDir) / ("cpp-" + scenario->Name + ".json"), std::ios::binary);
			o << result.Json;
		}
		std::cout << result.Summary << "\n";
		return 0;
	}
}

// ---------------------------------------------------------------- crash report: a native fault prints its stack (pdbs ship beside the exe)
namespace
{
	LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep)
	{
		HANDLE proc = GetCurrentProcess(), thread = GetCurrentThread();
		std::fprintf(stderr, "Bench.Cpp: native exception 0x%08lX at %p\n", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
		SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
		SymInitialize(proc, nullptr, TRUE);
		CONTEXT ctx = *ep->ContextRecord;
		STACKFRAME64 f{};
		f.AddrPC.Offset = ctx.Rip; f.AddrPC.Mode = AddrModeFlat;
		f.AddrFrame.Offset = ctx.Rbp; f.AddrFrame.Mode = AddrModeFlat;
		f.AddrStack.Offset = ctx.Rsp; f.AddrStack.Mode = AddrModeFlat;
		for (int i = 0; i < 48 && StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, thread, &f, &ctx, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr); i++)
		{
			char buf[sizeof(SYMBOL_INFO) + 512] = {};
			auto sym = reinterpret_cast<SYMBOL_INFO*>(buf);
			sym->SizeOfStruct = sizeof(SYMBOL_INFO); sym->MaxNameLen = 511;
			DWORD64 d = 0; DWORD dl = 0;
			IMAGEHLP_LINE64 line{}; line.SizeOfStruct = sizeof line;
			const char* name = SymFromAddr(proc, f.AddrPC.Offset, &d, sym) ? sym->Name : "?";
			if (SymGetLineFromAddr64(proc, f.AddrPC.Offset, &dl, &line)) std::fprintf(stderr, "  %s  %s(%lu)\n", name, line.FileName, line.LineNumber);
			else std::fprintf(stderr, "  %s\n", name);
		}
		std::fflush(stderr);
		return EXCEPTION_EXECUTE_HANDLER;
	}
}

int main(int argc, char** argv)
{
	SetUnhandledExceptionFilter(CrashFilter);
	try
	{
		return Bench::Main(argc, argv);
	}
	catch (const std::exception& e)
	{
		std::cerr << "Bench.Cpp: unhandled exception: " << e.what() << "\n";
		return 1;
	}
}
