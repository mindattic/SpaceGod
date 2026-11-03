#include "Game/SpacePlayerController.h"
#include "GameLaunch.h"

#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

ASpacePlayerController::ASpacePlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = false;
	bEnableMouseOverEvents = false;
}

void ASpacePlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (IsLocalController())
	{
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(Mode);
	}
}

void ASpacePlayerController::Host()
{
	if (GetNetMode() != NM_Standalone) return;
	// Re-open this map as a listen server, in the game scene; friends join with "open <host ip>" in the console.
	const FString Map = UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName());
	UGameplayStatics::OpenLevel(this, FName(*Map), true, FString::Printf(TEXT("listen?Scene=%s"), UTF8_TO_TCHAR(SpaceGod::GameLaunch::GameScene)));
}
