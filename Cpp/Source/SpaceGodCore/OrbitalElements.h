#pragma once
// Port of Unity.SpaceGod Core/OrbitalElements.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Kepler.h"

namespace SpaceGod::Core
{
	/// <summary>
	/// Keplerian elements with linear secular rates, in the layout of JPL's "Keplerian Elements
	/// for Approximate Positions of the Major Planets" (Standish): a, e, i, mean longitude L,
	/// longitude of perihelion ϖ, longitude of ascending node Ω, each with a per-century rate.
	/// Every body uses this one shape; moons and minor planets are converted into it.
	/// </summary>
	class SPACEGODCORE_API OrbitalElements final
	{
	public:
		double EpochJd = 0;
		double AKm = 0;
		double ADotKmPerCentury = 0;
		double E = 0;
		double EDot = 0;
		double IDeg = 0;
		double IDot = 0;
		double LDeg = 0;
		double LDotDegPerCentury = 0;
		double VarpiDeg = 0;
		double VarpiDot = 0;
		double NodeDeg = 0;
		double NodeDot = 0;

		/// <summary>
		/// Secular acceleration of the mean longitude: the coefficient of t² (degrees per century²).
		/// Zero except for Phobos, whose orbit is decaying under Mars's tides.
		/// </summary>
		double LDotDotDegPerCentury2 = 0;

	private:
		OrbitalElements() {}

	public:
		/// <summary>JPL planet table entry (J2000 epoch, a in AU, rates per Julian century).</summary>
		static cs::Ref<OrbitalElements> FromJpl(
			double aAu, double e, double iDeg, double lDeg, double varpiDeg, double nodeDeg,
			double aDotAu, double eDot, double iDot, double lDot, double varpiDot, double nodeDot);

		/// <summary>
		/// Classical elements at an epoch (SBDB / satellite tables), with optional nodal and
		/// apsidal precession in degrees per day.
		/// </summary>
		static cs::Ref<OrbitalElements> FromClassical(
			double epochJd, double aKm, double e, double iDeg, double nodeDeg, double argPeriDeg,
			double meanAnomalyDeg, double periodDays,
			double nodeRateDegPerDay = 0, double varpiRateDegPerDay = 0, double meanLongitudeAccelDegPerYear2 = 0);

		/// <summary>
		/// An orbit given by its geometry in the ecliptic frame: the direction of periapsis and the
		/// orbit's normal (the right-hand pole of its motion). Used where the real orbit is measured
		/// in another frame (a visual binary on the sky) or only its shape is known (exoplanets).
		/// </summary>
		static cs::Ref<OrbitalElements> FromVectors(double epochJd, double aKm, double e, DVec3 periapsisDirection, DVec3 normal,
			double meanAnomalyDeg, double periodDays);

		/// <summary>
		/// The periapsis direction and orbit normal (ecliptic J2000) of a visual binary's relative orbit
		/// (Campbell elements: inclination, position angle of the node, argument of periastron), for a
		/// pair at the given right ascension and declination. The sky is spanned by north, east and the
		/// line of sight away from us; which of the two nodes is ascending cannot be told from images
		/// alone, so the convention that the companion recedes past the given node is used.
		/// </summary>
		static void VisualBinaryGeometry(double raDeg, double decDeg, double iDeg, double nodeDeg, double argPeriDeg,
			DVec3& periapsisDirection, DVec3& normal);

		/// <summary>Kepler's third law: period in days for semi-major axis a (km) around gm (km³/s²).</summary>
		static double PeriodFromGm(double aKm, double gm)
		{
			return Astro::TwoPi * cs::Math::Sqrt(aKm * aKm * aKm / gm) / Astro::SecondsPerDay;
		}

		KeplerState At(double jd) const;
	};
}
