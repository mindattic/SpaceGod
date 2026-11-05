#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "SpaceGameState.generated.h"

/**
 * Which Unity scene the host's session is in (GameLaunch), replicated so every machine knows.
 *
 * US-T44 replaced the first Sol-only slice's clock anchor (the host's date extrapolated on clients) with co-op's
 * lockstep, the C# track's: the host's clock reaches every client in each frame record, bit for bit (Coop/CoopPeer), and
 * a client's app is started by its link actor (ASpaceCoopLink), not by the scene arriving here.
 */
UCLASS()
class ASpaceGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/// Host: which Unity scene this session is in (GameLaunch); replicated so clients know the same.
	void SetScene(const FString& InScene) { Scene = InScene; }
	const FString& GetScene() const { return Scene; }

private:
	UPROPERTY(Replicated)
	FString Scene;
};
