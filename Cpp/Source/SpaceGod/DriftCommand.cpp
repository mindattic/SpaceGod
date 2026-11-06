// US-T168 / US-T165: sg.Drift, the string aim and drift shown and screenshotted: a light fighter's 180 look with the hull
// whipping round after it, a heavy fighter's taut string, and a drift turned backwards while flying on forwards (its RCS
// jets firing, then the boost-out), at both scales. Also the dev menu's and the showcase tour's way in (US-T126, US-T147).

#include "UnitySpaceApp.h"

#include "GameLaunch.h"
#include "SpaceGodLog.h"
#include "Tour/ShowcaseTour.h"
#include "UI/CheatMenu.h"
#include "UnitySpaceAppActor.h"

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

	/// <summary>One run of the command, stepped once a frame (each step returns true when done), as sg.Tractor's.</summary>
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
					UE_LOG(LogSpaceGod, Display, TEXT("sg.Drift: no game running; starting a new one"));
					SpaceGod::GameLaunch::NewGame();
				}
				if (now - started > 180.0) return Finish(TEXT("no game came up"));
				return true;
			}
			if (at >= steps.size()) return Finish(nullptr);
			if (stepStart < 0) stepStart = now;
			if (steps[at].second(*app, now - stepStart))
			{
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Drift: %s"), *steps[at].first);
				at++;
				stepStart = -1;
			}
			else if (now - stepStart > 120.0) return Finish(*FString::Printf(TEXT("timed out at '%s'"), *steps[at].first));
			return true;
		}

		bool Finish(const TCHAR* failure)
		{
			if (auto* app = RunningApp()) app->DriftHeldByScript = false;
			if (failure) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.Drift: %s"), failure); }
			else { UE_LOG(LogSpaceGod, Display, TEXT("sg.Drift: done")); }
			if (quit) FPlatformMisc::RequestExit(false);
			return false;
		}
	};

	void Start(const TArray<FString>& args)
	{
		FString scale, shots;
		bool quit = false;
		for (int i = 0; i < args.Num(); i++)
		{
			const FString& a = args[i];
			if (a.Equals(TEXT("game"), ESearchCase::IgnoreCase) || a.Equals(TEXT("real"), ESearchCase::IgnoreCase)) scale = a.ToLower();
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
				if (!app.DriftShowcase(step, failed)) throw std::runtime_error(failed.empty() ? "it cannot be shown here" : failed.c_str());
				return true;
			};
		};
		auto look = [](double rightDeg, double upDeg)
		{
			return [rightDeg, upDeg](SpaceGod::UnitySpaceApp& app, double) { app.PilotLookBy(rightDeg, upDeg); return true; };
		};
		auto drift = [](bool held)
		{
			return [held](SpaceGod::UnitySpaceApp& app, double)
			{
				// Way along the nose first, in the same frame, so the drift has something to carry on.
				if (held)
				{
					cs::String failed;
					if (!app.DriftShowcase(2, failed)) throw std::runtime_error(failed.c_str());
				}
				app.DriftHeldByScript = held;
				return true;
			};
		};
		auto shot = [](FString path)
		{
			return [path](SpaceGod::UnitySpaceApp&, double)
			{
				FScreenshotRequest::RequestScreenshot(path, true, false);
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Drift: screenshot %s"), *path);
				return true;
			};
		};
		auto push = [&run](const TCHAR* name, Run::Step s) { run->steps.push_back({ name, std::move(s) }); };

		// The demo (and the tour): a light fighter flown, given way, turned to face backwards in a drift, then let go.
		auto demo = [&]()
		{
			push(TEXT("light fighter flown"), showcase(0));
			push(TEXT("settled"), wait(2.0));
			push(TEXT("drifting, way on"), drift(true));
			push(TEXT("look astern"), look(180, 6));
			push(TEXT("facing backwards, flying forwards"), wait(3.0));
			push(TEXT("let go: boost-out"), drift(false));
		};

		if (shots.IsEmpty())
		{
			if (!scale.IsEmpty()) push(TEXT("scale"), toScale(scale == TEXT("real") ? 0.0 : 1.0));
			demo();
		}
		else
		{
			push(TEXT("game up"), wait(3.0));
			for (int pass = 0; pass < 2; pass++)
			{
				bool game = pass == 0;
				if (!scale.IsEmpty() && (scale == TEXT("game")) != game) continue;
				FString tag = game ? TEXT("game") : TEXT("real");
				auto file = [&](const TCHAR* name) { return FPaths::Combine(shots, FString::Printf(TEXT("%s-%s.png"), name, *tag)); };
				push(game ? TEXT("game scale") : TEXT("realistic scale"), toScale(game ? 1.0 : 0.0));
				// US-T168, a light fighter: a full 180 at once, the hull lagging, then whipping round to meet it.
				push(TEXT("light fighter flown"), showcase(0));
				push(TEXT("settled"), wait(2.5));
				push(TEXT("shot"), shot(file(TEXT("light-ahead"))));
				push(TEXT("written"), wait(0.6));
				push(TEXT("look 180"), look(180, 6));
				push(TEXT("hull lagging"), wait(0.15));
				push(TEXT("shot"), shot(file(TEXT("light-look180-lagging"))));
				push(TEXT("hull whipping round"), wait(0.35));
				push(TEXT("shot"), shot(file(TEXT("light-look180-whip"))));
				push(TEXT("caught up"), wait(1.8));
				push(TEXT("shot"), shot(file(TEXT("light-look180-caught"))));
				push(TEXT("written"), wait(0.6));
				// US-T168, a heavy fighter: the string is taut, the look barely ahead of the turn.
				push(TEXT("heavy fighter flown"), showcase(1));
				push(TEXT("settled"), wait(2.5));
				push(TEXT("look 90"), look(90, 0));
				push(TEXT("swinging"), wait(0.5));
				push(TEXT("shot"), shot(file(TEXT("heavy-look-subtle"))));
				push(TEXT("written"), wait(1.5));
				// US-T165, drift: way on, turned to face backwards, RCS jets firing, then the boost-out.
				push(TEXT("light fighter flown"), showcase(0));
				push(TEXT("settled"), wait(2.5));
				push(TEXT("drifting, way on"), drift(true));
				push(TEXT("look astern"), look(180, 6));
				push(TEXT("RCS firing"), wait(0.12));
				push(TEXT("shot"), shot(file(TEXT("drift-rcs"))));
				push(TEXT("facing backwards"), wait(1.6));
				push(TEXT("shot"), shot(file(TEXT("drift-backwards"))));
				push(TEXT("written"), wait(0.5));
				push(TEXT("let go"), drift(false));
				push(TEXT("boosting out"), wait(0.3));
				push(TEXT("shot"), shot(file(TEXT("drift-boostout"))));
				push(TEXT("written"), wait(1.0));
				if (game)
				{
					// A frame burst for the video (tools: ffmpeg): a drift turned astern and let go, one frame a tick.
					push(TEXT("light fighter flown"), showcase(0));
					push(TEXT("settled"), wait(2.0));
					auto frame = std::make_shared<int>(0);
					auto dir = FPaths::Combine(shots, TEXT("video"));
					push(TEXT("burst"), [frame, dir](SpaceGod::UnitySpaceApp& app, double t)
					{
						int n = (*frame)++;
						if (n == 0)
						{
							cs::String failed;
							app.DriftShowcase(2, failed);
							app.DriftHeldByScript = true;
						}
						if (n == 20) app.PilotLookBy(180, 6);
						if (n == 110) app.DriftHeldByScript = false;
						FScreenshotRequest::RequestScreenshot(FPaths::Combine(dir, FString::Printf(TEXT("f%04d.png"), n)), true, false);
						return n >= 170;
					});
					push(TEXT("written"), wait(1.0));
				}
			}
		}

		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float)
		{
			try { return run->Tick(); }
			catch (const std::exception& e) { return run->Finish(*FString(UTF8_TO_TCHAR(e.what()))); }
		}));
	}

	/// US-T165/US-T168: <c>sg.Drift [game|real]</c> flies a light fighter of your own, gives it way and drifts it round to face
	/// backwards, then lets go; <c>sg.Drift shots &lt;dir&gt; [game|real] [quit]</c> screenshots the light fighter's 180 look, the
	/// heavy fighter's taut string and the drift at both scales (and a frame burst under dir/video at the game scale).
	FAutoConsoleCommand GDriftCommand(
		TEXT("sg.Drift"),
		TEXT("US-T165/US-T168: sg.Drift [game|real] drifts a light fighter round to face backwards while it flies on; sg.Drift shots <dir> "
			"[game|real] [quit] screenshots the string aim and drift at both scales."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Start(args); }));

	void RegisterDriftCheats()
	{
		using SpaceGod::UI::CheatEntry;
		using SpaceGod::UI::CheatRegistry;
		CheatRegistry::Add("Helm", CheatEntry::Console("Drift demo", "a light fighter given way, drifted round to face backwards, then boosting out (US-T165)", "sg.Drift"));
	}

	SpaceGod::UI::CheatRegistration GDriftCheats(&RegisterDriftCheats);

	/// US-T147: the showcase tour's stop, set up by this command.
	SpaceGod::Tour::TourRegistration GDriftTourStop([]
	{
		SpaceGod::Tour::TourRegistry::Add(SpaceGod::Tour::TourStop::Console("Flight", "drift", "Drift",
			"Hold R in a fighter to drift: it barrels on along its way while it turns free on its RCS thrusters, to face backwards and shoot what chases it; let go and it boosts out", "sg.Drift game")
			.Until([](SpaceGod::UnitySpaceApp& app, double t) { return app.PilotDrifting() && t > 2; }, 40).For(7).AtBothScales());
	});
}
