#include "Game/SpacePawn.h"
#include "Game/SolSystem.h"
#include "Sim/Astro.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"

namespace
{
	constexpr double MinSurfaceRadii = 1.02;
	constexpr double LookDegreesPerPixel = 0.2;
	constexpr double SendInterval = 0.1;
}

ASpacePawn::ASpacePawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	bReplicates = true;
	SetReplicateMovement(false);
	// Positions are drawn per machine from the frame body, so distance-based relevancy means nothing here.
	bAlwaysRelevant = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->SetFieldOfView(60.f);
}

void ASpacePawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ASpacePawn, RepBody, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(ASpacePawn, RepOffsetKm, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(ASpacePawn, RepRotation, COND_SkipOwner);
}

FString ASpacePawn::GetPlayerLabel() const
{
	const APlayerState* State = GetPlayerState();
	return State ? State->GetPlayerName() : TEXT("Player");
}

bool ASpacePawn::EnsureStarted(ASolSystem* Sol)
{
	if (!Sol) return false;
	if (!bStarted)
	{
		AddTickPrerequisiteActor(Sol);
		bStarted = true;
	}

	// A client's pawn can tick before its controller has replicated, so the first view waits for that.
	if (!bLocalViewStarted && IsLocallyControlled())
	{
		bLocalViewStarted = true;
		const int32 Earth = Sol->GetCatalog().IndexOf(TEXT("Earth"));
		// Start between Earth and the Sun, a little off the line, so the dayside fills the view.
		const FVector ToSun = (Sol->BodyWorld(0) - Sol->BodyWorld(Earth)).GetSafeNormal();
		ViewRotation = (-ToSun).Rotation();
		ViewRotation.Yaw += 25;
		ViewRotation.Pitch -= 15;
		FocusOn(Earth, 4.5);
	}
	return true;
}

void ASpacePawn::FocusOn(int32 Body, double DistanceRadii)
{
	if (Body == INDEX_NONE) return;
	FrameBody = Body;
	bFocused = true;
	FocusDistance = DistanceRadii > 0 ? DistanceRadii : 4.0;
	OffsetRadii = -ViewRotation.Vector() * FocusDistance;
}

void ASpacePawn::SystemView()
{
	ASolSystem* Sol = ASolSystem::Get(this);
	if (!Sol) return;
	// Far enough out that Neptune's orbit fills the view.
	const FBodyDefinition& Sun = Sol->GetCatalog().Root();
	const double Reach = Sol->GetScale().LocalDistance(Sun, 31 * Astro::AuKm) / Sol->GetScale().DisplayRadius(Sun);
	ViewRotation = FRotator(-55, ViewRotation.Yaw, 0);
	FocusOn(0, Reach * 1.9);
}

void ASpacePawn::JumpTo(const ASpacePawn* Other)
{
	ASolSystem* Sol = ASolSystem::Get(this);
	if (!Sol || !Other || Other->RepBody == INDEX_NONE) return;
	FrameBody = Other->RepBody;
	OffsetRadii = (Other->GetActorLocation() - Sol->BodyWorld(FrameBody)) / Sol->BodyRadiusWorld(FrameBody);
	ViewRotation = Other->RepRotation;
	bFocused = false;
}

