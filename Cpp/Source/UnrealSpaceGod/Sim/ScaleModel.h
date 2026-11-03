#pragma once

#include "CoreMinimal.h"
#include "Sim/BodyCatalog.h"

class FEphemeris;

/**
 * Maps true (km) geometry into scene units, blending between two presentations:
 *  - Realistic (Blend = 0): one linear scale, 1 unit = 1000 km. The planets are specks.
 *  - Gameplay (Blend = 1): radii and distances compressed by power laws so the system is navigable.
 * Every mapping is radial about a parent, so orbit shapes, phases and alignments stay true in both modes; blends
 * interpolate in log space. Port of Unity.SpaceGod Core/ScaleModel.cs, kept in the ecliptic frame (no axis swap).
 */
class FScaleModel
{
public:
	static constexpr double KmPerUnit = 1000.0;
	static constexpr double GameEarthRadius = 255.0;
	static constexpr double GameRadiusExponent = 0.45;
	static constexpr double GameAuScale = 20000.0;
	static constexpr double GameAuExponent = 0.55;
	static constexpr double GameLocalC = 1.17;
	static constexpr double GameLocalP = 0.44;

	/** 0 = realistic, 1 = gameplay. */
	double GetBlend() const { return Blend; }
	void SetBlend(double Value) { Blend = FMath::Clamp(Value, 0.0, 1.0); }

	static double LogLerp(double A, double B, double T);
	static double RealisticRadius(double RadiusKm) { return RadiusKm / KmPerUnit; }
	static double GameplayRadius(double RadiusKm);
	static double GameplayLocalDistance(const FBodyDefinition& Body, double DistanceKm);

	double DisplayRadius(double RadiusKm) const;
	double DisplayRadius(const FBodyDefinition& Body) const { return DisplayRadius(Body.RadiusKm); }

	/** Scene distance from the centre of Body for a true distance in km. */
	double LocalDistance(const FBodyDefinition& Body, double DistanceKm) const;

	/** Inverse of LocalDistance (monotonic, so a bisection in log space is exact enough). */
	double InverseLocalDistance(const FBodyDefinition& Body, double SceneDistance) const;

	/** Scene offset (ecliptic axes) from a body for a true ecliptic offset from it. */
	FVector LocalOffset(const FBodyDefinition& Body, const FVector& TrueOffsetKm) const;

	/** Inverse of LocalOffset. */
	FVector InverseLocalOffset(const FBodyDefinition& Body, const FVector& SceneOffset) const;

	/** Lays every body out in scene space (ecliptic axes, root at the origin), each relative to its placed parent. */
	void Layout(const FEphemeris& Ephemeris, TArray<FVector>& ScenePositions) const;

private:
	double Blend = 1.0;
};
