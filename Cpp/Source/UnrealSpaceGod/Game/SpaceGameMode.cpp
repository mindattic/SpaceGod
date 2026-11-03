#include "Game/SpaceGameMode.h"
#include "Game/SpaceGameState.h"
#include "Game/SpaceHUD.h"
#include "Game/SpacePlayerController.h"
#include "Bootstrap.h"
#include "Coop/CoopPeer.h"
#include "Coop/CoopSmoke.h"
#include "Coop/SpaceCoopLink.h"
#include "GameLaunch.h"
#include "UnitySpaceApp.h"
#include "UnitySpaceAppActor.h"
#include "UnrealSpace.h"
#include "UnrealSpaceGodLog.h"
#include "Smoke/SpaceGodSmoke.h"

#include "BodyCatalog.h"
#include "Fleet.h"
#include "Systems/Galaxy.h"
#include "World.h"
#include "Shared/Profiles/Profiles.h"

#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"

using SpaceGod::Coop::CoopPeer;
namespace CoopSmoke = SpaceGod::Coop::CoopSmoke;

ASpaceGameMode::ASpaceGameMode()
{
	DefaultPawnClass = nullptr; // the app's camera is the view (AUnitySpaceAppActor)
	PlayerControllerClass = ASpacePlayerController::StaticClass();
	GameStateClass = ASpaceGameState::StaticClass();
	HUDClass = ASpaceHUD::StaticClass();
}

void ASpaceGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	Scene = UGameplayStatics::ParseOption(Options, TEXT("Scene"));
	if (Scene.IsEmpty()) Scene = UTF8_TO_TCHAR(SpaceGod::GameLaunch::MenuScene);
}

void ASpaceGameMode::StartPlay()
{
	Super::StartPlay();

	// SceneManager.LoadScene: reopen this (empty) map for the other scene; a listen server keeps listening, and takes
	// its co-op clients along (ServerTravel) when it has any (US-T44).
	TWeakObjectPtr<UWorld> WeakWorld = GetWorld();
	SpaceGod::GameLaunch::SceneLoader = [WeakWorld](const cs::String& SceneName)
	{
		UWorld* World = WeakWorld.Get();
		if (!World) return;
		const FString Map = UWorld::RemovePIEPrefix(World->GetOutermost()->GetName());
		FString Options = FString::Printf(TEXT("Scene=%s"), *SpaceGod::UnrealSpace::ToFString(SceneName));
		if (World->GetNetMode() == NM_ListenServer)
		{
			Options += TEXT("?listen");
			if (World->GetNetDriver() && World->GetNetDriver()->ClientConnections.Num() > 0)
			{
				World->ServerTravel(FString::Printf(TEXT("%s?%s"), *Map, *Options));
				return;
			}
		}
		UGameplayStatics::OpenLevel(World, FName(*Map), true, Options);
	};

	// The host (or a standalone game) decides the scene; the GameState carries it to clients.
	if (ASpaceGameState* State = GetGameState<ASpaceGameState>()) State->SetScene(Scene);
	// -SpaceGodSmoke / -SpaceGodUiShots / -SpaceGodViews (tools/play.ps1, US-T38): the views check builds its own scene.
	if (SpaceGod::Smoke::BeginScene(GetWorld())) return;

	// US-T44: a listen server (tools/play.ps1 -Mode host) hosts co-op in the game scene; standalone stays the original
	// single-player path. (The C# track's ASpaceGameMode.BeginPlay.)
	const bool bHosting = GetNetMode() == NM_ListenServer;
	if (bHosting && CoopSmoke::Active())
	{
		FString Root = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SmokeProfiles"), TEXT("CoopHost")));
		IFileManager::Get().MakeDirectory(*Root, true);
		SpaceGod::Shared::Profiles::UseRoot(cs::String(TCHAR_TO_UTF8(*Root)));
	}
	const bool bGameScene = Scene == UTF8_TO_TCHAR(SpaceGod::GameLaunch::GameScene);
	if (bHosting && bGameScene) CoopPeer::StartHost(CoopPeer::NameFromCommandLine(CoopSmoke::Active() ? "Host" : ""));

	const double Started = FPlatformTime::Seconds();
	SpaceGod::Bootstrap::EnsureApp(GetWorld(), SpaceGod::UnrealSpace::FromFString(Scene));

	if (CoopPeer::Active() && bGameScene)
	{
		AUnitySpaceAppActor* Actor = AUnitySpaceAppActor::Find(GetWorld());
		if (SpaceGod::UnitySpaceApp* App = Actor ? Actor->GetApp() : nullptr)
		{
			UE_LOG(LogSpaceGod, Display, TEXT("UnitySpaceApp up in %lld ms: %d systems, %d bodies in %s, %d ships (C++)"),
				static_cast<long long>((FPlatformTime::Seconds() - Started) * 1000.0), App->Galaxy->Systems.Count(), App->Catalog()->Count(),
				*SpaceGod::UnrealSpace::ToFString(App->ActiveSystem->Name), App->Fleet()->Ships().Count());
			if (CoopSmoke::Active()) SpaceGod::UnityCompat::Application::SetTargetFrameRate(60); // the co-op smoke's pace
		}
	}
}

void ASpaceGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	// US-T44: each client that logs in gets its link actor (its RPCs).
	CoopPeer* Peer = CoopPeer::Current();
	if (Peer == nullptr || !Peer->IsHost() || NewPlayer == nullptr || NewPlayer->IsLocalController()) return;
	ASpaceCoopLink* Link = ASpaceCoopLink::SpawnFor(NewPlayer);
	if (Link == nullptr)
	{
		UE_LOG(LogSpaceGod, Error, TEXT("SpaceGodCoop: could not spawn a link for a new player"));
		return;
	}
	CoopLinks.Add(NewPlayer, Link);
	Peer->HostLogin(Link);
	CoopPeer::Log("a player logged in; link spawned");
}

void ASpaceGameMode::Logout(AController* Exiting)
{
	// US-T44: leaving hands the player's seat back to its AI on every peer.
	if (APlayerController* PC = Cast<APlayerController>(Exiting))
	{
		TObjectPtr<ASpaceCoopLink> Link;
		if (CoopLinks.RemoveAndCopyValue(PC, Link) && Link)
		{
			if (CoopPeer* Peer = CoopPeer::Current()) Peer->HostLogout(Link.Get());
			Link->Destroy();
		}
	}
	Super::Logout(Exiting);
}

void ASpaceGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SpaceGod::GameLaunch::SceneLoader = nullptr;
	if (CoopPeer* Peer = CoopPeer::Current(); Peer && Peer->IsHost()) CoopPeer::Stop(); // US-T44
	CoopLinks.Empty();
	Super::EndPlay(EndPlayReason);
}
