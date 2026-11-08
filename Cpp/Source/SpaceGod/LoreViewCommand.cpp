// US-T237: sg.LoreView, a check of the Prose texts the panels show: selects a body of the active system (the selection
// card is the detail view), logs the key every lore surface on that card asks for and what it got back (its text, or
// "placeholder"), and (with shot) saves the screen. sg.LoreView [<body name>] [shot <file>] [quit]; the body defaults to
// Earth. Point SPACEGOD_LORE_DIR at a folder of test texts to see them on the card without any approved lore.

#include "UnitySpaceApp.h"

#include "GameLaunch.h"
#include "GameLore.h"
#include "SpaceGodLog.h"
#include "UnitySpaceAppActor.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "Lore/LoreKeys.h"
#include "Systems/Galaxy.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "UnrealClient.h"

#include <memory>

namespace
{
	using namespace SpaceGod;

	UnitySpaceApp* LoreViewApp()
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

	FString U(const cs::String& s) { return FString(UTF8_TO_TCHAR(s.c_str())); }

	void Report(const TCHAR* surface, const cs::String& key, const cs::String& got)
	{
		UE_LOG(LogSpaceGod, Display, TEXT("sg.LoreView: %s  %s -> %s"), surface, *U(key), got.empty() ? TEXT("placeholder") : *U(got));
	}

	/// Selects the body and logs each lookup its card makes; false when the active system has no body of that name.
	bool Show(UnitySpaceApp& app, const FString& name)
	{
		const auto& cat = app.Catalog();
		const cs::String want = TCHAR_TO_UTF8(*name);
		const cs::String system = app.ActiveSystem != nullptr ? app.ActiveSystem->Name : cs::String();
		for (int i = 0; i < cat->Count(); i++)
		{
			auto b = (*cat)[i];
			if (!cs::Strings::EqualsOrdinalIgnoreCase(b->Name, want)) continue;
			app.Select(Target::OfBody(i));
			app.FocusSelected();
			UE_LOG(LogSpaceGod, Display, TEXT("sg.LoreView: %d text(s) loaded from %s"), GameLore().Count(), *U(GameLore().Folder()));
			Report(TEXT("system (galaxy map, short)"), Core::LoreKeys::System(system), LoreShort(Core::LoreKeys::System(system)));
			const cs::String key = Core::LoreKeys::Body(b->Kind, system, b->Name);
			Report(TEXT("body (selection card, long)"), key, LoreLong(key));
			for (const auto& s : app.Economy()->At(i))
			{
				const cs::String sk = Core::LoreKeys::Settlement(system, s->Name);
				Report(TEXT("settlement (selection card, short)"), sk, LoreShort(sk));
				Report(TEXT("station hail (comms, bark)"), Core::LoreKeys::Named(sk, Core::LoreKeys::Hail), LoreBark(sk, Core::LoreKeys::Hail, 0, cs::String()));
			}
			return true;
		}
		return false;
	}

	void LoreViewStart(const TArray<FString>& args)
	{
		FString body = TEXT("Earth"), shot;
		bool quit = false;
		for (int i = 0; i < args.Num(); i++)
		{
			if (args[i].Equals(TEXT("shot"), ESearchCase::IgnoreCase) && i + 1 < args.Num()) shot = args[++i];
			else if (args[i].Equals(TEXT("quit"), ESearchCase::IgnoreCase)) quit = true;
			else body = args[i];
		}
		struct State { double started = FPlatformTime::Seconds(); double stepAt = -1; int step = 0; bool askedNewGame = false; };
		auto st = std::make_shared<State>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([st, body, shot, quit](float)
		{
			double now = FPlatformTime::Seconds();
			auto finish = [quit](const TCHAR* why)
			{
				if (why) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.LoreView: %s"), why); }
				else { UE_LOG(LogSpaceGod, Display, TEXT("sg.LoreView: done")); }
				if (quit) FPlatformMisc::RequestExit(false);
				return false;
			};
			auto* app = LoreViewApp();
			if (app == nullptr)
			{
				if (!st->askedNewGame && now - st->started > 2.0)
				{
					st->askedNewGame = true;
					UE_LOG(LogSpaceGod, Display, TEXT("sg.LoreView: no game running; starting a new one"));
					GameLaunch::NewGame();
				}
				return now - st->started < 180.0 ? true : finish(TEXT("no game came up"));
			}
			if (st->stepAt < 0) st->stepAt = now;
			// The scene lays itself out first, then the card, then a moment for the camera to settle on the body.
			double wait = st->step == 0 ? 8.0 : 3.0;
			if (now - st->stepAt < wait) return true;
			if (st->step == 0)
			{
				if (!Show(*app, body)) return finish(*FString::Printf(TEXT("no body '%s' in the active system"), *body));
				st->step++;
				st->stepAt = now;
				return true;
			}
			if (st->step == 1 && !shot.IsEmpty())
			{
				FScreenshotRequest::RequestScreenshot(shot, true, false);
				UE_LOG(LogSpaceGod, Display, TEXT("sg.LoreView: screenshot %s"), *shot);
				st->step++;
				st->stepAt = now;
				return true;
			}
			return finish(nullptr);
		}));
	}

	FAutoConsoleCommand GLoreViewCommand(
		TEXT("sg.LoreView"),
		TEXT("US-T237: select a body and log the Prose text each panel lookup gets: sg.LoreView [<body>] [shot <file>] [quit]."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { LoreViewStart(args); }));
}
