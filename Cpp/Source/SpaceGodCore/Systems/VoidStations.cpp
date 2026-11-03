// Port of Assets/UnitySpace/Core/Systems/VoidStations.cs (see docs/MIGRATION.md).

#include "Systems/VoidStations.h"

#include "Astro.h"
#include "DVec3.h"
#include "Systems/CatalogueStars.h"
#include "Systems/Galaxy.h"
#include "Systems/GateNetwork.h"
#include "Systems/StarSystemSpec.h"
#include "Systems/SystemSources.h"
#include "Systems/VoidBaseBuilder.h"

#include <mutex>

namespace SpaceGod::Core
{
	namespace
	{
		// static List<StarEntry> stations;
		cs::Ref<cs::List<cs::Ref<StarEntry>>>& stations() { static cs::Ref<cs::List<cs::Ref<StarEntry>>> s; return s; }
		// static Dictionary<string, (string a, string b)> bridges (StringComparer.OrdinalIgnoreCase).
		using BridgeMap = cs::Dictionary<cs::String, std::pair<cs::String, cs::String>, cs::OrdinalIgnoreCaseComparer>;
		BridgeMap& bridges() { static BridgeMap d; return d; }
		// static readonly object gate = new object();
		std::mutex& gate() { static std::mutex m; return m; }
	}

	cs::String VoidStations::Name() const { return SourceName; }

	int VoidStations::Order() const { return 75; }

	bool VoidStations::ChartedFromStart() const { return true; }

	cs::List<cs::Ref<StarEntry>> VoidStations::Destinations()
	{
		cs::List<cs::Ref<StarEntry>> list;
		{
			auto sol = cs::New<StarEntry>();
			sol->Name = "Sol"; sol->PositionLy = DVec3::Zero; sol->Tag = "sol";
			list.Add(sol);
		}
		cs::HashSet<cs::String, cs::OrdinalIgnoreCaseComparer> names{ "Sol", "Sol System" };
		for (const auto& e : HandBuiltSystems::Charted())
			if (names.Add(e->Name)) list.Add(e);
		for (const auto& e : CatalogueStars::Stars())
			if (names.Add(e->Name)) list.Add(e);
		return list;
	}

	const cs::List<cs::Ref<StarEntry>>& VoidStations::Stations()
	{
		std::lock_guard<std::mutex> lock(gate());
		if (stations() == nullptr) Lay();
		return *stations();
	}

	std::pair<cs::String, cs::String> VoidStations::Bridges(const cs::String& name)
	{
		Stations();
		std::pair<cs::String, cs::String> pair;
		return bridges().TryGetValue(name, pair) ? pair : std::pair<cs::String, cs::String>(cs::String(), cs::String());
	}

	void VoidStations::Lay()
	{
		auto at = Destinations();
		int n = at.Count();
		// Prim's tree from Sol; ties go to the earlier system, so it is the same tree every run.
		auto inTree = cs::NewArray<bool>(n);
		auto best = cs::NewArray<double>(n);
		auto from = cs::NewArray<int>(n);
		for (int i = 0; i < n; i++)
		{
			best[i] = cs::Double::PositiveInfinity;
			from[i] = -1;
		}
		best[0] = 0;
		struct Laid { DVec3 ly; cs::String a, b; };
		cs::List<Laid> laid;
		for (int step = 0; step < n; step++)
		{
			int u = -1;
			for (int i = 0; i < n; i++)
				if (!inTree[i] && (u < 0 || best[i] < best[u])) u = i;
			inTree[u] = true;
			if (from[u] >= 0 && best[u] > MaxLegLy)
				for (const auto& p : GateNetwork::RelayPositions(at[from[u]]->PositionLy, at[u]->PositionLy, MaxLegLy))
					laid.Add(Laid{ p, at[from[u]]->Name, at[u]->Name });
			for (int v = 0; v < n; v++)
			{
				if (inTree[v]) continue;
				double d = (at[v]->PositionLy - at[u]->PositionLy).Length();
				if (d < best[v])
				{
					best[v] = d;
					from[v] = u;
				}
			}
		}
		laid.Sort([](const Laid& x, const Laid& y)
		{
			int c = cs::CompareTo(x.ly.Length(), y.ly.Length());
			return c != 0 ? c : cs::CompareTo(x.ly.X, y.ly.X);
		});
		stations() = cs::New<cs::List<cs::Ref<StarEntry>>>();
		bridges().Clear();
		for (int k = 0; k < laid.Count(); k++)
		{
			cs::String name = cs::Cat(Prefix, " ", k + 1);
			bridges()[name] = std::pair<cs::String, cs::String>(laid[k].a, laid[k].b);
			auto e = cs::New<StarEntry>();
			e->Name = name; e->PositionLy = laid[k].ly; e->Spectral = "Void station"; e->TeffK = VoidBaseBuilder::TinySunTeffK;
			e->MassSolar = 0; e->RadiusSolar = 0; e->LuminositySolar = VoidBaseBuilder::TinySuns * 1e-6; e->Source = SourceName; e->Tag = "void station";
			stations()->Add(e);
		}
	}

