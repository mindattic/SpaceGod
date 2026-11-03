#pragma once

#include "CoreMinimal.h"
#include "Sim/BodyCatalog.h"

/**
 * Body-fixed frames, so colour maps face the right way: the map's north pole and its 0 deg meridian on the equator,
 * as ecliptic J2000 unit vectors. IAU W = W0 + dW/dt * d for bodies with a prime meridian; synchronous moons point
 * their 0 deg meridian at the parent. Maps are east-positive. Port of Unity.SpaceGod Core/BodyOrientation.cs.
 */
namespace BodyOrientation
{
	FVector MapNorth(const FBodyDefinition& Body);

	/** Prime meridian rate about the map's north pole, deg/day. */
	double RateDegPerDay(const FBodyDefinition& Body);

	/**
	 * The map's north pole and its 0 deg longitude direction at Jd. TowardParent is the unit vector from the body to its
	 * parent; OrbitNormal (r x v relative to the parent) gives the spin axis of a synchronous moon with no IAU pole.
	 */
	void Frame(const FBodyDefinition& Body, double Jd, const FVector& TowardParent, const FVector& OrbitNormal,
		FVector& OutNorth, FVector& OutPrimeMeridian);

	/** Ascending node of the equator of a pole on the ICRF equator (RA = alpha0 + 90 deg), as an ecliptic vector. */
	FVector EquatorNode(const FVector& NorthEcliptic);
}
