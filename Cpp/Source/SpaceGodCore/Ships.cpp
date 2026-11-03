// Port of Unity.SpaceGod Core/Ships.cs (US-T8).

#include "Ships.h"
#include "Astro.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Outfitting.h"
#include "Economy/BeltMining.h"
#include "Economy/CargoHold.h"
#include "Economy/Propulsion.h"

namespace SpaceGod::Core
{
	const char* ToString(ShipClass v)
	{
		switch (v)
		{
		case ShipClass::Carrier: return "Carrier";
		case ShipClass::Battleship: return "Battleship";
		case ShipClass::Cruiser: return "Cruiser";
		case ShipClass::Destroyer: return "Destroyer";
		case ShipClass::Freighter: return "Freighter";
		case ShipClass::Miner: return "Miner";
		case ShipClass::Courier: return "Courier";
		case ShipClass::Fighter: return "Fighter";
		case ShipClass::Explorer: return "Explorer";
		case ShipClass::Gunship: return "Gunship";
		case ShipClass::OreTug: return "OreTug";
		case ShipClass::Interceptor: return "Interceptor";
		case ShipClass::Bomber: return "Bomber";
		case ShipClass::LaneTender: return "LaneTender";
		case ShipClass::GatePicket: return "GatePicket";
		case ShipClass::CommandCruiser: return "CommandCruiser";
		case ShipClass::Frigate: return "Frigate";
		case ShipClass::Corvette: return "Corvette";
		case ShipClass::Recon: return "Recon";
		case ShipClass::AssaultShuttle: return "AssaultShuttle";
		case ShipClass::GateBreaker: return "GateBreaker";
		case ShipClass::CaptureTug: return "CaptureTug";
		}
		return "7"; // C# prints an undefined enum value as its number; 7 is the only gap
	}

	const char* ToString(ShipRole v)
	{
		switch (v)
		{
		case ShipRole::Trader: return "Trader";
		case ShipRole::Miner: return "Miner";
		case ShipRole::Courier: return "Courier";
		case ShipRole::Patrol: return "Patrol";
		case ShipRole::Escort: return "Escort";
		case ShipRole::Free: return "Free";
		case ShipRole::Commuter: return "Commuter";
		case ShipRole::Tender: return "Tender";
		case ShipRole::Tug: return "Tug";
		case ShipRole::Hauler: return "Hauler";
		case ShipRole::Supply: return "Supply";
		case ShipRole::Colonist: return "Colonist";
		}
		return "";
	}

	const char* ToString(ShipPhase v)
	{
		switch (v)
		{
		case ShipPhase::Parked: return "Parked";
		case ShipPhase::Transit: return "Transit";
		case ShipPhase::Lane: return "Lane";
		case ShipPhase::Lurk: return "Lurk";
		case ShipPhase::Adrift: return "Adrift";
		case ShipPhase::Queued: return "Queued";
		case ShipPhase::Descent: return "Descent";
		case ShipPhase::Landed: return "Landed";
		case ShipPhase::Ascent: return "Ascent";
		}
		return "";
	}

	// ---------------------------------------------------------------- ShipClassSpec

	ShipClassSpec::ShipClassSpec(ShipClass c, cs::String name, double accelG, bool military)
	{
		Class = c;
		DisplayName = name;
		AccelG = accelG;
		IsMilitary = military;
	}

	const cs::List<cs::Ref<ShipClassSpec>>& ShipClassSpec::Specs()
	{
		// The constructor is private, so the Refs are made here rather than through cs::New.
		auto spec = [](ShipClass c, const char* name, double accelG, bool military) { return cs::Ref<ShipClassSpec>(new ShipClassSpec(c, name, accelG, military)); };
		static const cs::List<cs::Ref<ShipClassSpec>> specs =
		{
			spec(ShipClass::Carrier, "Carrier", 0.3, true),
			spec(ShipClass::Battleship, "Battleship", 0.3, true),
			spec(ShipClass::Cruiser, "Cruiser", 0.3, true),
			spec(ShipClass::Destroyer, "Destroyer", 0.3, true),
			// A truss freighter's drive is weak (U.SPC-T482): it rides the lanes, and off them is slow to speed up and slow down.
			spec(ShipClass::Freighter, "Freighter", 0.06, false),
			spec(ShipClass::Miner, "Ore hauler", 0.06, false),
			spec(ShipClass::Courier, "Courier", 1.2, false),
			// The retired Breacher hull's number (U.SPC-T517): no class, so no spec.
			nullptr,
			spec(ShipClass::Fighter, "Fighter", 1.0, true),
			spec(ShipClass::Explorer, "Explorer", 0.6, false),
			spec(ShipClass::Gunship, "Gunship", 0.7, true),
			spec(ShipClass::OreTug, "Ore tug", 0.08, false),
			spec(ShipClass::Interceptor, "Interceptor", 1.3, true),
			spec(ShipClass::Bomber, "Bomber", 0.6, true),
			spec(ShipClass::LaneTender, "Lane tender", 0.2, false),
			spec(ShipClass::GatePicket, "Gate picket", 0.8, true),
			spec(ShipClass::CommandCruiser, "Command cruiser", 0.3, true),
			// An engine-heavy frigate (U.SPC-T475): the fastest warship hull short of a strike craft.
			spec(ShipClass::Frigate, "Frigate", 0.6, true),
			// A patrol hull (U.SPC-T476): quick enough to run a smuggler down inside its system, no frigate's sprinter.
			spec(ShipClass::Corvette, "Corvette", 0.7, true),
			// The recon fighter (U.SPC-T485): as quick as an interceptor, it has to get ahead of the strike.
			spec(ShipClass::Recon, "Recon fighter", 1.3, true),
			// A boarding shuttle (U.SPC-T486): heavy for its size, no faster than a bomber.
			spec(ShipClass::AssaultShuttle, "Assault shuttle", 0.5, true),
			// The siege capital (U.SPC-T478): its plate makes it the slowest warship hull.
			spec(ShipClass::GateBreaker, "Gate breaker", 0.25, true),
			// A capture tug (U.SPC-T522): a lighter frame than an ore tug's, quicker to chase down and match a tumbling rock.
			spec(ShipClass::CaptureTug, "Capture tug", 0.15, false),
		};
		return specs;
	}

