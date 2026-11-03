#include "Game/SpaceGameMode.h"
#include "Game/SpaceGameState.h"
#include "Game/SpaceHUD.h"
#include "Game/SpacePawn.h"
#include "Game/SpacePlayerController.h"

ASpaceGameMode::ASpaceGameMode()
{
	DefaultPawnClass = ASpacePawn::StaticClass();
	PlayerControllerClass = ASpacePlayerController::StaticClass();
	GameStateClass = ASpaceGameState::StaticClass();
	HUDClass = ASpaceHUD::StaticClass();
}
