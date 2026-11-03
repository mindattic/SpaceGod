#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/LaneInterdictors.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class Ship;
	class FacilityKind;
	class RangeSetting;
	enum class ShipClass : int;
	enum class HullCategory : int;

	/// <summary>
	/// What throws a ship out of a hyperlane (U.SPC-T479): a field projector that disrupts the lane's magnetic field. The
	/// projector is heavy equipment, not a ship class (U.SPC-T507): a facility (<see cref="FacilityKinds.FieldProjector"/>)
	/// fitted into the internal space of any hull big enough to carry it and its reactor load (<see cref="CanCarry(ShipClass)"/>),
	/// so a destroyer, a cruiser or a raider's converted freighter can each be fitted out as a lane interdictor, and a yard
	/// fits and strips it like any other fitting. A group lying in wait beside a lane (see the lane ambushes in
	/// <see cref="War"/>) can pulse only while one of its hulls carries a working projector; the rest of the group fights
	/// what the pulse pulls out, in the battle beside the lane. A lane's route is predictable, so the projector is the
	/// pirate's and raider's favourite: every raider band fits one to a hull for its lane ambushes. Navies fit one to a
	/// destroyer of a lane picket, to pull raiders and enemies off the lanes out of their systems. The tunables are in the
	/// settings menu's AI group.
	/// </summary>
	/// [SettingsSource]
	class SPACEGODCORE_API LaneInterdictors
	{
	public:
		/// <summary>The projector's installed mass with its reactor, tonnes (one fixed fitting whatever the hull).</summary>
		static constexpr double ProjectorMassTonnes = 900;

		/// <summary>
		/// The least dry mass, tonnes, of a hull with the reactor load to spare for a projector: a destroyer's or a truss
		/// freighter's (9,000 t) and up, an ore hauler's at the margin; never a frigate, corvette or anything smaller.
		/// </summary>
		static constexpr double MinHullTonnes = 8000;

		/// <summary>The hull categories that may carry a projector: warships, capitals and freighters.</summary>
		static const cs::List<HullCategory>& Hulls();

		/// <summary>The facility kind.</summary>
		static cs::Ref<FacilityKind> Kind();

		/// <summary>Whether hulls of <paramref name="cls"/> are big enough to carry a projector and its reactor load (their
		/// category allowing, see <see cref="Hulls"/>).</summary>
		static bool CanCarry(ShipClass cls);

		/// <summary>Whether <paramref name="ship"/> could take a projector into its free space now.</summary>
		static bool CanFit(const cs::Ref<Ship>& ship);

		/// <summary>Fits a projector into <paramref name="ship"/> as part of its build or raising (no yard bill). False if
		/// the hull may not carry one or has no room for it.</summary>
		static bool Fit(const cs::Ref<Ship>& ship);

		/// <summary>Projectors in <paramref name="ship"/>: its refitted loadout's, or its design's as laid down.</summary>
		static int Count(const cs::Ref<Ship>& ship);

		/// <summary>Whether <paramref name="ship"/> carries a field projector.</summary>
		static bool Carries(const cs::Ref<Ship>& ship);

		static const cs::Ref<RangeSetting>& RechargeDays();

		static const cs::Ref<RangeSetting>& PicketChance();

		/// <summary>
		/// The hull in <paramref name="leader"/>'s group carrying a working field projector: the leader itself, else an escort
		/// flying with it (one of <paramref name="ships"/>), or null when none is left (lost, stripped, or never fitted).
		/// </summary>
		static cs::Ref<Ship> EmitterOf(const cs::Ref<Ship>& leader, const cs::List<cs::Ref<Ship>>& ships);

		/// <summary>Whether <paramref name="leader"/>'s group carries a working field projector.</summary>
		static bool Fielded(const cs::Ref<Ship>& leader, const cs::List<cs::Ref<Ship>>& ships);

	private:
		static bool Working(const cs::Ref<Ship>& s);
	};
}
