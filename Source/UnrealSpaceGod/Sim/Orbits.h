#pragma once

#include "CoreMinimal.h"

/** Instantaneous classical elements: angles in radians, a in km, mean motion in rad/day. */
struct FKeplerState
{
	double A = 0, E = 0, I = 0, Node = 0, ArgPeri = 0, MeanAnomaly = 0, MeanMotion = 0;

	FKeplerState WithMeanAnomaly(double M) const
	{
		FKeplerState K = *this;
		K.MeanAnomaly = M;
		return K;
	}
};

/**
 * Keplerian elements with linear secular rates, in the layout of JPL's "Keplerian Elements for Approximate
 * Positions of the Major Planets" (Standish): a, e, i, mean longitude L, longitude of perihelion varpi,
 * longitude of ascending node, each with a per-century rate. Moons and minor planets are converted into it.
 * Port of Unity.SpaceGod Core/OrbitalElements.cs.
 */
struct FOrbitalElements
{
	double EpochJd = 0;
	double AKm = 0, ADotKmPerCentury = 0;
	double E = 0, EDot = 0;
	double IDeg = 0, IDot = 0;
	double LDeg = 0, LDotDegPerCentury = 0;
	double VarpiDeg = 0, VarpiDot = 0;
	double NodeDeg = 0, NodeDot = 0;

	/** Secular acceleration of the mean longitude (deg/century^2): Phobos, spiralling in under Mars's tides. */
	double LDotDotDegPerCentury2 = 0;

	/** JPL planet table entry (J2000 epoch, a in AU, rates per Julian century). */
	static FOrbitalElements FromJpl(double AAu, double E, double IDeg, double LDeg, double VarpiDeg, double NodeDeg,
		double ADotAu, double EDot, double IDot, double LDot, double VarpiDot, double NodeDot);

	/** Classical elements at an epoch (SBDB / satellite tables), with optional nodal and apsidal precession in deg/day. */
	static FOrbitalElements FromClassical(double EpochJd, double AKm, double E, double IDeg, double NodeDeg, double ArgPeriDeg,
		double MeanAnomalyDeg, double PeriodDays, double NodeRateDegPerDay = 0, double VarpiRateDegPerDay = 0,
		double MeanLongitudeAccelDegPerYear2 = 0);

	/** Kepler's third law: period in days for semi-major axis a (km) around gm (km^3/s^2). */
	static double PeriodFromGm(double AKm, double Gm);

	FKeplerState At(double Jd) const;
};

/** Two-body propagation: Kepler's equation and the perifocal-to-reference rotation (Core/Kepler.cs). */
namespace Kepler
{
	double SolveEccentricAnomaly(double MeanAnomaly, double E);

	/** Position (km) in the elements' reference plane. */
	FVector Position(const FKeplerState& K);

	/** Velocity (km/day) in the same frame as Position. */
	FVector Velocity(const FKeplerState& K);

	/** Vis-viva speed in km/s. */
	double VisVivaSpeed(double Gm, double RKm, double AKm);
}
