#include "Bootstrap.h"
#include "GameLaunch.h"
#include "UnitySpaceAppActor.h"
#include "UI/MainMenu.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"

namespace SpaceGod
{
	void Bootstrap::EnsureApp(UWorld* world, const cs::String& scene)
	{
		// The main menu is a scene of its own, and the app is not made in it: the menu loads the game scene
		// when the player starts. A run for the tour, a capture or -continue goes straight there.
		if (scene == GameLaunch::MenuScene)
		{
			if (!GameLaunch::SkipsMenu())
			{
				UI::MainMenu::Create(world); // the scene's MainMenu (UI ticket)
				return;
			}
			// SceneManager.sceneLoaded += AfterSkippingMenu: the game scene's EnsureApp equips it once it has loaded.
			GameLaunch::LoadScene(GameLaunch::GameScene);
			return;
		}
		Equip(world);
	}

	void Bootstrap::Equip(UWorld* world)
	{
		AUnitySpaceAppActor* app = nullptr;
		for (TActorIterator<AUnitySpaceAppActor> it(world); it; ++it) { app = *it; break; }
		if (app == nullptr)
		{
			FActorSpawnParameters params;
			params.Name = TEXT("Unity.SpaceGod");
			params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			app = world->SpawnActor<AUnitySpaceAppActor>(AUnitySpaceAppActor::StaticClass(), FTransform::Identity, params);
		}

		if (HasCommandLineFlag("-demo"))
		{
			// -capture's own timer (if requested) keeps running unmodified: -demo only adds
			// the tour, it does not change what -capture does.
			// seam: CaptureRunner (the scripted tour) is outside this ticket; it attaches to `app` here when ported.
		}
	}

	bool Bootstrap::HasCommandLineFlag(const cs::String& flag)
	{
		// Environment.GetCommandLineArgs(): each whitespace-separated token, compared ignoring case.
		const TCHAR* cursor = FCommandLine::Get();
		FString token;
		const FString wanted(UTF8_TO_TCHAR(flag.c_str()));
		while (FParse::Token(cursor, token, false))
			if (token.Equals(wanted, ESearchCase::IgnoreCase)) return true;
		return false;
	}
}