	cs::Ref<ShipClassSpec> ShipClassSpec::Of(ShipClass c) { return Specs()[static_cast<int>(c)]; }

	// ---------------------------------------------------------------- Faction

	Faction::Faction(cs::String name, cs::String prefix, Rgb color)
	{
		Name = name;
		Prefix = prefix;
		Color = color;
	}

	// ---------------------------------------------------------------- ParkingOrbit

	double ParkingOrbit::PhaseAt(double jd) const { return PhaseAtEpoch + RateRadPerDay * (jd - EpochJd); }

	DVec3 ParkingOrbit::OffsetAt(double jd) const
	{
		double p = PhaseAt(jd);
		return Center + (U * cs::Math::Cos(p) + V * cs::Math::Sin(p)) * RadiusKm;
	}

	DVec3 ParkingOrbit::TangentAt(double jd) const
	{
		double p = PhaseAt(jd);
		return -U * cs::Math::Sin(p) + V * cs::Math::Cos(p);
	}

	DVec3 ParkingOrbit::VelocityAt(double jd) const { return TangentAt(jd) * (RateRadPerDay * RadiusKm); }

	double ParkingOrbit::PhaseFacing(DVec3 direction) const { return cs::Math::Atan2(DVec3::Dot(direction, V), DVec3::Dot(direction, U)); }

	double ParkingOrbit::SpeedKmS() const { return RateRadPerDay * RadiusKm / Astro::SecondsPerDay; }

	// ---------------------------------------------------------------- Ship

	double Ship::LengthKm() const { return Combat->Spec->LengthKm; }

	double Ship::TowTonnes() const { return Tow == nullptr ? 0 : cs::Math::Max(0.0, Tow->Tonnes); }

	double Ship::AccelG() const
	{
		double load = TowTonnes();
		if (load <= 0) return Spec->AccelG;
		double own = cs::Math::Max(1.0, DryMassTonnes() + PropellantCapacity());
		return Spec->AccelG * own / (own + load);
	}

	cs::Ref<CargoHold> Ship::PersonalHold() const
	{
		if (!personalHold) personalHold = EmptyHold(Loadout == nullptr ? 0 : Loadout->ExtraHoldRows());
		return personalHold;
	}

	cs::Ref<CargoHold> Ship::EmptyHold(int extraRows) const
	{
		auto spec = CargoHoldSpec::Of(Spec->Class);
		return cs::New<CargoHold>(spec.Width, cs::Math::Max(1, spec.Height + extraRows));
	}

	cs::Ref<CargoHold> Ship::ResizedHold(int extraRows)
	{
		auto hold = EmptyHold(extraRows);
		if (personalHold == nullptr) return hold;
		cs::List<cs::Ref<CargoItem>> moved;
		for (const auto& item : personalHold->Items())
			if (hold->Add(item->Good, item->X, item->Y, item->Rotated) == nullptr) moved.Add(item);
		for (const auto& item : moved)
			if (hold->AddFirstFit(item->Good) == nullptr) return nullptr;
		return hold;
	}

	// ---------------------------------------------------------------- propulsion (see Propulsion)

	const cs::Ref<CombatSpec>& Ship::Design() const
	{
		if (Combat && Combat->Spec) return Combat->Spec;
		return CombatSpec::Of(Spec->Class);
	}

	double Ship::DryMassTonnes() const { return Design()->DryMassTonnes; }

