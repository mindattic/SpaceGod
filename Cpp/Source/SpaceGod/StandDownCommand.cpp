// US-T140: sg.StandDown, the embody limits and the stand-down scenario run end to end (and its screenshots): the gate refusing a
// capital for want of Influence and a hull in a battle under way, then a pirate band closing on a lone freighter with the
// SpaceGod at its captain's helm, ordering the squad to stand down until the crews obey or mutiny, at both scales. Also the
// hook for the showcase tour (US-T147): "sg.StandDown" sets the stop up without the shots.

#include "UnitySpaceApp.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Camera/SpaceCamera.h"
#include "Combat/Battle.h"
#include "Combat/SquadOrders.h"
#include "Combat/War.h"
#include "Embodiment/Embodiables.h"
#include "Embodiment/Influence.h"
#include "Embodiment/ShipHelm.h"
#include "Fleet.h"
#include "GameLaunch.h"
#include "Ships.h"
#include "SimClock.h"
#include "SpaceGodLog.h"
#include "Target.h"
#include "UnitySpaceAppActor.h"
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

	/// One run, stepped once a frame on the core ticker (as sg.EvaRepair's).
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
					UE_LOG(LogSpaceGod, Display, TEXT("sg.StandDown: no game running; starting a new one"));
					SpaceGod::GameLaunch::NewGame();
				}
				if (now - started > 180.0) return Finish(TEXT("no game came up"));
				return true;
			}
			if (at >= steps.size()) return Finish(nullptr);
			if (stepStart < 0) stepStart = now;
			if (steps[at].second(*app, now - stepStart))
			{
				UE_LOG(LogSpaceGod, Display, TEXT("sg.StandDown: %s"), *steps[at].first);
				at++;
				stepStart = -1;
			}
			else if (now - stepStart > 120.0) return Finish(*FString::Printf(TEXT("timed out at '%s'"), *steps[at].first));
			return true;
		}

		bool Finish(const TCHAR* failure)
		{
			if (failure) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.StandDown: %s"), failure); }
			else { UE_LOG(LogSpaceGod, Display, TEXT("sg.StandDown: done")); }
			if (quit) FPlatformMisc::RequestExit(false);
			return false;
		}
	};

	FString F(const cs::String& s) { return FString(UTF8_TO_TCHAR(s.c_str())); }

	/// The biggest hull flying free (no fight, not landed, not an escort) the SpaceGod could reach for, or null.
	cs::Ref<Ship> BigHull(SpaceGod::UnitySpaceApp& app)
	{
		cs::Ref<Ship> best;
		int bestTier = 0;
		for (const auto& s : app.Fleet()->Ships())
		{
			if (s->Destroyed || s->IsEscort() || s->Battle != nullptr || s->Spec == nullptr || !app.Fleet()->IsPresent(s) || s->Phase == ShipPhase::Landed) continue;
			if (!Embodiables::HelmOf(app.World(), s)->CanEmbody()) continue;
			int tier = SpaceGodInfluence::TierOf(s->Spec->Class);
			if (tier > bestTier)
			{
				bestTier = tier;
				best = s;
			}
		}
		return bestTier >= 3 ? best : nullptr;
	}

	struct State
	{
		cs::Ref<Ship> fighting;
		int orders = 0;
		cs::String last;
	};

	void Start(const TArray<FString>& args)
	{
		FString shots;
		bool quit = false;
		for (int i = 0; i < args.Num(); i++)
		{
			const FString& a = args[i];
			if (a.Equals(TEXT("shots"), ESearchCase::IgnoreCase) && i + 1 < args.Num()) shots = args[++i];
			else if (a.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) quit = true;
		}
		auto run = std::make_shared<Run>();
		run->quit = quit;
		auto st = std::make_shared<State>();
		auto wait = [](double seconds) { return [seconds](SpaceGod::UnitySpaceApp&, double t) { return t >= seconds; }; };
		auto shot = [run, shots](const TCHAR* name)
		{
			if (shots.IsEmpty()) return;
			FString path = FPaths::Combine(shots, FString(name) + TEXT(".png"));
			run->steps.push_back({ FString::Printf(TEXT("shot %s"), *path), [path](SpaceGod::UnitySpaceApp&, double)
			{
				FScreenshotRequest::RequestScreenshot(path, true, false);
				return true;
			} });
			run->steps.push_back({ TEXT("shot written"), [](SpaceGod::UnitySpaceApp&, double t) { return t >= 3.0; } });
		};
		auto setScale = [](double goal)
		{
			return [goal](SpaceGod::UnitySpaceApp& app, double)
			{
				if (app.ScaleGoal != goal) app.ToggleScale();
				return !app.ScaleAnimating();
			};
		};

		run->steps.push_back({ TEXT("game up"), wait(3.0) });
		if (!shots.IsEmpty())
		{
			run->steps.push_back({ TEXT("game scale"), setScale(1.0) });
			run->steps.push_back({ TEXT("Influence back to level 1, limits on"), [](SpaceGod::UnitySpaceApp& app, double)
			{
				auto inf = SpaceGodInfluence::Of(app.World());
				inf->Xp = 0;
				inf->Waived = false;
				return true;
			} });
			// 1. Not Influence enough: the biggest hull flying free, out of reach of a level-1 SpaceGod.
			run->steps.push_back({ TEXT("a capital refused for want of Influence"), [](SpaceGod::UnitySpaceApp& app, double)
			{
				auto big = BigHull(app);
				if (big == nullptr) throw std::runtime_error("no big hull flying free to try");
				auto t = SpaceGod::Target::OfShip(big);
				app.Select(t);
				app.SpaceCamera->FocusOn(t);
				app.TogglePiloting();
				return true;
			} });
			run->steps.push_back({ TEXT("camera settled"), wait(3.5) });
			shot(TEXT("refusal-influence"));
			// 2. In battle: a skirmish at Uranus (two battleships a side, as sg.EvaRepair's), and a hull in it refused.
			run->steps.push_back({ TEXT("a hull in a battle under way refused"), [st](SpaceGod::UnitySpaceApp& app, double)
			{
				auto uranus = (*app.Catalog())["Uranus"];
				if (uranus == nullptr) throw std::runtime_error("no Uranus in this system");
				double jd = app.Clock->Jd;
				auto dir = DVec3(1, 0.15, 0.4).Normalized();
				auto side = DVec3::Cross(dir, DVec3::UnitZ).Normalized();
				cs::Ref<Ship> first;
				for (int i = 0; i < 2; i++)
				{
					auto a = app.Fleet()->SpawnAt(cs::Cat("Limits demo ", app.World()->Sol->Name, " ", i + 1), ShipClass::Battleship, app.World()->Sol, uranus->Index, dir * (2000 + i * 400), dir * -1.0, jd);
					app.Fleet()->SpawnAt(cs::Cat("Limits demo ", app.World()->Reach->Name, " ", i + 1), ShipClass::Battleship, app.World()->Reach, uranus->Index, dir * (2000 + i * 400) + side * 3000, dir, jd);
					if (first == nullptr) first = a;
				}
				app.World()->War->TryStartBattle(uranus->Index, app.World()->Sol, jd);
				if (first->Battle == nullptr) throw std::runtime_error("the skirmish did not start");
				st->fighting = first;
				auto t = SpaceGod::Target::OfShip(first);
				app.Select(t);
				app.SpaceCamera->FocusOn(t);
				app.TogglePiloting();
				return true;
			} });
			run->steps.push_back({ TEXT("camera settled"), wait(3.5) });
			shot(TEXT("refusal-battle"));
			// Away from that fight and the inner system's traffic for the scenario: the camera out at Eris (Mars if there is none).
			run->steps.push_back({ TEXT("camera to a quiet world"), [](SpaceGod::UnitySpaceApp& app, double)
			{
				auto mars = (*app.Catalog())["Eris"];
				if (mars == nullptr) mars = (*app.Catalog())["Mars"];
				if (mars == nullptr) return true;
				auto t = SpaceGod::Target::OfBody(mars->Index);
				app.Select(t);
				app.SpaceCamera->FocusOn(t);
				return true;
			} });
			run->steps.push_back({ TEXT("camera settled"), wait(4.0) });
		}
		// 3. The stand-down scenario, with Influence at level 4 (Presence).
		run->steps.push_back({ TEXT("the stand-down scenario"), [](SpaceGod::UnitySpaceApp& app, double)
		{
			auto inf = SpaceGodInfluence::Of(app.World());
			if (inf->Xp < SpaceGodInfluence::XpFor(4)) SpaceGodInfluence::Grant(app.World(), InfluenceSource::Dev, SpaceGodInfluence::XpFor(4) - inf->Xp, "dev grant");
			if (!app.StartStandDownScenario(120)) throw std::runtime_error("the scenario could not be laid out");
			return true;
		} });
		if (!shots.IsEmpty()) {
		run->steps.push_back({ TEXT("camera settled"), wait(6.0) });
		shot(TEXT("standdown-closing-game"));
		// The order, until the crews obey or mutiny (a fresh draw each ten game seconds; hesitation holds the strike back).
		run->steps.push_back({ TEXT("squad ordered to stand down until it obeys or mutinies"), [st](SpaceGod::UnitySpaceApp& app, double t)
		{
			if (st->orders > 0 && t < 11.0 * st->orders) return false;
			if (st->orders >= 5) return true;
			st->last = app.OrderSquadStandDown();
			st->orders++;
			UE_LOG(LogSpaceGod, Display, TEXT("sg.StandDown: order %d: %s"), st->orders, *F(st->last));
			bool settled = st->last.rfind("OBEY", 0) == 0 || st->last.rfind("MUTINY", 0) == 0 || st->last.rfind("Stand down:", 0) == 0;
			if (!settled) return false;
			return true;
		} });
		run->steps.push_back({ TEXT("toast up"), wait(1.5) });
		shot(TEXT("standdown-answer-game"));
		run->steps.push_back({ TEXT("realistic scale"), setScale(0.0) });
		run->steps.push_back({ TEXT("camera settled"), wait(3.0) });
		shot(TEXT("standdown-answer-real"));
		run->steps.push_back({ TEXT("game scale again, a dev grant for the meter's gain"), [](SpaceGod::UnitySpaceApp& app, double)
		{
			if (app.ScaleGoal != 1.0) app.ToggleScale();
			if (app.ScaleAnimating()) return false;
			SpaceGodInfluence::Grant(app.World(), InfluenceSource::Dev, 150, "dev grant");
			return true;
		} });
		run->steps.push_back({ TEXT("meter flashing"), wait(1.5) });
		shot(TEXT("influence-meter"));
		} // shots
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float)
		{
			try { return run->Tick(); }
			catch (const std::exception& e) { return run->Finish(*FString(UTF8_TO_TCHAR(e.what()))); }
		}));
	}

	/// US-T140: <c>sg.StandDown</c> sets up the stand-down scenario (a pirate band closing on a lone freighter, you at its
	/// captain's helm, Influence at least level 4); <c>sg.StandDown shots &lt;dir&gt; [quit]</c> first shows the embody gate
	/// refusing a capital for want of Influence and a hull in a battle under way, then orders the squad down until it obeys or
	/// mutinies, screenshotting each at game scale and the answer at realistic scale too. Works from the menu or -ExecCmds.
	FAutoConsoleCommand GStandDownCommand(
		TEXT("sg.StandDown"),
		TEXT("US-T140: the stand-down scenario (a pirate band closing on a lone freighter, you its captain); "
			"sg.StandDown shots <dir> [quit] also shows the embody limits refusing, and screenshots every step."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Start(args); }));
}
