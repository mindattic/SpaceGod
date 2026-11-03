#pragma once
// Port of Assets/UnitySpace/Core/Combat/BreacherBays.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	class RangeSetting;
	class FacilityKind;
	class Loadout;
	class Interior;
	class Ship;
	class ShipCombat;

	/// <summary>
	/// Breacher bays and the Breacher rounds they fire (U.SPC-T466, renamed from the Assault Bay and made a fitting any
	/// ship can carry by U.SPC-T521). A Breacher is a whole team of marines (<see cref="TeamSize"/>) sealed in an armoured
	/// round, fired at high velocity (<see cref="RoundSpeedKmS"/>) from a Breacher Bay: the firing room, a facility
	/// (<see cref="FacilityKinds.BreacherBay"/>) fitted into the internal space of any hull, navy, pirate or merchant escort,
	/// that has the free sections for it (<see cref="FacilityKind.Size"/>), the reactor power to spare for its breech
	/// (<see cref="PowerMw"/>, <see cref="Loadout.FreePowerMw"/>), the crew to man it (<see cref="Crew"/>,
	/// <see cref="Loadout.FreeCrew"/>) and a Marine Habitat to supply the teams (<see cref="FacilityKind.Requires"/>). The
	/// yard's fitting screen shows those costs; navies design it into the hulls built for boarding (the gate breaker) and
	/// fit it as a refit to their big hulls (<see cref="Outfitting.NavyFacilities"/>), raider leaders carry one. A Breacher is only
	/// ever such a round, never a ship (U.SPC-T517): it has no ship class, design, yard entry or crew of its own, and is
	/// drawn in the scene as its own bullet-shaped round model. The round flies like a bullet, with only a slight course
	/// correction (<see cref="SteerKmS2"/>, <see cref="Correct"/>), until it strikes a hull, drills in and lets its team out into the compartment it
	/// hit, where the compartment-by-compartment boarding fight (<see cref="Interior"/>) takes over and the target's own
	/// marines try to stop them. It cannot get through a shield still holding more than a quarter of its strength, and a
	/// round that misses or glances off goes adrift with its team alive inside (U.SPC-T519, <see cref="DriftingRounds"/>). The bay
	/// fires again only as fast as the habitats can ready the next team (<see cref="IntervalSeconds"/>). The lighter option
	/// is still the boarding pods' twelve-marine squads (<see cref="WeaponSpec.PodMarines"/>). Every figure is in the
	/// settings menu's Broadsides group.
	/// </summary>
	// [SettingsSource]
	class SPACEGODCORE_API BreacherBays
	{
		static cs::String Seconds(double v);

	public:
		static const cs::Ref<RangeSetting>& TeamSize();

		static const cs::Ref<RangeSetting>& ReadySeconds();

		static const cs::Ref<RangeSetting>& CycleSeconds();

		static const cs::Ref<RangeSetting>& RoundSpeedKmS();

		static const cs::Ref<RangeSetting>& SteerKmS2();

		static const cs::Ref<RangeSetting>& RangeKm();

		static const cs::Ref<RangeSetting>& ImpactDamage();

		/// <summary>How long a round is, km: a team's casing behind its drill head, what its model is drawn at (U.SPC-T517).</summary>
		static constexpr double RoundLengthKm = 0.06;

		/// <summary>A round's armour against point defence (a pod's is 40).</summary>
		static constexpr double RoundHp = 400;

		/// <summary>How much harder a team storming out of a round fights than a squad off a pod: the shock of the breach.</summary>
		static constexpr double ShockEdge = 1.15;

		/// <summary>A target's shields hold a round off above this share of their strength.</summary>
		static constexpr double ShieldShare = 0.25;

		/// <summary>The reactor power the bay's breech, cradle and magazine hoists draw, MW: a destroyer's reactor or bigger
		/// has it to spare (<see cref="Loadout.ReactorReservePerScale"/>), a corvette's or anything smaller has not.</summary>
		static constexpr double PowerMw = 30;

		/// <summary>The hands that man the bay (the breech crew, loaders and the fire-control watch): a hull with fewer to
		/// spare (<see cref="Loadout.FreeCrew"/>), a freighter's or a gunship's, cannot work one.</summary>
		static constexpr double Crew = 20;

		/// <summary>The facility kind.</summary>
		static cs::Ref<FacilityKind> Kind();

		/// <summary>Breacher bays in <paramref name="s"/>.</summary>
		static int Count(const cs::Ref<Ship>& s);

		/// <summary>Marine habitats in <paramref name="s"/>, which ready its teams.</summary>
		static int Habitats(const cs::Ref<Ship>& s);

		/// <summary>
		/// Seconds between rounds from <paramref name="s"/>'s bays: the slower of its bays' cycle (shared between them) and
		/// its habitats readying the next team (shared between them). Infinite with no bay or no habitat.
		/// </summary>
		static double IntervalSeconds(const cs::Ref<Ship>& s);

		/// <summary>Whether <paramref name="interior"/>'s habitats have a whole team ready to fire.</summary>
		static bool TeamReady(const cs::Ref<Interior>& interior);

		/// <summary>Whether a round would get into <paramref name="c"/>: no shield generator, or its shields down to a quarter.</summary>
		static bool Opens(const cs::Ref<ShipCombat>& c);

		/// <summary>
		/// Whether a hull with loadout <paramref name="l"/> could carry a bay at all, space and habitat aside: a hull its
		/// category and class allow, with the reactor power and the crew to spare for it.
		/// </summary>
		static bool CanCarry(const cs::Ref<Loadout>& l);

		/// <summary>Whether <paramref name="l"/>'s habitats quarter a whole team, so a bay in it could ever fire.</summary>
		static bool CanMan(const cs::Ref<Loadout>& l);

		/// <summary>
		/// Fits a Breacher bay into <paramref name="s"/> as part of its build, first fitting habitats until they quarter a
		/// whole team (a raider destroyer's one habitat is too small), as room allows. False if it could not take the bay
		/// (no room, or not the reactor power or crew to spare for it).
		/// </summary>
		static bool Equip(const cs::Ref<Ship>& s);

		/// <summary>
		/// A round's slight course correction: its velocity <paramref name="vel"/> turned toward <paramref name="toward"/>,
		/// its speed kept, by no more than <paramref name="accelKmS2"/> of sideways push over <paramref name="dt"/>.
		/// </summary>
		static DVec3 Correct(DVec3 vel, DVec3 toward, double accelKmS2, double dt);

		/// <summary>
		/// Where to aim a round fired at <paramref name="speedKmS"/> from <paramref name="from"/> (moving at
		/// <paramref name="fromVel"/>, which the round keeps) to meet a target at <paramref name="at"/> moving at
		/// <paramref name="atVel"/>: the target's place when the round gets there, if both hold their courses.
		/// </summary>
		static DVec3 Lead(DVec3 from, DVec3 fromVel, DVec3 at, DVec3 atVel, double speedKmS);
	};
}
