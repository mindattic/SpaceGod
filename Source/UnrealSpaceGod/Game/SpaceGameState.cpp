#include "Game/SpaceGameState.h"
#include "Game/SolSystem.h"
#include "Sim/Astro.h"
#include "Sim/SimClock.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

ASpaceGameState::ASpaceGameState()
{
	Clock.WarpIndex = SimClock::DefaultWarpIndex;
}

void ASpaceGameState::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		Clock.AnchorJd = Astro::JulianDate(FDateTime::UtcNow());
		Clock.AnchorServerSeconds = GetServerWorldTimeSeconds();
	}

	// Every machine that draws builds its own copy of the system; only the clock above is shared.
	if (GetNetMode() != NM_DedicatedServer)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SolSystem = GetWorld()->SpawnActor<ASolSystem>(ASolSystem::StaticClass(), FTransform::Identity, Params);
	}
}

void ASpaceGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASpaceGameState, Clock);
}

double ASpaceGameState::CurrentJd() const
{
	// A client that has not received the clock yet shows the present.
	if (Clock.AnchorJd == 0) return Astro::JulianDate(FDateTime::UtcNow());
	if (Clock.bPaused) return Clock.AnchorJd;
	const double Elapsed = GetServerWorldTimeSeconds() - Clock.AnchorServerSeconds;
	const double Rate = SimClock::Levels[FMath::Clamp(Clock.WarpIndex, 0, SimClock::NumLevels - 1)].SimSecondsPerSecond;
	return Clock.AnchorJd + Elapsed * Rate / Astro::SecondsPerDay;
}

void ASpaceGameState::Reanchor()
{
	Clock.AnchorJd = CurrentJd();
	Clock.AnchorServerSeconds = GetServerWorldTimeSeconds();
}

void ASpaceGameState::ChangeWarp(int32 Delta, const FString& Who)
{
	const int32 Next = FMath::Clamp(Clock.WarpIndex + Delta, 0, SimClock::NumLevels - 1);
	if (Next == Clock.WarpIndex) return;
	Reanchor();
	Clock.WarpIndex = Next;
	MulticastAnnounce(FString::Printf(TEXT("%s set time warp to %s"), *Who, SimClock::Levels[Next].Label));
}

void ASpaceGameState::TogglePause(const FString& Who)
{
	Reanchor();
	Clock.bPaused = !Clock.bPaused;
	MulticastAnnounce(FString::Printf(TEXT("%s %s time"), *Who, Clock.bPaused ? TEXT("paused") : TEXT("resumed")));
}

void ASpaceGameState::ResetToNow(const FString& Who)
{
	Clock.AnchorJd = Astro::JulianDate(FDateTime::UtcNow());
	Clock.AnchorServerSeconds = GetServerWorldTimeSeconds();
	MulticastAnnounce(FString::Printf(TEXT("%s brought time back to now"), *Who));
}

void ASpaceGameState::Ping(int32 Body, const FString& Who)
{
	MulticastPing(Body, Who);
}

void ASpaceGameState::MulticastAnnounce_Implementation(const FString& Text)
{
	Announcements.Add({ Text, FPlatformTime::Seconds() });
	if (Announcements.Num() > 8) Announcements.RemoveAt(0);
}

void ASpaceGameState::MulticastPing_Implementation(int32 Body, const FString& Who)
{
	Pings.Add({ Body, Who, FPlatformTime::Seconds() });
	if (Pings.Num() > 16) Pings.RemoveAt(0);
	if (const ASolSystem* Sol = SolSystem.Get(); Sol && Body >= 0 && Body < Sol->NumBodies())
		MulticastAnnounce_Implementation(FString::Printf(TEXT("%s marked %s"), *Who, *Sol->GetCatalog()[Body].Name));
}
