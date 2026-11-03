#pragma once

#include "CoreMinimal.h"
#include "Sim/BodyCatalog.h"

/**
 * Where every body is at a Julian date, by two-body Kepler propagation of its elements; the Moon follows ELP-2000.
 * Positions are km in the J2000 ecliptic frame; "relative" is to the parent, "helio" to the Sun's centre.
 * Analytic propagation is exact for any time warp and never drifts. Port of Unity.SpaceGod Core/Ephemeris.cs.
 */
class FEphemeris
{
public:
	explicit FEphemeris(TSharedRef<const FBodyCatalog> InCatalog);

	const FBodyCatalog& Catalog() const { return *CatalogRef; }
	double Jd() const { return CurrentJd; }

	const FVector& Relative(int32 Body) const { return RelativeKm[Body]; }
	const FVector& Helio(int32 Body) const { return HelioKm[Body]; }

	/** Velocity relative to the parent, km/s. */
	FVector RelativeVelocityKmS(int32 Body) const;

	void Update(double Jd);

	/** One full orbit from the body's current position running backwards along it, for a fading trail. */
	void SampleOrbitTrail(int32 Body, double Jd, TArray<FVector>& Output) const;

	/** Orbital speed relative to the parent (km/s) from vis-viva. */
	double OrbitalSpeedKmS(int32 Body) const;

private:
	struct FFrameBasis
	{
		FVector X = FVector::XAxisVector;
		FVector Y = FVector::YAxisVector;
		FVector Z = FVector::ZAxisVector;

		/** Equatorial frame of a pole: x toward the equator's ascending node on the ecliptic. */
		static FFrameBasis FromPole(const FVector& Pole);
		FVector ToEcliptic(const FVector& V) const { return X * V.X + Y * V.Y + Z * V.Z; }
	};

	TSharedRef<const FBodyCatalog> CatalogRef;
	double CurrentJd = 0;
	TArray<FVector> RelativeKm;
	TArray<FVector> HelioKm;
	TArray<FVector> VelocityKmDay;
	TArray<FFrameBasis> Frames;
	int32 MoonIndex = INDEX_NONE;
	int32 EarthIndex = INDEX_NONE;
	double MoonMassFraction = 0;
};
