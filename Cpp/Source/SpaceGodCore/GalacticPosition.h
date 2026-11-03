#pragma once
// Port of Unity.SpaceGod Core/GalacticPosition.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "Astro.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	/// <summary>
	/// A place anywhere in the galaxy, held to the same precision everywhere (U.SPC-T412): a whole-number sector
	/// (a cube <see cref="SectorKm"/> on a side, J2000 ecliptic axes, Sol's root at the centre of sector 0,0,0) and an
	/// offset in km from that sector's centre, never more than half a sector on any axis.
	///
	/// A single double loses its grip with distance: a light-year is 9.5e12 km, so a point 50,000 ly out held as one
	/// double of km is only good to about a hundred km, and a hull parked there would jitter and collide wrongly. Split
	/// this way the offset never exceeds half a sector, so it holds to a tenth of a millimetre whether the sector is
	/// next to Sol or across the galaxy, and the sector index (a long) is exact. Differences are taken sector by sector
	/// first (<see cref="KmFrom"/>), so two places near each other far from Sol still come out exact relative to each
	/// other: that is what a floating origin needs.
	///
	/// Inside a star system everything is still kept in that system's own doubles (km from its root, as its
	/// ephemeris and fleet have always had them); <see cref="StarSystem.Origin"/> is where that frame sits in the
	/// galaxy, and <see cref="StarSystem.At"/> / <see cref="StarSystem.LocalKm"/> convert between the two levels.
	/// </summary>
	struct GalacticPosition
	{
		/// <summary>The edge of one sector, km (a billion km, about 6.7 AU): small enough that the offset inside it holds to
		/// a tenth of a millimetre, large enough that a star system's inner worlds share a handful of sectors.</summary>
		static constexpr double SectorKm = 1e9;

		/// <summary>Sectors in a light-year (not a whole number: a light-year is just a length here).</summary>
		static constexpr double SectorsPerLy = Astro::LightYearKm / SectorKm;

		long long X = 0, Y = 0, Z = 0;

		/// <summary>Offset from the centre of sector (<see cref="X"/>, <see cref="Y"/>, <see cref="Z"/>), km, each axis within half a sector.</summary>
		DVec3 LocalKm;

		GalacticPosition() = default;

		/// <summary>The sector's centre plus <paramref name="localKm"/>, carried into neighbouring sectors as far as it reaches.</summary>
		GalacticPosition(long long x, long long y, long long z, DVec3 localKm);

		static const GalacticPosition Origin;

	private:
		/// <summary>Splits <paramref name="km"/> into whole sectors and what is left over, the remainder within half a sector.</summary>
		static void Carry(double km, long long& sectors, double& rest);

	public:
		/// <summary>The place <paramref name="ly"/> light-years from Sol's root (a catalogue star's position).</summary>
		static GalacticPosition FromLy(DVec3 ly);

		/// <summary>This place moved by <paramref name="km"/>.</summary>
		GalacticPosition Offset(DVec3 km) const { return GalacticPosition(X, Y, Z, LocalKm + km); }

		/// <summary>
		/// Where this place is from <paramref name="origin"/>, km: the sector difference (exact, in whole sectors) and the
		/// offsets' difference, added only at the end. Exact to well under a metre for any two places within a few
		/// light-years of each other, wherever in the galaxy they are.
		/// </summary>
		DVec3 KmFrom(const GalacticPosition& origin) const
		{
			return DVec3((X - origin.X) * SectorKm, (Y - origin.Y) * SectorKm, (Z - origin.Z) * SectorKm) + (LocalKm - origin.LocalKm);
		}

		/// <summary>Distance to <paramref name="other"/>, km.</summary>
		double DistanceKm(const GalacticPosition& other) const { return KmFrom(other).Length(); }

		/// <summary>Distance to <paramref name="other"/>, light-years.</summary>
		double DistanceLy(const GalacticPosition& other) const { return DistanceKm(other) / Astro::LightYearKm; }

		/// <summary>Light-years from Sol's root: for maps and the interstellar view, which draw at light-year scale.</summary>
		DVec3 Ly() const
		{
			return DVec3(
				(X + LocalKm.X / SectorKm) / SectorsPerLy,
				(Y + LocalKm.Y / SectorKm) / SectorsPerLy,
				(Z + LocalKm.Z / SectorKm) / SectorsPerLy);
		}

		/// <summary>The place a share <paramref name="t"/> of the way from <paramref name="a"/> to <paramref name="b"/>.</summary>
		static GalacticPosition Lerp(const GalacticPosition& a, const GalacticPosition& b, double t) { return a.Offset(b.KmFrom(a) * t); }

		bool Equals(const GalacticPosition& o) const { return X == o.X && Y == o.Y && Z == o.Z && LocalKm.Equals(o.LocalKm); }

		size_t HashCode() const;

		bool operator==(const GalacticPosition& b) const { return Equals(b); }
		bool operator!=(const GalacticPosition& b) const { return !Equals(b); }

		cs::String ToString() const;
	};

	inline const GalacticPosition GalacticPosition::Origin = GalacticPosition(0, 0, 0, DVec3::Zero);
}
