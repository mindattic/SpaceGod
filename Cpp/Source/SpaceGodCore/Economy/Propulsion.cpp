// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/Propulsion.cs (US-T19).

#include "Economy/Propulsion.h"
#include "Economy/Goods.h"
#include "Astro.h"
#include "Ships.h"
#include "Trajectories.h"

namespace SpaceGod::Core
{
	const cs::List<Good>& Propulsion::ReactionMass()
	{
		static const cs::List<Good> reactionMass = { Good::Water, Good::Hydrocarbons };
		return reactionMass;
	}

	double Propulsion::MassRatio(double deltaVKmS, double exhaustKmS) { return cs::Math::Exp(cs::Math::Max(0.0, deltaVKmS) / exhaustKmS); }

	double Propulsion::DeltaV(double exhaustKmS, double wetTonnes, double dryTonnes)
	{
		return dryTonnes <= 0 || wetTonnes <= dryTonnes ? 0 : exhaustKmS * cs::Math::Log(wetTonnes / dryTonnes);
	}

	double Propulsion::PropellantBurned(double deltaVKmS, double exhaustKmS, double startTonnes)
	{
		return startTonnes * (1 - 1 / MassRatio(deltaVKmS, exhaustKmS));
	}

	double Propulsion::BrachistochroneDeltaV(double accelKmS2, double distanceKm) { return 2 * cs::Math::Sqrt(cs::Math::Max(0.0, accelKmS2 * distanceKm)); }

	double Propulsion::TransitDeltaV(const cs::Ref<Ship>& s)
	{
		if (s->Conic != nullptr) return s->Conic->DeltaVKmS();
		double days = s->PhaseEndJd - s->PhaseStartJd;
		if (days <= 0) return 0;
		return (s->AccelKmPerDay2 * days + (s->ArrivalVelocity - s->DepartureVelocity).Length()) / Astro::SecondsPerDay + s->GravityLossKmS;
	}

	double Propulsion::PropellantFor(const cs::Ref<Ship>& ship, double deltaVKmS) { return PropellantBurned(deltaVKmS, ship->ExhaustVelocityKmS(), ship->MassTonnes()); }

	double Propulsion::PropellantFor(const cs::Ref<Ship>& ship, double deltaVKmS, double cargoTonnes, double propellantTonnes)
	{
		return PropellantBurned(deltaVKmS, ship->ExhaustVelocityKmS(), ship->DryMassTonnes() + cargoTonnes + propellantTonnes);
	}

	double Propulsion::Spend(const cs::Ref<Ship>& ship, double deltaVKmS)
	{
		if (deltaVKmS <= 0) return 1;
		double need = PropellantFor(ship, deltaVKmS);
		if (need <= ship->Propellant())
		{
			ship->SetPropellant(ship->Propellant() - need);
			return 1;
		}
		double have = ship->Propellant();
		double reached = DeltaV(ship->ExhaustVelocityKmS(), ship->MassTonnes(), ship->MassTonnes() - have);
		ship->SetPropellant(0);
		return cs::Math::Max(0.0, cs::Math::Min(1.0, reached / deltaVKmS));
	}

	void Propulsion::ApplyManoeuvre(const cs::Ref<Ship>& ship, DVec3& pos, DVec3& vel, DVec3 posBefore, DVec3 velBefore, double dt)
	{
		auto change = vel - velBefore;
		double share = Spend(ship, change.Length());
		if (share >= 1) return;
		vel = velBefore + change * share;
		pos = posBefore + (velBefore + vel) * (0.5 * dt);
	}

	double Propulsion::TankerLoad(const cs::Ref<Ship>& ship) { return cs::Math::Max(0.0, ship->PropellantCapacity() - ship->Propellant()); }

	double Propulsion::TankerFee(double tonnes) { return 40000 + tonnes * 3 * PropellantBasePrice(); }

	double Propulsion::PropellantBasePrice()
	{
		return GoodInfo::Of(Good::Water)->BasePrice + HeliumThreePerTonne * GoodInfo::Of(Good::HeliumThree)->BasePrice;
	}

	double Propulsion::Transfer(const cs::Ref<Ship>& from, const cs::Ref<Ship>& to, double tonnes)
	{
		double moved = cs::Math::Max(0.0, cs::Math::Min(tonnes, cs::Math::Min(from->Propellant(), to->PropellantCapacity() - to->Propellant())));
		from->SetPropellant(from->Propellant() - moved);
		to->SetPropellant(to->Propellant() + moved);
		return moved;
	}
}
