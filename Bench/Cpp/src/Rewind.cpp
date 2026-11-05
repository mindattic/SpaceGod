// Bench.Rewind (US-T99): keyframe capture/restore over the game's save format, and the codecs the probe measures.
// See Rewind.h. Built only with /DBENCH_REWIND.

#if defined(BENCH_REWIND)

#include "Rewind.h"
#include "Simulation.h"

#include "SimClock.h"
#include "Ships.h"
#include "Combat/Battle.h"
#include "Systems/Galaxy.h"
#include "Persistence/ObjectGraph.h"
#include "Persistence/SaveGame.h"

#include <cstring>
#include <stdexcept>

#if defined(BENCH_WITH_OODLE)
#include "oodle2.h"
#endif

// The wrapper root, registered like any Core class (a name no C# type has: these keyframes never leave the Bench).
namespace SpaceGod::Core
{
	SG_SERIALIZE(::Bench::RewindKeyframe, "Bench.RewindKeyframe",
		SG_F(Data), SG_F(Followed), SG_F(CourseShip), SG_F(CourseDestination), SG_F(Skirmish), SG_F(BattleTimeFor),
		SG_F(BattlesStarted), SG_F(CourseArrived), SG_F(CourseEnded))
}

namespace Bench::Rewind
{
	using namespace SpaceGod::Core;

	static cs::Ref<SaveData> DataOf(Simulation& sim)
	{
		auto data = cs::New<SaveData>();
		data->Clock = sim.Clock;
		data->Galaxy = sim.Galaxy;
		data->Session->ActiveSystem = sim.ActiveSystem->Name;
		return data;
	}

	Bytes Capture(Simulation& sim)
	{
		auto k = cs::New<RewindKeyframe>();
		k->Data = DataOf(sim);
		k->Followed = sim.Followed;
		k->CourseShip = sim.CourseShip;
		k->CourseDestination = sim.CourseDestination;
		k->Skirmish = sim.Skirmish;
		k->BattleTimeFor = sim.BattleTimeFor();
		k->BattlesStarted = sim.BattlesStarted;
		k->CourseArrived = sim.CourseArrived;
		k->CourseEnded = sim.CourseEnded;
		return ObjectGraph::ToBytes(k).Raw();
	}

	cs::Ref<Simulation> Restore(const Bytes& raw)
	{
		auto k = ObjectGraph::FromBytes(cs::List<uint8_t>(raw)).As<RewindKeyframe>();
		if (k == nullptr || k->Data == nullptr) throw std::runtime_error("keyframe holds no game");
		auto sim = Simulation::Restore(k->Data->Clock, k->Data->Galaxy, k->Data->Session->ActiveSystem);
		sim->Followed = k->Followed;
		sim->CourseShip = k->CourseShip;
		sim->CourseDestination = k->CourseDestination;
		sim->Skirmish = k->Skirmish;
		sim->SetBattleTimeFor(k->BattleTimeFor);
		sim->BattlesStarted = k->BattlesStarted;
		sim->CourseArrived = k->CourseArrived;
		sim->CourseEnded = k->CourseEnded;
		return sim;
	}

	Bytes SaveFile(Simulation& sim)
	{
		return SaveGame::ToBytes(DataOf(sim), "rewind-probe", "", cs::DateTime()).Raw();
	}

	const char* Name(Codec c)
	{
		switch (c)
		{
		case Codec::Deflate1: return "deflate-1";
		case Codec::KrakenFast: return "kraken-fast";
		case Codec::KrakenNormal: return "kraken-normal";
		case Codec::MermaidFast: return "mermaid-fast";
		}
		return "?";
	}

	bool Available(Codec c)
	{
#if defined(BENCH_WITH_OODLE)
		(void)c;
		return true;
#else
		return c == Codec::Deflate1;
#endif
	}

#if defined(BENCH_WITH_OODLE)
	// The engine's SDK build ships with no plugins installed (UE installs its own): give it the SDK's default
	// allocator and logging. No job system: every encode and decode here is single-threaded.
	static void OodleInit()
	{
		static const bool done = [] {
			OodleCore_Plugins_SetAllocators(OodleCore_Plugin_MallocAligned_Default, OodleCore_Plugin_Free_Default);
			OodleCore_Plugins_SetPrintf(OodleCore_Plugin_Printf_Default);
			return true;
		}();
		(void)done;
	}

	static size_t DictPad(size_t dictSize) { return (OODLELZ_BLOCK_LEN - dictSize % OODLELZ_BLOCK_LEN) % OODLELZ_BLOCK_LEN; }