void ASpacePawn::ApplyInput(const FSpaceViewInput& Input, float DeltaSeconds)
{
	if (FrameBody == INDEX_NONE) return;

	ViewRotation.Yaw += Input.LookDelta.X * LookDegreesPerPixel;
	ViewRotation.Pitch = FMath::Clamp(ViewRotation.Pitch - Input.LookDelta.Y * LookDegreesPerPixel, -89.0, 89.0);

	if (!Input.Move.IsNearlyZero()) bFocused = false;

	if (bFocused)
	{
		if (Input.Wheel != 0) FocusDistance = FMath::Max(1.3, FocusDistance * FMath::Pow(0.85, (double)Input.Wheel));
		OffsetRadii = -ViewRotation.Vector() * FocusDistance;
		return;
	}

	if (Input.Wheel != 0) SwimSpeed = FMath::Clamp(SwimSpeed * FMath::Pow(1.25, (double)Input.Wheel), 0.01, 100.0);

	const FRotationMatrix Axes(ViewRotation);
	const FVector Direction = Axes.GetScaledAxis(EAxis::X) * Input.Move.X + Axes.GetScaledAxis(EAxis::Y) * Input.Move.Y
		+ FVector::ZAxisVector * Input.Move.Z;
	// Speed scales with height above the frame body, in its radii per second.
	double Speed = FMath::Max(0.05, OffsetRadii.Size() - 1.0) * 0.8 * SwimSpeed;
	if (Input.bBoost) Speed *= 4;
	if (Input.bFine) Speed *= 0.2;
	OffsetRadii += Direction.GetClampedToMaxSize(1.0) * Speed * DeltaSeconds;
}

void ASpacePawn::Reframe(ASolSystem* Sol)
{
	// In free flight, the body nearest in its own radii becomes the frame, so a scale switch keeps the view near it.
	const FVector Here = GetActorLocation();
	double Best = OffsetRadii.Size();
	int32 BestBody = FrameBody;
	for (int32 I = 0; I < Sol->NumBodies(); ++I)
	{
		if (!Sol->GetCatalog()[I].IsVisible()) continue;
		const double Radii = (Here - Sol->BodyWorld(I)).Size() / Sol->BodyRadiusWorld(I);
		if (Radii < Best * 0.8)
		{
			Best = Radii;
			BestBody = I;
		}
	}
	if (BestBody != FrameBody)
	{
		OffsetRadii = (Here - Sol->BodyWorld(BestBody)) / Sol->BodyRadiusWorld(BestBody);
		FrameBody = BestBody;
	}
}

void ASpacePawn::UpdateLocal(ASolSystem* Sol, float DeltaSeconds)
{
	if (!bFocused) Reframe(Sol);
	if (OffsetRadii.Size() < MinSurfaceRadii)
		OffsetRadii = OffsetRadii.GetSafeNormal(UE_SMALL_NUMBER, -ViewRotation.Vector()) * MinSurfaceRadii;

	const FVector Location = Sol->BodyWorld(FrameBody) + OffsetRadii * Sol->BodyRadiusWorld(FrameBody);
	SetActorLocationAndRotation(Location, ViewRotation);
	Sol->SetLightFocus(FrameBody);

	SendTimer -= DeltaSeconds;
	if (SendTimer > 0) return;
	SendTimer = SendInterval;
	const FVector OffsetKm = Sol->WorldOffsetToTrue(FrameBody, OffsetRadii * Sol->BodyRadiusWorld(FrameBody));
	if (HasAuthority())
	{
		RepBody = FrameBody;
		RepOffsetKm = OffsetKm;
		RepRotation = ViewRotation;
	}
	else
	{
		ServerUpdateView(FrameBody, OffsetKm, ViewRotation);
	}
}

void ASpacePawn::ServerUpdateView_Implementation(int32 Body, const FVector& OffsetKm, const FRotator& Rotation)
{
	RepBody = Body;
	RepOffsetKm = OffsetKm;
	RepRotation = Rotation;
}

void ASpacePawn::UpdateRemote(ASolSystem* Sol)
{
	if (RepBody < 0 || RepBody >= Sol->NumBodies()) return;
	const FVector Location = Sol->BodyWorld(RepBody) + Sol->TrueOffsetToWorld(RepBody, RepOffsetKm);
	SetActorLocationAndRotation(Location, RepRotation);
}

void ASpacePawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ASolSystem* Sol = ASolSystem::Get(this);
	if (!EnsureStarted(Sol)) return;

	if (IsLocallyControlled() && FrameBody != INDEX_NONE) UpdateLocal(Sol, DeltaSeconds);
	else UpdateRemote(Sol);
}
