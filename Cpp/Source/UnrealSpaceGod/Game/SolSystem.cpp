#include "Game/SolSystem.h"
#include "Game/SpaceGameState.h"
#include "Sim/Astro.h"
#include "Sim/BodyOrientation.h"
#include "ProceduralMeshComponent.h"
#include "Components/LineBatchComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"

namespace
{
	// Created by Content/Python/init_unreal.py the first time the editor opens the project.
	const TCHAR* BodyMaterialPath = TEXT("/Game/UnrealSpaceGod/Materials/M_Body.M_Body");
	const TCHAR* RingMaterialPath = TEXT("/Game/UnrealSpaceGod/Materials/M_Ring.M_Ring");
	const TCHAR* FallbackMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	const TCHAR* WhiteTexturePath = TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture");

	constexpr int32 LatSegments = 64;
	constexpr int32 LonSegments = 128;
	constexpr int32 RingSegments = 256;
	constexpr double ScaleEaseSeconds = 1.5;

	/**
	 * The unit sphere every body shares, in the layout of Unity.SpaceGod's MeshFactory.UvSphere and SourceArt/SOURCES.md:
	 * u = east longitude from -180 (left) to +180 (right), v = latitude from north (top, V = 0 in Unreal) to south.
	 * Local +X is the 0 deg meridian, +Z the map's north, and east is local -Y, so the mesh turns with a proper rotation
	 * (see ASolSystem::UpdateBodies) once ecliptic Y is mirrored into the world.
	 * Front faces wind with (v1-v0) x (v2-v0) pointing inward, as UKismetProceduralMeshLibrary::GenerateBoxMesh does.
	 */
	struct FUnitSphere
	{
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FLinearColor> Colors;
		TArray<FProcMeshTangent> Tangents;

		FUnitSphere()
		{
			for (int32 I = 0; I <= LatSegments; ++I)
			{
				const double Lat = -Astro::Pi / 2 + Astro::Pi * I / LatSegments;
				for (int32 J = 0; J <= LonSegments; ++J)
				{
					const double Lon = -Astro::Pi + Astro::TwoPi * J / LonSegments;
					const FVector P(std::cos(Lat) * std::cos(Lon), -std::cos(Lat) * std::sin(Lon), std::sin(Lat));
					Vertices.Add(P);
					Normals.Add(P);
					UVs.Add(FVector2D(double(J) / LonSegments, 1.0 - double(I) / LatSegments));
					Colors.Add(FLinearColor::White);
					Tangents.Add(FProcMeshTangent(FVector(-std::sin(Lon), -std::cos(Lon), 0), false));
				}
			}
			const int32 Row = LonSegments + 1;
			for (int32 I = 0; I < LatSegments; ++I)
			{
				for (int32 J = 0; J < LonSegments; ++J)
				{
					const int32 A = I * Row + J, B = A + 1, C = A + Row, D = C + 1;
					Triangles.Append({ A, B, C, B, D, C });
				}
			}
		}
	};

	const FUnitSphere& UnitSphere()
	{
		static const FUnitSphere Sphere;
		return Sphere;
	}

	/** Saturn's main rings from the inside out: radius (km) and opacity. The C ring is faint, the Cassini Division nearly empty. */
	struct FRingStation { double Km; float Alpha; };
	const FRingStation RingStations[] =
	{
		{ 74658, 0.05f }, { 92000, 0.12f }, { 92000, 0.55f }, { 105000, 0.85f }, { 117580, 0.7f },
		{ 117580, 0.04f }, { 122170, 0.04f }, { 122170, 0.45f }, { 133400, 0.4f }, { 136775, 0.25f },
	};
	const FRgb RingColor(0.82f, 0.75f, 0.62f);
}

