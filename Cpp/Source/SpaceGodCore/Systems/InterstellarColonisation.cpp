// Port of Assets/UnitySpace/Core/Systems/InterstellarColonisation.cs (US-T17).

#include "Systems/InterstellarColonisation.h"
#include "Systems/Galaxy.h"
#include "Systems/GateNetwork.h"
#include "Systems/JumpGate.h"
#include "Systems/InterstellarTraffic.h"
#include "Economy/Colonisation.h"
#include "Economy/Contracts.h"
#include "Economy/Settlement.h"
#include "Ephemeris.h"
#include "World.h"

namespace SpaceGod::Core
{
	InterstellarColonisation::InterstellarColonisation(const cs::Ref<Galaxy>& galaxy)
		: galaxy(galaxy)
	{
		for (const auto& s : galaxy->Systems) Wire(s);
		galaxy->SystemAdded += [this](cs::Ref<StarSystem> s, double jd) { (void)jd; Wire(s); };
	}

	void InterstellarColonisation::Wire(const cs::Ref<StarSystem>& home)
	{
		auto c = home->World->Colonisation;
		if (c == nullptr) return;
		c->Abroad = [this, home](cs::Ref<Faction> f, cs::Ref<Settlement> from, double jd) { return SitesFrom(home, f, from, jd); };
		c->SailAbroad = [this, home](cs::Ref<ColonyShip> v, double jd) { return Sail(home, v, jd); };
	}

	bool InterstellarColonisation::Sail(const cs::Ref<StarSystem>& home, const cs::Ref<ColonyShip>& v, double jd)
	{
		if (v == nullptr || v->Hull == nullptr || v->Into == nullptr || galaxy->Traffic == nullptr) return false;
		cs::Ref<StarSystem> to = nullptr;
		for (const auto& t : galaxy->Systems)
			if (t->World->Colonisation == v->Into) to = t;
		if (to == nullptr) return false;
		auto voyage = galaxy->Traffic->Send(v->Hull, home, to, VoyagePurpose::Colonise, jd);
		if (voyage == nullptr) return false;
		voyage->StayUntilJd = cs::Double::PositiveInfinity;
		voyage->TargetBody = v->Site->Body;
		voyage->SiteName = v->Site->Name;
		return true;
	}

	cs::List<cs::Ref<ForeignSite>> InterstellarColonisation::SitesFrom(const cs::Ref<StarSystem>& home, const cs::Ref<Faction>& f, const cs::Ref<Settlement>& from, double jd)
	{
		cs::List<cs::Ref<ForeignSite>> list;
		if (galaxy->Network == nullptr || from == nullptr) return list;
		for (const auto& t : galaxy->Systems)
		{
			if (t == home || !t->OpenToSettlement()) continue;
			auto c = t->World->Colonisation;
			if (c == nullptr || c->Awaited()) continue;
			auto route = galaxy->Network->Route(home, t, f, jd);
			if (route == nullptr || route->Count() == 0) continue;
			auto depart = (*route)[0];
			auto arrive = (*route)[route->Count() - 1]->Partner;
			double outward = Contracts::TravelDays((home->Ephemeris->HelioAt(from->Body, jd) - depart->PositionKm).Length());
			double through = GateNetwork::RouteDays(route);
			for (const auto& site : c->Sites)
			{
				if (site->Score() <= 0 || Colonisation::Occupied(t->World->Economy, site->Body, jd) || c->Bound(site->Body)) continue;
				double inward = Contracts::TravelDays((t->Ephemeris->HelioAt(site->Body, jd) - arrive->PositionKm).Length());
				auto fs = cs::New<ForeignSite>();
				fs->Site = site; fs->Into = c; fs->Economy = t->World->Economy; fs->Where = t->Name;
				fs->Days = outward + through + inward;
				list.Add(fs);
			}
		}
		return list;
	}
}
