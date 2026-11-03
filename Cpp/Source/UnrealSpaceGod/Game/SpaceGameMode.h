#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SpaceGameMode.generated.h"

/**
 * Unreal's entry point for a "scene" (docs: GameLaunch). The map is empty; the option ?Scene=MainMenu|UnitySpace
 * names the Unity scene it stands for (MainMenu, the first in Unity's build list, when absent), and BeginPlay runs
 * Bootstrap::EnsureApp for it, as Unity's [RuntimeInitializeOnLoadMethod(AfterSceneLoad)] does. No pawn: the app
 * owns the one camera (AUnitySpaceAppActor). The GameState mirrors the host's clock for co-op.
 */
UCLASS()
class ASpaceGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASpaceGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	FString Scene;
};