ASolSystem::ASolSystem()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	bReplicates = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Orbits = CreateDefaultSubobject<ULineBatchComponent>(TEXT("Orbits"));
	Orbits->SetupAttachment(Root);

	// Lights only the fallback material; the generated M_Body lights each body from the Sun itself.
	Sunlight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sunlight"));
	Sunlight->SetupAttachment(Root);
	Sunlight->SetMobility(EComponentMobility::Movable);
	Sunlight->SetIntensity(6.f);
	Sunlight->SetCastShadows(false);
}

ASolSystem* ASolSystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const ASpaceGameState* State = World ? World->GetGameState<ASpaceGameState>() : nullptr;
	return State ? State->GetSolSystem() : nullptr;
}

void ASolSystem::BeginPlay()
{
	Super::BeginPlay();
	Catalog = FBodyCatalog::CreateSolSystem();
	Ephemeris = MakeShared<FEphemeris>(Catalog.ToSharedRef());
	Ephemeris->Update(Astro::JulianDate(FDateTime::UtcNow()));
	Scale.Layout(*Ephemeris, Scene);
	LightFocus = Catalog->IndexOf(TEXT("Earth"));
	BuildBodies();
	UpdateBodies();
}

UTexture2D* ASolSystem::LoadBodyTexture(const FString& Name)
{
	if (Name.IsEmpty()) return LoadObject<UTexture2D>(nullptr, WhiteTexturePath);

	const FString AssetPath = FString::Printf(TEXT("/Game/UnrealSpaceGod/Textures/%s.%s"), *Name, *Name);
	UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *AssetPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Texture)
	{
		// Before the editor has imported the maps, read the JPEG straight from SourceArt (no mips).
		const FString File = FPaths::Combine(FPaths::ProjectDir(), TEXT("SourceArt/Textures"), Name + TEXT(".jpg"));
		if (FPaths::FileExists(File)) Texture = FImageUtils::ImportFileAsTexture2D(File);
	}
	if (!Texture) Texture = LoadObject<UTexture2D>(nullptr, WhiteTexturePath);
	if (Texture) Textures.Add(Texture);
	return Texture;
}

void ASolSystem::BuildBodies()
{
	UMaterialInterface* BodyMaterial = LoadObject<UMaterialInterface>(nullptr, BodyMaterialPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	bUsingFallbackMaterial = BodyMaterial == nullptr;
	if (bUsingFallbackMaterial)
	{
		UE_LOG(LogTemp, Warning, TEXT("Unreal.SpaceGod: %s is missing (open the project in the editor once to generate it); using flat-lit fallback."), BodyMaterialPath);
		BodyMaterial = LoadObject<UMaterialInterface>(nullptr, FallbackMaterialPath);
	}

	const FUnitSphere& Sphere = UnitSphere();
	Meshes.SetNum(Catalog->Num());
	Materials.SetNum(Catalog->Num());

	for (int32 I = 0; I < Catalog->Num(); ++I)
	{
		const FBodyDefinition& B = (*Catalog)[I];
		if (!B.IsVisible()) continue;

		UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this, *FString::Printf(TEXT("Body_%s"), *B.Name));
		Mesh->SetupAttachment(Root);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCastShadow(false);
		Mesh->bUseAsyncCooking = false;
		Mesh->RegisterComponent();
		Mesh->CreateMeshSection_LinearColor(0, Sphere.Vertices, Sphere.Triangles, Sphere.Normals, Sphere.UVs, Sphere.Colors, Sphere.Tangents, false);

		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(BodyMaterial, this);
		const FLinearColor Base = B.Color.ToLinear();
		const bool bTextured = !B.Texture.IsEmpty();
		if (bUsingFallbackMaterial)
		{
			Mid->SetVectorParameterValue(TEXT("Color"), Base);
		}
		else
		{
			Mid->SetTextureParameterValue(TEXT("BaseMap"), LoadBodyTexture(B.Texture));
			Mid->SetVectorParameterValue(TEXT("Tint"), !bTextured || B.bTintTexture ? Base : FLinearColor::White);
			Mid->SetVectorParameterValue(TEXT("AtmosphereColor"), B.AtmosphereColor.ToLinear());
			Mid->SetScalarParameterValue(TEXT("AtmosphereStrength"), B.AtmosphereStrength);
			Mid->SetScalarParameterValue(TEXT("Haze"), B.SurfaceHaze);
			Mid->SetScalarParameterValue(TEXT("SelfLit"), B.IsStar() ? 1.f : 0.f);
			Mid->SetScalarParameterValue(TEXT("Brightness"), B.IsStar() ? 6.f : 1.f);
		}
		Mesh->SetMaterial(0, Mid);

		Meshes[I] = Mesh;
		Materials[I] = Mid;

		if (B.HasRings() && !Rings)
		{
			RingBody = I;
			Rings = NewObject<UProceduralMeshComponent>(this, *FString::Printf(TEXT("Rings_%s"), *B.Name));
			Rings->SetupAttachment(Mesh);
			Rings->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Rings->SetCastShadow(false);
			Rings->RegisterComponent();
			UMaterialInterface* RingMaterial = LoadObject<UMaterialInterface>(nullptr, RingMaterialPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!RingMaterial) RingMaterial = LoadObject<UMaterialInterface>(nullptr, FallbackMaterialPath);
			UMaterialInstanceDynamic* RingMid = UMaterialInstanceDynamic::Create(RingMaterial, this);
			RingMid->SetVectorParameterValue(TEXT("Color"), RingColor.ToLinear());
			Rings->SetMaterial(0, RingMid);
		}
	}
}

