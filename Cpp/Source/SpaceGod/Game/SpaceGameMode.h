#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SpaceGameMode.generated.h"

class ASpaceCoopLink;

/**
 * Unreal's entry point for a "scene" (docs: GameLaunch). The map is empty; the option ?Scene=MainMenu|UnitySpace
 * names the Unity scene it stands for (MainMenu, the first in Unity's build list, when absent), and BeginPlay runs
 * Bootstrap::EnsureApp for it, as Unity's [RuntimeInitializeOnLoadMethod(AfterSceneLoad)] does. No pawn: the app
 * owns the one camera (AUnitySpaceAppActor).
 *
 * Co-op (US-T44, the C# track's US-T39): a listen server (tools/play.ps1 -Mode host) hosts; each client that logs in
 * gets its link actor (ASpaceCoopLink, its RPCs) and leaving hands its seat back. Standalone stays the original
 * single-player path.
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
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;

private:
	FString Scene;

	UPROPERTY()
	TMap<TObjectPtr<APlayerController>, TObjectPtr<ASpaceCoopLink>> CoopLinks;
};
