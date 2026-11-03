#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "SpaceGameState.generated.h"

/**
 * The host's simulation clock as clients see it: an anchor (a Julian date at a server time) plus the warp index.
 * Only the host's app changes its clock (SimClock, driven by the app's own input or by commands its Session applies);
 * the host publishes an anchor whenever the warp or pause changes, or the date jumps, and every client works the
 * current date out from the anchor and the server's clock, so all players see the planets in the same places without
 * the date streaming every frame. (Kept from the first Sol-only slice; the co-op ticket networks the Session.)
 */
USTRUCT()
struct FSimClockState
{
	GENERATED_BODY()

	UPROPERTY() double AnchorJd = 0;
	UPROPERTY() double AnchorServerSeconds = 0;
	UPROPERTY() int32 WarpIndex = 0;
	UPROPERTY() double SimSecondsPerSecond = 1;
	UPROPERTY() bool bPaused = false;
};

UCLASS()
class ASpaceGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	ASpaceGameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	/// The host's date now, as a client reckons it (0 before the first anchor arrives).
	double HostJd() const;
	const FSimClockState& GetClock() const { return Clock; }

	/// Host: which Unity scene this session is in (GameLaunch); replicated so clients equip the same.
	void SetScene(const FString& InScene) { Scene = InScene; }

private:
	UFUNCTION()
	void OnRep_Scene();

	/// Host: re-anchors from its app's clock when the warp or pause changed or the date left the extrapolation.
	void PublishHostClock();

	UPROPERTY(Replicated)
	FSimClockState Clock;

	UPROPERTY(ReplicatedUsing = OnRep_Scene)
	FString Scene;
};
