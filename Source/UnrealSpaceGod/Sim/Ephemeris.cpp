#include "Sim/Ephemeris.h"
#include "Sim/Astro.h"
#include "Sim/LunarTheory.h"

FEphemeris::FFrameBasis FEphemeris::FFrameBasis::FromPole(const FVector& Pole)
{
	FFrameBasis F;
	F.Z = Pole.GetSafeNormal();
	const FVector Node = FVector::CrossProduct(FVector::ZAxisVector, F.Z);
	F.X = Node.SizeSquared() < 1e-12 ? FVector::XAxisVector : Node.GetSafeNormal();
	F.Y = FVector::CrossProduct(F.Z, F.X);
	return F;
}

FEphemeris::FEphemeris(TSharedRef<const FBodyCatalog> InCatalog)
	: CatalogRef(InCatalog)
{
	const FBodyCatalog& Cat = *CatalogRef;
	RelativeKm.SetNumZeroed(Cat.Num());
	HelioKm.SetNumZeroed(Cat.Num());
	VelocityKmDay.SetNumZeroed(Cat.Num());
	Frames.SetNum(Cat.Num());

	for (int32 I = 0; I < Cat.Num(); ++I)
	{
		const FBodyDefinition& B = Cat[I];
		if (B.Plane != EOrbitPlane::ParentEquator || B.ParentIndex < 0)
			Frames[I] = FFrameBasis();
		else if (!FMath::IsNaN(B.LaplacePoleRaDeg))
			Frames[I] = FFrameBasis::FromPole(Astro::PoleFromRaDec(B.LaplacePoleRaDeg, B.LaplacePoleDecDeg));
		else
			Frames[I] = FFrameBasis::FromPole(Cat[B.ParentIndex].SpinAxis());
	}

	const int32 Earth = Cat.IndexOf(TEXT("Earth"));
	const int32 Moon = Cat.IndexOf(TEXT("Moon"));
	if (Earth != INDEX_NONE && Moon != INDEX_NONE && Cat[Moon].ParentIndex == Earth)
	{
		EarthIndex = Earth;
		MoonIndex = Moon;
		MoonMassFraction = Cat[Moon].Gm / (Cat[Earth].Gm + Cat[Moon].Gm);
	}
}

FVector FEphemeris::RelativeVelocityKmS(int32 Body) const
{
	return VelocityKmDay[Body] / Astro::SecondsPerDay;
}

void FEphemeris::Update(double Jd)
{
	CurrentJd = Jd;
	const FBodyCatalog& Cat = *CatalogRef;
	for (int32 I = 0; I < Cat.Num(); ++I)
	{
		const FBodyDefinition& B = Cat[I];
		if (!B.Elements.IsSet())
		{
			RelativeKm[I] = FVector::ZeroVector;
			VelocityKmDay[I] = FVector::ZeroVector;
			continue;
		}
		const FKeplerState K = B.Elements->At(Jd);
		RelativeKm[I] = Frames[I].ToEcliptic(Kepler::Position(K)) * B.OrbitScale;
		VelocityKmDay[I] = Frames[I].ToEcliptic(Kepler::Velocity(K)) * B.OrbitScale;
	}

	// The Moon follows a lunar theory, not an ellipse.
	if (MoonIndex >= 0)
	{
		RelativeKm[MoonIndex] = LunarTheory::Position(Jd);
		constexpr double H = 1e-3;
		VelocityKmDay[MoonIndex] = (LunarTheory::Position(Jd + H) - LunarTheory::Position(Jd - H)) / (2 * H);
	}

	// JPL's "Earth" elements are the Earth-Moon barycentre; Earth sits 4,670 km from it.
	if (EarthIndex >= 0)
	{
		RelativeKm[EarthIndex] -= RelativeKm[MoonIndex] * MoonMassFraction;
		VelocityKmDay[EarthIndex] -= VelocityKmDay[MoonIndex] * MoonMassFraction;
	}

	for (int32 I = 0; I < Cat.Num(); ++I)
	{
		const int32 P = Cat[I].ParentIndex;
		HelioKm[I] = P < 0 ? RelativeKm[I] : HelioKm[P] + RelativeKm[I];
	}
}

void FEphemeris::SampleOrbitTrail(int32 Body, double Jd, TArray<FVector>& Output) const
{
	const FBodyDefinition& B = (*CatalogRef)[Body];
	const FKeplerState K = B.Elements->At(Jd);
	const double Now = Kepler::SolveEccentricAnomaly(K.MeanAnomaly, K.E);
	const int32 N = Output.Num();
	const double Sense = K.MeanMotion >= 0 ? 1 : -1;
	// The Moon (lunar theory) and Earth (barycentre offset) are drawn off their mean ellipse; shift the trail to meet them.
	FVector Offset = FVector::ZeroVector;
	if (Body == MoonIndex) Offset = LunarTheory::Position(Jd) - Frames[Body].ToEcliptic(Kepler::Position(K)) * B.OrbitScale;
	else if (Body == EarthIndex) Offset = -LunarTheory::Position(Jd) * MoonMassFraction;
	for (int32 I = 0; I < N; ++I)
	{
		const double Ecc = Now - Sense * Astro::TwoPi * I / FMath::Max(1, N - 1);
		const double M = Ecc - K.E * std::sin(Ecc);
		Output[I] = Frames[Body].ToEcliptic(Kepler::Position(K.WithMeanAnomaly(M))) * B.OrbitScale + Offset;
	}
}

double FEphemeris::OrbitalSpeedKmS(int32 Body) const
{
	const FBodyDefinition& B = (*CatalogRef)[Body];
	if (B.ParentIndex < 0 || !B.Elements.IsSet()) return 0;
	if (B.OrbitScale != 1) return RelativeVelocityKmS(Body).Size();
	const double Gm = (*CatalogRef)[B.ParentIndex].Gm + B.Gm;
	return Kepler::VisVivaSpeed(Gm, RelativeKm[Body].Size(), B.Elements->At(CurrentJd).A);
}