	static void OodleOf(Codec c, OodleLZ_Compressor& comp, OodleLZ_CompressionLevel& level)
	{
		OodleInit();
		switch (c)
		{
		case Codec::KrakenFast: comp = OodleLZ_Compressor_Kraken; level = OodleLZ_CompressionLevel_Fast; return;
		case Codec::KrakenNormal: comp = OodleLZ_Compressor_Kraken; level = OodleLZ_CompressionLevel_Normal; return;
		case Codec::MermaidFast: comp = OodleLZ_Compressor_Mermaid; level = OodleLZ_CompressionLevel_Fast; return;
		default: throw std::runtime_error("not an Oodle codec");
		}
	}
#endif

	Bytes Compress(Codec c, const Bytes& cur, const Bytes* dict)
	{
		if (c == Codec::Deflate1)
		{
			if (dict != nullptr) throw std::runtime_error("deflate takes no 4 MB dictionary (32 KB window)");
			return SaveGame::Deflate(cur);
		}
#if defined(BENCH_WITH_OODLE)
		OodleLZ_Compressor comp;
		OodleLZ_CompressionLevel level;
		OodleOf(c, comp, level);
		// With a dictionary the encoder needs the dictionary and the new data in one buffer, dictionary first, and the
		// distance back to its start a multiple of OODLELZ_BLOCK_LEN (zero padding in front of the dictionary).
		Bytes joined;
		const uint8_t* src = cur.data();
		const uint8_t* base = nullptr;
		if (dict != nullptr)
		{
			size_t pad = DictPad(dict->size());
			joined.assign(pad + dict->size() + cur.size(), 0);
			std::memcpy(joined.data() + pad, dict->data(), dict->size());
			std::memcpy(joined.data() + pad + dict->size(), cur.data(), cur.size());
			base = joined.data();
			src = joined.data() + pad + dict->size();
		}
		Bytes out(static_cast<size_t>(OodleLZ_GetCompressedBufferSizeNeeded(comp, static_cast<OO_SINTa>(cur.size()))));
		OO_SINTa n = OodleLZ_Compress(comp, src, static_cast<OO_SINTa>(cur.size()), out.data(), level, nullptr, base);
		if (n <= 0) throw std::runtime_error(std::string("OodleLZ_Compress failed: ") + Name(c) + ", " + std::to_string(cur.size()) + " bytes" + (dict ? " with a dictionary" : ""));
		out.resize(static_cast<size_t>(n));
		return out;
#else
		(void)cur; (void)dict;
		throw std::runtime_error("built without Oodle");
#endif
	}

	Bytes Decompress(Codec c, const Bytes& comp, size_t rawLen, const Bytes* dict)
	{
		if (c == Codec::Deflate1) return SaveGame::Inflate(comp.data(), comp.size());
#if defined(BENCH_WITH_OODLE)
		OodleInit();
		size_t pre = dict ? DictPad(dict->size()) + dict->size() : 0;
		Bytes joined(pre + rawLen, 0);
		if (dict) std::memcpy(joined.data() + (pre - dict->size()), dict->data(), dict->size());
		uint8_t* dst = joined.data() + pre;
		OO_SINTa n = OodleLZ_Decompress(comp.data(), static_cast<OO_SINTa>(comp.size()), dst, static_cast<OO_SINTa>(rawLen),
			OodleLZ_FuzzSafe_Yes, OodleLZ_CheckCRC_No, OodleLZ_Verbosity_None,
			dict ? joined.data() : nullptr, dict ? static_cast<OO_SINTa>(joined.size()) : 0);
		// With a dictionary the count includes the inset before rawBuf.
		if (n != static_cast<OO_SINTa>(pre + rawLen)) throw std::runtime_error("OodleLZ_Decompress failed");
		if (!dict) return joined;
		return Bytes(joined.begin() + static_cast<std::ptrdiff_t>(pre), joined.end());
#else
		(void)comp; (void)rawLen; (void)dict;
		throw std::runtime_error("built without Oodle");
#endif
	}

	// ---------------------------------------------------------------- structural graph diff (diagnostics)
	namespace
	{
		using Graph::Node;
		using Graph::Value;

		std::string Show(const Value& v)
		{
			char b[64];
			switch (v.ValueTag)
			{
			case Value::Tag::Null: return "null";
			case Value::Tag::Ref: std::snprintf(b, sizeof b, "#%d", v.Id); return b;
			case Value::Tag::Prim:
				if (v.Code == Graph::TypeCode::Double) { std::snprintf(b, sizeof b, "%.17g", v.Prim.D); return b; }
				std::snprintf(b, sizeof b, "%lld", static_cast<long long>(v.Prim.I)); return b;
			default:
			{
				std::string s = "{";
				for (const auto& i : v.Items) s += Show(i) + ",";
				return s + "}";
			}
			}
		}