void ASolSystem::BuildRings(int32 Body)
{
	const FBodyDefinition& B = (*Catalog)[Body];
	const double DisplayRadius = Scale.DisplayRadius(B);
	const FLinearColor Color = RingColor.ToLinear();
	const int32 NumStations = UE_ARRAY_COUNT(RingStations);
	const int32 Row = RingSegments + 1;

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;

	// Two copies of the annulus, one facing each side of the ring plane (the planet's equator, local XY).
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const int32 Base = Vertices.Num();
		const FVector Normal = Side == 0 ? FVector::ZAxisVector : -FVector::ZAxisVector;
		for (int32 S = 0; S < NumStations; ++S)
		{
			// Ring radii go through the planet's own local mapping, as its moons do, in units of its drawn radius.
			const double R = Scale.LocalDistance(B, RingStations[S].Km) / DisplayRadius;
			for (int32 J = 0; J <= RingSegments; ++J)
			{
				const double T = Astro::TwoPi * J / RingSegments;
				Vertices.Add(FVector(R * std::cos(T), R * std::sin(T), 0));
				Normals.Add(Normal);
				UVs.Add(FVector2D(double(S) / (NumStations - 1), double(J) / RingSegments));
				Colors.Add(FLinearColor(Color.R, Color.G, Color.B, RingStations[S].Alpha));
				Tangents.Add(FProcMeshTangent(FVector(-std::sin(T), std::cos(T), 0), false));
			}
		}
		for (int32 S = 0; S + 1 < NumStations; ++S)
		{
			for (int32 J = 0; J < RingSegments; ++J)
			{
				const int32 A = Base + S * Row + J, Bv = A + 1, C = A + Row, D = C + 1;
				if (Side == 0) Triangles.Append({ A, Bv, C, Bv, D, C });
				else Triangles.Append({ A, C, Bv, Bv, C, D });
			}
		}
	}

	Rings->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
	RingsBuiltAtBlend = Scale.GetBlend();
}

FVector ASolSystem::TrueOffsetToWorld(int32 Body, const FVector& TrueKm) const
{
	return SceneToWorld(Scale.LocalOffset((*Catalog)[Body], TrueKm));
}

FVector ASolSystem::WorldOffsetToTrue(int32 Body, const FVector& WorldOffset) const
{
	return Scale.InverseLocalOffset((*Catalog)[Body], WorldToScene(WorldOffset));
}

