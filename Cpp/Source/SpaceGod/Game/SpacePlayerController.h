#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SpacePlayerController.generated.h"

/**
 * The local player's seat in Unreal: it owns the keyboard and mouse (AUnitySpaceAppActor reads them through it each
 * frame into Input::InputState, with Unity.SpaceGod's own bindings) and views through the app's camera. Nothing here
 * changes the world: every player action goes through the app's Core Session.
 *
 * Co-op: the console command `Host` reopens the game as a listen server; friends join with `open <host ip>` (or
 * tools/play.ps1 -Mode host / join). Co-op's messages go through each client's ASpaceCoopLink (US-T44).
 */
UCLASS()
class ASpacePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ASpacePlayerController();

	virtual void BeginPlay() override;

	/// Reopens this map (in its current scene) as a listen server.
	UFUNCTION(Exec)
	void Host();
};
