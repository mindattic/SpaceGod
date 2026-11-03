// Port of Unity.SpaceGod Core/World.Colonies.cs (US-T8): part of the partial class World (World.h).

#include "World.h"

#include "Fleet.h"
#include "Combat/War.h"
#include "Economy/Colonisation.h"
#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "Systems/Galaxy.h"
#include "Systems/InterstellarTraffic.h"

namespace SpaceGod::Core
{
	namespace
	{
		using EconomyT = ::SpaceGod::Core::Economy;
		using ColonisationT = ::SpaceGod::Core::Colonisation;
	}

	// ---------------------------------------------------------------- the colony ships' hulls (U.SPC-T524)

	void World::WireColonyHulls(cs::Ref<ColonisationT> c)
	{
		c->Embark = [this](cs::Ref<ColonyShip> v, double jd) { return EmbarkColonists(v, jd); };
		c->Refit = [this](cs::Ref<Ship> hull) { RefitColonyHull(hull); };
		c->Lost = [this](cs::Ref<ColonyShip> v, cs::Ref<Faction> by)
		{
			auto hull = v->Hull;
			cs::String text = hull != nullptr && hull->Destroyed && !hull->Scrapped
				? cs::Cat("The colony ship ", v->Name, " is lost with all ", cs::Fmt(v->Colonists, "N0"), " of its colonists")
				: by != nullptr
					? cs::Cat("The colony ship ", v->Name, " is taken by the ", by->Name, ", with its ", cs::Fmt(v->Colonists, "N0"), " colonists")
					: cs::Cat("The colony ship ", v->Name, ", bound for ", v->Site->Name, v->Abroad() ? cs::Cat(" in ", v->Where) : cs::String(), ", is never heard from again: its ", cs::Fmt(v->Colonists, "N0"), " colonists are given up for lost");
			Log(Economy->Jd, text, WorldEventKind::Loss, v->From ? v->From->Body : -1);
		};
		c->Returned = [this](cs::Ref<ColonyShip> v)
		{
			Log(Economy->Jd, cs::Cat("The colony ship ", v->Name, " brings its ", cs::Fmt(v->Colonists, "N0"), " colonists home to ", v->From->Name),
				WorldEventKind::Info, v->From->Body);
		};
	}

	cs::Ref<Ship> World::EmbarkColonists(cs::Ref<ColonyShip> v, double jd)
	{
		if (v == nullptr || v->From == nullptr || v->Owner == nullptr) return nullptr;
		auto hull = Fleet->AddShip("", ShipClass::Freighter, v->Owner, ShipRole::Colonist);
		hull->Name = v->Name;
		hull->CapacityTonnes = EconomyT::CapacityOf(ShipClass::Freighter);
		hull->Colony = v;
		hull->Ports = cs::List<int>{ v->From->Body };
		double tonnes = 0;
		for (double t : v->Cargo) tonnes += t;
		hull->Cargo = cs::Cat(cs::Fmt(v->Colonists, "N0"), " colonists and ", cs::Fmt(tonnes, "N0"), " t of starter goods for ") +
			(v->Reinforcing != nullptr ? v->Reinforcing->Name : v->Site->Name + (v->Abroad() ? cs::Cat(" in ", v->Where) : cs::String()));
		Fleet->Station(hull, v->From->Body, cs::Double::IsNaN(Jd) ? jd : cs::Math::Max(jd, Jd));
		NotifyAdded(hull);
		return hull;
	}

	bool World::ColonyHullArrived(cs::Ref<Ship> hull, double jd)
	{
		auto v = hull->Colony;
		if (v == nullptr || v->Of == nullptr) return false;
		switch (v->Of->HullArrived(hull, Colonisation, Economy, jd))
		{
		case ColonyLanding::Founded:
			BreakUpColonyHull(hull, jd);
			return true;
		case ColonyLanding::Reinforced:
		case ColonyLanding::Returned:
			RefitColonyHull(hull);
			return false;
		case ColonyLanding::TurnedBack:
			hull->Cargo = cs::Cat(cs::Fmt(v->Colonists, "N0"), " colonists, turned back for ", v->From->Name);
			// Abroad, it goes home through the gates now; at home, it flies back to the port (see ChooseNext).
			if (hull->Voyage != nullptr) hull->Voyage->StayUntilJd = jd;
			return false;
		default:
			break;
		}
		return false;
	}

	int World::ColonyHullNext(cs::Ref<Ship> hull, int here, double jd)
	{
		auto v = hull->Colony;
		cs::Nullable<int> bearing = ColonisationT::HullDestination(hull, Colonisation);
		if (!bearing.has_value())
		{
			RefitColonyHull(hull);
			return -1;
		}
		if (*bearing >= 0) return *bearing;
		// (While it is still being fitted out, before it has been sent through the gates, it just waits.)
		if (v->Hull == hull && hull->Voyage == nullptr && v->Of == Colonisation && v->Abroad() && !v->Returning)
		{
			v->Of->TurnBack(v);
			hull->Cargo = cs::Cat(cs::Fmt(v->Colonists, "N0"), " colonists, turned back for ", v->From->Name);
			Log(jd, cs::Cat("The colony ship ", hull->Name, " cannot get through the gates to ", v->Where, " and turns back for ", v->From->Name), WorldEventKind::Info, here);
			return v->Body();
		}
		return here;
	}

	void World::BreakUpColonyHull(cs::Ref<Ship> hull, double jd)
	{
		hull->Voyage = nullptr;
		hull->GateKm.reset();
		hull->Colony = nullptr;
		Economy->Release(hull);
		if (War) War->HandOverCommand(hull);
		hull->Scrapped = true;
		// Gone from the fleet without a wreck.
		hull->Destroyed = true;
		hull->GateDeparted = true;
		int body = hull->Orbit ? hull->Orbit->Body : -1;
		Log(jd, cs::Cat("The colony ship ", hull->Name, " is broken up where it landed, its hull the settlers' first habitat"), WorldEventKind::Economy, body);
	}

	void World::RefitColonyHull(cs::Ref<Ship> hull)
	{
		if (hull == nullptr || hull->Destroyed) return;
		hull->Colony = nullptr;
		if (hull->CapacityTonnes <= 0) hull->CapacityTonnes = EconomyT::CapacityOf(hull->Spec->Class);
		auto v = hull->Voyage;
		if (v != nullptr && v->Purpose == VoyagePurpose::Colonise)
		{
			// Where it is, it goes about as it pleases until it sets out for home, where it trades.
			hull->Role = ShipRole::Free;
			hull->Cargo = "Empty, homeward";
			v->HomeRole = ShipRole::Trader;
			if (v->Home && v->Home->World.get() == this) v->HomePorts = PortBodies();
			v->StayUntilJd = 0;
			return;
		}
		hull->Role = ShipRole::Trader;
		hull->Cargo = "Empty, looking for work";
		hull->Ports = PortBodies();
	}
}
