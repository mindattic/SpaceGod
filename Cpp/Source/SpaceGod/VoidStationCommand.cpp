// US-T84: sg.VoidStation, a debug jump to a void station (and its screenshots at both scales).

#include "UnitySpaceApp.h"

#include "Camera/SpaceCamera.h"
#include "Camera/StarTravel.h"
#include "GameLaunch.h"
#include "SimClock.h"
#include "SpaceGodLog.h"
#include "Systems/Galaxy.h"
#include "Systems/VoidStations.h"
#include "Target.h"
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

using namespace SpaceGod::Core;

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

	/// <summary>
	/// One run of the command, stepped once a frame on the core ticker: each step waits until it is done (returns true),
	/// so the trip, the scale change and the camera's move all play out as they do in the game.
	/// </summary>
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
				// Started from the main menu (or -ExecCmds at launch): a new game first.
				if (!askedNewGame && now - started > 2.0)
				{
					askedNewGame = true;
					UE_LOG(LogSpaceGod, Display, TEXT("sg.VoidStation: no game running; starting a new one"));
					SpaceGod::GameLaunch::NewGame();
				}
				if (now - started > 180.0) return Finish(TEXT("no game came up"));
				return true;
			}
			if (at >= steps.size()) return Finish(nullptr);
			if (stepStart < 0) stepStart = now;
			if (steps[at].second(*app, now - stepStart))
			{
				UE_LOG(LogSpaceGod, Display, TEXT("sg.VoidStation: %s"), *steps[at].first);
				at++;
				stepStart = -1;
			}
			else if (now - stepStart > 240.0) return Finish(*FString::Printf(TEXT("timed out at '%s'"), *steps[at].first));
			return true;
		}

		bool Finish(const TCHAR* failure)
		{
			if (failure) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.VoidStation: %s"), failure); }
			else { UE_LOG(LogSpaceGod, Display, TEXT("sg.VoidStation: done")); }
			if (quit) FPlatformMisc::RequestExit(false);
			return false;
		}
	};

	cs::Ref<StarSystem> FindStation(SpaceGod::UnitySpaceApp& app, int number)
	{
		cs::String name = cs::Cat(VoidStations::Prefix, " ", number);
		auto s = app.Galaxy->Realize(name, app.Clock->Jd);
		if (s != nullptr) return s;
		for (const auto& x : app.Galaxy->Systems)
			if (x->IsVoidBase) return x;
		return nullptr;
	}

	void Start(const TArray<FString>& args)
	{
		int number = 1;
		double radii = 3.2;
		FString scale, shots;
		bool quit = false;
		for (int i = 0; i < args.Num(); i++)
		{
			const FString& a = args[i];
			if (a.IsNumeric() && !a.Contains(TEXT("."))) number = FCString::Atoi(*a);
			else if (a.Equals(TEXT("game"), ESearchCase::IgnoreCase) || a.Equals(TEXT("real"), ESearchCase::IgnoreCase)) scale = a.ToLower();
			else if (a.StartsWith(TEXT("radii="), ESearchCase::IgnoreCase)) radii = FCString::Atod(*a.Mid(6));
			else if (a.Equals(TEXT("shots"), ESearchCase::IgnoreCase) && i + 1 < args.Num()) shots = args[++i];
			else if (a.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) quit = true;
		}

		auto run = std::make_shared<Run>();
		run->quit = quit;
		auto station = std::make_shared<cs::Ref<StarSystem>>();
		auto wait = [](double seconds) { return [seconds](SpaceGod::UnitySpaceApp&, double t) { return t >= seconds; }; };
		run->steps.push_back({ TEXT("game up"), wait(2.0) });
		run->steps.push_back({ FString::Printf(TEXT("travelling to Void Station %d"), number), [station, number](SpaceGod::UnitySpaceApp& app, double)
		{
			*station = FindStation(app, number);
			if (*station == nullptr) throw std::runtime_error("no void station in this galaxy");
			UE_LOG(LogSpaceGod, Display, TEXT("sg.VoidStation: %s"), UTF8_TO_TCHAR((*station)->Name.c_str()));
			app.TravelTo(*station);
			return true;
		} });
		run->steps.push_back({ TEXT("arrived"), [station](SpaceGod::UnitySpaceApp& app, double t)
		{
			if (app.ActiveSystem != *station && !app.Travel->Active && t > 5.0) app.TravelTo(*station);
			return app.ActiveSystem == *station && !app.Travel->Active;
		} });
		run->steps.push_back({ TEXT("settled"), wait(1.5) });

		auto setScale = [](double goal)
		{
			return [goal](SpaceGod::UnitySpaceApp& app, double)
			{
				if (app.ScaleGoal != goal) app.ToggleScale();
				return !app.ScaleAnimating();
			};
		};
		auto focus = [](double r)
		{
			return [r](SpaceGod::UnitySpaceApp& app, double)
			{
				app.SpaceCamera->FocusOn(SpaceGod::Target::OfBody(0), r);
				return true;
			};
		};

		if (shots.IsEmpty())
		{
			if (!scale.IsEmpty()) run->steps.push_back({ TEXT("scale set"), setScale(scale == TEXT("game") ? 1.0 : 0.0) });
			run->steps.push_back({ TEXT("focused on the generator"), focus(radii) });
		}
		else
		{
			// Each scale: the whole station with its suns, then the close-up shell, then a closer look through the frame.
			for (double goal : { 1.0, 0.0 })
			{
				const TCHAR* label = goal > 0.5 ? TEXT("game") : TEXT("real");
				run->steps.push_back({ FString::Printf(TEXT("%s scale"), label), setScale(goal) });
				struct Shot { double radii; const TCHAR* name; };
				for (Shot s : { Shot { 9.0, TEXT("wide") }, Shot { 3.2, TEXT("close") }, Shot { 1.7, TEXT("detail") } })
				{
					run->steps.push_back({ FString::Printf(TEXT("%s focus at %.1f radii"), label, s.radii), focus(s.radii) });
					run->steps.push_back({ TEXT("camera settled"), wait(4.0) });
					FString path = FPaths::Combine(shots, FString::Printf(TEXT("voidstation-%s-%s.png"), label, s.name));
					run->steps.push_back({ FString::Printf(TEXT("shot %s"), *path), [path](SpaceGod::UnitySpaceApp&, double)
					{
						FScreenshotRequest::RequestScreenshot(path, false, false);
						return true;
					} });
					run->steps.push_back({ TEXT("shot written"), wait(1.0) });
				}
			}
		}

		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float)
		{
			try { return run->Tick(); }
			catch (const std::exception& e) { return run->Finish(*FString(UTF8_TO_TCHAR(e.what()))); }
		}));
	}

	/// US-T84: <c>sg.VoidStation [n] [game|real] [radii=R]</c> travels to Void Station n (1, the nearest Sol, by default),
	/// sets the scale if one is named and focuses the generator. <c>shots &lt;dir&gt; [quit]</c> instead takes its pictures
	/// at both scales (wide, close and detail) into dir, then quits if asked. Works from the menu or -ExecCmds too.
	FAutoConsoleCommand GVoidStationCommand(
		TEXT("sg.VoidStation"),
		TEXT("US-T84: travel to a void station and focus it: sg.VoidStation [n] [game|real] [radii=R]; "
			"sg.VoidStation [n] shots <dir> [quit] screenshots it at both scales."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Start(args); }));
}
