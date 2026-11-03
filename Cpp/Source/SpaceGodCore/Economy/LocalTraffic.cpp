// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/LocalTraffic.cs (US-T19).

#include "Economy/LocalTraffic.h"
#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Diplomacy.h"
#include "Fleet.h"
#include "Ships.h"
#include "World.h"

namespace SpaceGod::Core
{
	const char* ToString(CivilCraft v)
	{
		switch (v)
		{
			case CivilCraft::None: return "None";
			case CivilCraft::Bus: return "Bus";
			case CivilCraft::Taxi: return "Taxi";
			case CivilCraft::Skiff: return "Skiff";
			case CivilCraft::Runabout: return "Runabout";
			case CivilCraft::Tourer: return "Tourer";
		}
		return "";
	}

	int LocalTraffic::Seats(CivilCraft c)
	{
		switch (c)
		{
			case CivilCraft::Bus: return 40;
			case CivilCraft::Taxi: return 4;
			case CivilCraft::Skiff: return 1;
			case CivilCraft::Runabout: return 4;
			case CivilCraft::Tourer: return 6;
			default: return 0;
		}
	}

	bool LocalTraffic::IsPersonal(CivilCraft c) { return c == CivilCraft::Skiff || c == CivilCraft::Runabout || c == CivilCraft::Tourer; }

	int LocalTraffic::BusesFor(double population) { return population < 1e5 ? 0 : population < 1e8 ? 1 : 2; }

	int LocalTraffic::TaxisFor(double population) { return population < 1e5 ? 0 : population < 1e7 ? 1 : population < 1e9 ? 2 : 3; }

	int LocalTraffic::PersonalFor(double population)
	{
		return population < 1e4 ? 0 : cs::Math::Min(4, 1 + cs::ToInt(cs::Math::Floor(cs::Math::Log10(population / 1e4) / 2)));
	}

	cs::Ref<CombatSpec> LocalTraffic::DesignOf(CivilCraft c)
	{
		switch (c)
		{
			case CivilCraft::Bus: return BusDesign();
			case CivilCraft::Taxi: return TaxiDesign();
			case CivilCraft::Skiff: return SkiffDesign();
			case CivilCraft::Runabout: return RunaboutDesign();
			case CivilCraft::Tourer: return TourerDesign();
			default: return nullptr;
		}
	}

	const cs::Ref<CombatSpec>& LocalTraffic::BusDesign() { static const cs::Ref<CombatSpec> d = Design("Orbital bus", 0.06, 1.5, 45, 600, 200, 0.22, 12, 3, 1.0); return d; }
	const cs::Ref<CombatSpec>& LocalTraffic::TaxiDesign() { static const cs::Ref<CombatSpec> d = Design("Taxi", 0.02, 0.2, 8, 250, 80, 0.3, 24, 1, 2.0); return d; }
	const cs::Ref<CombatSpec>& LocalTraffic::SkiffDesign() { static const cs::Ref<CombatSpec> d = Design("Single-seat skiff", 0.008, 0.02, 1, 120, 40, 0.34, 32, 1, 3.0); return d; }
	const cs::Ref<CombatSpec>& LocalTraffic::RunaboutDesign() { static const cs::Ref<CombatSpec> d = Design("Four-seat runabout", 0.015, 0.08, 3, 200, 60, 0.3, 26, 1, 2.0); return d; }
	const cs::Ref<CombatSpec>& LocalTraffic::TourerDesign() { static const cs::Ref<CombatSpec> d = Design("Six-seat tourer", 0.025, 0.15, 5, 300, 90, 0.27, 20, 2, 1.5); return d; }

