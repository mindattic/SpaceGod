// Bench.Rewind (US-T99, phase 1: measurement only): `--rewind-probe`. Included by Program.cpp inside namespace Bench,
// after Setup/Done, only when built with /DBENCH_REWIND (tools/build-bench-cpp.ps1 -Rewind -> Bench.Rewind.exe).
//
// Per scenario, in one process:
//   pass 1  the plain run (no keyframes): tick cost and game time per tick, bucketed by warp -> replay speed; final digest.
//   pass 2  the same run again, capturing keyframes (the game's save graph, Rewind::Capture) at a schedule of pairs
//           (base, base + interval) for intervals 1 min .. 30 days, spread over the run; per keyframe the raw size and
//           every codec's size and time; per pair the delta (Oodle with the base keyframe as dictionary) and the
//           naive same-offset diff. At two checkpoints (1/3 and 2/3 of the run) it records the live digest at the
//           checkpoint, `verify` ticks later and at the end.
//   replay  per checkpoint: decompress + restore the keyframe into a fresh Simulation, digest (must equal the live one at
//           that tick), re-simulate `verify` ticks (must match), then on to the end (must match the live final digest).
// Writes Bench/results/rewind-<scenario>.json and prints a summary.

#if defined(BENCH_REWIND)

	struct RewindOptions
	{
		int Samples = 4;
		int Verify = 60;
		int Trace = 0;   // every N ticks after a checkpoint: live vs replay digest hashes, first divergence reported
		int GraphDiff = 0;  // when a restored keyframe re-captures differently, list this many differing graph entries
		bool Decks = false; // prints carrier flight-deck state (not in the save) live at each checkpoint and after restore
		int DumpAt = -1; // writes the live and the replayed digest text at this tick (rewind-<scenario>.t<T>.live/.replay.txt)
	};

	struct RwBuilt
	{
		cs::Ref<Simulation> Sim;
		double Dt = 0, StartJd = 0;
	};

	inline RwBuilt RwStart(const ScenarioFile& file, const Scenario& sc)
	{
		RwBuilt b;
		b.Sim = Simulation::Build(file.StartJd, file.Seed);
		b.Sim->Clock->SetWarp(sc.Warp);
		Setup(*b.Sim, sc);
		if (sc.ThenWarp >= 0) b.Sim->Clock->SetWarp(sc.ThenWarp);
		b.Dt = static_cast<double>(std::min(1.0f / static_cast<float>(sc.Fps), 0.1f));
		b.StartJd = b.Sim->Clock->Jd;
		return b;
	}

	inline int RwBattlesRunning(Simulation& sim)
	{
		int n = 0;
		for (const auto& s : sim.Galaxy->Systems) n += s->World->War->Battles.Count();
		return n;
	}

	/// Diagnostic (--rewind-decks): the carrier flight-deck state, which lives in FlightDecks' static table rather than
	/// in the save graph. Calls FlightDecks::Of, which builds (fresh, empty) decks for a hull that had none yet.
	inline std::string RwDecks(Simulation& sim)
	{
		int decks = 0, aboard = 0, finals = 0, timers = 0;
		for (const auto& s : sim.Galaxy->Systems)
			for (const auto& b : s->World->War->Battles)
				for (const auto& u : b->Units)
					for (const auto& d : FlightDecks::Of(u))
					{
						decks++;
						aboard += d->Aboard.Count();
						finals += d->Final != nullptr;
						timers += d->NextLaunch > 0;
					}
		char buf[160];
		std::snprintf(buf, sizeof buf, "decks=%d aboard=%d onFinal=%d launchTimers=%d", decks, aboard, finals, timers);
		return buf;
	}

	inline std::string RwHash(Simulation& sim, const Scenario& sc) { return Digest::Hex(Digest::Fnv1a64(Digest::Write(sim, sc.Name))); }

	struct RwJson
	{
		std::string s;
		std::string Num(double d) { char b[64]; std::snprintf(b, sizeof b, "%.6g", d); return b; }
		std::string Q(const std::string& v) { return JsonEscape(v); }
	};

	int RewindProbe(const ScenarioFile& file, const Scenario& sc, const std::string& outDir, const RewindOptions& opt)
	{
		using namespace Rewind;
		RwJson J;
		std::string& o = J.s;
		const std::vector<Codec> codecs = { Codec::Deflate1, Codec::MermaidFast, Codec::KrakenFast, Codec::KrakenNormal };
		const std::vector<Codec> deltaCodecs = { Codec::KrakenFast, Codec::KrakenNormal };
		std::printf("rewind-probe %s\n", sc.Name.c_str());

		// ---------------------------------------------------------------- pass 1: plain run
		std::vector<double> jd;       // jd[t] = clock after t ticks (t = 0: after the setup)
		std::vector<int> battles;     // battles running after t ticks
		struct Bucket { long long Ticks = 0; double Ms = 0, SimSec = 0; };
		std::map<int, Bucket> byWarp;
		std::string plainHash;
		double plainTickMs = 0;
		{
			auto b = RwStart(file, sc);
			auto& sim = *b.Sim;
			jd.push_back(sim.Clock->Jd);
			battles.push_back(RwBattlesRunning(sim));
			std::string stop;
			for (int t = 0; t < sc.Ticks; t++)
			{
				if (Done(sim, sc, b.StartJd, stop)) break;
				int warp = sim.Clock->WarpIndex;
				double jd0 = sim.Clock->Jd;
				auto a = Clock::now();
				sim.Tick(b.Dt);
				double ms = Ms(Clock::now() - a);
				plainTickMs += ms;
				auto& bk = byWarp[warp];
				bk.Ticks++;
				bk.Ms += ms;
				bk.SimSec += (sim.Clock->Jd - jd0) * 86400.0;
				jd.push_back(sim.Clock->Jd);
				battles.push_back(RwBattlesRunning(sim));
			}
			plainHash = RwHash(sim, sc);
		}
		const int T = static_cast<int>(jd.size()) - 1;
		std::printf("  pass 1: %d ticks, %.3f game-days, %.1f s ticking, digest %s\n", T, jd[T] - jd[0], plainTickMs / 1000, plainHash.c_str());

		// ---------------------------------------------------------------- the schedule
		const double intervals[] = { 10, 60, 600, 3600, 86400, 604800, 2592000 };
		struct Pair { double Interval; int A, B; };
		std::vector<Pair> pairs;
		auto firstAtLeast = [&](int from, double sec) {
			for (int t = from + 1; t <= T; t++) if ((jd[t] - jd[from]) * 86400.0 >= sec * (1 - 1e-3)) return t; // a jd step of 60 s lands a hair under 60
			return -1;
		};
		for (double iv : intervals)
		{
			int bmax = -1;
			for (int bb = T - 1; bb >= 0; bb--) if (firstAtLeast(bb, iv) >= 0) { bmax = bb; break; }
			if (bmax < 0) continue;
			std::set<int> bases;
			for (int s = 0; s < opt.Samples; s++)
				bases.insert(opt.Samples == 1 ? 0 : static_cast<int>(std::lround(static_cast<double>(s) * bmax / (opt.Samples - 1))));
			for (int a : bases)
			{
				int bt = firstAtLeast(a, iv);
				if (bt < 0) continue;
				double actual = (jd[bt] - jd[a]) * 86400.0;
				if (actual > 2.0 * iv) continue; // the tick is coarser than the interval
				pairs.push_back({ iv, a, bt });
			}
		}
		std::vector<int> checkpoints;
		for (int c : { T / 3, (2 * T) / 3 }) if (c > 0 && c < T && std::find(checkpoints.begin(), checkpoints.end(), c) == checkpoints.end()) checkpoints.push_back(c);
		std::set<int> captureAt;
		for (const auto& p : pairs) { captureAt.insert(p.A); captureAt.insert(p.B); }
		for (int c : checkpoints) captureAt.insert(c);
		std::map<int, int> liveRefs; // keyframes still needed by a pair
		for (const auto& p : pairs) liveRefs[p.A]++;

		// ---------------------------------------------------------------- pass 2: capture
		struct Cap { int Tick; double Jd; int Battles; size_t Raw; double CaptureMs; std::map<Codec, std::pair<size_t, double>> Comp; std::map<Codec, double> DecMs; };
		struct PairResult { Pair P; double Actual; int BattlesA, BattlesB; size_t RawA, RawB, SameOffsetDiff; std::map<Codec, std::pair<size_t, double>> Delta; double DeltaDecMs; bool DeltaRoundTrip; };
		struct Check { int Tick; std::string AtK, AtKV, End; int KV; Bytes Raw; Bytes Comp; std::map<int, std::string> Trace; };
		auto dumpPath = [&](const std::string& what) { return fs::path(outDir) / ("rewind-" + sc.Name + ".t" + std::to_string(opt.DumpAt) + "." + what + ".txt"); };
		std::vector<Cap> caps;
		std::vector<PairResult> pairResults;
		std::vector<Check> checks;
		std::map<int, Bytes> held;
		std::string probedHash;
		size_t saveFileBytes = 0;
		double saveFileMs = 0;
		{
			auto b = RwStart(file, sc);
			auto& sim = *b.Sim;
			std::string stop;
			auto at = [&](int t) {
				for (auto& c : checks)
				{
					if (c.KV == t) c.AtKV = RwHash(sim, sc);
					if (opt.Trace > 0 && t > c.Tick && (t - c.Tick) % opt.Trace == 0) c.Trace[t] = RwHash(sim, sc);
				}
				if (t == opt.DumpAt)
				{
					{ std::ofstream d(dumpPath("live"), std::ios::binary); d << Digest::Write(sim, sc.Name); }
					Bytes k = Capture(sim);
					std::ofstream d(fs::path(dumpPath("live")).replace_extension(".keyframe"), std::ios::binary);
					d.write(reinterpret_cast<const char*>(k.data()), static_cast<std::streamsize>(k.size()));
				}
				if (!captureAt.count(t)) return;
				Cap cap{};
				cap.Tick = t;
				cap.Jd = sim.Clock->Jd;
				cap.Battles = RwBattlesRunning(sim);
				auto a = Clock::now();
				Bytes raw = Capture(sim);
				cap.CaptureMs = Ms(Clock::now() - a);
				cap.Raw = raw.size();
				for (Codec c : codecs)
				{
					if (!Available(c)) continue;
					auto c0 = Clock::now();
					Bytes z = Compress(c, raw);
					double cms = Ms(Clock::now() - c0);
					auto d0 = Clock::now();
					Bytes back = Decompress(c, z, raw.size());
					cap.DecMs[c] = Ms(Clock::now() - d0);
					if (back != raw) throw std::runtime_error(std::string("codec round trip failed: ") + Name(c));
					cap.Comp[c] = { z.size(), cms };
				}
				// Pairs that end here.
				for (const auto& p : pairs)
				{
					if (p.B != t) continue;
					const Bytes& base = held.at(p.A);
					PairResult r{};
					r.P = p;
					r.Actual = (jd[p.B] - jd[p.A]) * 86400.0;
					r.BattlesA = battles[p.A];
					r.BattlesB = cap.Battles;
					r.RawA = base.size();
					r.RawB = raw.size();
					r.SameOffsetDiff = SameOffsetDiffBytes(base, raw);
					for (Codec c : deltaCodecs)
					{
						if (!Available(c)) continue;
						auto c0 = Clock::now();
						Bytes z = Compress(c, raw, &base);
						r.Delta[c] = { z.size(), Ms(Clock::now() - c0) };
						if (c == Codec::KrakenFast)
						{
							auto d0 = Clock::now();
							Bytes back = Decompress(c, z, raw.size(), &base);
							r.DeltaDecMs = Ms(Clock::now() - d0);
							r.DeltaRoundTrip = back == raw;
						}
					}
					pairResults.push_back(std::move(r));
					if (--liveRefs[p.A] == 0) held.erase(p.A);
				}
				if (std::find(checkpoints.begin(), checkpoints.end(), t) != checkpoints.end())
				{
					Check c{};
					c.Tick = t;
					c.AtK = RwHash(sim, sc);
					if (opt.Decks) std::printf("  live at tick %d: %s\n", t, RwDecks(sim).c_str());
					c.KV = std::min(t + opt.Verify, T);
					c.Comp = Available(Codec::KrakenFast) ? Compress(Codec::KrakenFast, raw) : Compress(Codec::Deflate1, raw);
					c.Raw = raw;
					checks.push_back(std::move(c));
				}
				if (liveRefs.count(t) && liveRefs[t] > 0) held[t] = std::move(raw);
				caps.push_back(std::move(cap));
			};
			at(0);
			int t = 0;
			for (; t < sc.Ticks; t++)
			{
				if (Done(sim, sc, b.StartJd, stop)) break;
				sim.Tick(b.Dt);
				at(t + 1);
			}
			if (t != T) std::printf("  WARNING: pass 2 ran %d ticks, pass 1 %d\n", t, T);
			probedHash = RwHash(sim, sc);
			for (auto& c : checks) c.End = probedHash;
			auto s0 = Clock::now();
			saveFileBytes = SaveFile(sim).size();
			saveFileMs = Ms(Clock::now() - s0);
		}
		std::printf("  pass 2: %zu keyframes, %zu pairs, digest %s (%s pass 1)\n", caps.size(), pairResults.size(), probedHash.c_str(),
			probedHash == plainHash ? "same as" : "DIFFERS FROM");

		// ---------------------------------------------------------------- replay from each checkpoint
		struct Replay { int Tick; double DecMs, RestoreMs; std::string AtK, AtKV, End; bool OkK, OkKV, OkEnd; int Ticks; double TickMs, SimSec; int FirstDiff = -1; bool RoundTrip = false; };
		std::vector<Replay> replays;
		for (auto& c : checks)
		{
			Replay r{};
			r.Tick = c.Tick;
			Codec codec = Available(Codec::KrakenFast) ? Codec::KrakenFast : Codec::Deflate1;
			auto d0 = Clock::now();
			Bytes raw = Decompress(codec, c.Comp, c.Raw.size());
			r.DecMs = Ms(Clock::now() - d0);
			auto r0 = Clock::now();
			auto simRef = Restore(raw);
			r.RestoreMs = Ms(Clock::now() - r0);
			auto& sim = *simRef;
			r.AtK = RwHash(sim, sc);
			r.OkK = r.AtK == c.AtK;
			if (opt.Decks) std::printf("  restored at tick %d: %s\n", c.Tick, RwDecks(sim).c_str());
			{
				// Round trip: the restored world written again must be the keyframe's bytes (what a save loses on load shows here).
				Bytes again = Capture(sim);
				r.RoundTrip = again == raw;
				if (!r.RoundTrip)
				{
					size_t i = 0;
					while (i < std::min(again.size(), raw.size()) && again[i] == raw[i]) i++;
					std::printf("  replay from tick %d: re-captured keyframe differs (%zu vs %zu bytes, first at offset %zu)\n", c.Tick, again.size(), raw.size(), i);
					if (opt.GraphDiff > 0) std::printf("%s", GraphDiff(raw, again, opt.GraphDiff).c_str());
				}
			}
			std::string stop;
			int t = c.Tick;
			double jdStart = jd[0]; // Done's startJd: the clock after the setup
			for (; t < sc.Ticks; t++)
			{
				if (Done(sim, sc, jdStart, stop)) break;
				double j0 = sim.Clock->Jd;
				auto a = Clock::now();
				sim.Tick(1.0 * static_cast<double>(std::min(1.0f / static_cast<float>(sc.Fps), 0.1f)));
				r.TickMs += Ms(Clock::now() - a);
				r.SimSec += (sim.Clock->Jd - j0) * 86400.0;
				r.Ticks++;
				if (t + 1 == c.KV) { r.AtKV = RwHash(sim, sc); r.OkKV = r.AtKV == c.AtKV; }
				auto tr = c.Trace.find(t + 1);
				if (r.FirstDiff < 0 && tr != c.Trace.end() && RwHash(sim, sc) != tr->second)
				{
					r.FirstDiff = t + 1;
					std::printf("  replay from tick %d: first traced divergence at tick %d (previous trace point matched)\n", c.Tick, t + 1);
				}
				if (t + 1 == opt.DumpAt)
				{
					auto p = dumpPath("replay-from" + std::to_string(c.Tick));
					{ std::ofstream d(p, std::ios::binary); d << Digest::Write(sim, sc.Name); }
					Bytes k = Capture(sim);
					std::ofstream d(fs::path(p).replace_extension(".keyframe"), std::ios::binary);
					d.write(reinterpret_cast<const char*>(k.data()), static_cast<std::streamsize>(k.size()));
				}
			}
			if (c.KV == c.Tick) { r.AtKV = r.AtK; r.OkKV = r.OkK; }
			r.End = RwHash(sim, sc);
			r.OkEnd = r.End == c.End && t == T;
			std::printf("  replay from tick %d: restore %.0f ms (+%.0f ms decompress); digest at %d %s, at %d %s, at end (%d) %s\n",
				c.Tick, r.RestoreMs, r.DecMs, c.Tick, r.OkK ? "MATCH" : "MISMATCH", c.KV, r.OkKV ? "MATCH" : "MISMATCH", t, r.OkEnd ? "MATCH" : "MISMATCH");
			replays.push_back(std::move(r));
		}

		// ---------------------------------------------------------------- report
		o += "{\n";
		o += "  \"scenario\": " + J.Q(sc.Name) + ",\n";
		o += "  \"ticks\": " + std::to_string(T) + ",\n";
		o += "  \"simDays\": " + J.Num(jd[T] - jd[0]) + ",\n";
		o += "  \"plainDigest\": " + J.Q(plainHash) + ",\n";
		o += "  \"probedDigest\": " + J.Q(probedHash) + ",\n";
		o += "  \"saveFileBytes\": " + std::to_string(saveFileBytes) + ",\n";
		o += "  \"saveFileMs\": " + J.Num(saveFileMs) + ",\n";
		o += "  \"peakWorkingSetBytes\": " + std::to_string(PeakWorkingSet()) + ",\n";
		o += "  \"replayByWarp\": [";
		bool first = true;
		for (const auto& [w, bk] : byWarp)
		{
			o += std::string(first ? "\n" : ",\n") + "    { \"warp\": " + std::to_string(w) + ", \"ticks\": " + std::to_string(bk.Ticks) + ", \"tickMeanMs\": " + J.Num(bk.Ms / bk.Ticks) +
				", \"gameSecPerTick\": " + J.Num(bk.SimSec / bk.Ticks) + ", \"gameSecPerWallSec\": " + J.Num(bk.SimSec / (bk.Ms / 1000)) + " }";
			first = false;
		}
		o += "\n  ],\n  \"keyframes\": [";
		first = true;
		for (const auto& c : caps)
		{
			o += std::string(first ? "\n" : ",\n") + "    { \"tick\": " + std::to_string(c.Tick) + ", \"gameSec\": " + J.Num((c.Jd - jd[0]) * 86400.0) + ", \"battles\": " + std::to_string(c.Battles) +
				", \"raw\": " + std::to_string(c.Raw) + ", \"captureMs\": " + J.Num(c.CaptureMs);
			for (const auto& [codec, v] : c.Comp)
				o += std::string(", \"") + Name(codec) + "\": { \"bytes\": " + std::to_string(v.first) + ", \"ms\": " + J.Num(v.second) + ", \"decMs\": " + J.Num(c.DecMs.at(codec)) + " }";
			o += " }";
			first = false;
		}
		o += "\n  ],\n  \"deltas\": [";
		first = true;
		for (const auto& r : pairResults)
		{
			o += std::string(first ? "\n" : ",\n") + "    { \"interval\": " + J.Num(r.P.Interval) + ", \"actualSec\": " + J.Num(r.Actual) + ", \"from\": " + std::to_string(r.P.A) +
				", \"to\": " + std::to_string(r.P.B) + ", \"battlesFrom\": " + std::to_string(r.BattlesA) + ", \"battlesTo\": " + std::to_string(r.BattlesB) +
				", \"rawFrom\": " + std::to_string(r.RawA) + ", \"rawTo\": " + std::to_string(r.RawB) + ", \"sameOffsetDiff\": " + std::to_string(r.SameOffsetDiff);
			for (const auto& [codec, v] : r.Delta)
				o += std::string(", \"") + Name(codec) + "\": { \"bytes\": " + std::to_string(v.first) + ", \"ms\": " + J.Num(v.second) + " }";
			o += ", \"deltaDecMs\": " + J.Num(r.DeltaDecMs) + ", \"deltaRoundTrip\": " + (r.DeltaRoundTrip ? "true" : "false") + " }";
			first = false;
		}
		o += "\n  ],\n  \"replays\": [";
		first = true;
		for (const auto& r : replays)
		{
			o += std::string(first ? "\n" : ",\n") + "    { \"fromTick\": " + std::to_string(r.Tick) + ", \"decompressMs\": " + J.Num(r.DecMs) + ", \"restoreMs\": " + J.Num(r.RestoreMs) +
				", \"digestAtRestore\": " + J.Q(r.AtK) + ", \"matchAtRestore\": " + (r.OkK ? "true" : "false") +
				", \"digestAfterVerify\": " + J.Q(r.AtKV) + ", \"matchAfterVerify\": " + (r.OkKV ? "true" : "false") +
				", \"digestAtEnd\": " + J.Q(r.End) + ", \"matchAtEnd\": " + (r.OkEnd ? "true" : "false") + ", \"firstTracedDivergence\": " + std::to_string(r.FirstDiff) + ", \"recaptureIdentical\": " + (r.RoundTrip ? "true" : "false") +
				", \"ticks\": " + std::to_string(r.Ticks) + ", \"tickMs\": " + J.Num(r.TickMs) + ", \"gameSecPerWallSec\": " + J.Num(r.TickMs > 0 ? r.SimSec / (r.TickMs / 1000) : 0) + " }";
			first = false;
		}
		o += "\n  ]\n}\n";
		{
			std::ofstream f(fs::path(outDir) / ("rewind-" + sc.Name + ".json"), std::ios::binary);
			f << o;
		}

		// Console summary.
		for (const auto& [w, bk] : byWarp)
			std::printf("  replay warp %d: %.3f ms/tick, %.4g game-s/tick -> %.4g game-s per wall-s\n", w, bk.Ms / bk.Ticks, bk.SimSec / bk.Ticks, bk.SimSec / (bk.Ms / 1000));
		if (!caps.empty())
		{
			const auto& c = caps.back();
			std::printf("  last keyframe: raw %zu B, capture %.0f ms", c.Raw, c.CaptureMs);
			for (const auto& [codec, v] : c.Comp) std::printf(", %s %zu B (%.0f ms, dec %.0f ms)", Name(codec), v.first, v.second, c.DecMs.at(codec));
			std::printf("\n");
		}
		for (const auto& r : pairResults)
		{
			std::printf("  delta %8.0fs (%8.0fs, battles %d->%d): raw same-offset diff %zu B", r.P.Interval, r.Actual, r.BattlesA, r.BattlesB, r.SameOffsetDiff);
			for (const auto& [codec, v] : r.Delta) std::printf(", %s %zu B", Name(codec), v.first);
			std::printf("\n");
		}
		bool ok = probedHash == plainHash;
		for (const auto& r : replays) ok = ok && r.OkK && r.OkKV && r.OkEnd;
		std::printf("  determinism: %s\n", ok ? "ALL MATCH" : "MISMATCH");
		return ok ? 0 : 3;
	}

#endif
