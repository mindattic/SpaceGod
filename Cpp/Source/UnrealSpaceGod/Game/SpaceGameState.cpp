#include "Game/SpaceGameState.h"

#include "Net/UnrealNetwork.h"

void ASpaceGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASpaceGameState, Scene);
}
