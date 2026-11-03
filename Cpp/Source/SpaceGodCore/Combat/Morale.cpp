// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/Morale.cs (see docs/MIGRATION.md).

#include "Combat/Morale.h"

#include "Ships.h"

namespace SpaceGod::Core
{
	const char* ToString(SurrenderTerms v)
	{
		switch (v)
		{
		case SurrenderTerms::RansomCargo: return "RansomCargo";
		case SurrenderTerms::TakeShip: return "TakeShip";
		case SurrenderTerms::LetGo: return "LetGo";
		}
		return "";
	}

	double Morale::Resolve(const cs::Ref<Faction>& faction, bool isMilitary)
	{
		double temperament = faction->IsPirate ? 0.55 : isMilitary ? 1.35 : 0.5;
		// Crews who have watched their shipmates die (and not come home from the pods) break sooner.
		return temperament * cs::Math::Max(0.3, faction->CrewMorale);
	}

	double Morale::SupplyResolve(double readiness) { return 0.4 + 0.6 * Clamp01(readiness); }

	double Morale::DamageToll(double hullFraction, double shieldFraction)
	{
		return Clamp01((1 - Clamp01(hullFraction)) * 0.75 + (1 - Clamp01(shieldFraction)) * 0.25);
	}

	double Morale::Outgunned(double ownSideStrength, double enemySideStrength)
	{
		double total = ownSideStrength + enemySideStrength;
		return total <= 0 ? 0 : Clamp01((enemySideStrength - ownSideStrength) / total);
	}

	double Morale::Pressure(double damageToll, double outgunned, double resolve)
	{
		return (Clamp01(damageToll) * 0.55 + Clamp01(outgunned) * 0.45) / cs::Math::Max(0.1, resolve);
	}

	bool Morale::ShouldSurrender(double pressure, double threshold) { return pressure >= threshold; }

	bool Morale::StrikesColours(double damageToll, double outgunned, double resolve, double threshold)
	{
		return damageToll >= MinDamageToStrike && ShouldSurrender(Pressure(damageToll, outgunned, resolve), threshold);
	}

	bool Morale::ShouldDemandCargo(double victimOutgunned, double victimDamageToll)
	{
		return victimOutgunned >= 0.3 || (victimOutgunned >= 0.1 && victimDamageToll >= 0.4);
	}

	SurrenderTerms Morale::ChooseTerms(bool captorIsPirate, bool targetIsWarship, bool targetHasCargo, bool captorCanBoard, double roll)
	{
		SurrenderTerms terms;
		if (captorIsPirate)
		{
			// Disable and loot (U.SPC-T374, see RaidDoctrine): the hold is what they came for; only now and then is
			// the hull itself worth the trouble of taking.
			if (targetHasCargo) terms = roll < PirateRansomShare ? SurrenderTerms::RansomCargo : SurrenderTerms::TakeShip;
			else terms = roll < 0.75 ? SurrenderTerms::TakeShip : SurrenderTerms::LetGo;
		}
		else terms = targetIsWarship && roll < 0.85 ? SurrenderTerms::TakeShip : SurrenderTerms::LetGo;
		if (terms == SurrenderTerms::TakeShip && !captorCanBoard)
			terms = captorIsPirate && targetHasCargo ? SurrenderTerms::RansomCargo : SurrenderTerms::LetGo;
		return terms;
	}

	double Morale::Clamp01(double v) { return cs::Math::Max(0.0, cs::Math::Min(1.0, v)); }
}
