// US-T410: sg.HoldRewind selftest [quit] -- the hold rewind (docs/REWIND.md, "Hold to rewind") checked end to end in the
// running game: a fight is started off Uranus (as sg.BigBattle starts its own) and watched; after a few seconds the
// self-test marks a resume point and hashes the fight there, plays on, holds the rewind back to the mark by itself and
// lets go, then checks the fight resumed is exactly the fight as it was (and the hulls lost since are back, unbooked),
// that a second hold is refused for the cooldown, that the next one never goes back past the first one's resume point,
// and that the playback eases up to its top rate. Each check logs "HoldRewind selftest: PASS|FAIL ..."; the last line
// is "HoldRewind selftest: PASS|FAIL (n checks, m failed)". With quit the game exits after it (status 0 on a pass).
//
//   ./tools/play.ps1 -Offscreen -Capture '' -Seconds 300 -ExecCmds "sg.HoldRewind selftest quit"
//
// US-T424: "sg.HoldRewind selftest birth quit" checks a fight born in the window instead: a hull flown off Uranus with no
// fight about, a fight begun on it, a hold back to before it began (the fight taken back, the hull free at its helm), and
// the fight begun again being the same one by number and name.

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
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "InputCoreTypes.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"

#include <memory>

using namespace SpaceGod::Core;

namespace
{
	AUnitySpaceAppActor* HoldRewindActor()
	{
		if (!GEngine) return nullptr;
		for (const FWorldContext& c : GEngine->GetWorldContexts())
		{
			if (c.WorldType != EWorldType::Game && c.WorldType != EWorldType::PIE) continue;
			auto* actor = AUnitySpaceAppActor::Find(c.World());
			if (actor != nullptr && actor->GetApp() != nullptr) return actor;
		}
		return nullptr;
	}

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
		/// <summary>US-T424: "birth": the check of a fight born in the window (a hull flown off Uranus, a fight begun on
		/// it, a hold back to before it began) instead of the fight-long one.</summary>
		bool Birth = false;
		double Started = FPlatformTime::Seconds();
		cs::Ref<Battle> Fight;

		/// <summary>US-T420: the hold key is pressed for real: Shift+R as key events into Slate (the path the OS keyboard
		/// takes: Slate, the game viewport, the player controller the game reads), or, if Slate does not route them to the
		/// game here, straight into the player controller.</summary>
		bool KeyDown = false, ViaController = false, WasScripted = false;
		int WaitedTicks = 0;

		static APlayerController* Controller()
		{
			if (!GEngine || GEngine->GameViewport == nullptr) return nullptr;
			UWorld* w = GEngine->GameViewport->GetWorld();
			return w != nullptr ? w->GetFirstPlayerController() : nullptr;
		}

		void SendKeys(bool down)
		{
			if (!ViaController && FSlateApplication::IsInitialized())
			{
				FSlateApplication& s = FSlateApplication::Get();
				FModifierKeysState shift(true, false, false, false, false, false, false, false, false);
				FModifierKeysState none;
				if (down)
				{
					s.ProcessKeyDownEvent(FKeyEvent(EKeys::LeftShift, shift, 0, false, 0, 0));
					s.ProcessKeyDownEvent(FKeyEvent(EKeys::R, shift, 0, false, 'R', 'R'));
				}
				else
				{
					s.ProcessKeyUpEvent(FKeyEvent(EKeys::R, shift, 0, false, 'R', 'R'));
					s.ProcessKeyUpEvent(FKeyEvent(EKeys::LeftShift, none, 0, false, 0, 0));
				}
				return;
			}
			if (APlayerController* pc = Controller())
			{
				EInputEvent e = down ? IE_Pressed : IE_Released;
				if (down) pc->InputKey(FInputKeyParams(EKeys::LeftShift, e, 1.0));
				pc->InputKey(FInputKeyParams(EKeys::R, e, down ? 1.0 : 0.0));
				if (!down) pc->InputKey(FInputKeyParams(EKeys::LeftShift, e, 0.0));
			}
		}

		void DriveKeys(SpaceGod::UnitySpaceApp* app)
		{
			bool want = app->HoldRewindSelfTestWantsKey();
			if (want != KeyDown)
			{
				SendKeys(want);
				KeyDown = want;
				WaitedTicks = 0;
				return;
			}
			// Held, and the game has not seen it after a while: Slate is not routing to the game in this run.
			if (want && !ViaController && !app->HoldRewindKeyHeld() && ++WaitedTicks > 30)
			{
				UE_LOG(LogSpaceGod, Display, TEXT("sg.HoldRewind selftest: Slate did not route the keys to the game here; pressing them on the player controller"));
				SendKeys(false);
				ViaController = true;
				SendKeys(true);
				WaitedTicks = 0;
			}
		}

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
			if (!Began && Birth)
			{
				app->Clock->SetPaused(false);
				app->Clock->SetWarp(0);
				if (!app->BeginHoldRewindBirthSelfTest()) return Finish(false, TEXT("could not take the helm for the birth check"));
				app->SetHoldRewindSelfTestRealKeys(true);
				if (auto* actor = HoldRewindActor())
				{
					WasScripted = actor->bScriptedInput;
					actor->bScriptedInput = false;
				}
				Began = true;
				UE_LOG(LogSpaceGod, Display, TEXT("sg.HoldRewind selftest birth: at the helm off Uranus"));
				return true;
			}
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
				app->SetHoldRewindSelfTestRealKeys(true);
				// US-T420: a headless run feeds the app scripted input (Bootstrap: no window, no keyboard); for the test
				// the app reads Unreal's own input again (UnrealInputDevice::Feed from the player controller), so the keys
				// pressed below go the whole way a player's do. Put back when the test ends.
				if (auto* actor = HoldRewindActor())
				{
					WasScripted = actor->bScriptedInput;
					actor->bScriptedInput = false;
				}
				Began = true;
				UE_LOG(LogSpaceGod, Display, TEXT("sg.HoldRewind selftest: %s, %d units"), UTF8_TO_TCHAR(Fight->Name.c_str()), Fight->Units.Count());
				return true;
			}
			bool running = app->TickHoldRewindSelfTest();
			DriveKeys(app);
			if (running) return true;
			if (KeyDown) SendKeys(false);
			if (auto* actor = HoldRewindActor()) actor->bScriptedInput = WasScripted;
			UE_LOG(LogSpaceGod, Display, TEXT("sg.HoldRewind selftest: the hold key was pressed through %s"), ViaController ? TEXT("the player controller") : TEXT("Slate (the OS keyboard's path)"));
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
			{
				if (args[i].Equals(TEXT("quit"), ESearchCase::IgnoreCase)) run->Quit = true;
				if (args[i].Equals(TEXT("birth"), ESearchCase::IgnoreCase)) run->Birth = true;
			}
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float)
			{
				try { return run->Tick(); }
				catch (const std::exception& e) { return run->Finish(false, *FString(UTF8_TO_TCHAR(e.what()))); }
			}));
		}));
}
