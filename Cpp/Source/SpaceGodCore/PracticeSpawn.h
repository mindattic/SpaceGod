#pragma once
// Port of Unity.SpaceGod Core/PracticeSpawn.cs (US-T8).

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	class Ship;

	/// <summary>Something a practice fighter must not be spawned inside: a sphere, heliocentric km.</summary>
	struct SpawnObstacle
	{
		DVec3 CentreKm;
		double RadiusKm = 0;

		SpawnObstacle() = default;
		SpawnObstacle(DVec3 centreKm, double radiusKm);
	};

	/// <summary>
	/// The practice fighter (U.SPC-T601): Shift+V (or the button on the Spawn &amp; Pilot screen) puts a home-side fighter
	/// at the camera's spot, wherever it is, and the player at its helm, to fly alongside the hulls there and get the feel
	/// of their scale and of the flight. One per system: pressing again moves the same hull (repaired, refuelled) to the new
	/// spot rather than adding another, so practice never fills the fleet, or a save, with spare fighters; only one lost,
	/// taken, or still in a fight is replaced by a new one. This is the placement: the spot nudged clear of every hull,
	/// station, wreck and world near it, so the fighter never starts inside anything.
	/// </summary>
	class PracticeSpawn
	{
	public:
		/// <summary>What a practice fighter is called (after its faction's prefix): how one is found again, a save later.</summary>
		static inline const cs::String Name = "Practice Fighter";

		/// <summary>Whether <paramref name="ship"/> is a practice fighter.</summary>
		static bool Is(const cs::Ref<Ship>& ship);

		/// <summary>The clear water kept between the fighter's hull and anything beside it, km: a few hull lengths, so
		/// it starts right alongside without touching.</summary>
		static double GapKm(double selfLengthKm);

	private:
		/// <summary>Most nudges tried before settling: enough for a spot crowded by a fleet in formation.</summary>
		static constexpr int MaxNudges = 64;

	public:
		/// <summary>
		/// Where a fighter <paramref name="selfLengthKm"/> long goes, as near <paramref name="atKm"/> as it can: left
		/// where it is if nothing is within reach, else pushed straight out from whatever it would touch (sideways off
		/// <paramref name="forward"/> if it sits dead on something's centre) to just clear it, again until nothing is
		/// touched. Every position is heliocentric km.
		/// </summary>
		static DVec3 Clear(DVec3 atKm, DVec3 forward, double selfLengthKm, const cs::List<SpawnObstacle>& obstacles);

	private:
		/// <summary>A direction square to <paramref name="forward"/>: the way out from dead centre.</summary>
		static DVec3 Side(DVec3 forward);
	};
}
