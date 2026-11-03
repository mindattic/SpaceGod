#pragma once
// Port of Unity.SpaceGod Core/Kepler.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "Astro.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	/// <summary>Instantaneous classical elements: angles in radians, a in km, mean motion in rad/day.</summary>
	struct KeplerState
	{
		double A = 0;
		double E = 0;
		double I = 0;
		double Node = 0;
		double ArgPeri = 0;
		double MeanAnomaly = 0;
		double MeanMotion = 0;

		KeplerState() = default;
		KeplerState(double a, double e, double i, double node, double argPeri, double meanAnomaly, double meanMotion)
		{
			A = a;
			E = e;
			I = i;
			Node = node;
			ArgPeri = argPeri;
			MeanAnomaly = meanAnomaly;
			MeanMotion = meanMotion;
		}

		KeplerState WithMeanAnomaly(double m) const { return KeplerState(A, E, I, Node, ArgPeri, m, MeanMotion); }

		double PeriodDays() const { return Astro::TwoPi / cs::Math::Abs(MeanMotion); }
	};

	/// <summary>Two-body (Keplerian) propagation: Kepler's equation and the perifocal-to-reference rotation.</summary>
	class SPACEGODCORE_API Kepler
	{
	public:
		/// <summary>Solves M = E - e·sin(E) for E by Newton-Raphson; robust up to e≈0.99.</summary>
		static double SolveEccentricAnomaly(double meanAnomaly, double e);

		/// <summary>Position (km) in the elements' reference plane (x toward the reference direction, z toward its pole).</summary>
		static DVec3 Position(const KeplerState& k);

		/// <summary>Velocity (km/day) in the same frame as <see cref="Position"/>.</summary>
		static DVec3 Velocity(const KeplerState& k);

	private:
		static DVec3 Rotate(const KeplerState& k, double xp, double yp);

	public:
		/// <summary>Circular-orbit angular rate (rad/day) at radius r around a body with gravitational parameter gm (km³/s²).</summary>
		static double CircularRate(double gm, double radiusKm)
		{
			return cs::Math::Sqrt(gm / (radiusKm * radiusKm * radiusKm)) * Astro::SecondsPerDay;
		}

		/// <summary>Vis-viva speed in km/s.</summary>
		static double VisVivaSpeed(double gm, double rKm, double aKm)
		{
			return cs::Math::Sqrt(cs::Math::Max(0.0, gm * (2.0 / rKm - 1.0 / aKm)));
		}
	};
}
