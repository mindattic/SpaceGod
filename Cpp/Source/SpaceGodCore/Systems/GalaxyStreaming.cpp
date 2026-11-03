// Port of Assets/UnitySpace/Core/Systems/GalaxyStreaming.cs (US-T17): GalaxyStreaming, and its part of the Galaxy
// partial class (declared in Systems/Galaxy.h).

#include "Systems/GalaxyStreaming.h"
#include "Systems/Galaxy.h"
#include "Systems/SystemSources.h"
#include "Systems/StarSystemSpec.h"
#include "Systems/DeepSpace.h"
#include "Systems/GateConstruction.h"
#include "Systems/InterstellarTraffic.h"
#include "Settings/GameSetting.h"
#include "Fleet.h"
#include "Ships.h"
#include "World.h"

namespace SpaceGod::Core
{
	// ================================================================ GalaxyStreaming

	const cs::Ref<RangeSetting>& GalaxyStreaming::ChartRadius()
	{
		static const cs::Ref<RangeSetting> setting = cs::New<RangeSetting>("streaming.chartRadiusLy", SettingGroups::Streaming,
			"Star chart radius", 20, 5, 60, 1,
			cs::String("How far about you (light-years) the galaxy's stars are charted: shown between the systems and open to a cruise drive. ") +
			"Farther charts more stars.",
			[](double v) { return cs::Fmt(v, "0") + " ly"; }, "galaxy stars chart streaming radius procedural");
		return setting;
	}

	const cs::Ref<RangeSetting>& GalaxyStreaming::SystemRadius()
	{
		static const cs::Ref<RangeSetting> setting = cs::New<RangeSetting>("streaming.systemRadiusLy", SettingGroups::Streaming,
			"System streaming radius", 3, 0, 12, 0.5,
			cs::String("How near you (light-years) a charted star is built into a full star system that runs with the rest. A system built ") +
			"this way that nobody has gone to or gated is let go again once you are far off, and comes back the same.",
			[](double v) { return cs::Fmt(v, "0.#") + " ly"; }, "galaxy systems streaming radius simulate load");
		return setting;
	}

	const cs::Ref<RangeSetting>& GalaxyStreaming::MaxCharted()
	{
		static const cs::Ref<RangeSetting> setting = cs::New<RangeSetting>("streaming.maxChartedStars", SettingGroups::Streaming,
			"Charted stars", 400, 50, 3000, 50,
			"The most stars kept on the charts at once, the nearest to you first (the galaxy's core is far denser than anywhere near Sol).",
			[](double v) { return cs::Fmt(v, "0"); }, "galaxy stars chart count budget");
		return setting;
	}

	// ================================================================ Galaxy (GalaxyStreaming.cs part)

	cs::List<cs::String> Galaxy::Streamed() const
	{
		cs::List<cs::String> r;
		for (const auto& n : streamed) r.Add(n.Value);
		return r;
	}

	void Galaxy::Explore(DVec3 atLy, double jd)
	{
		exploredAtLy = atLy;
		double chartLy = GalaxyStreaming::ChartRadius()->Value, systemLy = GalaxyStreaming::SystemRadius()->Value;
		int cap = GalaxyStreaming::MaxCharted()->IntValue();
		bool changed = false;

		// Let go first, so what is charted next has the room.
		double forgetLy = chartLy + ForgetMarginLy;
		if (NearbyStars.RemoveAll([&](const NearbyStar& n) { return charted.Contains(n.Name) && (n.PositionLy - atLy).Length() > forgetLy && !Held(n.Name); }) > 0)
		{
			// HashSet.RemoveWhere: each entry tested in enumeration order, the matches removed in that order.
			for (const auto& name : charted.ToList())
				if (!NearbyStars.Exists([&](const NearbyStar& n) { return EqualsNoCase(n.Name, name.Value); })) charted.Remove(name);
			changed = true;
		}
		for (const auto& s : Systems.ToArray())
			if (streamed.Contains(s->Name) && (s->PositionLy - atLy).Length() > cs::Math::Max(systemLy, 1.0) + ForgetMarginLy && CanRelease(s))
			{
				Release(s, jd);
				changed = true;
			}

		// The pilot finds what is undiscovered about them first (U.SPC-T456); what is still undiscovered is charted and built by nobody.
		if (DiscoverAbout(GalacticPosition::FromLy(atLy), jd, "you") > 0) changed = true;

		int count = 0;
		for (const auto& n : NearbyStars)
			if (charted.Contains(n.Name)) count++;
		for (const auto& e : SystemSources::StarsNear(atLy, chartLy, Seed))
		{
			if (IsUndiscovered(e->Name) || SimulatedAs(e) != nullptr) continue;
			if ((e->PositionLy - atLy).Length() <= systemLy)
			{
				if (Realize(e, jd) != nullptr) changed = true;
				continue;
			}
			if (ChartedStar(e->Name) || count >= cap) continue;
			NearbyStars.Add(e->ToNearbyStar());
			charted.Add(e->Name);
			count++;
			changed = true;
		}
		if (changed) ChartVersion++;
	}

