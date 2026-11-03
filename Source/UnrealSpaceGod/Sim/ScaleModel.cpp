#include "Sim/ScaleModel.h"
#include "Sim/Astro.h"
#include "Sim/Ephemeris.h"

namespace
{
	constexpr double EarthRadiusKm = 6371.0;
}

double FScaleModel::LogLerp(double A, double B, double T)
{
	if (A <= 0 || B <= 0) return A + (B - A) * T;
	return std::exp(std::log(A) + (std::log(B) - std::log(A)) * T);
}

double FScaleModel::GameplayRadius(double RadiusKm)
{
	return GameEarthRadius * std::pow(RadiusKm / EarthRadiusKm, GameRadiusExponent);
}

double FScaleModel::DisplayRadius(double RadiusKm) const
{
	return LogLerp(RealisticRadius(RadiusKm), GameplayRadius(RadiusKm), Blend);
}

double FScaleModel::LocalDistance(const FBodyDefinition& Body, double DistanceKm) const
{
	return LogLerp(DistanceKm / KmPerUnit, GameplayLocalDistance(Body, DistanceKm), Blend);
}

double FScaleModel::GameplayLocalDistance(const FBodyDefinition& Body, double DistanceKm)
{
	const double R = Body.RadiusKm;
	const double RDisplay = GameplayRadius(R);
	if (DistanceKm <= R) return R > 0 ? RDisplay * DistanceKm / R : 0;

	if (Body.UsesStellarMapping())
	{
		return RDisplay + GameAuScale *
			(std::pow(DistanceKm / Astro::AuKm, GameAuExponent) - std::pow(R / Astro::AuKm, GameAuExponent));
	}

	const double X = DistanceKm / R;
	return RDisplay * (1.0 + GameLocalC * std::pow(X - 1.0, GameLocalP));
}

double FScaleModel::InverseLocalDistance(const FBodyDefinition& Body, double SceneDistance) const
{
	if (SceneDistance <= 0) return 0;
	double Lo = std::log(FMath::Max(Body.RadiusKm, 1.0) * 1e-6);
	double Hi = std::log(1e15);
	for (int32 I = 0; I < 90; ++I)
	{
		const double Mid = 0.5 * (Lo + Hi);
		if (LocalDistance(Body, std::exp(Mid)) < SceneDistance) Lo = Mid;
		else Hi = Mid;
	}
	return std::exp(0.5 * (Lo + Hi));
}

FVector FScaleModel::LocalOffset(const FBodyDefinition& Body, const FVector& TrueOffsetKm) const
{
	const double D = TrueOffsetKm.Size();
	if (D <= 0) return FVector::ZeroVector;
	return TrueOffsetKm * (LocalDistance(Body, D) / D);
}

FVector FScaleModel::InverseLocalOffset(const FBodyDefinition& Body, const FVector& SceneOffset) const
{
	const double S = SceneOffset.Size();
	if (S <= 0) return FVector::ZeroVector;
	return SceneOffset * (InverseLocalDistance(Body, S) / S);
}

void FScaleModel::Layout(const FEphemeris& Ephemeris, TArray<FVector>& ScenePositions) const
{
	const FBodyCatalog& Cat = Ephemeris.Catalog();
	ScenePositions.SetNum(Cat.Num());
	for (int32 I = 0; I < Cat.Num(); ++I)
	{
		const FBodyDefinition& B = Cat[I];
		if (B.ParentIndex < 0)
		{
			ScenePositions[I] = FVector::ZeroVector;
			continue;
		}
		ScenePositions[I] = ScenePositions[B.ParentIndex] + LocalOffset(Cat[B.ParentIndex], Ephemeris.Relative(I));
	}
}
