// US-T239: sg.ScanView and sg.EncounterView, checks of the scan panel's tabs and of an encounter as a Prose conversation.
//
//   sg.ScanView [<body>] [ship] [tab <id>] [subtab <n>] [shot <file>] [quit]
//     selects the body (default Earth), or with "ship" the active system's first hull whose pilot has been met (else any
//     hull), opens the scan on that tab and subtab, logs every tab's Prose key and what it got (its text or "placeholder"),
//     and (with shot) saves the screen.
//   sg.EncounterView [shot <file>] [quit]
//     takes the helm of the hull with the most to hail near it (sg.Comms's showcase), hails its nearest hull that is not
//     hostile, logs the encounter kind and the conversation found (or none: the plain panel), and (with shot) saves the screen.
//
// Point SPACEGOD_LORE_DIR at a folder of test texts to see them on screen without any approved lore.

#include "UnitySpaceApp.h"

#include "GameLaunch.h"
#include "GameLore.h"
#include "SpaceGodLog.h"
#include "UnitySpaceAppActor.h"
#include "UI/Hud.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Comms/Comms.h"
#include "Comms/Pilots.h"
#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "Fleet.h"
#include "Law/Law.h"
#include "Lore/Encounters.h"
#include "Lore/LoreKeys.h"
#include "Lore/ScanTabs.h"
#include "Ships.h"
#include "Systems/Galaxy.h"
#include "World.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "UnrealClient.h"

#include <functional>
#include <memory>

namespace
{
	using namespace SpaceGod;

	UnitySpaceApp* ScanApp()
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

	void ReportTabs(const TCHAR* what, const cs::String& entity, Core::ScanSubject subject)
	{
		for (const auto& tab : Core::ScanTabs::Of(subject))
		{
			const cs::String key = Core::ScanTabs::Key(entity, tab.Id);
			const cs::String got = LoreLong(key);
			UE_LOG(LogSpaceGod, Display, TEXT("sg.ScanView: %s %s tab %s  %s -> %s"), what, ANSI_TO_TCHAR(Core::ScanTabs::Name(subject)), ANSI_TO_TCHAR(tab.Id),
				*U(key), got.empty() ? TEXT("placeholder") : *U(got));
		}
	}

	/// Selects the thing to scan and logs every tab lookup of each of its subtabs; false when there is nothing to select.
	bool Scan(UnitySpaceApp& app, const FString& name, bool ship, const FString& tab, int subtab)
	{
		const cs::String system = app.ActiveSystem != nullptr ? app.ActiveSystem->Name : cs::String();
		UE_LOG(LogSpaceGod, Display, TEXT("sg.ScanView: %d text(s) loaded from %s"), GameLore().Count(), *U(GameLore().Folder()));
		bool found = false;
		if (ship)
		{
			auto pilots = app.World()->Law != nullptr ? app.World()->Law->Pilots : nullptr;
			cs::Ref<Core::Ship> pick;
			for (const auto& s : app.World()->Fleet->Ships())
			{
				if (s == nullptr || s->Destroyed || s->GateDeparted) continue;
				if (pick == nullptr) pick = s;
				if (pilots != nullptr && pilots->Find(s) != nullptr) { pick = s; break; }
			}
			if (pick == nullptr) return false;
			app.Select(Target::OfShip(pick));
			app.FocusSelected();
			bool warship = (pick->Spec != nullptr && pick->Spec->IsMilitary) || (pick->Faction != nullptr && pick->Faction->IsPirate);
			ReportTabs(TEXT("ship"), Core::LoreKeys::Ship(system, pick->Name), warship ? Core::ScanSubject::Warship : Core::ScanSubject::Ship);
			if (auto p = pilots != nullptr ? pilots->Find(pick) : nullptr) ReportTabs(TEXT("pilot"), Core::LoreKeys::Pilot(p->Name), Core::ScanSubject::Pilot);
			found = true;
		}
		else
		{
			const auto& cat = app.Catalog();
			const cs::String want = TCHAR_TO_UTF8(*name);
			for (int i = 0; i < cat->Count() && !found; i++)
			{
				auto b = (*cat)[i];
				if (!cs::Strings::EqualsOrdinalIgnoreCase(b->Name, want)) continue;
				app.Select(Target::OfBody(i));
				app.FocusSelected();
				ReportTabs(TEXT("body"), Core::LoreKeys::Body(b->Kind, system, b->Name), Core::ScanTabs::OfBody(b->Kind));
				for (const auto& s : app.Economy()->At(i))
					if (s->Discovered()) ReportTabs(TEXT("settlement"), Core::LoreKeys::Settlement(system, s->Name), Core::ScanSubject::Settlement);
				found = true;
			}
		}
		if (found && app.CoopHud() != nullptr) app.CoopHud()->SetScan(TCHAR_TO_UTF8(*tab), subtab);
		return found;
	}

