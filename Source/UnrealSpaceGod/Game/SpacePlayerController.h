#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SpacePlayerController.generated.h"

class ASolSystem;
class ASpacePawn;

/**
 * Reads the keyboard and mouse each frame (Unity.SpaceGod's god-view keys; see ControlsHelp in SpaceHUD.cpp) and
 * drives the local camera pawn. Anything that changes the shared world, the clock or a mark, goes to the server.
 */
UCLASS()
class ASpacePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ASpacePlayerController();

	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;

	int32 GetSelectedBody() const { return SelectedBody; }
	bool IsHelpShown() const { return bShowHelp; }

	UFUNCTION(Server, Reliable) void ServerChangeWarp(int32 Delta);
	UFUNCTION(Server, Reliable) void ServerTogglePause();
	UFUNCTION(Server, Reliable) void ServerResetToNow();
	UFUNCTION(Server, Reliable) void ServerPing(int32 Body);

private:
	bool Pressed(const FKey& Key) const { return WasInputKeyJustPressed(Key); }
	bool Down(const FKey& Key) const { return IsInputKeyDown(Key); }
	FString PlayerLabel() const;

	int32 PickBody(const FVector2D& Screen, const ASolSystem* Sol) const;
	void Select(int32 Body, ASpacePawn* SpacePawn, bool bFocusIfAlready);
	void CycleBodies(int32 Direction, ASpacePawn* SpacePawn, const ASolSystem* Sol);
	void JumpToNextTeammate(ASpacePawn* SpacePawn);
	void Host();

	int32 SelectedBody = INDEX_NONE;
	bool bShowHelp = false;
	double LastClickSeconds = -10;
	int32 LastClickBody = INDEX_NONE;
	int32 TeammateCursor = 0;
};
