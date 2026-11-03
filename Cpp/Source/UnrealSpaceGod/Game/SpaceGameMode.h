#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SpaceGameMode.generated.h"

/** Server-side rules: every player who joins gets a camera pawn; the GameState carries the shared clock. */
UCLASS()
class ASpaceGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASpaceGameMode();
};
