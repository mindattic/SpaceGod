#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "SpacePawn.generated.h"

class UCameraComponent;
class ASolSystem;

/** One frame of the local player's camera input, gathered by ASpacePlayerController. */
struct FSpaceViewInput
{
	FVector2D LookDelta = FVector2D::ZeroVector; // pixels, while the right button is held
	FVector Move = FVector::ZeroVector;          // forward, right, up in -1..1
	float Wheel = 0;                             // notches, + = in
	bool bBoost = false;
	bool bFine = false;
};

/**
 * A player's camera: Homeworld-style focus (orbit a body, wheel to zoom) and Quake-style free flight (WASD swims,
 * leaving focus). The view is held relative to a frame body in units of its drawn radius, so it stays put on the
 * world through a gameplay/realistic scale switch.
 *
 * Co-op: the owner sends its view to the server as a frame body plus a true ecliptic offset in km, which is
 * independent of anyone's scale setting; other players' machines map it through their own scale to draw a marker.
 */
UCLASS()
class ASpacePawn : public APawn
{
	GENERATED_BODY()

public:
	ASpacePawn();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void ApplyInput(const FSpaceViewInput& Input, float DeltaSeconds);

	/** Orbit a body at a distance in its drawn radii (default: about four, framing it). */
	void FocusOn(int32 Body, double DistanceRadii = -1);

	/** Back out to see the whole planetary system around the Sun. */
	void SystemView();

	/** Fly to a teammate's viewpoint (free flight, their frame body and heading). */
	void JumpTo(const ASpacePawn* Other);

	bool IsFocused() const { return bFocused; }
	int32 GetFrameBody() const { return FrameBody; }

	/** Frame body as seen by a remote machine (from replication), or this machine's when locally controlled. */
	int32 GetSharedFrameBody() const { return IsLocallyControlled() ? FrameBody : RepBody; }

	FString GetPlayerLabel() const;

private:
	bool EnsureStarted(ASolSystem* Sol);
	void UpdateLocal(ASolSystem* Sol, float DeltaSeconds);
	void UpdateRemote(ASolSystem* Sol);
	void Reframe(ASolSystem* Sol);

	UFUNCTION(Server, Unreliable)
	void ServerUpdateView(int32 Body, const FVector& OffsetKm, const FRotator& Rotation);

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(Replicated) int32 RepBody = INDEX_NONE;
	UPROPERTY(Replicated) FVector RepOffsetKm = FVector::ZeroVector;
	UPROPERTY(Replicated) FRotator RepRotation = FRotator::ZeroRotator;

	bool bStarted = false;
	bool bLocalViewStarted = false;
	bool bFocused = true;
	int32 FrameBody = INDEX_NONE;
	FVector OffsetRadii = FVector::ZeroVector; // world axes, in frame-body drawn radii
	FRotator ViewRotation = FRotator(-20, 0, 0);
	double FocusDistance = 4.0;
	double SwimSpeed = 1.0;
	double SendTimer = 0;
};
