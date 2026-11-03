#include "Game/SpacePlayerController.h"
#include "Game/SolSystem.h"
#include "Game/SpaceGameState.h"
#include "Game/SpacePawn.h"
#include "GameFramework/PlayerState.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/Package.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
	/** 1-9 and 0, as in Unity.SpaceGod: the planets out from the Sun, then Pluto, and 0 for the Sun. */
	const TCHAR* NumberBodies[10] = { TEXT("Sun"), TEXT("Mercury"), TEXT("Venus"), TEXT("Earth"), TEXT("Mars"), TEXT("Jupiter"),
		TEXT("Saturn"), TEXT("Uranus"), TEXT("Neptune"), TEXT("Pluto") };

	constexpr double DoubleClickSeconds = 0.35;
	constexpr float MinPickPixels = 14.f;
}

ASpacePlayerController::ASpacePlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = false;
}

void ASpacePlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController()) return;
	FInputModeGameAndUI Mode;
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
}

FString ASpacePlayerController::PlayerLabel() const
{
	return PlayerState ? PlayerState->GetPlayerName() : TEXT("Player");
}

int32 ASpacePlayerController::PickBody(const FVector2D& Screen, const ASolSystem* Sol) const
{
	if (!PlayerCameraManager) return INDEX_NONE;
	const FVector CamLocation = PlayerCameraManager->GetCameraLocation();
	const FVector CamRight = FRotationMatrix(PlayerCameraManager->GetCameraRotation()).GetScaledAxis(EAxis::Y);

	int32 Best = INDEX_NONE;
	double BestDistance = TNumericLimits<double>::Max();
	for (int32 I = 0; I < Sol->NumBodies(); ++I)
	{
		if (!Sol->GetCatalog()[I].IsVisible()) continue;
		const FVector Center = Sol->BodyWorld(I);
		FVector2D C, Edge;
		if (!ProjectWorldLocationToScreen(Center, C) || !ProjectWorldLocationToScreen(Center + CamRight * Sol->BodyRadiusWorld(I), Edge)) continue;
		const double Reach = FMath::Max<double>(MinPickPixels, (Edge - C).Size());
		if ((Screen - C).Size() > Reach) continue;
		const double Distance = (Center - CamLocation).Size();
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = I;
		}
	}
	return Best;
}

void ASpacePlayerController::Select(int32 Body, ASpacePawn* SpacePawn, bool bFocusIfAlready)
{
	if (Body == INDEX_NONE) return;
	if (bFocusIfAlready && SelectedBody == Body) SpacePawn->FocusOn(Body);
	SelectedBody = Body;
}

void ASpacePlayerController::CycleBodies(int32 Direction, ASpacePawn* SpacePawn, const ASolSystem* Sol)
{
	const int32 N = Sol->NumBodies();
	int32 I = SelectedBody == INDEX_NONE ? SpacePawn->GetFrameBody() : SelectedBody;
	for (int32 Step = 0; Step < N; ++Step)
	{
		I = (I + Direction + N) % N;
		if (Sol->GetCatalog()[I].IsVisible()) break;
	}
	SelectedBody = I;
	SpacePawn->FocusOn(I);
}

void ASpacePlayerController::JumpToNextTeammate(ASpacePawn* SpacePawn)
{
	TArray<ASpacePawn*> Others;
	for (TActorIterator<ASpacePawn> It(GetWorld()); It; ++It)
		if (*It != SpacePawn) Others.Add(*It);
	if (Others.Num() == 0) return;
	TeammateCursor = TeammateCursor % Others.Num();
	SpacePawn->JumpTo(Others[TeammateCursor]);
	++TeammateCursor;
}

void ASpacePlayerController::Host()
{
	if (GetNetMode() != NM_Standalone) return;
	// Re-open this map as a listen server; friends join with "open <host ip>" in the console.
	const FString Map = UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName());
	UGameplayStatics::OpenLevel(this, FName(*Map), true, TEXT("listen"));
}

void ASpacePlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	ASpacePawn* SpacePawn = Cast<ASpacePawn>(GetPawn());
	ASolSystem* Sol = ASolSystem::Get(this);
	if (!SpacePawn || !Sol) return;

	FSpaceViewInput Input;
	if (Down(EKeys::RightMouseButton))
	{
		float Dx = 0, Dy = 0;
		GetInputMouseDelta(Dx, Dy);
		Input.LookDelta = FVector2D(Dx, -Dy) * 4.0;
	}
	Input.Move.X = (Down(EKeys::W) ? 1 : 0) - (Down(EKeys::S) ? 1 : 0);
	Input.Move.Y = (Down(EKeys::D) ? 1 : 0) - (Down(EKeys::A) ? 1 : 0);
	Input.Move.Z = (Down(EKeys::E) || Down(EKeys::RightBracket) ? 1 : 0) - (Down(EKeys::Q) || Down(EKeys::C) ? 1 : 0);
	Input.Wheel = (Pressed(EKeys::MouseScrollUp) ? 1 : 0) - (Pressed(EKeys::MouseScrollDown) ? 1 : 0);
	const bool bShift = Down(EKeys::LeftShift) || Down(EKeys::RightShift);
	Input.bBoost = bShift;
	Input.bFine = Down(EKeys::LeftControl) || Down(EKeys::RightControl);
	SpacePawn->ApplyInput(Input, DeltaTime);

	if (Pressed(EKeys::LeftMouseButton))
	{
		float Mx = 0, My = 0;
		if (GetMousePosition(Mx, My))
		{
			const int32 Hit = PickBody(FVector2D(Mx, My), Sol);
			const double Now = FPlatformTime::Seconds();
			if (Hit != INDEX_NONE && Hit == LastClickBody && Now - LastClickSeconds < DoubleClickSeconds) SpacePawn->FocusOn(Hit);
			if (Hit != INDEX_NONE) SelectedBody = Hit;
			LastClickBody = Hit;
			LastClickSeconds = Now;
		}
	}

	const FKey Numbers[10] = { EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
	for (int32 K = 0; K < 10; ++K)
		if (Pressed(Numbers[K])) Select(Sol->GetCatalog().IndexOf(NumberBodies[K]), SpacePawn, true);

	if (Pressed(EKeys::Tab)) CycleBodies(bShift ? -1 : 1, SpacePawn, Sol);
	if (Pressed(EKeys::F) && SelectedBody != INDEX_NONE) SpacePawn->FocusOn(SelectedBody);
	if (Pressed(EKeys::Home)) SpacePawn->SystemView();
	if (Pressed(EKeys::Escape))
	{
		if (bShowHelp) bShowHelp = false;
		else SelectedBody = INDEX_NONE;
	}

	if (Pressed(EKeys::SpaceBar)) ServerTogglePause();
	if (Pressed(EKeys::Comma)) ServerChangeWarp(-1);
	if (Pressed(EKeys::Period)) ServerChangeWarp(+1);
	if (Pressed(EKeys::T)) ServerResetToNow();

	if (Pressed(EKeys::G)) Sol->ToggleScale();
	if (Pressed(EKeys::O)) Sol->bShowOrbits = !Sol->bShowOrbits;
	if (Pressed(EKeys::L)) Sol->bShowLabels = !Sol->bShowLabels;
	if (Pressed(EKeys::H) || Pressed(EKeys::F1)) bShowHelp = !bShowHelp;

	if (Pressed(EKeys::J)) JumpToNextTeammate(SpacePawn);
	if (Pressed(EKeys::M) && SelectedBody != INDEX_NONE) ServerPing(SelectedBody);
	if (Pressed(EKeys::F9)) Host();
}

void ASpacePlayerController::ServerChangeWarp_Implementation(int32 Delta)
{
	if (ASpaceGameState* State = GetWorld()->GetGameState<ASpaceGameState>()) State->ChangeWarp(FMath::Clamp(Delta, -1, 1), PlayerLabel());
}

void ASpacePlayerController::ServerTogglePause_Implementation()
{
	if (ASpaceGameState* State = GetWorld()->GetGameState<ASpaceGameState>()) State->TogglePause(PlayerLabel());
}

void ASpacePlayerController::ServerResetToNow_Implementation()
{
	if (ASpaceGameState* State = GetWorld()->GetGameState<ASpaceGameState>()) State->ResetToNow(PlayerLabel());
}

void ASpacePlayerController::ServerPing_Implementation(int32 Body)
{
	if (ASpaceGameState* State = GetWorld()->GetGameState<ASpaceGameState>()) State->Ping(Body, PlayerLabel());
}