		bool Same(const Value& a, const Value& b)
		{
			if (a.ValueTag != b.ValueTag) return false;
			switch (a.ValueTag)
			{
			case Value::Tag::Null: return true;
			case Value::Tag::Ref: return a.Id == b.Id;
			case Value::Tag::Prim: return a.Prim.U == b.Prim.U;
			default:
				if (a.Items.size() != b.Items.size()) return false;
				for (size_t i = 0; i < a.Items.size(); i++) if (!Same(a.Items[i], b.Items[i])) return false;
				return true;
			}
		}

		struct Parsed
		{
			Graph::Reader R;
			int Count = 0;
			explicit Parsed(const Bytes& raw)
			{
				Graph::ByteReader br(raw.data(), raw.size());
				br.I32();
				br.I32();
				const Graph::ClassInfo* cls = nullptr;
				R.ReadAll(br, cls);
				for (int id = 1;; id++)
				{
					try { R.NodeOf(id); Count = id; }
					catch (...) { break; }
				}
			}
		};

		std::string TypeName(const Node& n) { return n.Type ? std::string(n.Type->FullName) : std::string("?"); }
	}

	std::string GraphDiff(const Bytes& a, const Bytes& b, int max)
	{
		Parsed A(a), B(b);
		std::string out;
		if (A.Count != B.Count) out += "  entries " + std::to_string(A.Count) + " vs " + std::to_string(B.Count) + "\n";
		// Who refers to whom (first referrer), to print a path to a differing entry.
		std::vector<std::pair<int, std::string>> parent(static_cast<size_t>(A.Count) + 1, { 0, std::string() });
		for (int id = 1; id <= A.Count; id++)
		{
			const Node& n = A.R.NodeOf(id);
			auto note = [&](const Value& v, const std::string& via) {
				if (v.ValueTag == Value::Tag::Ref && v.Id > 0 && v.Id <= A.Count && parent[static_cast<size_t>(v.Id)].first == 0) parent[static_cast<size_t>(v.Id)] = { id, via };
			};
			for (size_t f = 0; f < n.Fields.size(); f++) note(n.Fields[f], n.Type && f < n.Type->Fields.size() ? std::string(n.Type->Fields[f].first) : "?");
			for (size_t i = 0; i < n.Items.size(); i++) note(n.Items[i], "[" + std::to_string(i) + "]");
			for (size_t i = 0; i < n.Keys.size(); i++) note(n.Keys[i], "{key " + std::to_string(i) + "}");
		}
		auto path = [&](int id) {
			std::string p;
			for (int depth = 0; id > 0 && depth < 8; depth++)
			{
				const auto& [up, via] = parent[static_cast<size_t>(id)];
				p = TypeName(A.R.NodeOf(id)) + (p.empty() ? "" : " <- " + p);
				if (up == 0) break;
				p = "." + via + " " + p;
				id = up;
			}
			return p;
		};
		int shown = 0, differing = 0;
		for (int id = 1; id <= std::min(A.Count, B.Count); id++)
		{
			const Node& x = A.R.NodeOf(id);
			const Node& y = B.R.NodeOf(id);
			std::string what;
			if (x.Entry != y.Entry || x.Str != y.Str || TypeName(x) != TypeName(y)) what = "kind/type/string";
			else if (x.Fields.size() != y.Fields.size() || x.Items.size() != y.Items.size() || x.Keys.size() != y.Keys.size())
				what = "sizes fields " + std::to_string(x.Fields.size()) + "/" + std::to_string(y.Fields.size()) + " items " + std::to_string(x.Items.size()) + "/" + std::to_string(y.Items.size());
			else
			{
				for (size_t f = 0; f < x.Fields.size() && what.empty(); f++)
					if (!Same(x.Fields[f], y.Fields[f]))
						what = "field " + (x.Type && f < x.Type->Fields.size() ? std::string(x.Type->Fields[f].first) : "?") + ": " + Show(x.Fields[f]) + " vs " + Show(y.Fields[f]);
				for (size_t i = 0; i < x.Items.size() && what.empty(); i++)
					if (!Same(x.Items[i], y.Items[i])) what = "item " + std::to_string(i) + ": " + Show(x.Items[i]) + " vs " + Show(y.Items[i]);
				for (size_t i = 0; i < x.Keys.size() && what.empty(); i++)
					if (!Same(x.Keys[i], y.Keys[i])) what = "key " + std::to_string(i) + ": " + Show(x.Keys[i]) + " vs " + Show(y.Keys[i]);
			}
			if (what.empty()) continue;
			differing++;
			if (shown++ < max) out += "  #" + std::to_string(id) + " " + what + "\n      at " + path(id) + "\n";
		}
		out += "  " + std::to_string(differing) + " entries differ\n";
		return out;
	}

	size_t SameOffsetDiffBytes(const Bytes& a, const Bytes& b)
	{
		size_t n = std::min(a.size(), b.size()), d = 0;
		for (size_t i = 0; i < n; i++) d += a[i] != b[i];
		return d + (std::max(a.size(), b.size()) - n);
	}
}

#endif
