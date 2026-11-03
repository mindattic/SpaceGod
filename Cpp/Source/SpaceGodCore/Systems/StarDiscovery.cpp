// Port of Assets/UnitySpace/Core/Systems/StarDiscovery.cs (US-T17): DiscoveryState, StarDiscovery, StarDiscoveredEvent,
// and its part of the Galaxy partial class (declared in Systems/Galaxy.h).

#include "Systems/StarDiscovery.h"
#include "Systems/Galaxy.h"
#include "Systems/SystemSources.h"
#include "Systems/CatalogueStars.h"
#include "Systems/VoidStations.h"
#include "Systems/DeepSpace.h"
#include "Settings/GameSetting.h"
#include "Ships.h"

namespace SpaceGod::Core
{
	const char* ToString(DiscoveryState v)
	{
		switch (v)
		{
		case DiscoveryState::Discovered: return "Discovered";
		case DiscoveryState::Undiscovered: return "Undiscovered";
		}
		return "";
	}

	// ================================================================ StarDiscovery

	const cs::Ref<RangeSetting>& StarDiscovery::Range()
	{
		static const cs::Ref<RangeSetting> setting = cs::New<RangeSetting>("simulation.discoveryRangeLy", SettingGroups::Simulation,
			"Discovery range", VoidStations::MaxLegLy, 1, 20, 0.5,
			cs::String("How near (light-years) you, or a ship crossing open space, must come to an undiscovered star or void station past ") +
			"the known neighbourhood to discover it and put it on the charts. Below 7 ly some stars can only be found by passing them.",
			[](double v) { return cs::Fmt(v, "0.#") + " ly"; }, "discovery discover undiscovered explore range stars charts");
		return setting;
	}

	cs::List<StarDiscovery::Lookout>& StarDiscovery::Lookouts()
	{
		static cs::List<Lookout> lookouts{ Lookout(&Pilot), Lookout(&Cruises) };
		return lookouts;
	}

	cs::List<StarDiscovery::Sighting> StarDiscovery::Pilot(const cs::Ref<Galaxy>& galaxy, double jd)
	{
		(void)jd;
		cs::List<Sighting> r;
		if (galaxy->Viewpoint.has_value()) r.Add({ *galaxy->Viewpoint, "you" });
		return r;
	}

	cs::List<StarDiscovery::Sighting> StarDiscovery::Cruises(const cs::Ref<Galaxy>& galaxy, double jd)
	{
		cs::List<Sighting> r;
		for (const auto& c : galaxy->DeepSpace->Cruises())
			if (c->Leader() != nullptr) r.Add({ c->PositionAt(jd), "the " + c->Leader()->Name });
		return r;
	}

	// ================================================================ Galaxy (StarDiscovery.cs part)

	bool Galaxy::IsUndiscovered(const cs::String& name) const
	{
		// C#: name != null && ... (a null string is the empty string here, which is never a star's name).
		return undiscoveredNames.Contains(name);
	}

	void Galaxy::SetUpNeighbourhood()
	{
		for (const auto& e : SystemSources::StarsNear(DVec3::Zero, CatalogueStars::ChartedToLy + CatalogueStars::ClearLy, Seed, [](const cs::Ref<SystemSource>& s) { return s->ChartedFromStart(); }))
		{
			if (SimulatedAs(e) != nullptr || ChartedStar(e->Name) || IsUndiscovered(e->Name)) continue;
			if (e->PositionLy.Length() <= StarDiscovery::KnownToLy) NearbyStars.Add(e->ToNearbyStar());
			else
			{
				undiscovered.Add(e->ToNearbyStar());
				undiscoveredNames.Add(e->Name);
			}
		}
		ChartVersion++;
	}

	bool Galaxy::Discover(const cs::String& name, double jd, const cs::String& by)
	{
		if (!IsUndiscovered(name)) return false;
		int i = undiscovered.FindIndex([&](const NearbyStar& n) { return EqualsNoCase(n.Name, name); });
		auto star = undiscovered[i];
		undiscovered.RemoveAt(i);
		undiscoveredNames.Remove(star.Name);
		if (Find(star.Name) == nullptr && !ChartedStar(star.Name)) NearbyStars.Add(star);
		auto e = cs::New<StarDiscoveredEvent>();
		e->Name = star.Name; e->PositionLy = star.PositionLy; e->By = by; e->Jd = jd;
		Discoveries.Add(e);
		DeepSpace->Log(jd, cs::Cat(star.Name, " is discovered", (!by.empty() ? " by " + by : cs::String()), ", ") +
			cs::Cat(cs::Fmt(star.PositionLy.Length(), "0.0"), " ly from Sol"), star.Origin());
		ChartVersion++;
		StarDiscovered.Invoke(e);
		return true;
	}

	int Galaxy::DiscoverAbout(GalacticPosition at, double jd, const cs::String& by)
	{
		if (undiscovered.Count() == 0) return 0;
		double range = StarDiscovery::Range()->Value;
		cs::List<cs::String> near;
		for (const auto& n : undiscovered)
			if (n.Origin().DistanceLy(at) <= range) near.Add(n.Name);
		int found = 0;
		for (const auto& name : near)
			if (Discover(name, jd, by)) found++;
		return found;
	}

	void Galaxy::LookForStars(double jd)
	{
		if (undiscovered.Count() == 0) return;
		if (!cs::Double::IsNaN(lookedJd) && jd - lookedJd < CoarseStepDays && jd >= lookedJd) return;
		lookedJd = jd;
		for (const auto& lookout : StarDiscovery::Lookouts().ToArray())
			for (const auto& [at, by] : lookout(Self(), jd))
			{
				DiscoverAbout(at, jd, by);
				if (undiscovered.Count() == 0) return;
			}
	}

	bool Galaxy::Unhide(const cs::String& name)
	{
		if (!undiscoveredNames.Remove(name)) return false;
		undiscovered.RemoveAll([&](const NearbyStar& n) { return EqualsNoCase(n.Name, name); });
		ChartVersion++;
		return true;
	}
}
