#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/Gunnery.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	class BattleUnit;

	/// <summary>
	/// Aimed fire (U.SPC-T270): the geometry that decides whether a shot actually lands, for the guns the
	/// player points and for every shot fired at the player's hull. A beam is a ray, a round a swept
	/// segment, and either hits only what it really passes through, so a crosshair laid on target hits
	/// and a hull jinking across a gunner's sights makes him miss.
	/// </summary>
	class SPACEGODCORE_API Gunnery
	{
	public:
		/// <summary>How far off the nose a player's gun swings to meet the crosshair, deg; the railgun's
		/// long spinal mount barely moves.</summary>
		static constexpr double GimbalDeg = 12; static constexpr double RailgunGimbalDeg = 5;

		/// <summary>The smallest a hull is to a shot, km: a fighter's own length is a few metres across.</summary>
		static constexpr double MinHitRadiusKm = 0.012;

		/// <summary>Heat a player's guns build per point of damage fired, the share of full heat they shed
		/// each second, and how far they must cool once overheated before they fire again.</summary>
		static constexpr double HeatPerDamage = 0.004; static constexpr double HeatCoolPerS = 0.2; static constexpr double HeatRecoverBelow = 0.35;

		/// <summary>A missile lock builds while the crosshair sits within <see cref="LockConeDeg"/> of the
		/// locked target, over <see cref="LockSeconds"/>; once made it holds while the target stays within
		/// <see cref="LockHoldDeg"/> of the nose.</summary>
		static constexpr double LockConeDeg = 4; static constexpr double LockSeconds = 1.5; static constexpr double LockHoldDeg = 35;

		/// <summary>How long a gunner takes to read a change in a target's course, seconds (see
		/// <see cref="BattleUnit.SeenVel"/>): a steady course is tracked, a jink is aimed behind.</summary>
		static constexpr double TrackingLagS = 0.35;

		/// <summary>The scatter of a gun of no accuracy at all, deg (a perfect one has none), and the
		/// scatter of the player's own guns by kind.</summary>
		static constexpr double MaxScatterDeg = 1.5; static constexpr double AutocannonScatterDeg = 0.35; static constexpr double BeamScatterDeg = 0.05;

		/// <summary>How big a unit is to a shot.</summary>
		static double HitRadius(const cs::Ref<BattleUnit>& u);

		/// <summary>
		/// How far along the ray from <paramref name="origin"/> down unit <paramref name="dir"/> it first
		/// meets the sphere of radius <paramref name="radius"/> about <paramref name="centre"/>, within
		/// <paramref name="maxT"/>; -1 if it never does. A ray starting inside the sphere meets it at once.
		/// </summary>
		static double RaySphere(DVec3 origin, DVec3 dir, double maxT, DVec3 centre, double radius);

		/// <summary>
		/// When a shot at <paramref name="speed"/> (km/s, relative to the shooter) meets a target now at
		/// <paramref name="relPos"/> moving at <paramref name="relVel"/> (both relative to the shooter),
		/// seconds; NaN if it can never catch it.
		/// </summary>
		static double InterceptTime(DVec3 relPos, DVec3 relVel, double speed);

		/// <summary>
		/// Where to aim so a shot at <paramref name="speed"/> from a shooter at <paramref name="shooterPos"/>
		/// moving at <paramref name="shooterVel"/> meets a target at <paramref name="targetPos"/> moving at
		/// <paramref name="targetVel"/>: the lead pip. The target itself when no shot can catch it.
		/// </summary>
		static DVec3 LeadPoint(DVec3 shooterPos, DVec3 shooterVel, DVec3 targetPos, DVec3 targetVel, double speed);

		/// <summary>The crosshair <paramref name="aim"/>, or as near it as a gun on the nose
		/// <paramref name="forward"/> can swing within <paramref name="gimbalDeg"/>.</summary>
		static DVec3 Gimbal(DVec3 aim, DVec3 forward, double gimbalDeg);

		/// <summary><paramref name="dir"/> (unit) knocked off line by a random angle of about
		/// <paramref name="sigmaRad"/>, in a random direction across it.</summary>
		static DVec3 Scatter(DVec3 dir, double sigmaRad, cs::Random& rng);
	};
}
