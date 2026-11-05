// US-T137: sg.Comms, the comms panel shown for a check or a screenshot: takes the helm of the lawful hull with the most to
// hail near it, opens comms on its nearest hull, hails it, asks about the area and about its pilot, and (with shot) saves
// the screen. sg.Comms [shot <file>] [quit].

#include "UnitySpaceApp.h"

#include "GameLaunch.h"
#include "SpaceGodLog.h"
#include "Tour/ShowcaseTour.h"
#include "UnitySpaceAppActor.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "UnrealClient.h"

#include <memory>
#include <stdexcept>

namespace
{
	SpaceGod::UnitySpaceApp* CommsRunningApp()
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

	void CommsStart(const TArray<FString>& args)
	{
		FString shot;
		bool quit = false;
		for (int i = 0; i < args.Num(); i++)
		{
			if (args[i].Equals(TEXT("shot"), ESearchCase::IgnoreCase) && i + 1 < args.Num()) shot = args[++i];
			else if (args[i].Equals(TEXT("quit"), ESearchCase::IgnoreCase)) quit = true;
		}
		struct State { double started = FPlatformTime::Seconds(); double stepAt = -1; int step = 0; bool askedNewGame = false; };
		auto st = std::make_shared<State>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([st, shot, quit](float)
		{
			double now = FPlatformTime::Seconds();
			auto finish = [quit](const TCHAR* why)
			{
				if (why) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.Comms: %s"), why); }
				else { UE_LOG(LogSpaceGod, Display, TEXT("sg.Comms: done")); }
				if (quit) FPlatformMisc::RequestExit(false);
				return false;
			};
			auto* app = CommsRunningApp();
			if (app == nullptr)
			{
				if (!st->askedNewGame && now - st->started > 2.0)
				{
					st->askedNewGame = true;
					UE_LOG(LogSpaceGod, Display, TEXT("sg.Comms: no game running; starting a new one"));
					SpaceGod::GameLaunch::NewGame();
				}
				return now - st->started < 180.0 ? true : finish(TEXT("no game came up"));
			}
			if (st->stepAt < 0) st->stepAt = now;
			// A few seconds for the scene to lay itself out, then the helm, then a moment for the view to settle on it.
			double wait = st->step == 0 ? 8.0 : st->step == 1 ? 4.0 : 2.0;
			if (now - st->stepAt < wait) return true;
			if (st->step <= 1)
			{
				cs::String failed;
				if (!app->CommsShowcase(st->step, failed)) return finish(*FString(UTF8_TO_TCHAR(failed.c_str())));
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Comms: step %d"), st->step);
				if (st->step == 1)
					for (const auto& l : app->CommsLog()) UE_LOG(LogSpaceGod, Display, TEXT("sg.Comms: %s: %s"), UTF8_TO_TCHAR(l.Who.c_str()), UTF8_TO_TCHAR(l.Text.c_str()));
				st->step++;
				st->stepAt = now;
				return true;
			}
			if (st->step == 2)
			{
				if (shot.IsEmpty()) return finish(nullptr);
				FScreenshotRequest::RequestScreenshot(shot, true, false);
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Comms: screenshot %s"), *shot);
				st->step++;
				st->stepAt = now;
				return true;
			}
			return finish(nullptr);
		}));
	}

	FAutoConsoleCommand GCommsCommand(
		TEXT("sg.Comms"),
		TEXT("US-T137: take a helm with ships near it, open comms, hail the nearest and ask about the area: sg.Comms [shot <file>] [quit]."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { CommsStart(args); }));

	/// US-T147: the showcase tour's stop, set up by sg.Comms's own showcase steps.
	SpaceGod::Tour::TourRegistration GCommsTourStop([]
	{
		auto hailed = std::make_shared<bool>(false);
		SpaceGod::Tour::TourRegistry::Add(SpaceGod::Tour::TourStop::Make("Embodiment", "comms-hail", "Hailing a ship",
			"Shift+H at any controls: pick who to hail, then what to say; the answers come from the simulation as it stands, the pilot's own name and story too")
			.At(30)
			.WithSetup([hailed](SpaceGod::UnitySpaceApp& app)
			{
				*hailed = false;
				cs::String failed;
				app.CommsShowcase(0, failed);
				return failed;
			})
			.Until([hailed](SpaceGod::UnitySpaceApp& app, double t)
			{
				if (t < 3) return false;
				if (!*hailed)
				{
					*hailed = true;
					cs::String failed;
					if (!app.CommsShowcase(1, failed)) throw std::runtime_error(failed.c_str());
				}
				return t > 5;
			}, 20)
			.For(10));
	});
}
