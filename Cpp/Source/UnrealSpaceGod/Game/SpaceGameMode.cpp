#include "Game/SpaceGameMode.h"
#include "Game/SpaceGameState.h"
#include "Game/SpaceHUD.h"
#include "Game/SpacePlayerController.h"
#include "Bootstrap.h"
#include "GameLaunch.h"
#include "UnrealSpace.h"
#include "Smoke/SpaceGodSmoke.h"

#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

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

	// SceneManager.LoadScene: reopen this (empty) map for the other scene; a listen server keeps listening.
	TWeakObjectPtr<UWorld> WeakWorld = GetWorld();
	SpaceGod::GameLaunch::SceneLoader = [WeakWorld](const cs::String& SceneName)
	{
		UWorld* World = WeakWorld.Get();
		if (!World) return;
		const FString Map = UWorld::RemovePIEPrefix(World->GetOutermost()->GetName());
		FString Options = FString::Printf(TEXT("Scene=%s"), *SpaceGod::UnrealSpace::ToFString(SceneName));
		if (World->GetNetMode() == NM_ListenServer) Options += TEXT("?listen");
		UGameplayStatics::OpenLevel(World, FName(*Map), true, Options);
	};

	// The host (or a standalone game) decides the scene; the GameState carries it to clients, which equip their own
	// app for it when it arrives (ASpaceGameState::OnRep_Scene).
	if (ASpaceGameState* State = GetGameState<ASpaceGameState>()) State->SetScene(Scene);
	// -SpaceGodSmoke / -SpaceGodUiShots / -SpaceGodViews (tools/play.ps1, US-T38): the views check builds its own scene.
	if (SpaceGod::Smoke::BeginScene(GetWorld())) return;
	SpaceGod::Bootstrap::EnsureApp(GetWorld(), SpaceGod::UnrealSpace::FromFString(Scene));
}

void ASpaceGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SpaceGod::GameLaunch::SceneLoader = nullptr;
	Super::EndPlay(EndPlayReason);
}
