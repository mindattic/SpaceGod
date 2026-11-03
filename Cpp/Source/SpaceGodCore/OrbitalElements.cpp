// Port of Unity.SpaceGod Core/OrbitalElements.cs (see docs/MIGRATION.md).

#include "OrbitalElements.h"

namespace SpaceGod::Core
{
	cs::Ref<OrbitalElements> OrbitalElements::FromJpl(
		double aAu, double e, double iDeg, double lDeg, double varpiDeg, double nodeDeg,
		double aDotAu, double eDot, double iDot, double lDot, double varpiDot, double nodeDot)
	{
		// The constructor is private (as in C#), so not cs::New.
		cs::Ref<OrbitalElements> r(new OrbitalElements());
		r->EpochJd = Astro::J2000;
		r->AKm = aAu * Astro::AuKm;
		r->ADotKmPerCentury = aDotAu * Astro::AuKm;
		r->E = e;
		r->EDot = eDot;
		r->IDeg = iDeg;
		r->IDot = iDot;
		r->LDeg = lDeg;
		r->LDotDegPerCentury = lDot;
		r->VarpiDeg = varpiDeg;
		r->VarpiDot = varpiDot;
		r->NodeDeg = nodeDeg;
		r->NodeDot = nodeDot;
		return r;
	}

	cs::Ref<OrbitalElements> OrbitalElements::FromClassical(
		double epochJd, double aKm, double e, double iDeg, double nodeDeg, double argPeriDeg,
		double meanAnomalyDeg, double periodDays,
		double nodeRateDegPerDay, double varpiRateDegPerDay, double meanLongitudeAccelDegPerYear2)
	{
		double varpi = nodeDeg + argPeriDeg;
		cs::Ref<OrbitalElements> r(new OrbitalElements());
		r->LDotDotDegPerCentury2 = meanLongitudeAccelDegPerYear2 * 1e4;
		r->EpochJd = epochJd;
		r->AKm = aKm;
		r->E = e;
		r->IDeg = iDeg;
		r->LDeg = meanAnomalyDeg + varpi;
		r->LDotDegPerCentury = 360.0 / periodDays * Astro::DaysPerCentury;
		r->VarpiDeg = varpi;
		r->VarpiDot = varpiRateDegPerDay * Astro::DaysPerCentury;
		r->NodeDeg = nodeDeg;
		r->NodeDot = nodeRateDegPerDay * Astro::DaysPerCentury;
		return r;
	}

	cs::Ref<OrbitalElements> OrbitalElements::FromVectors(double epochJd, double aKm, double e, DVec3 periapsisDirection, DVec3 normal,
		double meanAnomalyDeg, double periodDays)
	{
		auto h = normal.Normalized();
		auto p = (periapsisDirection - h * DVec3::Dot(periapsisDirection, h)).Normalized();
		double i = cs::Math::Acos(cs::Math::Max(-1.0, cs::Math::Min(1.0, h.Z))) * Astro::Rad2Deg;
		auto n = DVec3::Cross(DVec3::UnitZ, h);
		n = n.LengthSquared() < 1e-12 ? DVec3::UnitX : n.Normalized();
		double node = cs::Math::Atan2(n.Y, n.X) * Astro::Rad2Deg;
		double argPeri = cs::Math::Atan2(DVec3::Dot(DVec3::Cross(n, p), h), DVec3::Dot(n, p)) * Astro::Rad2Deg;
		return FromClassical(epochJd, aKm, e, i, node, argPeri, meanAnomalyDeg, periodDays);
	}

	void OrbitalElements::VisualBinaryGeometry(double raDeg, double decDeg, double iDeg, double nodeDeg, double argPeriDeg,
		DVec3& periapsisDirection, DVec3& normal)
	{
		double ra = raDeg * Astro::Deg2Rad, dec = decDeg * Astro::Deg2Rad;
		auto north = DVec3(-cs::Math::Sin(dec) * cs::Math::Cos(ra), -cs::Math::Sin(dec) * cs::Math::Sin(ra), cs::Math::Cos(dec));
		auto east = DVec3(-cs::Math::Sin(ra), cs::Math::Cos(ra), 0);
		auto away = DVec3(cs::Math::Cos(dec) * cs::Math::Cos(ra), cs::Math::Cos(dec) * cs::Math::Sin(ra), cs::Math::Sin(dec));
		double i = iDeg * Astro::Deg2Rad, node = nodeDeg * Astro::Deg2Rad, w = argPeriDeg * Astro::Deg2Rad;
		auto Sky = [&](double u)
		{
			return north * (cs::Math::Cos(u) * cs::Math::Cos(node) - cs::Math::Sin(u) * cs::Math::Sin(node) * cs::Math::Cos(i))
				+ east * (cs::Math::Cos(u) * cs::Math::Sin(node) + cs::Math::Sin(u) * cs::Math::Cos(node) * cs::Math::Cos(i))
				+ away * (cs::Math::Sin(u) * cs::Math::Sin(i));
		};
		auto p = Astro::EquatorialToEcliptic(Sky(w));
		auto q = Astro::EquatorialToEcliptic(Sky(w + cs::Math::PI / 2));
		periapsisDirection = p.Normalized();
		normal = DVec3::Cross(p, q).Normalized();
	}

	KeplerState OrbitalElements::At(double jd) const
	{
		double t = (jd - EpochJd) / Astro::DaysPerCentury;
		double a = AKm + ADotKmPerCentury * t;
		double e = cs::Math::Max(0.0, cs::Math::Min(0.99, E + EDot * t));
		double i = (IDeg + IDot * t) * Astro::Deg2Rad;
		double l = LDeg + LDotDegPerCentury * t + LDotDotDegPerCentury2 * t * t;
		double varpi = VarpiDeg + VarpiDot * t;
		double node = NodeDeg + NodeDot * t;
		double m = Astro::WrapRadians((l - varpi) * Astro::Deg2Rad);
		// Mean motion is dM/dt = dL/dt - d(varpi)/dt, not the mean-longitude rate alone.
		double n = (LDotDegPerCentury + 2 * LDotDotDegPerCentury2 * t - VarpiDot) / Astro::DaysPerCentury * Astro::Deg2Rad;
		return KeplerState(a, e, i, node * Astro::Deg2Rad, (varpi - node) * Astro::Deg2Rad, m, n);
	}
}
