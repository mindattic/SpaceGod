#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SpacePlayerController.generated.h"

/**
 * The local player's seat in Unreal: it owns the keyboard and mouse (AUnitySpaceAppActor reads them through it each
 * frame into Input::InputState, with Unity.SpaceGod's own bindings) and views through the app's camera. Nothing here
 * changes the world: every player action goes through the app's Core Session.
 *
 * Co-op (kept from the first slice): the console command `Host` reopens the game as a listen server; friends join
 * with `open <host ip>`. The co-op ticket adds the command RPCs (Core::CommandCodec bytes to the host's session).
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
