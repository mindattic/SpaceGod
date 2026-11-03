#pragma once

#include "CoreMinimal.h"
#include <cmath>
#include <limits>

/** Physical constants and time conversions shared by the whole simulation (port of Unity.SpaceGod Core/Astro.cs). */
namespace Astro
{
	constexpr double AuKm = 149597870.7;
	constexpr double SecondsPerDay = 86400.0;
	constexpr double DaysPerCentury = 36525.0;
	constexpr double J2000 = 2451545.0;
	constexpr double Pi = 3.14159265358979323846;
	constexpr double TwoPi = Pi * 2.0;
	constexpr double Deg2Rad = Pi / 180.0;
	constexpr double Rad2Deg = 180.0 / Pi;
	constexpr double LightSpeedKmPerSec = 299792.458;

	/** Mean obliquity of the ecliptic at J2000, used to turn IAU pole RA/Dec into ecliptic vectors. */
	constexpr double ObliquityDeg = 23.4392911;

	inline double NaN() { return std::numeric_limits<double>::quiet_NaN(); }

	inline FDateTime J2000Utc() { return FDateTime(2000, 1, 1, 12, 0, 0); }

	/** Julian date for a UTC instant. UTC vs TT (~69 s) is ignored: it moves Earth ~2000 km. */
	inline double JulianDate(const FDateTime& Utc) { return J2000 + (Utc - J2000Utc()).GetTotalDays(); }

	inline FDateTime ToDateTime(double Jd)
	{
		// FDateTime spans years 1-9999; clamp so absurd time warps cannot overflow.
		const double MaxDays = (FDateTime::MaxValue() - J2000Utc()).GetTotalDays() - 1;
		const double MinDays = (FDateTime::MinValue() - J2000Utc()).GetTotalDays() + 1;
		const double Days = FMath::Clamp(Jd - J2000, MinDays, MaxDays);
		return J2000Utc() + FTimespan::FromDays(Days);
	}

	inline double WrapRadians(double A)
	{
		A = std::fmod(A, TwoPi);
		if (A > Pi) A -= TwoPi;
		else if (A < -Pi) A += TwoPi;
		return A;
	}

	inline double WrapDegrees360(double A)
	{
		A = std::fmod(A, 360.0);
		return A < 0 ? A + 360.0 : A;
	}

	/** An equatorial J2000 vector turned into the ecliptic J2000 frame. */
	inline FVector EquatorialToEcliptic(const FVector& V)
	{
		const double Eps = ObliquityDeg * Deg2Rad;
		return FVector(V.X, V.Y * std::cos(Eps) + V.Z * std::sin(Eps), -V.Y * std::sin(Eps) + V.Z * std::cos(Eps));
	}

	/** An ecliptic J2000 vector turned into the equatorial J2000 frame. */
	inline FVector EclipticToEquatorial(const FVector& V)
	{
		const double Eps = ObliquityDeg * Deg2Rad;
		return FVector(V.X, V.Y * std::cos(Eps) - V.Z * std::sin(Eps), V.Y * std::sin(Eps) + V.Z * std::cos(Eps));
	}

	/** Unit vector (ecliptic J2000 frame) for an IAU pole given in equatorial RA/Dec. */
	inline FVector PoleFromRaDec(double RaDeg, double DecDeg)
	{
		const double Ra = RaDeg * Deg2Rad, Dec = DecDeg * Deg2Rad;
		return EquatorialToEcliptic(FVector(std::cos(Dec) * std::cos(Ra), std::cos(Dec) * std::sin(Ra), std::sin(Dec))).GetSafeNormal();
	}

	inline double SmoothStep(double Edge0, double Edge1, double X)
	{
		const double T = FMath::Clamp((X - Edge0) / (Edge1 - Edge0), 0.0, 1.0);
		return T * T * (3 - 2 * T);
	}
}
