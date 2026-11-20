// US-T410: sg.HoldRewind selftest [quit] -- the hold rewind (docs/REWIND.md, "Hold to rewind") checked end to end in the
// running game: a fight is started off Uranus (as sg.BigBattle starts its own) and watched; after a few seconds the
// self-test marks a resume point and hashes the fight there, plays on, holds the rewind back to the mark by itself and
// lets go, then checks the fight resumed is exactly the fight as it was (and the hulls lost since are back, unbooked),
// that a second hold is refused for the cooldown, that the next one never goes back past the first one's resume point,
// and that the playback eases up to its top rate. Each check logs "HoldRewind selftest: PASS|FAIL ..."; the last line
// is "HoldRewind selftest: PASS|FAIL (n checks, m failed)". With quit the game exits after it (status 0 on a pass).
//
//   ./tools/play.ps1 -Offscreen -Capture '' -Seconds 300 -ExecCmds "sg.HoldRewind selftest quit"

#include "UnitySpaceApp.h"

#include "GameLaunch.h"
#include "SceneTour.h"
#include "SimClock.h"
#include "SpaceGodLog.h"
#include "UnitySpaceAppActor.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Combat/Battle.h"
#include "Ships.h"
#include "World.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"

#include <memory>

using namespace SpaceGod::Core;

namespace
{
	SpaceGod::UnitySpaceApp* HoldRewindApp()
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

	/// <summary>A small fight that lasts: a line and escorts each side (few craft, so few launches and landings to stop a
	/// resume), started off Uranus where the live war's traffic keeps out of it.</summary>
	cs::List<ShipClass> SelfTestFleet()
	{
		return cs::List<ShipClass>{ ShipClass::Battleship, ShipClass::Cruiser, ShipClass::Cruiser, ShipClass::Destroyer, ShipClass::Destroyer,
			ShipClass::Frigate, ShipClass::Corvette };
	}

	struct SelfTestRun
	{
		bool Quit = false, AskedNewGame = false, Began = false;
		double Started = FPlatformTime::Seconds();
		cs::Ref<Battle> Fight;

		bool Tick()
		{
			auto* app = HoldRewindApp();
			double now = FPlatformTime::Seconds();
			if (app == nullptr || app->World() == nullptr)
			{
				if (!AskedNewGame && now - Started > 2.0)
				{
					AskedNewGame = true;
					SpaceGod::GameLaunch::NewGame();
				}
				return now - Started < 180.0 || Finish(false, TEXT("no game came up"));
			}
			if (now - Started < 3.0) return true;
			if (!Began)
			{
				auto uranus = (*app->Catalog())["Uranus"];
				int body = uranus != nullptr ? uranus->Index : 0;
				Fight = SpaceGod::SceneTour::TriggerSkirmish(*app, body, app->World()->Sol, app->World()->Reach, SelfTestFleet());
				if (Fight == nullptr) return Finish(false, TEXT("the fight did not start"));
				// Run on to contact on the fight's own clock, then watch it at real time.
				Fight->Advance(20, false);
				app->Clock->SetPaused(false);
				app->Clock->SetWarp(0);
				app->WatchBattle(Fight);
				app->BeginHoldRewindSelfTest(Fight);
				Began = true;
				UE_LOG(LogSpaceGod, Display, TEXT("sg.HoldRewind selftest: %s, %d units"), UTF8_TO_TCHAR(Fight->Name.c_str()), Fight->Units.Count());
				return true;
			}
			if (app->TickHoldRewindSelfTest()) return true;
			bool passed = app->HoldRewindSelfTestPassed();
			return Finish(passed, passed ? nullptr : TEXT("see the FAIL lines above"));
		}

		bool Finish(bool passed, const TCHAR* why)
		{
			if (passed) { UE_LOG(LogSpaceGod, Display, TEXT("sg.HoldRewind selftest: PASS")); }
			else { UE_LOG(LogSpaceGod, Warning, TEXT("sg.HoldRewind selftest: FAIL%s%s"), why ? TEXT(": ") : TEXT(""), why ? why : TEXT("")); }
			if (Quit) FPlatformMisc::RequestExitWithStatus(false, passed ? 0 : 1);
			return false;
		}
	};

	FAutoConsoleCommand GHoldRewindCommand(
		TEXT("sg.HoldRewind"),
		TEXT("US-T410: the hold rewind: sg.HoldRewind selftest [quit] checks it end to end in a fight started off Uranus."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args)
		{
			if (args.Num() == 0 || !args[0].Equals(TEXT("selftest"), ESearchCase::IgnoreCase))
			{
				UE_LOG(LogSpaceGod, Warning, TEXT("sg.HoldRewind: usage: sg.HoldRewind selftest [quit]"));
				return;
			}
			auto run = std::make_shared<SelfTestRun>();
			for (int i = 1; i < args.Num(); i++)
				if (args[i].Equals(TEXT("quit"), ESearchCase::IgnoreCase)) run->Quit = true;
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float)
			{
				try { return run->Tick(); }
				catch (const std::exception& e) { return run->Finish(false, *FString(UTF8_TO_TCHAR(e.what()))); }
			}));
		}));
}
