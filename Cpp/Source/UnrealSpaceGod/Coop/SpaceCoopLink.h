#pragma once

// US-T44: a player's network link in co-op, the C# track's CSharp/.../Runtime/Coop/SpaceCoopLink.cs (US-T39) in C++.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Coop/CoopPeer.h"
#include "SpaceCoopLink.generated.h"

class APlayerController;
class AUnitySpaceAppActor;

/**
 * One remote player's link: the host spawns one per client that logs in, owned by that client's player controller and
 * relevant only to it, so the client may call its server RPCs and the host its client RPCs. Everything co-op says goes
 * through four RPCs: a reliable and an unreliable message each way (CoopPeer reads them).
 *
 * On the client the link is also what runs the game: the app actor does not step itself on a client (NM_Client), and
 * the link's Tick steps it once for each host frame waiting, up to MaxFramesPerTick a tick (a client that fell behind
 * catches up), as ASpaceGameMode's player loop does on the C# track's host.
 */
UCLASS()
class ASpaceCoopLink : public AActor, public SpaceGod::Coop::ICoopLink
{
	GENERATED_BODY()

public:
	ASpaceCoopLink();

	static constexpr int MaxFramesPerTick = 8;

	// ---- the four RPCs

	UFUNCTION(Client, Reliable)
	void ClientMessage(const FString& Message);

	UFUNCTION(Client, Unreliable)
	void ClientPresence(const FString& Message);

	UFUNCTION(Server, Reliable)
	void ServerMessage(const FString& Message);

	UFUNCTION(Server, Unreliable)
	void ServerPresence(const FString& Message);

	// ICoopLink
	virtual void Send(const cs::String& message) override;
	virtual void SendUnreliable(const cs::String& message) override;

	/// The host's link to `Player`, a client that just logged in.
	static ASpaceCoopLink* SpawnFor(APlayerController* Player);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	bool bStarted = false;
	bool bHelloSent = false;
	double StartSeconds = 0;
};
