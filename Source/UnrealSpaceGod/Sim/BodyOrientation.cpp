#include "Sim/BodyOrientation.h"
#include "Sim/Astro.h"

FVector BodyOrientation::MapNorth(const FBodyDefinition& Body)
{
	return Body.bMapNorthFlipped ? -Body.Pole : Body.Pole;
}

double BodyOrientation::RateDegPerDay(const FBodyDefinition& Body)
{
	if (!FMath::IsNaN(Body.PrimeMeridianRateDegPerDay)) return Body.PrimeMeridianRateDegPerDay;
	if (Body.RotationHours == 0) return 0;
	const double AboutPole = 360.0 * 24.0 / Body.RotationHours;
	return Body.bMapNorthFlipped ? -AboutPole : AboutPole;
}

void BodyOrientation::Frame(const FBodyDefinition& Body, double Jd, const FVector& TowardParent, const FVector& OrbitNormal,
	FVector& OutNorth, FVector& OutPrimeMeridian)
{
	OutNorth = MapNorth(Body);
	if (Body.bFacesParent && FMath::IsNaN(Body.PoleRaDeg) && OrbitNormal.SizeSquared() > 0)
	{
		const FVector N = OrbitNormal.GetSafeNormal();
		OutNorth = FVector::DotProduct(N, OutNorth) >= 0 ? N : -N;
	}
	if (Body.bFacesParent && TowardParent.SizeSquared() > 0)
	{
		// Sub-parent point, projected onto the equator.
		const FVector P = TowardParent - OutNorth * FVector::DotProduct(TowardParent, OutNorth);
		if (P.SizeSquared() > 1e-18)
		{
			OutPrimeMeridian = P.GetSafeNormal();
			return;
		}
	}

	const FVector Node = EquatorNode(OutNorth);
	const FVector East = FVector::CrossProduct(OutNorth, Node);
	const double W0 = FMath::IsNaN(Body.PrimeMeridianDeg) ? 0 : Body.PrimeMeridianDeg;
	const double W = std::fmod(W0 + RateDegPerDay(Body) * (Jd - Astro::J2000), 360.0) * Astro::Deg2Rad;
	OutPrimeMeridian = Node * std::cos(W) + East * std::sin(W);
}

FVector BodyOrientation::EquatorNode(const FVector& NorthEcliptic)
{
	const FVector Eq = Astro::EclipticToEquatorial(NorthEcliptic);
	FVector Q = FVector::CrossProduct(FVector::ZAxisVector, Eq);
	// A pole exactly at the celestial pole (Earth) has its node at RA 90 deg.
	if (Q.SizeSquared() < 1e-18) Q = FVector::YAxisVector;
	return Astro::EquatorialToEcliptic(Q.GetSafeNormal());
}
