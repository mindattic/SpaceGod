#pragma once

#include "CoreMinimal.h"
#include "Sim/Orbits.h"
#include <limits>

enum class EBodyKind : uint8 { Star, Planet, DwarfPlanet, Moon, Barycentre, Station, Asteroid };

/** Which plane a body's orbital elements are measured against. */
enum class EOrbitPlane : uint8
{
	/** J2000 ecliptic (planets, the Moon, minor planets). */
	Ecliptic,
	/** The parent's equator, or the moon's Laplace plane when its pole is set (regular satellites). */
	ParentEquator,
};

enum class ELagrangePoint : uint8 { None, L4, L5 };

/** A display colour as authored in Unity.SpaceGod (sRGB, 0-1). */
struct FRgb
{
	float R = 0.7f, G = 0.7f, B = 0.7f;
	FRgb() = default;
	FRgb(float InR, float InG, float InB) : R(InR), G(InG), B(InB) {}

	FLinearColor ToLinear() const
	{
		return FLinearColor::FromSRGBColor(FColor(
			(uint8)FMath::RoundToInt(FMath::Clamp(R, 0.f, 1.f) * 255.f),
			(uint8)FMath::RoundToInt(FMath::Clamp(G, 0.f, 1.f) * 255.f),
			(uint8)FMath::RoundToInt(FMath::Clamp(B, 0.f, 1.f) * 255.f)));
	}
};

/** Static, physical description of one body; real data unless noted. Port of Core/BodyDefinition.cs. */
struct FBodyDefinition
{
	FString Name;
	EBodyKind Kind = EBodyKind::Planet;
	FString ParentName;
	double RadiusKm = 0;

	/** Gravitational parameter GM in km^3/s^2. */
	double Gm = 0;

	TOptional<FOrbitalElements> Elements;
	EOrbitPlane Plane = EOrbitPlane::Ecliptic;

	/** IAU north pole, equatorial J2000. NaN = unknown (falls back to ecliptic north). */
	double PoleRaDeg = std::numeric_limits<double>::quiet_NaN();
	double PoleDecDeg = std::numeric_limits<double>::quiet_NaN();

	/** Sidereal rotation period in hours; negative = retrograde about the IAU pole. */
	double RotationHours = 0;

	FRgb Color;
	FString Texture;
	float TexturePoleMargin = 0;

	/** The colour map is a single-band mosaic, so it is multiplied by Color. */
	bool bTintTexture = false;
	FString NormalMap;
	bool bGasGiant = false;

	/** How much of the surface an opaque atmosphere hides in visible light (Titan's haze, Venus's clouds). */
	float SurfaceHaze = 0;

	/** IAU prime meridian angle W at J2000 (deg) and its rate (deg/day). NaN = unknown / from RotationHours. */
	double PrimeMeridianDeg = std::numeric_limits<double>::quiet_NaN();
	double PrimeMeridianRateDegPerDay = std::numeric_limits<double>::quiet_NaN();

	/** Synchronous rotator whose 0 deg meridian faces its parent. */
	bool bFacesParent = false;

	/** The map's north is the opposite of Pole (Pluto; Uranus's moons). */
	bool bMapNorthFlipped = false;

	FRgb AtmosphereColor = FRgb(0, 0, 0);
	float AtmosphereStrength = 0;

	double RingInnerKm = 0;
	double RingOuterKm = 0;

	/** Pole of a moon's Laplace plane, equatorial J2000. NaN = the parent's equator. */
	double LaplacePoleRaDeg = std::numeric_limits<double>::quiet_NaN();
	double LaplacePoleDecDeg = std::numeric_limits<double>::quiet_NaN();

	FString Summary;

	ELagrangePoint TrojanPoint = ELagrangePoint::None;
	FString CoOrbitalWith;

	/** Multiplies the body's orbit about its parent (binary stars share one relative orbit). */
	double OrbitScale = 1;

	// Resolved by FBodyCatalog.
	int32 Index = -1;
	int32 ParentIndex = -1;
	FVector Pole = FVector::ZAxisVector;
	double SoiKm = TNumericLimits<double>::Max();
	double MoonReachKm = 0;
	double SystemReachKm = 0;
	double InnermostChildKm = 0;

	/** Direction of the spin angular momentum: the IAU pole, flipped for retrograde rotators. */
	FVector SpinAxis() const { return RotationHours < 0 ? -Pole : Pole; }
	bool IsVisible() const { return Kind != EBodyKind::Barycentre; }
	bool HasRings() const { return RingOuterKm > 0; }
	bool IsStar() const { return Kind == EBodyKind::Star; }
	bool UsesStellarMapping() const { return Kind == EBodyKind::Star || Kind == EBodyKind::Barycentre; }
};

/** The bodies of the simulation with their real physical and orbital data. Port of Core/BodyCatalog.cs. */
class FBodyCatalog
{
public:
	explicit FBodyCatalog(TArray<FBodyDefinition> Definitions);

	static TSharedRef<FBodyCatalog> CreateSolSystem();

	int32 Num() const { return Bodies.Num(); }
	const FBodyDefinition& operator[](int32 Index) const { return Bodies[Index]; }
	const FBodyDefinition& Root() const { return Bodies[0]; }
	const TArray<int32>& ChildrenOf(int32 Index) const { return Children[Index]; }

	/** Index of a body by name (case-insensitive), or INDEX_NONE. */
	int32 IndexOf(const FString& Name) const
	{
		const int32* Found = ByName.Find(Name);
		return Found ? *Found : INDEX_NONE;
	}

private:
	TArray<FBodyDefinition> Bodies;
	TMap<FString, int32> ByName;
	TArray<TArray<int32>> Children;
};

FString BodyKindName(EBodyKind Kind);
