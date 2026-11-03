#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/Morale.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class Faction;

	/// <summary>What a captor decides to do with a hull that has struck its colours.</summary>
	enum class SurrenderTerms : int
	{
		/// <summary>Hand over the cargo; the ship is released.</summary>
		RansomCargo,

		/// <summary>Boarded and taken as a prize, through the normal boarding path.</summary>
		TakeShip,

		/// <summary>Let it go unharmed.</summary>
		LetGo,
	};
	const char* ToString(SurrenderTerms v);

	/// <summary>
	/// Whether a battered hull strikes its colours instead of fighting to the last, and what its captor
	/// then does about it. Pure functions of hull/shield loss, the fight's odds and a faction's
	/// temperament, so the model can be tuned and exercised without a running battle.
	/// </summary>
	class Morale
	{
	public:
		/// <summary>
		/// How stubbornly <paramref name="faction"/> holds on rather than striking its colours: below 1
		/// breaks sooner than the baseline, above 1 holds on longer. Pirates are here to profit, not
		/// die; a navy crew is the most stubborn.
		/// </summary>
		static double Resolve(const cs::Ref<Faction>& faction, bool isMilitary);

		/// <summary>
		/// How a crew's resolve holds up with its fleet's <paramref name="readiness"/> (U.SPC-T93, see
		/// <see cref="ShipCombat.Readiness"/>): undimmed in supply, down to 0.4 of it on empty magazines and dry tanks.
		/// </summary>
		static double SupplyResolve(double readiness);

		/// <summary>
		/// Combined damage toll, 0 (pristine) to 1 (wrecked): hull loss counts for more than a dropped
		/// screen, since a hull-stripped ship is closer to destruction either way.
		/// </summary>
		static double DamageToll(double hullFraction, double shieldFraction);

		/// <summary>
		/// How outgunned a side is, 0 (even or ahead) to 1 (hopeless): the enemy's share of the two
		/// sides' combined strength, over even.
		/// </summary>
		static double Outgunned(double ownSideStrength, double enemySideStrength);

		/// <summary>
		/// How close a hull is to giving up: a blend of its own damage and the fight's odds, eased by
		/// how stubborn its faction is. Compare against a per-ship threshold (<see cref="ShouldSurrender"/>)
		/// rather than a fixed cutoff, so otherwise-identical ships do not all break on the same tick.
		/// </summary>
		static double Pressure(double damageToll, double outgunned, double resolve);

		/// <summary>True once pressure has overtaken a ship's own breaking point.</summary>
		static bool ShouldSurrender(double pressure, double threshold);

		/// <summary>
		/// A hull has to be hurt before it gives up: a ship that is merely outgunned runs (or fights) first.
		/// Below this <see cref="DamageToll"/> no amount of bad odds makes it strike its colours.
		/// </summary>
		static constexpr double MinDamageToStrike = 0.2;

		/// <summary>
		/// Whether a hull strikes its colours this tick: battered past <see cref="MinDamageToStrike"/> and
		/// under more <see cref="Pressure"/> than its own breaking point.
		/// </summary>
		static bool StrikesColours(double damageToll, double outgunned, double resolve, double threshold);

		/// <summary>
		/// Whether pirates hail a player's ship with "your cargo or your life": clearly the stronger side,
		/// or ahead and with the victim already badly hurt. Asked once per battle; refusing means a fight.
		/// </summary>
		static bool ShouldDemandCargo(double victimOutgunned, double victimDamageToll);

		/// <summary>
		/// What the captor does once a hull has struck its colours: pirates are in it for the money, so
		/// they ransom cargo when there is any to take and otherwise board for the hull itself; a navy
		/// has no use for a merchant's cargo but still takes an enemy warship as a prize. A captor with
		/// nobody able to board can't take the ship, so it settles for the cargo, or lets it go.
		/// </summary>
		static SurrenderTerms ChooseTerms(bool captorIsPirate, bool targetIsWarship, bool targetHasCargo, bool captorCanBoard, double roll);

		/// <summary>How often pirates settle for a surrendered hull's cargo rather than taking the hull (U.SPC-T374).</summary>
		static constexpr double PirateRansomShare = 0.85;

	private:
		static double Clamp01(double v);
	};
}
