// Port of Unity.SpaceGod Core/RenderLod.cs (see docs/MIGRATION.md).

#include "RenderLod.h"
#include "Fleet.h"
#include "ShipLod.h"
#include "Ships.h"
#include "Settings/GameSetting.h"
#include "Persistence/ObjectGraph.h"

#include <tuple>

namespace SpaceGod::Core
{
	const char* ToString(LodTier v)
	{
		switch (v)
		{
		case LodTier::Full: return "Full";
		case LodTier::Simplified: return "Simplified";
		case LodTier::Icon: return "Icon";
		case LodTier::Aggregate: return "Aggregate";
		case LodTier::Culled: return "Culled";
		}
		return "";
	}

	const char* ToString(LodFar v)
	{
		switch (v)
		{
		case LodFar::None: return "None";
		case LodFar::Impostor: return "Impostor";
		case LodFar::Icon: return "Icon";
		}
		return "";
	}

	// ---------------------------------------------------------------- LodClass

	LodClass::LodClass(const cs::String& key, const cs::String& name, cs::Func<double> modelBelowPx, LodFar far, bool simplifies)
	{
		if (cs::Strings::IsNullOrWhiteSpace(key)) throw cs::ArgumentException("A render class needs a key. (Parameter 'key')");
		Key = key;
		Name = cs::Strings::IsNullOrWhiteSpace(name) ? key : name;
		this->modelBelowPx = modelBelowPx ? modelBelowPx : cs::Func<double>([] { return 0.0; });
		Far = far;
		Simplifies = simplifies;
	}

	// ---------------------------------------------------------------- LodCluster

	LodCluster::LodCluster(int index, LodMark m)
	{
		Representative = index;
		X = m.X;
		Y = m.Y;
		Members.Add(index);
	}

	// ---------------------------------------------------------------- RenderLod

	struct RenderLod::State
	{
		cs::Ref<RangeSetting> SimplifyBelowPx, CullBelowPx, AggregatePx;
		cs::List<cs::Ref<LodClass>> classes;
		cs::Dictionary<cs::String, cs::Ref<LodClass>> byKey;
		std::mutex gate; // C#: lock (classes)
		cs::Ref<LodClass> Ship, Body, Gate, Structure, DockingRing, Platform, Fighter, Pod, Wreck;
	};

