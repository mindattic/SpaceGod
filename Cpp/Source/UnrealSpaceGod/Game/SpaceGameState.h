#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "SpaceGameState.generated.h"

class ASolSystem;

/**
 * The shared simulation clock: an anchor (a Julian date at a server time) plus the warp and pause state. Only the
 * server changes it; every client works the current date out from the replicated anchor and the server's clock, so
 * all players see the planets in the same places without the date streaming every frame.
 */
USTRUCT()
struct FSimClockState
{
	GENERATED_BODY()

	UPROPERTY() double AnchorJd = 0;
	UPROPERTY() double AnchorServerSeconds = 0;
	UPROPERTY() int32 WarpIndex = 4;
	UPROPERTY() bool bPaused = false;
};

/** A line in the co-op log: who changed the time, who marked what. */
struct FCoopAnnouncement
{
	FString Text;
	double RealSeconds = 0;
};

/** A body a player marked for everyone, shown with a ping ring for a few seconds. */
struct FCoopPing
{
	int32 Body = INDEX_NONE;
	FString Player;
	double RealSeconds = 0;
};

UCLASS()
class ASpaceGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	ASpaceGameState();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	double CurrentJd() const;
	int32 GetWarpIndex() const { return Clock.WarpIndex; }
	bool IsPaused() const { return Clock.bPaused; }
	ASolSystem* GetSolSystem() const { return SolSystem; }

	// Server only: the controllers' RPCs land here.
	void ChangeWarp(int32 Delta, const FString& Who);
	void TogglePause(const FString& Who);
	void ResetToNow(const FString& Who);
	void Ping(int32 Body, const FString& Who);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastAnnounce(const FString& Text);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPing(int32 Body, const FString& Who);

	const TArray<FCoopAnnouncement>& GetAnnouncements() const { return Announcements; }
	const TArray<FCoopPing>& GetPings() const { return Pings; }

private:
	void Reanchor();

	UPROPERTY(Replicated)
	FSimClockState Clock;

	UPROPERTY()
	TObjectPtr<ASolSystem> SolSystem;

	TArray<FCoopAnnouncement> Announcements;
	TArray<FCoopPing> Pings;
};