	double Ship::PropellantCapacity() const { return Design()->PropellantTonnes; }

	double Ship::ExhaustVelocityKmS() const { return cs::Math::Max(1.0, Design()->ExhaustVelocityKmS); }

	double Ship::Propellant() const { return propellant.has_value() ? *propellant : PropellantCapacity(); }

	void Ship::SetPropellant(double value) { propellant = cs::Math::Max(0.0, cs::Math::Min(PropellantCapacity(), value)); }

	double Ship::PropellantFraction() const { return PropellantCapacity() > 0 ? Propellant() / PropellantCapacity() : 0; }

	double Ship::CarriedTonnes() const
	{
		double t = HasCargo ? CargoTonnes : 0;
		if (personalHold != nullptr) for (const auto& item : personalHold->Items()) t += item->Tonnes();
		return t;
	}

	double Ship::MassTonnes() const { return DryMassTonnes() + Propellant() + CarriedTonnes(); }

	double Ship::DeltaVBudgetKmS() const { return Propulsion::DeltaV(ExhaustVelocityKmS(), MassTonnes(), MassTonnes() - Propellant()); }

	double Ship::PlunderTonnes() const
	{
		double t = 0;
		// (C#'s `Plunder != null`: an empty list adds nothing.)
		for (double x : Plunder) t += x;
		return t;
	}

	cs::Ref<Ship> Ship::Controller() { return Leader ? Leader : shared_from_this(); }

	void Ship::ResetForArrival()
	{
		BandId = -1;
		Route.Clear(); // Route = null
		RouteIndex = 0;
		Phase = ShipPhase::Parked;
		Orbit = nullptr;
		DepartureOrbit = nullptr;
		OriginBody = -1;
		DestinationBody = -1;
		PhaseStartJd = 0;
		PhaseEndJd = 0;
		FrameBody = 0;
		DepartureVelocity = DVec3(); // DVec3.Zero
		ArrivalVelocity = DVec3();
		Path.Clear(); // Path = null
		PathCumulative.Clear();
		PathLength = 0;
		AccelKmPerDay2 = 0;
		Conic = nullptr;
		GravityLossKmS = 0;
		PlannedConicDepartJd = cs::Double::NaN;
		PlannedConicDays = 0;
		PlannedDirect = false;
		Journey.Clear(); // Journey = null
		JourneyIndex = 0;
		FinalBody = -1;
		LaneLeg = LaneHop();
		LaneFromOffset = DVec3();
		LaneToOffset = DVec3();
		LaneLengthKm = 0;
		LaneStar = -1;
		LaneShape = -1;
		LaneStartProgress = 0;
		LaneEndProgress = 1;
		LaneHold = 0;
		LaneHoldSide = DVec3();
		LaneSurged = false;
		LurkLane = nullptr;
		LurkAt = 0;
		LurkDays = 0;
		Stalled = false;
		// Not Destroyed or Derelict: a hull crossing never carries those into this call (a cruise drops a lost
		// or boarded hull from its group on the spot, see DeepSpace.Destroy, and Derelict is a system-combat
		// outcome a gate's passage or a cruise never produces), so clearing them here would only ever be able to
		// revive one, never settle it.
		Salvage.Clear();
		// The market it was loaded for (and the contract bonus it posted) belongs to the origin system's
		// economy, so the goods arrive unbooked. Captains in the brig (CaptainsHeld) are not: they are aboard,
		// same as the hull's own crew, and cross with it.
		CargoDestination = nullptr;
		CargoBonus = 0;
		CargoBonusPayer = nullptr;
		LoadedFrom = nullptr;
		LoadedJd = 0;
		LoadedCredits = 0;
		Scrapped = false;
		OutOfPropellant = false;
		OutOfPropellantSinceJd = 0;
		StrandedInFlight = false;
		LegPropellant = 0;
		StrandJd = cs::Double::NaN;
		GateDeparted = false;
		GateKm.reset();
		StakeoutKm.reset();
		StakeoutUntilJd = 0;
		PatrolWing = false;
		Convoy = false;
		CorvetteStation = -1;
		Civil = CivilCraft::None;
		Wander = false;
		Homing = false;
		Ring = nullptr;
		HoldPoint = nullptr;
		Battle = nullptr;
	}

	cs::Ref<Ship> Ship::Tombstone() const
	{
		// MemberwiseClone: a shallow copy. (The C# clone shares the readonly Salvage list with the original; here it is
		// copied by value. A tombstone is gone and nothing touches its salvage, so the difference is never seen.)
		auto t = cs::New<Ship>(*this);
		t->Destroyed = true;
		t->GateDeparted = true;
		t->Leader = nullptr;
		t->Battle = nullptr;
		t->Voyage = nullptr;
		t->GateKm.reset();
		return t;
	}
}