	void Galaxy::ExploreIfMoved(double jd)
	{
		if (!Viewpoint.has_value()) return;
		GalacticPosition at = *Viewpoint;
		auto ly = at.Ly();
		if (cs::Double::IsNaN(exploredAtLy.X) || (ly - exploredAtLy).Length() >= ExploreStepLy) Explore(ly, jd);
	}

	bool Galaxy::ChartedStar(const cs::String& name) const
	{
		for (const auto& n : NearbyStars)
			if (EqualsNoCase(n.Name, name)) return true;
		return false;
	}

	cs::Ref<StarSystem> Galaxy::SimulatedAs(const cs::Ref<StarEntry>& e) const
	{
		for (const auto& s : Systems)
		{
			// C#: string.Equals(s.Spec?.NeighbourName, e.Name, OrdinalIgnoreCase); a null string is the empty string here.
			cs::String neighbour = s->Spec != nullptr ? s->Spec->NeighbourName : cs::String();
			if (EqualsNoCase(s->Name, e->Name)
				|| EqualsNoCase(neighbour, e->Name)
				|| (s->PositionLy - e->PositionLy).Length() < 0.01) return s;
		}
		return nullptr;
	}

	cs::Ref<StarSystem> Galaxy::Realize(const cs::String& name, double jd)
	{
		auto existing = Find(name);
		if (existing != nullptr) return existing;
		for (const auto& n : NearbyStars)
			if (EqualsNoCase(n.Name, name))
			{
				auto e = SystemSources::Entry(n.Name, n.PositionLy, Seed);
				return e == nullptr ? nullptr : Realize(e, jd);
			}
		return nullptr;
	}

	cs::Ref<StarSystem> Galaxy::Realize(const cs::Ref<StarEntry>& e, double jd)
	{
		auto existing = SimulatedAs(e);
		if (existing != nullptr) return existing;
		// Nobody builds or goes to a star nobody knows is there (U.SPC-T456).
		if (IsUndiscovered(e->Name)) return nullptr;
		auto system = SystemSources::Build(e, jd, Seed, Polities);
		if (system == nullptr) return nullptr;
		streamed.Add(system->Name);
		charted.Remove(e->Name);
		AddSystem(system, jd);
		ChartVersion++;
		return system;
	}

	bool Galaxy::CanRelease(const cs::Ref<StarSystem>& s) const
	{
		if (s == Focus || s->Gates().Count() > 0 || s->Settled() || s->World->Fleet->Ships().Count() > 0) return false;
		for (const auto& c : DeepSpace->Cruises())
			if (c->To == s) return false;
		if (Construction != nullptr)
			for (const auto& p : Construction->Projects())
				if (p->Active() && (p->To == s || p->From == s)) return false;
		return true;
	}

	void Galaxy::Release(const cs::Ref<StarSystem>& s, double jd)
	{
		Systems.Remove(s);
		streamed.Remove(s->Name);
		SystemRemoved.Invoke(s, jd);
	}

	bool Galaxy::Held(const cs::String& name) const
	{
		for (const auto& c : DeepSpace->Cruises())
			if (EqualsNoCase(c->ToName, name)) return true;
		for (const auto& s : Systems)
			for (const auto& ship : s->World->Fleet->Ships())
				if (ship->Voyage != nullptr && EqualsNoCase(ship->Voyage->StarName, name)) return true;
		return false;
	}
}