	/// Hails the nearest hull that is not hostile from the helm sg.Comms's showcase took, and logs the conversation found.
	bool Encounter(UnitySpaceApp& app)
	{
		auto c = app.CommsHere();
		cs::Ref<Core::Ship> pick;
		for (const auto& p : Core::Comms::Contacts(c))
			if (p.Ship != nullptr && (pick == nullptr || Core::Comms::StanceOf(c, p) != Core::CommsStance::Hostile))
			{
				pick = p.Ship;
				if (Core::Comms::StanceOf(c, p) != Core::CommsStance::Hostile) break;
			}
		if (pick == nullptr) return false;
		auto kind = Core::Encounters::KindOf(c, Core::CommsParty::OfShip(pick));
		if (!app.HailShip(pick)) return false;
		UE_LOG(LogSpaceGod, Display, TEXT("sg.EncounterView: hailed the %s (%s encounter): %s"), *U(pick->Name), ANSI_TO_TCHAR(Core::Encounters::Name(kind)),
			app.EncounterKey().empty() ? TEXT("no conversation, the plain panel") : *U(app.EncounterKey()));
		for (const auto& l : app.CommsLog()) UE_LOG(LogSpaceGod, Display, TEXT("sg.EncounterView:   %s: %s"), *U(l.Who), *U(l.Text));
		return true;
	}

	struct State
	{
		double started = FPlatformTime::Seconds();
		double stepAt = -1;
		int step = 0;
		bool askedNewGame = false;
	};

	/// Runs <paramref name="steps"/> one a tick, each once its wait is up (the first after the scene has laid itself out),
	/// then the screenshot, then quits when asked.
	void Run(const TCHAR* name, std::function<bool(UnitySpaceApp&, int, FString&)> steps, int count, FString shot, bool quit)
	{
		auto st = std::make_shared<State>();
		FString label = name;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([st, steps, count, shot, quit, label](float)
		{
			double now = FPlatformTime::Seconds();
			auto finish = [quit, &label](const FString& why)
			{
				if (!why.IsEmpty()) { UE_LOG(LogSpaceGod, Warning, TEXT("%s: %s"), *label, *why); }
				else { UE_LOG(LogSpaceGod, Display, TEXT("%s: done"), *label); }
				if (quit) FPlatformMisc::RequestExit(false);
				return false;
			};
			auto* app = ScanApp();
			if (app == nullptr)
			{
				if (!st->askedNewGame && now - st->started > 2.0)
				{
					st->askedNewGame = true;
					GameLaunch::NewGame();
				}
				return now - st->started < 180.0 ? true : finish(TEXT("no game came up"));
			}
			if (st->stepAt < 0) st->stepAt = now;
			double wait = st->step == 0 ? 8.0 : 3.0;
			if (now - st->stepAt < wait) return true;
			if (st->step < count)
			{
				FString failed;
				if (!steps(*app, st->step, failed)) return finish(failed);
				st->step++;
				st->stepAt = now;
				return true;
			}
			if (st->step == count && !shot.IsEmpty())
			{
				FScreenshotRequest::RequestScreenshot(shot, true, false);
				UE_LOG(LogSpaceGod, Display, TEXT("%s: screenshot %s"), *label, *shot);
				st->step++;
				st->stepAt = now;
				return true;
			}
			return finish(FString());
		}));
	}

	void ScanViewStart(const TArray<FString>& args)
	{
		FString body = TEXT("Earth"), shot, tab = TEXT("overview");
		int subtab = 0;
		bool quit = false, ship = false;
		for (int i = 0; i < args.Num(); i++)
		{
			if (args[i].Equals(TEXT("shot"), ESearchCase::IgnoreCase) && i + 1 < args.Num()) shot = args[++i];
			else if (args[i].Equals(TEXT("tab"), ESearchCase::IgnoreCase) && i + 1 < args.Num()) tab = args[++i];
			else if (args[i].Equals(TEXT("subtab"), ESearchCase::IgnoreCase) && i + 1 < args.Num()) subtab = FCString::Atoi(*args[++i]);
			else if (args[i].Equals(TEXT("ship"), ESearchCase::IgnoreCase)) ship = true;
			else if (args[i].Equals(TEXT("quit"), ESearchCase::IgnoreCase)) quit = true;
			else body = args[i];
		}
		Run(TEXT("sg.ScanView"), [body, ship, tab, subtab](UnitySpaceApp& app, int, FString& failed)
		{
			if (Scan(app, body, ship, tab, subtab)) return true;
			failed = ship ? FString(TEXT("no hull in the active system")) : FString::Printf(TEXT("no body '%s' in the active system"), *body);
			return false;
		}, 1, shot, quit);
	}

	void EncounterViewStart(const TArray<FString>& args)
	{
		FString shot;
		bool quit = false;
		for (int i = 0; i < args.Num(); i++)
		{
			if (args[i].Equals(TEXT("shot"), ESearchCase::IgnoreCase) && i + 1 < args.Num()) shot = args[++i];
			else if (args[i].Equals(TEXT("quit"), ESearchCase::IgnoreCase)) quit = true;
		}
		Run(TEXT("sg.EncounterView"), [](UnitySpaceApp& app, int step, FString& failed)
		{
			if (step == 0)
			{
				cs::String why;
				if (app.CommsShowcase(0, why)) return true;
				failed = U(why);
				return false;
			}
			if (Encounter(app)) return true;
			failed = TEXT("nobody to hail");
			return false;
		}, 2, shot, quit);
	}

	FAutoConsoleCommand GScanViewCommand(
		TEXT("sg.ScanView"),
		TEXT("US-T239: open the scan panel on a body (or a hull) and log each tab's Prose lookup: sg.ScanView [<body>] [ship] [tab <id>] [subtab <n>] [shot <file>] [quit]."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { ScanViewStart(args); }));

	FAutoConsoleCommand GEncounterViewCommand(
		TEXT("sg.EncounterView"),
		TEXT("US-T239: take a helm, hail the nearest hull that is not hostile and log the Prose encounter conversation found: sg.EncounterView [shot <file>] [quit]."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { EncounterViewStart(args); }));
}
