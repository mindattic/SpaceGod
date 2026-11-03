// Port of Bench/CSharp/src/Simulation.cs over the C++ Core (see Simulation.h).

#include "Simulation.h"

#include "SimClock.h"
#include "World.h"
#include "Fleet.h"
#include "Ships.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Ephemeris.h"
#include "Combat/War.h"
#include "Combat/Battle.h"
#include "Combat/CombatState.h"
#include "Combat/CombatSpecs.h"
#include "Systems/Galaxy.h"
#include "Systems/InterstellarTraffic.h"
#include "Systems/TravelCourse.h"

namespace Bench
{
	using namespace SpaceGod::Core;

	cs::Ref<sg::World> Simulation::World() const { return ActiveSystem->World; }

	cs::Ref<Simulation> Simulation::Build(double startJd, int seed)
	{
		auto sim = cs::New<Simulation>();
		// Clock = SimClock.StartingNow(); Clock.SetWarp(SimClock.StartingWarp.Index) -- the setting's default, unbound.
		sim->Clock = cs::New<SimClock>(startJd);
		sim->Clock->SetWarp(SimClock::DefaultWarpIndex);
		sim->Galaxy = Galaxy::CreateDefault(sim->Clock->Jd, seed);
		// SubscribeWorlds: the game only toasts from these; the Bench counts.
		for (const auto& system : sim->Galaxy->Systems) sim->SubscribeWorld(system);
		Simulation* raw = sim.get();
		sim->Galaxy->SystemAdded += [raw](cs::Ref<StarSystem> s, double) { raw->SubscribeWorld(s); };
		sim->Activate(sim->Galaxy->Systems[0]);
		return sim;
	}

	void Simulation::SubscribeWorld(const cs::Ref<StarSystem>& s)
	{
		s->World->War->BattleStarted += [this](cs::Ref<sg::Battle>) { BattlesStarted++; };
	}

	void Simulation::Activate(const cs::Ref<StarSystem>& system)
	{
		ActiveSystem = system;
		Galaxy->Promote(system, Clock->Jd);
		Galaxy->Viewpoint = system->Origin;
	}

	DVec3 Simulation::PlayerHelioKm() const
	{
		const auto& cat = ActiveSystem->Catalog;
		int body = 0;
		for (int i = 0; i < cat->Count(); i++)
			if ((*cat)[i]->Name == "Earth") { body = i; break; }
		return ActiveSystem->Ephemeris->Helio(body);
	}

	void Simulation::Tick(double dt)
	{
		Clock->Advance(dt);
		Galaxy->Update(Clock->Jd);
		World()->UpdateAdventures(Clock->Jd, PlayerHelioKm());
		BattleTime();
		UpdateCourseShip();
	}

	void Simulation::BattleTime()
	{
		cs::Ref<sg::Battle> battle = Followed ? Followed->Battle : nullptr;
		if (battle == nullptr || battle == battleTimeFor) return;
		EnterBattleTime(battle);
	}

	void Simulation::EnterBattleTime(const cs::Ref<sg::Battle>& battle)
	{
		battleTimeFor = battle;
		if (Clock->WarpIndex > 0) Clock->SetWarp(0);
	}

	void Simulation::WatchBattle(const cs::Ref<sg::Battle>& battle)
	{
		cs::Ref<Ship> flag;
		for (const auto& u : battle->Units)
			if (u->Ship != nullptr && u->Present() && (flag == nullptr || u->Combat->Spec->Hull > flag->Combat->Spec->Hull)) flag = u->Ship;
		if (flag == nullptr) return;
		Followed = flag;
		EnterBattleTime(battle);
	}

	cs::Ref<sg::Battle> Simulation::TriggerSkirmish(int body, const cs::Ref<Faction>& aggressor, const cs::Ref<Faction>& defender, const cs::List<ShipClass>& classes)
	{
		double jd = Clock->Jd;
		DVec3 dir = DVec3(1, 0.15, 0.4).Normalized();
		DVec3 side = DVec3::Cross(DVec3(0, 1, 0), dir).Normalized();
		for (int i = 0; i < classes.Count(); i++)
			World()->Fleet->SpawnAt(cs::Cat("Showcase ", aggressor->Name, " ", i + 1), classes[i], aggressor, body, dir * (2000 + i * 400), -dir, jd);
		for (int i = 0; i < classes.Count(); i++)
			World()->Fleet->SpawnAt(cs::Cat("Showcase ", defender->Name, " ", i + 1), classes[i], defender, body, dir * (2000 + i * 400) + side * 3000, dir, jd);
		return World()->War->TryStartBattle(body, aggressor, jd);
	}

	bool Simulation::StartCrewCourse(const cs::Ref<Ship>& ship, const cs::Ref<StarSystem>& to)
	{
		if (ship->Faction != nullptr && ship->Faction->IsHouse) return false;
		auto course = TravelCourse::Plan(Galaxy->Network, ActiveSystem, to, ship->Faction, Clock->Jd);
		if (course == nullptr || Galaxy->Traffic == nullptr || Galaxy->Traffic->SendPlotted(ship, ActiveSystem, to, Clock->Jd) == nullptr) return false;
		CourseShip = ship;
		CourseDestination = to;
		Followed = ship;
		return true;
	}

	void Simulation::UpdateCourseShip()
	{
		if (CourseShip == nullptr || CourseEnded) return;
		auto ship = CourseShip;
		if (ship->Destroyed) { CourseEnded = true; return; }
		auto v = ship->Voyage;
		auto where = SystemHolding(ship);
		if (where == nullptr) return; // inside a gate
		if (where != ActiveSystem) Activate(where);
		if (v == nullptr || v->Purpose != VoyagePurpose::Plotted)
		{
			CourseEnded = true;
			CourseArrived = where == CourseDestination;
		}
	}

	cs::Ref<StarSystem> Simulation::SystemHolding(const cs::Ref<Ship>& ship) const
	{
		for (const auto& s : Galaxy->Systems)
		{
			const auto& ships = s->World->Fleet->Ships();
			if (ship->Id >= 0 && ship->Id < ships.Count() && ships[ship->Id] == ship) return s;
		}
		return nullptr;
	}
}
