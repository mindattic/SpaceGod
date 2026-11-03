// Port of Unity.SpaceGod Core/Astro.cs (see docs/MIGRATION.md).

#include "Astro.h"

#include <cmath>

namespace SpaceGod::Core
{
	const cs::DateTime& Astro::J2000Utc()
	{
		static const cs::DateTime value(2000, 1, 1, 12, 0, 0, cs::DateTimeKind::Utc);
		return value;
	}

	double Astro::JulianDate(const cs::DateTime& utc)
	{
		return J2000 + (utc.ToUniversalTime() - J2000Utc()).TotalDays();
	}

	cs::DateTime Astro::ToDateTime(double jd)
	{
		double days = jd - J2000;
		// DateTime spans years 1-9999; clamp so absurd time warps cannot throw.
		double maxDays = (cs::DateTime::MaxValue - J2000Utc()).TotalDays() - 1;
		double minDays = (cs::DateTime::MinValue - J2000Utc()).TotalDays() + 1;
		days = cs::Math::Max(minDays, cs::Math::Min(maxDays, days));
		return J2000Utc().AddDays(days);
	}

	double Astro::WrapRadians(double a)
	{
		a = std::fmod(a, TwoPi); // C# double % is fmod
		if (a > cs::Math::PI) a -= TwoPi;
		else if (a < -cs::Math::PI) a += TwoPi;
		return a;
	}

	double Astro::WrapDegrees360(double a)
	{
		a = std::fmod(a, 360.0);
		return a < 0 ? a + 360.0 : a;
	}

	DVec3 Astro::PoleFromRaDec(double raDeg, double decDeg)
	{
		double ra = raDeg * Deg2Rad, dec = decDeg * Deg2Rad, eps = ObliquityDeg * Deg2Rad;
		double x = cs::Math::Cos(dec) * cs::Math::Cos(ra);
		double y = cs::Math::Cos(dec) * cs::Math::Sin(ra);
		double z = cs::Math::Sin(dec);
		return DVec3(x, y * cs::Math::Cos(eps) + z * cs::Math::Sin(eps), -y * cs::Math::Sin(eps) + z * cs::Math::Cos(eps)).Normalized();
	}

	DVec3 Astro::EquatorialToEcliptic(const DVec3& v)
	{
		double eps = ObliquityDeg * Deg2Rad;
		return DVec3(v.X, v.Y * cs::Math::Cos(eps) + v.Z * cs::Math::Sin(eps), -v.Y * cs::Math::Sin(eps) + v.Z * cs::Math::Cos(eps));
	}

	DVec3 Astro::EclipticToEquatorial(const DVec3& v)
	{
		double eps = ObliquityDeg * Deg2Rad;
		return DVec3(v.X, v.Y * cs::Math::Cos(eps) - v.Z * cs::Math::Sin(eps), v.Y * cs::Math::Sin(eps) + v.Z * cs::Math::Cos(eps));
	}

	void Astro::RaDecOf(const DVec3& ecliptic, double& raDeg, double& decDeg)
	{
		auto q = EclipticToEquatorial(ecliptic).Normalized();
		raDeg = WrapDegrees360(cs::Math::Atan2(q.Y, q.X) * Rad2Deg);
		decDeg = cs::Math::Asin(cs::Math::Max(-1.0, cs::Math::Min(1.0, q.Z))) * Rad2Deg;
	}

	double Astro::SmoothStep(double edge0, double edge1, double x)
	{
		double t = cs::Math::Max(0.0, cs::Math::Min(1.0, (x - edge0) / (edge1 - edge0)));
		return t * t * (3 - 2 * t);
	}
}
