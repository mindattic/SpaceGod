// US-T95: sg.Tractor, a cargo-laden wreck blown open ahead of the hull flown and the tractor beam on its pods (and the
// screenshots of it, at both scales). Also the showcase tour's way in (US-T147: a TourStop::Console on "sg.Tractor demo").

#include "UnitySpaceApp.h"

#include "GameLaunch.h"
#include "SpaceGodLog.h"
#include "UnitySpaceAppActor.h"

#include "Camera/SpaceCamera.h"
#include "Target.h"
#include "SimClock.h"
#include "Ships.h"
#include "World.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
	SpaceGod::UnitySpaceApp* RunningApp()
	{
		if (!GEngine) return nullptr;
		for (const FWorldContext& c : GEngine->GetWorldContexts())
		{
			if (c.WorldType != EWorldType::Game && c.WorldType != EWorldType::PIE) continue;
			auto* actor = AUnitySpaceAppActor::Find(c.World());
			if (actor != nullptr && actor->GetApp() != nullptr) return actor->GetApp();
		}
		return nullptr;
	}

	/// <summary>One run of the command, stepped once a frame (each step returns true when done), as sg.BioMass's.</summary>
	struct Run
	{
		using Step = std::function<bool(SpaceGod::UnitySpaceApp&, double)>;
		std::vector<std::pair<FString, Step>> steps;
		size_t at = 0;
		double stepStart = -1;
		double started = FPlatformTime::Seconds();
		bool askedNewGame = false;
		bool quit = false;

		bool Tick()
		{
			auto* app = RunningApp();
			double now = FPlatformTime::Seconds();
			if (app == nullptr)
			{
				if (!askedNewGame && now - started > 2.0)
				{
					askedNewGame = true;
					UE_LOG(LogSpaceGod, Display, TEXT("sg.Tractor: no game running; starting a new one"));
					SpaceGod::GameLaunch::NewGame();
				}
				if (now - started > 180.0) return Finish(TEXT("no game came up"));
				return true;
			}
			if (at >= steps.size()) return Finish(nullptr);
			if (stepStart < 0) stepStart = now;
			if (steps[at].second(*app, now - stepStart))
			{
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Tractor: %s"), *steps[at].first);
				at++;
				stepStart = -1;
			}
			else if (now - stepStart > 120.0) return Finish(*FString::Printf(TEXT("timed out at '%s'"), *steps[at].first));
			return true;
		}

		bool Finish(const TCHAR* failure)
		{
			if (failure) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.Tractor: %s"), failure); }
			else { UE_LOG(LogSpaceGod, Display, TEXT("sg.Tractor: done")); }
			if (quit) FPlatformMisc::RequestExit(false);
			return false;
		}
	};

	void Start(const TArray<FString>& args)
	{
		FString mode = TEXT("demo"), scale, shots;
		bool quit = false;
		for (int i = 0; i < args.Num(); i++)
		{
			const FString& a = args[i];
			if (a.Equals(TEXT("wreck"), ESearchCase::IgnoreCase) || a.Equals(TEXT("demo"), ESearchCase::IgnoreCase)) mode = a.ToLower();
			else if (a.Equals(TEXT("game"), ESearchCase::IgnoreCase) || a.Equals(TEXT("real"), ESearchCase::IgnoreCase)) scale = a.ToLower();
			else if (a.Equals(TEXT("shots"), ESearchCase::IgnoreCase) && i + 1 < args.Num()) shots = args[++i];
			else if (a.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) quit = true;
		}

		auto run = std::make_shared<Run>();
		run->quit = quit;
		auto wait = [](double seconds) { return [seconds](SpaceGod::UnitySpaceApp&, double t) { return t >= seconds; }; };
		auto toScale = [](double goal)
		{
			return [goal](SpaceGod::UnitySpaceApp& app, double)
			{
				if (app.ScaleGoal != goal) app.ToggleScale();
				return !app.ScaleAnimating();
			};
		};
		auto showcase = [](int step)
		{
			return [step](SpaceGod::UnitySpaceApp& app, double)
			{
				cs::String failed;
				if (!app.TractorShowcase(step, failed)) throw std::runtime_error(failed.empty() ? "it cannot be shown here" : failed.c_str());
				return true;
			};
		};
		auto shot = [](FString path)
		{
			return [path](SpaceGod::UnitySpaceApp&, double)
			{
				FScreenshotRequest::RequestScreenshot(path, true, false);
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Tractor: screenshot %s"), *path);
				return true;
			};
		};

		if (mode == TEXT("wreck") && shots.IsEmpty())
		{
			run->steps.push_back({ TEXT("cargo wreck spawned"), [](SpaceGod::UnitySpaceApp& app, double)
			{
				cs::String said;
				auto w = app.SpawnCargoWreck(said);
				if (w == nullptr) throw std::runtime_error(said.c_str());
				app.ShowToast("A laden freighter breaks up just ahead, cargo pods spilling: Shift+T to tractor them in");
				return true;
			} });
		}
		else
		{
			bool both = !shots.IsEmpty() && scale.IsEmpty();
			double first = scale == TEXT("real") ? 0.0 : 1.0;
			run->steps.push_back({ TEXT("game up"), wait(shots.IsEmpty() ? 0.0 : 3.0) });
			if (!scale.IsEmpty() || both) run->steps.push_back({ first > 0.5 ? TEXT("game scale") : TEXT("realistic scale"), toScale(first) });
			run->steps.push_back({ TEXT("at a helm"), showcase(0) });
			if (!shots.IsEmpty())
				// Further back than the usual chase, so the hull, the wreck ahead and the beam between share the frame.
				run->steps.push_back({ TEXT("wide chase"), [](SpaceGod::UnitySpaceApp& app, double)
				{
					if (app.World() != nullptr && app.World()->PilotedShip != nullptr) app.SpaceCamera->EnterPilotChase(SpaceGod::Target::OfShip(app.World()->PilotedShip), 16);
					return true;
				} });
			run->steps.push_back({ TEXT("settled"), wait(3.0) });
			run->steps.push_back({ TEXT("cargo wreck blown open ahead"), showcase(1) });
			run->steps.push_back({ TEXT("pods drifting"), wait(1.5) });
			run->steps.push_back({ TEXT("tractor beam locked"), showcase(2) });
			if (!shots.IsEmpty())
			{
				FString tag = first > 0.5 ? TEXT("game") : TEXT("real");
				run->steps.push_back({ TEXT("pulling"), wait(2.5) });
				run->steps.push_back({ TEXT("shot"), shot(FPaths::Combine(shots, FString::Printf(TEXT("tractor-%s.png"), *tag))) });
				run->steps.push_back({ TEXT("shot written"), wait(1.5) });
				if (both)
				{
					run->steps.push_back({ TEXT("realistic scale"), toScale(0.0) });
					run->steps.push_back({ TEXT("wide chase"), [](SpaceGod::UnitySpaceApp& app, double)
					{
						if (app.World() != nullptr && app.World()->PilotedShip != nullptr) app.SpaceCamera->EnterPilotChase(SpaceGod::Target::OfShip(app.World()->PilotedShip), 16);
						return true;
					} });
					run->steps.push_back({ TEXT("settled at the realistic scale"), wait(2.0) });
					run->steps.push_back({ TEXT("tractor beam held"), showcase(2) });
					run->steps.push_back({ TEXT("pulling"), wait(1.5) });
					run->steps.push_back({ TEXT("shot"), shot(FPaths::Combine(shots, TEXT("tractor-real.png"))) });
					run->steps.push_back({ TEXT("shot written"), wait(1.5) });
				}
			}
		}

		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float)
		{
			try { return run->Tick(); }
			catch (const std::exception& e) { return run->Finish(*FString(UTF8_TO_TCHAR(e.what()))); }
		}));
	}

	/// US-T95: <c>sg.Tractor [demo|wreck] [game|real] [shots &lt;dir&gt;] [quit]</c>. <c>wreck</c> blows a cargo-laden freighter
	/// apart just ahead of the hull flown (or the camera); <c>demo</c> (the default) also takes a helm if none is held and locks
	/// the tractor beam onto the nearest pod. <c>shots</c> saves tractor-game.png and tractor-real.png of the beam pulling a pod.
	FAutoConsoleCommand GTractorCommand(
		TEXT("sg.Tractor"),
		TEXT("US-T95: sg.Tractor wreck spawns a cargo-laden wreck ahead; sg.Tractor [demo] [game|real] also takes a helm and locks the "
			"tractor beam on its pods; sg.Tractor shots <dir> [quit] screenshots the beam pulling a pod at both scales."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Start(args); }));
}
