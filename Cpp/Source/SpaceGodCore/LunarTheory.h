#pragma once
// Port of Unity.SpaceGod Core/LunarTheory.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	/// <summary>
	/// The Moon's geocentric position from the principal terms of the ELP-2000/82 lunar theory, as
	/// tabulated by Meeus (Astronomical Algorithms, 2nd ed., ch. 47, tables 47.A and 47.B), turned
	/// from the mean ecliptic and equinox of date to J2000. A Kepler ellipse cannot follow the Moon:
	/// the Sun's pull swings it up to 1.3° ahead or behind (evection) and 0.7° more (variation).
	/// With these terms it stays within a few hundredths of a degree of JPL Horizons.
	/// </summary>
	class SPACEGODCORE_API LunarTheory
	{
	public:
		// The coefficient tables (LrArgs, Lcoef, Rcoef, BArgs, Bcoef) are private constant arrays in LunarTheory.cpp.

		/// <summary>Geocentric position of the Moon (km, J2000 ecliptic) at a Julian date (TT ≈ TDB).</summary>
		static DVec3 Position(double jd);
	};
}
