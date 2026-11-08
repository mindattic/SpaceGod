#include "Bootstrap.h"
#include "GameLaunch.h"
#include "UnitySpaceAppActor.h"
#include "UI/MainMenu.h"
#include "ModelViewer.h"
#include "Shared/Profiles/Profiles.h"
#include "Input/InputDevice.h"
#include "MindAtticHeadless.h"
#include "UI/UiMotion.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"

namespace SpaceGod
{
	namespace
	{
		/// Environment.GetCommandLineArgs() for the ported code that reads it (ModelViewer's -modelviewer <name>).
		void FillCommandLineArgs()
		{
			auto& args = Shared::Profiles::CommandLineArgs;
			if (args.Count() > 0) return;
			args.Add(cs::String(TCHAR_TO_UTF8(FPlatformProcess::ExecutablePath())));
			const TCHAR* cursor = FCommandLine::Get();
			FString token;
			while (FParse::Token(cursor, token, false)) args.Add(cs::String(TCHAR_TO_UTF8(*token)));
		}

		/// ModelViewer's [RuntimeInitializeOnLoadMethod(AfterSceneLoad)] Ensure after every scene load, and Unity's player
		/// loop for it: Tick once a frame after the actors (the app's Update), DestroyInstance at exit (US-T31).
		void EnsureModelViewer()
		{
			ModelViewer::Ensure();
			static bool hooked = false;
			if (hooked) return;
			hooked = true;
			FWorldDelegates::OnWorldPostActorTick.AddLambda([](UWorld* w, ELevelTick, float)
			{
				static uint64 lastFrame = ~0ull;
				if (w == nullptr || !w->IsGameWorld() || lastFrame == GFrameCounter) return;
				lastFrame = GFrameCounter;
				ModelViewer::Tick();
			});
			FCoreDelegates::OnPreExit.AddLambda([] { ModelViewer::DestroyInstance(); });
		}
	}

	void Bootstrap::EnsureApp(UWorld* world, const cs::String& scene)
	{
		FillCommandLineArgs();
		EnsureModelViewer();
		UI::UiMotion::SceneArrived(); // US-T255: a fade held black for this scene comes back in
		// The main menu is a scene of its own, and the app is not made in it: the menu loads the game scene
		// when the player starts. A run for the tour, a capture or -continue goes straight there.
		if (scene == GameLaunch::MenuScene)
		{
			if (!GameLaunch::SkipsMenu())
			{
				// MainMenu.unity's camera and starfield need the scene host (no app): spawned first, then the menu.
				FActorSpawnParameters params;
				params.Name = TEXT("MainMenu");
				params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				params.bDeferConstruction = true;
				if (auto* host = world->SpawnActor<AUnitySpaceAppActor>(AUnitySpaceAppActor::StaticClass(), FTransform::Identity, params))
				{
					host->bSceneHost = true;
					TWeakObjectPtr<UWorld> menuWorld = world;
					host->SceneUpdate = [menuWorld] { if (menuWorld.IsValid()) UI::MainMenu::DrawGui(menuWorld.Get(), nullptr); };
					host->FinishSpawning(FTransform::Identity);
				}
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
			app->AddCaptureRunner(); // app.gameObject.AddComponent<CaptureRunner>() (US-T31)
		}

		// US-T233, US-T234: a headless run (--headless or MINDATTIC_HEADLESS=1, decided by the shared MindAtticShared
		// plugin, Unreal.Shared) is this same ordinary session (not the -SpaceGodSmoke tour, not -SpaceGodViews) for
		// scripted/automated testing. The plugin keeps the window away and logs MINDATTIC_HEADLESS_READY; with no
		// window there is no real mouse/keyboard for UnitySpaceAppActor::Tick to feed, so the app takes scripted input
		// like every other driven run here, left for a test or CI harness to write (the ScriptedDevice pattern,
		// console ExecCmds, or the frames FrameFeed.h hands back under -RenderOffScreen).
		if (FMindAtticHeadless::IsActive())
		{
			app->bScriptedInput = true;
			Input::InputDevice::current().Clear();
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