	RenderLod::State& RenderLod::S()
	{
		// C#'s static field initialisers, in textual order.
		static State* s = []
		{
			auto* st = new State();
			st->SimplifyBelowPx = cs::New<RangeSetting>("lod.simplifyPx", SettingGroups::RenderBudget,
				"Full model to simplified", 48, 8, 400, 1,
				"A ship or structure shorter than this on screen drops its turrets, damage, glows, shields and exhaust and draws its hull alone. Higher is faster with many ships in view.",
				cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0"), " px"); }), "lod simplified detail model hardpoints turret performance");
			st->CullBelowPx = cs::New<RangeSetting>("lod.cullPx", SettingGroups::RenderBudget,
				"Cull below", 1, 0, 4, 0.25,
				"Anything smaller than this on screen that has no icon (fighters, pods, wrecks, structures, rings, gates and platforms) is not drawn at all. Ships and worlds keep their icons and fold into marks instead.",
				cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0.##"), " px"); }), "lod cull pixel hide small far performance");
			st->AggregatePx = cs::New<RangeSetting>("lod.aggregatePx", SettingGroups::RenderBudget,
				"Merge icons closer than", 14, 0, 64, 1,
				"Far-off ships of one side whose icons come closer than this on screen are drawn as one mark with how many it stands for: a fleet, or a crowd of traffic round a port. 0 draws every icon.",
				cs::Func<cs::String, double>([](double v) { return v <= 0 ? cs::String("Off") : cs::Cat(cs::Fmt(v, "0"), " px"); }), "lod aggregate merge cluster fleet icon mark count performance");

			auto reg = [st](cs::Ref<LodClass> c) { Add(*st, c); return c; };
			st->Ship = reg(cs::New<LodClass>("ship", "Ship", [] { return ShipLod::IconBelowPx()->Value; }, LodFar::Icon, true));
			st->Body = reg(cs::New<LodClass>("body", "World", [] { return 0.0; }, LodFar::Icon, false));
			st->Gate = reg(cs::New<LodClass>("gate", "Jump gate", [] { return 0.0; }, LodFar::None, false));
			st->Structure = reg(cs::New<LodClass>("structure", "Structure", [] { return 4.0; }, LodFar::None, false));
			st->DockingRing = reg(cs::New<LodClass>("ring", "Docking ring", [] { return 3.0; }, LodFar::None, false));
			st->Platform = reg(cs::New<LodClass>("platform", "Defence platform", [] { return 0.0; }, LodFar::None, false));
			st->Fighter = reg(cs::New<LodClass>("fighter", "Fighter", [] { return 3.0; }, LodFar::Impostor, false));
			st->Pod = reg(cs::New<LodClass>("pod", "Escape pod", [] { return 3.0; }, LodFar::Impostor, false));
			st->Wreck = reg(cs::New<LodClass>("wreck", "Wreck", [] { return 0.0; }, LodFar::None, false));
			return st;
		}();
		return *s;
	}

	const cs::Ref<RangeSetting>& RenderLod::SimplifyBelowPx() { return S().SimplifyBelowPx; }
	const cs::Ref<RangeSetting>& RenderLod::CullBelowPx() { return S().CullBelowPx; }
	const cs::Ref<RangeSetting>& RenderLod::AggregatePx() { return S().AggregatePx; }
	const cs::Ref<LodClass>& RenderLod::Ship() { return S().Ship; }
	const cs::Ref<LodClass>& RenderLod::Body() { return S().Body; }
	const cs::Ref<LodClass>& RenderLod::Gate() { return S().Gate; }
	const cs::Ref<LodClass>& RenderLod::Structure() { return S().Structure; }
	const cs::Ref<LodClass>& RenderLod::DockingRing() { return S().DockingRing; }
	const cs::Ref<LodClass>& RenderLod::Platform() { return S().Platform; }
	const cs::Ref<LodClass>& RenderLod::Fighter() { return S().Fighter; }
	const cs::Ref<LodClass>& RenderLod::Pod() { return S().Pod; }
	const cs::Ref<LodClass>& RenderLod::Wreck() { return S().Wreck; }

	const cs::List<cs::Ref<LodClass>>& RenderLod::Classes() { return S().classes; }

	void RenderLod::Add(State& s, const cs::Ref<LodClass>& c)
	{
		if (c == nullptr) throw cs::ArgumentNullException("c");
		std::lock_guard<std::mutex> hold(s.gate);
		if (s.byKey.ContainsKey(c->Key)) throw cs::ArgumentException(cs::Cat("A render class is already registered as ", c->Key, " (Parameter 'c')"));
		s.byKey[c->Key] = c;
		s.classes.Add(c);
	}

	cs::Ref<LodClass> RenderLod::Register(cs::Ref<LodClass> c)
	{
		Add(S(), c);
		return c;
	}

	cs::Ref<LodClass> RenderLod::Find(const cs::String& key)
	{
		auto& s = S();
		std::lock_guard<std::mutex> hold(s.gate);
		cs::Ref<LodClass> c;
		return s.byKey.TryGetValue(key, c) ? c : nullptr; // C#: key != null && ... (a C++ string is never null)
	}

	LodTier RenderLod::Tier(const cs::Ref<LodClass>& c, double px)
	{
		return Tier(px, c->ModelBelowPx(), c->Far, c->Simplifies, SimplifyBelowPx()->Value, CullBelowPx()->Value);
	}

	LodTier RenderLod::Tier(double px, double modelBelowPx, LodFar far, bool simplifies, double simplifyBelowPx, double cullBelowPx)
	{
		if (px >= cs::Math::Max(modelBelowPx, cullBelowPx) && px > 0)
			return simplifies && px < simplifyBelowPx ? LodTier::Simplified : LodTier::Full;
		if (far == LodFar::Icon) return LodTier::Icon;
		if (far == LodFar::Impostor && px >= cullBelowPx) return LodTier::Icon;
		return LodTier::Culled;
	}

	namespace
	{
		// double.CompareTo / int.CompareTo.
		int CompareTo(double a, double b)
		{
			if (a < b) return -1;
			if (a > b) return 1;
			if (a == b) return 0;
			return cs::Double::IsNaN(a) ? (cs::Double::IsNaN(b) ? 0 : -1) : 1;
		}
		int CompareTo(int a, int b) { return a < b ? -1 : (a > b ? 1 : 0); }

		using CellKey = std::tuple<long long, long long, int>;
		struct CellHash
		{
			size_t operator()(const CellKey& k) const
			{
				size_t h = std::hash<long long>{}(std::get<0>(k));
				h = cs::HashCombine(h, std::hash<long long>{}(std::get<1>(k)));
				return cs::HashCombine(h, std::hash<int>{}(std::get<2>(k)));
			}
		};
	}

	cs::List<cs::Ref<LodCluster>> RenderLod::Aggregate(const cs::List<LodMark>& marks, double radiusPx)
	{
		cs::List<cs::Ref<LodCluster>> result;
		if (marks.Count() == 0) return result;
		auto order = cs::NewArray<int>(marks.Count());
		for (int i = 0; i < order.Count(); i++) order[i] = i;
		// Pinned first, then heaviest first; ties by index so the result is stable frame to frame.
		order.Sort([&](int a, int b)
		{
			const auto& ma = marks[a];
			const auto& mb = marks[b];
			if (ma.Pinned != mb.Pinned) return ma.Pinned ? -1 : 1;
			int w = CompareTo(mb.Weight, ma.Weight);
			return w != 0 ? w : CompareTo(a, b);
		});

		bool merge = radiusPx > 0;
		double cell = merge ? radiusPx : 1;
		double r2 = radiusPx * radiusPx;
		cs::Dictionary<CellKey, cs::Ref<cs::List<cs::Ref<LodCluster>>>, CellHash> grid;
		for (int i : order)
		{
			const auto& m = marks[i];
			if (m.Pinned || !merge)
			{
				auto own = cs::New<LodCluster>(i, m);
				result.Add(own);
				continue;
			}
			long long cx = cs::ToLong(cs::Math::Floor(m.X / cell)), cy = cs::ToLong(cs::Math::Floor(m.Y / cell));
			cs::Ref<LodCluster> into;
			double best = cs::Double::MaxValue;
			for (long long dx = -1; dx <= 1; dx++)
				for (long long dy = -1; dy <= 1; dy++)
				{
					cs::Ref<cs::List<cs::Ref<LodCluster>>> near;
					if (!grid.TryGetValue(CellKey(cx + dx, cy + dy, m.Group), near)) continue;
					for (const auto& c : *near)
					{
						double ex = c->X - m.X, ey = c->Y - m.Y, d2 = ex * ex + ey * ey;
						if (d2 <= r2 && d2 < best)
						{
							best = d2;
							into = c;
						}
					}
				}
			if (into != nullptr)
			{
				into->Members.Add(i);
				continue;
			}
			auto made = cs::New<LodCluster>(i, m);
			result.Add(made);
			auto key = CellKey(cx, cy, m.Group);
			cs::Ref<cs::List<cs::Ref<LodCluster>>> list;
			if (!grid.TryGetValue(key, list)) grid[key] = list = cs::New<cs::List<cs::Ref<LodCluster>>>();
			list->Add(made);
		}
		return result;
	}

	int RenderLod::LiveShips(const cs::Ref<Fleet>& fleet)
	{
		if (fleet == nullptr) return 0;
		int n = 0;
		for (const auto& s : fleet->Ships())
			if (!s->Destroyed) n++;
		return n;
	}

	// Save statics (docs/PERSISTENCE.md, "Statics"; US-T41): C#'s ObjectGraph walks RenderLod's private byKey table like
	// any other static (its classes list is RenderLod::Classes(), declared with the rest in StaticTables.cpp). The table is
	// file-local here (State), and S() private: an explicit instantiation may name it ([temp.explicit]), and the friend
	// function it defines hands it on, so RenderLod.h stays as ported.
	namespace
	{
		template <auto Member, class Tag> struct Expose { friend constexpr auto Address(Tag) { return Member; } };
		struct RenderLodState {};
		constexpr auto Address(RenderLodState);
		template struct Expose<&RenderLod::S, RenderLodState>;
	}
	SG_STATIC_OF("Unity.SpaceGod.Core.RenderLod", "byKey", Address(RenderLodState{})().byKey)
}
