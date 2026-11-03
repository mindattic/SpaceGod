#pragma once

#include "CoreMinimal.h"

/**
 * The Moon's geocentric position from the principal terms of ELP-2000/82 (Meeus, Astronomical Algorithms ch. 47),
 * turned from the ecliptic of date to J2000. A Kepler ellipse cannot follow the Moon: evection and variation swing it
 * by up to 2 degrees. Port of Unity.SpaceGod Core/LunarTheory.cs.
 */
namespace LunarTheory
{
	/** Geocentric position of the Moon (km, J2000 ecliptic) at a Julian date. */
	FVector Position(double Jd);
}
