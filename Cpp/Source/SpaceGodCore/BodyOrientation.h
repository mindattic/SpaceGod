#pragma once
// Port of Unity.SpaceGod Core/BodyOrientation.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	class BodyDefinition;

	/// <summary>
	/// Body-fixed frames, so colour maps face the right way: the map's north pole and the direction
	/// of its 0° meridian on the equator, both as ecliptic J2000 unit vectors.
	///
	/// Bodies with an IAU prime meridian (planets, Ceres, Pluto) use W = W0 + dW/dt·d (Archinal et
	/// al. 2018, IAU WGCCRE; periodic terms dropped), measured from the node Q of the body's equator
	/// on the ICRF equator, at right ascension α0 + 90°. Synchronous moons point their 0° meridian
	/// at the parent, which is how the IAU defines it for them (mean sub-planet longitude 0°).
	///
	/// Maps are east-positive: with the map's north up, longitude increases to the right, which is
	/// counter-clockwise seen from above the north pole.
	/// </summary>
	class SPACEGODCORE_API BodyOrientation
	{
	public:
		/// <summary>The map's north pole (ecliptic unit vector).</summary>
		static DVec3 MapNorth(const cs::Ref<BodyDefinition>& body);

		/// <summary>Prime meridian rate about the map's north pole, deg/day (prograde rotators are positive about their north).</summary>
		static double RateDegPerDay(const cs::Ref<BodyDefinition>& body);

		/// <summary>
		/// The map's north pole and its 0°-longitude direction at <paramref name="jd"/>.
		/// <paramref name="towardParent"/> is the unit vector from the body to its parent, used by synchronous moons.
		/// </summary>
		static void Frame(const cs::Ref<BodyDefinition>& body, double jd, DVec3 towardParent, DVec3& north, DVec3& primeMeridian);

		/// <summary>
		/// As above. A synchronous moon with no IAU pole of its own spins about its orbit normal
		/// (<paramref name="orbitNormal"/>, r × v relative to the parent): its spin axis follows the orbit as
		/// it precesses, which matters for Triton, inclined 157° to Neptune's equator.
		/// </summary>
		static void Frame(const cs::Ref<BodyDefinition>& body, double jd, DVec3 towardParent, DVec3 orbitNormal, DVec3& north, DVec3& primeMeridian);

		/// <summary>Ascending node Q of the equator of a pole on the ICRF equator (RA = α0 + 90°), as an ecliptic vector.</summary>
		static DVec3 EquatorNode(DVec3 northEcliptic);

		/// <summary>Planetocentric east longitude (deg, -180..180) and latitude of a body-fixed direction.</summary>
		static void LonLat(DVec3 north, DVec3 primeMeridian, DVec3 direction, double& lonDeg, double& latDeg);

	private:
		static DVec3 EclipticToEquatorial(DVec3 v);

		static DVec3 EquatorialToEcliptic(DVec3 v);
	};
}
