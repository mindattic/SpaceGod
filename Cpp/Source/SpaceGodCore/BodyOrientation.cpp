// Port of Unity.SpaceGod Core/BodyOrientation.cs (see docs/MIGRATION.md).

#include "BodyOrientation.h"
#include "Astro.h"
#include "BodyDefinition.h"

#include <cmath>

namespace SpaceGod::Core
{
	DVec3 BodyOrientation::MapNorth(const cs::Ref<BodyDefinition>& body) { return body->MapNorthFlipped ? -body->Pole : body->Pole; }

	double BodyOrientation::RateDegPerDay(const cs::Ref<BodyDefinition>& body)
	{
		if (!cs::Double::IsNaN(body->PrimeMeridianRateDegPerDay)) return body->PrimeMeridianRateDegPerDay;
		if (body->RotationHours == 0) return 0;
		double aboutPole = 360.0 * 24.0 / body->RotationHours;
		return body->MapNorthFlipped ? -aboutPole : aboutPole;
	}

	void BodyOrientation::Frame(const cs::Ref<BodyDefinition>& body, double jd, DVec3 towardParent, DVec3& north, DVec3& primeMeridian)
	{
		Frame(body, jd, towardParent, DVec3::Zero, north, primeMeridian);
	}

	void BodyOrientation::Frame(const cs::Ref<BodyDefinition>& body, double jd, DVec3 towardParent, DVec3 orbitNormal, DVec3& north, DVec3& primeMeridian)
	{
		north = MapNorth(body);
		if (body->FacesParent && cs::Double::IsNaN(body->PoleRaDeg) && orbitNormal.LengthSquared() > 0)
		{
			auto n = orbitNormal.Normalized();
			north = DVec3::Dot(n, north) >= 0 ? n : -n;
		}
		if (body->FacesParent && towardParent.LengthSquared() > 0)
		{
			// Sub-parent point, projected onto the equator.
			auto p = towardParent - north * DVec3::Dot(towardParent, north);
			if (p.LengthSquared() > 1e-18)
			{
				primeMeridian = p.Normalized();
				return;
			}
		}

		auto node = EquatorNode(north);
		auto east = DVec3::Cross(north, node);
		double w0 = cs::Double::IsNaN(body->PrimeMeridianDeg) ? 0 : body->PrimeMeridianDeg;
		double w = std::fmod(w0 + RateDegPerDay(body) * (jd - Astro::J2000), 360.0) * Astro::Deg2Rad;
		primeMeridian = node * cs::Math::Cos(w) + east * cs::Math::Sin(w);
	}

	DVec3 BodyOrientation::EquatorNode(DVec3 northEcliptic)
	{
		auto eq = EclipticToEquatorial(northEcliptic);
		auto q = DVec3::Cross(DVec3::UnitZ, eq);
		// A pole exactly at the celestial pole (Earth, α0 = 0) has its node at RA 90°.
		if (q.LengthSquared() < 1e-18) q = DVec3::UnitY;
		return EquatorialToEcliptic(q.Normalized());
	}

	void BodyOrientation::LonLat(DVec3 north, DVec3 primeMeridian, DVec3 direction, double& lonDeg, double& latDeg)
	{
		auto east = DVec3::Cross(north, primeMeridian);
		auto d = direction.Normalized();
		latDeg = cs::Math::Asin(cs::Math::Max(-1.0, cs::Math::Min(1.0, DVec3::Dot(d, north)))) * Astro::Rad2Deg;
		lonDeg = cs::Math::Atan2(DVec3::Dot(d, east), DVec3::Dot(d, primeMeridian)) * Astro::Rad2Deg;
	}

	DVec3 BodyOrientation::EclipticToEquatorial(DVec3 v)
	{
		double eps = Astro::ObliquityDeg * Astro::Deg2Rad;
		return DVec3(v.X, v.Y * cs::Math::Cos(eps) - v.Z * cs::Math::Sin(eps), v.Y * cs::Math::Sin(eps) + v.Z * cs::Math::Cos(eps));
	}

	DVec3 BodyOrientation::EquatorialToEcliptic(DVec3 v)
	{
		double eps = Astro::ObliquityDeg * Astro::Deg2Rad;
		return DVec3(v.X, v.Y * cs::Math::Cos(eps) + v.Z * cs::Math::Sin(eps), -v.Y * cs::Math::Sin(eps) + v.Z * cs::Math::Cos(eps));
	}
}
