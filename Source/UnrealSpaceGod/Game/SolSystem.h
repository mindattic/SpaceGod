#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sim/BodyCatalog.h"
#include "Sim/Ephemeris.h"
#include "Sim/ScaleModel.h"
#include "SolSystem.generated.h"

class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class ULineBatchComponent;
class UDirectionalLightComponent;
class UTexture2D;

/**
 * The Sol system drawn in the world: one textured, IAU-oriented sphere per body, Saturn's rings and the orbit trails,
 * laid out by the scale model for the shared simulation date. Spawned locally on every machine that renders (it is
 * not replicated): the date comes from ASpaceGameState, which the server owns, and the scale blend is each player's own.
 *
 * World axes: the scene's ecliptic X stays X, ecliptic north is +Z and ecliptic Y becomes -Y, so the orbits turn the
 * same way on screen in Unreal's left-handed frame as they do in the sky. One scene unit is UUPerUnit uu.
 */
UCLASS()
class ASolSystem : public AActor
{
	GENERATED_BODY()

public:
	ASolSystem();

	static constexpr double UUPerUnit = 1000.0;

	static FVector SceneToWorld(const FVector& SceneEcliptic) { return FVector(SceneEcliptic.X, -SceneEcliptic.Y, SceneEcliptic.Z) * UUPerUnit; }
	static FVector WorldToScene(const FVector& World) { return FVector(World.X, -World.Y, World.Z) / UUPerUnit; }
	static FVector EclipticDirToWorld(const FVector& D) { return FVector(D.X, -D.Y, D.Z); }

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	const FBodyCatalog& GetCatalog() const { return *Catalog; }
	const FEphemeris& GetEphemeris() const { return *Ephemeris; }
	const FScaleModel& GetScale() const { return Scale; }
	int32 NumBodies() const { return Catalog->Num(); }
	double GetJd() const { return Ephemeris->Jd(); }

	FVector BodyWorld(int32 Body) const { return SceneToWorld(Scene[Body]); }
	double BodyRadiusWorld(int32 Body) const { return Scale.DisplayRadius((*Catalog)[Body]) * UUPerUnit; }

	/** A world offset from a body for a true ecliptic offset in km, mapped by this player's scale. */
	FVector TrueOffsetToWorld(int32 Body, const FVector& TrueKm) const;

	/** Inverse of TrueOffsetToWorld: the true ecliptic km a world offset from a body stands for. */
	FVector WorldOffsetToTrue(int32 Body, const FVector& WorldOffset) const;

	/** Gameplay (true) or realistic scale, eased between over a second and a half like Unity.SpaceGod. */
	bool IsGameplayScale() const { return TargetBlend >= 0.5; }
	void ToggleScale() { TargetBlend = IsGameplayScale() ? 0.0 : 1.0; }

	/** The body whose side the shadow-casting light faces (the local player's frame body). */
	void SetLightFocus(int32 Body) { LightFocus = Body; }

	bool bShowOrbits = true;
	bool bShowLabels = true;

	/** The locally spawned system, or null before the GameState has begun play. */
	static ASolSystem* Get(const UObject* WorldContext);

private:
	void BuildBodies();
	void BuildRings(int32 Body);
	void UpdateBodies();
	void UpdateOrbits();
	UTexture2D* LoadBodyTexture(const FString& Name);

	TSharedPtr<FBodyCatalog> Catalog;
	TSharedPtr<FEphemeris> Ephemeris;
	FScaleModel Scale;
	double TargetBlend = 1.0;
	double RingsBuiltAtBlend = -1.0;
	int32 RingBody = INDEX_NONE;
	int32 LightFocus = INDEX_NONE;
	TArray<FVector> Scene;
	TArray<FVector> TrailScratch;

	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<ULineBatchComponent> Orbits;
	UPROPERTY() TObjectPtr<UDirectionalLightComponent> Sunlight;
	UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> Meshes;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> Rings;
	UPROPERTY() TArray<TObjectPtr<UTexture2D>> Textures;
	bool bUsingFallbackMaterial = false;
};
