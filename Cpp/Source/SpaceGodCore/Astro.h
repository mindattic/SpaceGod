#pragma once
// Port of Unity.SpaceGod Core/Astro.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	/// <summary>Physical constants and time conversions shared by the whole simulation.</summary>
	class Astro
	{
	public:
		static constexpr double AuKm = 149597870.7;
		static constexpr double SecondsPerDay = 86400.0;
		static constexpr double DaysPerCentury = 36525.0;
		static constexpr double J2000 = 2451545.0;
		static constexpr double Deg2Rad = cs::Math::PI / 180.0;
		static constexpr double Rad2Deg = 180.0 / cs::Math::PI;
		static constexpr double TwoPi = cs::Math::PI * 2.0;

		/// <summary>Standard gravity in km/day², the unit ship drives are specified in (g).</summary>
		static constexpr double GravityKmPerDay2 = 9.80665e-3 * SecondsPerDay * SecondsPerDay;

		/// <summary>Mean obliquity of the ecliptic at J2000, used to turn IAU pole RA/Dec into ecliptic vectors.</summary>
		static constexpr double ObliquityDeg = 23.4392911;

		static constexpr double LightSpeedKmPerSec = 299792.458;

		/// <summary>Julian year light-year (IAU), km.</summary>
		static constexpr double LightYearKm = 9460730472580.8;

	private:
		// C#: static readonly DateTime J2000Utc = new DateTime(2000, 1, 1, 12, 0, 0, DateTimeKind.Utc);
		static const cs::DateTime& J2000Utc();

	public:
		/// <summary>Julian date for a UTC instant. UTC vs TT (~69 s) is ignored: it moves Earth ~2000 km.</summary>
		static double JulianDate(const cs::DateTime& utc);

		static cs::DateTime ToDateTime(double jd);

		static double WrapRadians(double a);

		static double WrapDegrees360(double a);

		/// <summary>Unit vector (ecliptic J2000 frame) for an IAU pole given in equatorial RA/Dec.</summary>
		static DVec3 PoleFromRaDec(double raDeg, double decDeg);

		/// <summary>An equatorial J2000 vector turned into the ecliptic J2000 frame.</summary>
		static DVec3 EquatorialToEcliptic(const DVec3& v);

		/// <summary>An ecliptic J2000 vector turned into the equatorial J2000 frame.</summary>
		static DVec3 EclipticToEquatorial(const DVec3& v);

		/// <summary>Unit vector (ecliptic J2000) toward an equatorial right ascension and declination.</summary>
		static DVec3 DirectionFromRaDec(double raDeg, double decDeg) { return PoleFromRaDec(raDeg, decDeg); }

		/// <summary>Right ascension and declination (degrees) of an ecliptic J2000 direction.</summary>
		static void RaDecOf(const DVec3& ecliptic, double& raDeg, double& decDeg);

		/// <summary>Right ascension in hours, minutes and seconds, as degrees.</summary>
		static double Hms(double h, double m, double s) { return (h + m / 60 + s / 3600) * 15; }

		/// <summary>Declination from degrees, arcminutes and arcseconds; <paramref name="south"/> makes it negative.</summary>
		static double Dms(double d, double m, double s, bool south = false)
		{
			return (south ? -1 : 1) * (cs::Math::Abs(d) + m / 60 + s / 3600);
		}

		static double SmoothStep(double edge0, double edge1, double x);
	};
}
