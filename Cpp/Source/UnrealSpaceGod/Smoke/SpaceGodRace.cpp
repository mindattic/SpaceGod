// -SpaceGodRace (US-T46, docs/RACE.md): the C++ track's race tour, line for line the C# track's RaceRun.cs (same
// script, same frame counts), written as a SceneTour::Routine (C#'s IEnumerator) stepped once a frame from the core
// ticker. Run with -UseFixedTimeStep -FPS=60: every frame is 1/60 s of game time (the same simulation on both tracks,
// frame for frame) while the engine runs as fast as it can (no frame cap, no VSync). The shared sampler (SpaceGodRender's
// SpaceGodRace.cpp) times every frame and writes the results.

#include "Smoke/SpaceGodRace.h"

#include "Bootstrap.h"
#include "GameLaunch.h"
#include "SceneTour.h"
#include "UnitySpaceApp.h"
#include "UnitySpaceAppActor.h"
#include "UnrealSpaceGodLog.h"
#include "Target.h"
#include "Camera/SpaceCamera.h"
#include "Camera/StarTravel.h"
#include "Input/InputDevice.h"
#include "UI/MainMenu.h"
#include "UnityCompat/UnityRendering.h"
#include "UnityCompat/UnityRenderingTypes.h"
#include "UnityCompat/UnityRuntime.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Fleet.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"
#include "Combat/Battle.h"
#include "Combat/War.h"
#include "Shared/Profiles/Profiles.h"
#include "Systems/Galaxy.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

namespace SpaceGod::Race
{
	using namespace SpaceGod::Core;
	using namespace SpaceGod::UnityCompat;
	using Routine = SceneTour::Routine;

	namespace
	{
		constexpr double StartJd = 2461348.0; // 2026-11-03 12:00 UTC, the screenshots' (US-T45)
		constexpr int Seed = 1;
		constexpr int Warm = 120, IdleFrames = 1200, BattleWarm = 60, BattleFrames = 1200, MaxWarpFrames = 30, ArrivedFrames = 300;

		UWorld* GameWorld()
		{
			if (!GEngine) return nullptr;
			for (const FWorldContext& c : GEngine->GetWorldContexts())
				if (c.World() && c.World()->IsGameWorld() && c.World()->HasBegunPlay()) return c.World();
			return nullptr;
		}

		/// The game's app in the current scene (a scene change is a map load here), or null.
		UnitySpaceApp* App()
		{
			UWorld* w = GameWorld();
			AUnitySpaceAppActor* a = w ? AUnitySpaceAppActor::Find(w) : nullptr;
			if (!a || !a->HasActorBegunPlay()) return nullptr;
			// Wherever the real cursor is, nothing is hovered or pressed (the C# track's UnrealHost.NoPlayerInput), in
			// every scene, as the screenshots.
			if (!a->bScriptedInput)
			{
				a->bScriptedInput = true;
				Input::InputDevice::current().Clear();
			}
			return a->GetApp();
		}

		/// SystemLibrary.ExecuteConsoleCommand, as the C# track calls it.
		void Cmd(const FString& c) { UKismetSystemLibrary::ExecuteConsoleCommand(GameWorld(), c); }
		FString F(double v) { return FString::Printf(TEXT("%.3f"), v); }
		void Note(const TCHAR* k, double v) { Cmd(FString::Printf(TEXT("sg.Race.Note %s %s"), k, *F(v))); }

		/// One frame, handing the frame's probe times to the sampler.
		Routine Frame()
		{
			co_yield nullptr;
			Cmd(FString::Printf(TEXT("sg.Race.Frame %s %s %s"), *F(Probe::SimMs), *F(Probe::AppMs), *F(Probe::GuiMs)));
			Probe::SimMs = 0;
		}

		Routine Frames(int n)
		{
			for (int i = 0; i < n; i++) co_yield Frame();
		}

		void Uncapped()
		{
			Application::SetTargetFrameRate(-1);
			Cmd(TEXT("r.VSync 0"));
			Cmd(TEXT("t.MaxFPS 0"));
		}

		FString ResultsFile()
		{
			FString v;
			if (FParse::Value(FCommandLine::Get(), TEXT("-SpaceGodRaceOut="), v)) return v;
			return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("race-cpp.json"));
		}