	bool VoidStations::Claims(DVec3 ly) const
	{
		if (ly.Length() > CatalogueStars::ChartedToLy + ClearLy) return false;
		for (const auto& s : Stations())
			if ((s->PositionLy - ly).Length() < ClearLy) return true;
		return false;
	}

	cs::List<cs::Ref<StarEntry>> VoidStations::StarsNear(DVec3 centreLy, double radiusLy, int /*seed*/)
	{
		cs::List<cs::Ref<StarEntry>> result;
		for (const auto& s : Stations())
			if ((s->PositionLy - centreLy).Length() <= radiusLy)
			{
				auto e = cs::New<StarEntry>();
				e->Name = s->Name; e->PositionLy = s->PositionLy; e->Spectral = s->Spectral; e->TeffK = s->TeffK; e->MassSolar = s->MassSolar;
				e->RadiusSolar = s->RadiusSolar; e->LuminositySolar = s->LuminositySolar; e->Source = s->Source; e->Tag = s->Tag;
				result.Add(e);
			}
		return result;
	}

	cs::Ref<SystemSpec> VoidStations::Define(const cs::Ref<StarEntry>& star, int /*seed*/)
	{
		cs::Ref<StarEntry> row = nullptr;
		for (const auto& s : Stations())
			if (cs::Strings::EqualsOrdinalIgnoreCase(s->Name, star->Name)) row = s;
		if (row == nullptr) return nullptr;
		auto [a, b] = Bridges(row->Name);
		cs::String summary = cs::Cat("A void station in open space between ", a, " and ", b, ", ", cs::Fmt(row->PositionLy.Length(), "0.0"), " ",
			"light-years from the Sun: no star, no planet, a relay so the way between them has a stop. Fiction.");
		double ra = 0, dec = 0;
		Astro::RaDecOf(row->PositionLy, ra, dec);
		auto starSpec = cs::New<StarSpec>();
		starSpec->Name = row->Name; starSpec->Spectral = row->Spectral; starSpec->MassSolar = row->MassSolar; starSpec->RadiusSolar = row->RadiusSolar;
		starSpec->LuminositySolar = row->LuminositySolar; starSpec->TeffK = row->TeffK; starSpec->Summary = summary;
		auto spec = cs::New<SystemSpec>();
		spec->Name = row->Name; spec->NeighbourName = row->Name; spec->RaDeg = ra; spec->DecDeg = dec; spec->DistanceLy = row->PositionLy.Length();
		spec->Primary = StellarNode::Single(starSpec);
		spec->Summary = summary;
		return spec;
	}

	cs::Ref<StarSystem> VoidStations::Build(const cs::Ref<StarEntry>& star, const cs::Ref<SystemSpec>& /*spec*/, double jd, const cs::Ref<Polities>& polities)
	{
		return StarSystem::CreateVoidBase(star->Name, star->PositionLy, jd, polities);
	}
}