void ASolSystem::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Catalog) return;

	const ASpaceGameState* State = GetWorld()->GetGameState<ASpaceGameState>();
	const double Jd = State ? State->CurrentJd() : Astro::JulianDate(FDateTime::UtcNow());

	const double Step = DeltaSeconds / ScaleEaseSeconds;
	const double Blend = Scale.GetBlend();
	Scale.SetBlend(Blend < TargetBlend ? FMath::Min(TargetBlend, Blend + Step) : FMath::Max(TargetBlend, Blend - Step));

	Ephemeris->Update(Jd);
	Scale.Layout(*Ephemeris, Scene);
	UpdateBodies();
	UpdateOrbits();
}

void ASolSystem::UpdateBodies()
{
	const double Jd = Ephemeris->Jd();
	const FVector SunWorld = BodyWorld(0);

	for (int32 I = 0; I < Catalog->Num(); ++I)
	{
		UProceduralMeshComponent* Mesh = Meshes[I];
		if (!Mesh) continue;
		const FBodyDefinition& B = (*Catalog)[I];

		FVector TowardParent = FVector::ZeroVector, OrbitNormal = FVector::ZeroVector;
		if (B.ParentIndex >= 0)
		{
			TowardParent = -Ephemeris->Relative(I).GetSafeNormal();
			OrbitNormal = FVector::CrossProduct(Ephemeris->Relative(I), Ephemeris->RelativeVelocityKmS(I));
		}
		FVector North, PrimeMeridian;
		BodyOrientation::Frame(B, Jd, TowardParent, OrbitNormal, North, PrimeMeridian);

		const FVector Location = BodyWorld(I);
		const FQuat Rotation = FRotationMatrix::MakeFromXZ(EclipticDirToWorld(PrimeMeridian), EclipticDirToWorld(North)).ToQuat();
		Mesh->SetWorldTransform(FTransform(Rotation, Location, FVector(BodyRadiusWorld(I))));

		if (UMaterialInstanceDynamic* Mid = Materials[I])
		{
			if (!bUsingFallbackMaterial)
			{
				const FVector ToSun = (SunWorld - Location).GetSafeNormal();
				Mid->SetVectorParameterValue(TEXT("SunDir"), FLinearColor(ToSun.X, ToSun.Y, ToSun.Z, 0.f));
			}
		}
	}

	if (Rings && RingBody != INDEX_NONE && FMath::Abs(Scale.GetBlend() - RingsBuiltAtBlend) > 1e-3)
		BuildRings(RingBody);

	if (LightFocus != INDEX_NONE && LightFocus != 0)
		Sunlight->SetWorldRotation((BodyWorld(LightFocus) - SunWorld).Rotation());
}

void ASolSystem::UpdateOrbits()
{
	Orbits->Flush();
	if (!bShowOrbits) return;

	const double Jd = Ephemeris->Jd();
	for (int32 I = 0; I < Catalog->Num(); ++I)
	{
		const FBodyDefinition& B = (*Catalog)[I];
		if (B.ParentIndex < 0 || !B.Elements.IsSet() || !B.IsVisible()) continue;

		const int32 N = B.Kind == EBodyKind::Moon ? 128 : 256;
		TrailScratch.SetNum(N);
		Ephemeris->SampleOrbitTrail(I, Jd, TrailScratch);

		const FBodyDefinition& Parent = (*Catalog)[B.ParentIndex];
		const FVector ParentScene = Scene[B.ParentIndex];
		const FLinearColor Color = B.Color.ToLinear();
		FVector Prev = SceneToWorld(ParentScene + Scale.LocalOffset(Parent, TrailScratch[0]));
		for (int32 K = 1; K < N; ++K)
		{
			const FVector Next = SceneToWorld(ParentScene + Scale.LocalOffset(Parent, TrailScratch[K]));
			const float Fade = 0.05f + 0.6f * (1.f - float(K) / N);
			Orbits->DrawLine(Prev, Next, FLinearColor(Color.R * Fade, Color.G * Fade, Color.B * Fade, 1.f), SDPG_World);
			Prev = Next;
		}
	}
}