		Routine Run()
		{
			const FString results = ResultsFile();
			Cmd(TEXT("sg.Race.Note track Cpp"));
			Cmd(TEXT("sg.Race.Note runtime MSVC_") + FString::FromInt(_MSC_FULL_VER));
			UnitySpaceApp::StartJd = StartJd;
			while (!UI::MainMenu::Showing() && App() == nullptr) co_yield nullptr;
			Cmd(TEXT("sg.Race.NoteAge processToMenuMs"));
			// The menu up for 30 frames (its first frames end before New Game's galaxy is built), then New Game.
			for (int i = 0; i < 30; i++) co_yield nullptr;
			const double t = Probe::Now();
			if (UI::MainMenu::Showing())
			{
				UnityRandom::InitState(Seed);
				GameLaunch::NewGame();
			}
			UnitySpaceApp* app = nullptr;
			for (int i = 0; i < 6000; i++)
			{
				app = App();
				if (app != nullptr && app->Clock->Jd > StartJd) break; // the first frame the game has run: playable
				co_yield nullptr;
			}
			if (app == nullptr) { UE_LOG(LogSpaceGod, Error, TEXT("SpaceGodRace: no game")); Application::Quit(); co_return; }
			Cmd(TEXT("sg.Race.NoteAge processToPlayableMs"));
			Note(TEXT("newGameToPlayableMs"), Probe::Now() - t);
			Uncapped();

			// Sol, as the game opens (default warp, the camera beside Earth).
			co_yield Frames(Warm);
			Uncapped();
			Cmd(TEXT("sg.Race.Phase sol-idle"));
			co_yield Frames(IdleFrames);
			Cmd(TEXT("sg.Race.End"));
			app = App();
			Note(TEXT("solIdleEndJd"), app->Clock->Jd);
			Note(TEXT("solIdleShips"), app->Fleet()->Ships().Count());
			Cmd(TEXT("memreport -full"));

			// Three and a half days at 1 week/s (each frame 2.8 hours of game time; every system stepped every frame). The two highest
			// warps, 1 yr/s and 1 month/s, trip US-T59 on both tracks within weeks of game time (a ring captured mid-queue).
			app = App();
			app->Clock->SetPaused(false);
			app->Clock->SetWarp(SimClock::Levels().Count() - 3);
			Cmd(TEXT("sg.Race.Phase week-warp"));
			co_yield Frames(MaxWarpFrames);
			Cmd(TEXT("sg.Race.End"));
			app = App();
			app->Clock->SetWarp(SimClock::DefaultWarpIndex);
			Note(TEXT("weekWarpEndJd"), app->Clock->Jd);
			co_yield Frames(30);

			// A staged battle (SceneTour's broadside showcase): 2 battleships + a cruiser each for Sol and Reach off
			// Uranus, watched at battle time, then 1 min/s.
			int uranus = (*app->Catalog())["Uranus"]->Index;
			double jd = app->Clock->Jd;
			auto dir = DVec3(1, 0.15, 0.4).Normalized();
			auto side = DVec3::Cross(DVec3(0, 1, 0), dir).Normalized();
			cs::List<ShipClass> classes{ ShipClass::Battleship, ShipClass::Battleship, ShipClass::Cruiser };
			cs::Ref<Faction> a = app->World()->Sol, d = app->World()->Reach;
			for (int i = 0; i < classes.Count(); i++)
				app->Fleet()->SpawnAt(cs::Cat("Showcase ", a->Name, " ", i + 1), classes[i], a, uranus, dir * (2000 + i * 400), -dir, jd);
			for (int i = 0; i < classes.Count(); i++)
				app->Fleet()->SpawnAt(cs::Cat("Showcase ", d->Name, " ", i + 1), classes[i], d, uranus, dir * (2000 + i * 400) + side * 3000, dir, jd);
			auto battle = app->World()->War->TryStartBattle(uranus, a, jd);
			if (battle != nullptr)
			{
				app->Clock->SetPaused(false);
				app->WatchBattle(battle);
				app->Clock->SetWarp(2);
				co_yield Frames(BattleWarm);
				Cmd(TEXT("sg.Race.Phase battle"));
				int n = 0;
				for (; n < BattleFrames; n++) co_yield Frame();
				Cmd(TEXT("sg.Race.End"));
				Note(TEXT("battleUnits"), battle->Units.Count());
				Note(TEXT("battleOver"), battle->Over ? 1 : 0);
			}
			else UE_LOG(LogSpaceGod, Error, TEXT("SpaceGodRace: no battle"));
			app = App();
			app->Clock->SetWarp(SimClock::DefaultWarpIndex);
			co_yield Frames(30);

			// To Alpha Centauri (StarTravel: the jump between the stars, the system promoted), a while there, and home
			// again (not timed).
			app = App();
			auto ac = app->Galaxy->Find("Alpha Centauri");
			cs::Ref<StarSystem> sol = app->ActiveSystem;
			if (ac != nullptr)
			{
				// The view's own trip (a followed ship, the battle's flagship, would be sent on a crew course instead).
				app->SpaceCamera->ExitFocus();
				app->Select(Target::None());
				Cmd(TEXT("sg.Race.Phase galaxy-ftl"));
				app->TravelTo(ac);
				int f = 0;
				while (App()->Travel->Active && f < 3000) { f++; co_yield Frame(); }
				Note(TEXT("ftlTravelFrames"), f);
				co_yield Frames(ArrivedFrames);
				Cmd(TEXT("sg.Race.End"));
				Note(TEXT("ftlArrivedIn"), App()->ActiveSystem->Name == "Alpha Centauri" ? 1 : 0);
				App()->TravelTo(sol);
				for (int g = 0; App()->Travel->Active && g < 3000; g++) co_yield nullptr;
				co_yield Frames(30);
			}

			Uncapped();
			// Saves and loads (Core/Persistence, the original's format), three of each, timed.
			double saveSum = 0, loadSum = 0, saveMin = 1e300, loadMin = 1e300;
			for (int k = 0; k < 3; k++)
			{
				app = App();
				double s0 = Probe::Now();
				bool ok = app->SaveTo("race", "US-T46 race");
				double sMs = Probe::Now() - s0;
				co_yield Frames(5);
				app = App();
				double l0 = Probe::Now();
				ok &= app->LoadFrom("race");
				double lMs = Probe::Now() - l0;
				co_yield Frames(5);
				if (!ok) UE_LOG(LogSpaceGod, Error, TEXT("SpaceGodRace: save/load failed"));
				saveSum += sMs; loadSum += lMs; saveMin = FMath::Min(saveMin, sMs); loadMin = FMath::Min(loadMin, lMs);
				Note(*FString::Printf(TEXT("save%dMs"), k + 1), sMs);
				Note(*FString::Printf(TEXT("load%dMs"), k + 1), lMs);
			}
			Note(TEXT("saveMinMs"), saveMin);
			Note(TEXT("loadMinMs"), loadMin);
			Note(TEXT("saveMeanMs"), saveSum / 3);
			Note(TEXT("loadMeanMs"), loadSum / 3);
			Uncapped();

			Note(TEXT("endJd"), App()->Clock->Jd);
			Cmd(TEXT("sg.Race.Write ") + results);
			UE_LOG(LogSpaceGod, Display, TEXT("SpaceGodRace: done -> %s"), *results);
			co_yield Frames(2);
			Application::Quit();
		}

		SceneTour::Runner runner;

		bool Tick(float)
		{
			if (!GameWorld()) return true;
			runner.MoveNext();
			return true;
		}
	}

	void BeginScene(UWorld*)
	{
		static bool started = false;
		if (started || !Bootstrap::HasCommandLineFlag("-SpaceGodRace")) return;
		started = true;
		// The race's profile in a scratch folder, fresh each run (as the screenshots' ShotProfiles).
		FString root = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RaceProfiles")));
		IFileManager::Get().DeleteDirectory(*root, false, true);
		IFileManager::Get().MakeDirectory(*root, true);
		Shared::Profiles::UseRoot(cs::String(TCHAR_TO_UTF8(*root)));
		runner = SceneTour::Runner(Run());
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick), 0.0f);
	}
}