	cs::Ref<CombatSpec> LocalTraffic::Design(const cs::String& name, double lengthKm, double massKt, double thrustMN, double hull, double shield, double maxSpeed,
		double turnDegS, double crew, double agility)
	{
		double accel = thrustMN / (massKt * 1000);
		double pitchYaw = massKt * lengthKm * lengthKm / 12 * 10000 * agility;
		auto d = cs::New<CombatSpec>();
		d->DesignName = name; d->LengthKm = lengthKm;
		d->MassKt = massKt; d->MainThrustMN = thrustMN; d->RetroThrustMN = thrustMN * 0.75; d->LateralThrustMN = thrustMN * 0.4; d->VerticalThrustMN = thrustMN * 0.4;
		d->PitchYawTorqueMNKm = pitchYaw; d->RollTorqueMNKm = pitchYaw / 4;
		d->Hull = hull; d->Shield = shield; d->ShieldRegen = shield / 40; d->Armor = 0; d->MaxSpeed = maxSpeed; d->Accel = accel * 0.4; d->Decel = accel * 0.45;
		d->BoostSpeedMultiplier = 1.3; d->BoostDurationS = 12; d->TurnRateDegS = turnDegS;
		d->Crew = crew; d->Marines = 0;
		d->DryMassTonnes = massKt * 1000 * 0.7; d->PropellantTonnes = massKt * 1000 * 0.3; d->ExhaustVelocityKmS = 4000;
		d->Layout = cs::List<cs::Ref<SubsystemDef>>
		{
			cs::New<SubsystemDef>(SubsystemKind::Bridge, 0, 0.04, 0.25),
			cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.03, 0.1),
			cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.03, -0.2),
			cs::New<SubsystemDef>(SubsystemKind::Engine, 0, 0, -0.45),
			cs::New<SubsystemDef>(SubsystemKind::Reactor, 0, 0, -0.25),
		};
		return d;
	}

	const cs::List<cs::String>& LocalTraffic::PersonalNames()
	{
		static const cs::List<cs::String> names =
		{
			"Sunday Driver", "Pocket Rocket", "Weekender", "Little Wonder", "Moonshot", "Joyride", "Afterglow", "Day Tripper",
			"Lazy Susan", "Second Thoughts", "Gadabout", "Starling", "Puddle Jumper", "Retirement Plan", "Midlife", "Picnic",
			"Easy Does It", "Bright Idea", "Homebody", "Spare Change", "Tin Lizzie", "Odd Socks", "Free Spirit", "Night Owl",
			"Wishful Thinking", "Sightseer", "Hopscotch", "Daydream", "Wayfarer", "Small Hours",
		};
		return names;
	}

	const cs::List<CivilCraft>& LocalTraffic::PersonalKinds()
	{
		static const cs::List<CivilCraft> kinds = { CivilCraft::Runabout, CivilCraft::Skiff, CivilCraft::Tourer };
		return kinds;
	}

	int LocalTraffic::NeighbourhoodOf(const cs::Ref<BodyCatalog>& catalog, int body)
	{
		while (body > 0 && (*catalog)[body]->ParentIndex >= 0 && !(*catalog)[(*catalog)[body]->ParentIndex]->UsesStellarMapping()) body = (*catalog)[body]->ParentIndex;
		return body;
	}

	cs::List<cs::Ref<Ship>> LocalTraffic::Raise(const cs::Ref<World>& w, double jd)
	{
		auto raised = cs::List<cs::Ref<Ship>>();
		auto catalog = w->Catalog();
		auto ports = cs::List<cs::Ref<Settlement>>();
		for (const auto& s : w->Economy->Settlements)
			if (s->Kind != SettlementKind::Haven && !s->Hidden && !s->Fallen && s->Owner != nullptr && !s->Owner->IsPirate && s->Population > 0) ports.Add(s);
		ports.Sort([](const cs::Ref<Settlement>& a, const cs::Ref<Settlement>& b) { return cs::Compare(b->Population, a->Population); });
		auto places = w->FreeShipPorts();
		int personal = 0;
		for (const auto& home : ports)
		{
			if (raised.Count() >= MaxCraftPerSystem) break;
			int hood = NeighbourhoodOf(catalog, home->Body);
			// The planet's port bodies, this one first, then the most populous.
			auto stops = cs::List<int>{ home->Body };
			for (const auto& s : ports)
				if (!stops.Contains(s->Body) && NeighbourhoodOf(catalog, s->Body) == hood && w->Diplomacy()->CanDock(home->Owner, s->Owner)) stops.Add(s->Body);
			auto sights = cs::List<int>();
			for (int b : places)
				if (NeighbourhoodOf(catalog, b) == hood) sights.Add(b);
			if (!sights.Contains(home->Body)) sights.Add(home->Body);

			if (stops.Count() >= 2)
			{
				// stops.GetRange(0, Math.Min(LineLength, stops.Count)).ToArray()
				auto line = cs::List<int>(stops.begin(), stops.begin() + cs::Math::Min(LineLength, stops.Count()));
				for (int i = 0; i < BusesFor(home->Population) && raised.Count() < MaxCraftPerSystem; i++)
				{
					auto bus = Add(w, cs::Cat(home->Name, " Bus ", i + 1), CivilCraft::Bus, home->Owner, line, line[i % line.Count()], jd);
					bus->Route = line;
					bus->RouteIndex = i % line.Count();
					bus->Cargo = cs::Cat("Passengers on the ", home->Name, " line");
					raised.Add(bus);
				}
				for (int i = 0; i < TaxisFor(home->Population) && raised.Count() < MaxCraftPerSystem; i++)
				{
					auto taxi = Add(w, cs::Cat(home->Name, " Cab ", i + 1), CivilCraft::Taxi, home->Owner, stops.ToArray(), home->Body, jd);
					taxi->Cargo = "A fare";
					raised.Add(taxi);
				}
			}
			for (int i = 0; i < PersonalFor(home->Population) && raised.Count() < MaxCraftPerSystem; i++)
			{
				auto kind = PersonalKinds()[personal % PersonalKinds().Count()];
				cs::String name = cs::Cat(PersonalNames()[personal % PersonalNames().Count()], personal >= PersonalNames().Count() ? cs::Cat(" ", personal / PersonalNames().Count() + 1) : cs::String());
				personal++;
				auto craft = Add(w, name, kind, w->Independents ? w->Independents : home->Owner, sights.ToArray(), home->Body, jd);
				craft->Cargo = "Private flight";
				raised.Add(craft);
			}
		}
		return raised;
	}

	cs::Ref<Ship> LocalTraffic::Add(const cs::Ref<World>& w, const cs::String& name, CivilCraft kind, const cs::Ref<Faction>& owner, const cs::List<int>& ports, int at, double jd)
	{
		auto ship = w->Fleet->AddShip(name, ShipClass::Courier, owner, ShipRole::Commuter);
		// Its own registration, not a merchant's or a navy's prefix.
		ship->Name = name;
		ship->Civil = kind;
		ship->Combat = cs::New<ShipCombat>(DesignOf(kind), ShipClass::Courier);
		ship->Ports = ports;
		w->Fleet->Station(ship, at, jd);
		return ship;
	}
}
