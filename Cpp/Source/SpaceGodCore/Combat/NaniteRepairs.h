#pragma once
// US-T102: nanite repair systems, a hardpoint module that rebuilds a damaged hull over time.

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class BattleUnit;
	class RangeSetting;
	class ShipCombat;

	/// <summary>
	/// A nanite repair system (US-T102, <see cref="ModuleKind.NaniteRepair"/>) is a hardpoint module: it goes on a shield
	/// mount in place of a shield array, so a hull trades the field over that patch for a swarm that rebuilds it. In a
	/// fight every standing system (<see cref="SubsystemKind.NaniteRepair"/>) works by its own health: it knits the hull's
	/// points back up to <see cref="HullCap"/> of whole, rebuilds every damaged section of the hull's structure that is
	/// still on it (a keel section or sponson, and the joints still holding them, see <see cref="ModularHull"/>) and mends
	/// the hardpoints still standing, the worst first. A section shattered or come away, a hardpoint shot out and the
	/// system itself once shot out are beyond it (a capital's damage control teams bring a shot-out system back, as they
	/// do an emitter). An EMP or anything else that stuns the hull stops the swarm while it lasts. Gunners know it and go
	/// for the system on a hurt hull. Pure arithmetic on the battle step, so co-op and the Bench stay deterministic; no
	/// stock design carries one and AI yards never fit one, so AI-vs-AI fights only change where a player fitted it.
	/// </summary>
	// [SettingsSource]
	class SPACEGODCORE_API NaniteRepairs final
	{
	public:
		static void RunClassConstructor();

		/// <summary>How fast nanite systems work: 0 leaves them inert, 1 as designed.</summary>
		static const cs::Ref<RangeSetting>& Rate();

		/// <summary>How whole nanites can rebuild a hull's points; past that it takes a yard.</summary>
		static const cs::Ref<RangeSetting>& HullCap();

		/// <summary>The share of the hull's points, of each section's structure and of each mount's hit points one whole
		/// system rebuilds a second, at <see cref="Rate"/> 1.</summary>
		static constexpr double SharePerSecond = 0.004;

		/// <summary>How whole nanites can mend a hardpoint still standing.</summary>
		static constexpr double MountCap = 0.85;

		/// <summary>The live nanite capacity aboard: every standing system's health summed (0 with none).</summary>
		static double Capacity(const cs::Ref<ShipCombat>& c);

		/// <summary>Whether the hull carries a nanite system at all, standing or shot out.</summary>
		static bool Carries(const cs::Ref<ShipCombat>& c);

		/// <summary>One battle step of the hull's nanites: rebuilds its points, its sections and its standing mounts.
		/// Returns the hull points rebuilt.</summary>
		static double Step(const cs::Ref<BattleUnit>& u, double dt);

		/// <summary>The work itself on a hull with <paramref name="capacity"/> of nanites for <paramref name="dt"/> seconds.
		/// Returns the hull points rebuilt.</summary>
		static double Rebuild(const cs::Ref<ShipCombat>& c, double capacity, double dt);
	};
}
