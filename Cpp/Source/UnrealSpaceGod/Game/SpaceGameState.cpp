#include "Game/SpaceGameState.h"
#include "UnitySpaceAppActor.h"
#include "UnitySpaceApp.h"
#include "Bootstrap.h"
#include "GameLaunch.h"
#include "UnrealSpace.h"
#include "SimClock.h"
#include "Astro.h"

#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

ASpaceGameState::ASpaceGameState()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
}

void ASpaceGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASpaceGameState, Clock);
	DOREPLIFETIME(ASpaceGameState, Scene);
}

void ASpaceGameState::OnRep_Scene()
{
	// A client joining the host's game scene equips its own app (the menu stays the host's).
	if (HasAuthority() || Scene != UTF8_TO_TCHAR(SpaceGod::GameLaunch::GameScene)) return;
	SpaceGod::Bootstrap::EnsureApp(GetWorld(), SpaceGod::UnrealSpace::FromFString(Scene));
}

void ASpaceGameState::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority() && GetNetMode() != NM_Standalone) PublishHostClock();
}

double ASpaceGameState::HostJd() const
{
	if (Clock.AnchorJd == 0) return 0;
	if (Clock.bPaused) return Clock.AnchorJd;
	const double Elapsed = GetServerWorldTimeSeconds() - Clock.AnchorServerSeconds;
	return Clock.AnchorJd + Elapsed * Clock.SimSecondsPerSecond / SpaceGod::Core::Astro::SecondsPerDay;
}

void ASpaceGameState::PublishHostClock()
{
	AUnitySpaceAppActor* Host = AUnitySpaceAppActor::Find(GetWorld());
	SpaceGod::UnitySpaceApp* App = Host ? Host->GetApp() : nullptr;
	if (!App || !App->Clock) return;
	const auto& SimClock = *App->Clock;
	const bool bPaused = SimClock.PauseHeld();
	const double Rate = SpaceGod::Core::SimClock::Levels()[SimClock.WarpIndex].SimSecondsPerSecond * SimClock.Speed;
	// Re-anchor on any change of rate, or once the extrapolated date drifts more than a second (sim) from the host's.
	const bool bDrifted = FMath::Abs(HostJd() - SimClock.Jd) * SpaceGod::Core::Astro::SecondsPerDay > FMath::Max(1.0, Rate * 0.05);
	if (Clock.AnchorJd != 0 && Clock.WarpIndex == SimClock.WarpIndex && Clock.bPaused == bPaused && !bDrifted) return;
	Clock.AnchorJd = SimClock.Jd;
	Clock.AnchorServerSeconds = GetServerWorldTimeSeconds();
	Clock.WarpIndex = SimClock.WarpIndex;
	Clock.SimSecondsPerSecond = Rate;
	Clock.bPaused = bPaused;
}
