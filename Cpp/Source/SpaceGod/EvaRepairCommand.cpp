// US-T135: sg.EvaRepair, a debug run out onto a capital ship's hull as an EVA repair worker welding a damaged shield
// generator (and its screenshots at both scales).

#include "UnitySpaceApp.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Camera/SpaceCamera.h"
#include "Combat/Battle.h"
#include "Combat/Broadsides.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Embodiment/Embodiables.h"
#include "Embodiment/Influence.h" // US-T140: the dev waiver
#include "Embodiment/RepairWorker.h"
#include "GameLaunch.h"
#include "Combat/War.h"
#include "Fleet.h"
#include "Ships.h"
#include "SimClock.h"
#include "SpaceGodLog.h"
#include "Target.h"
#include "Tour/ShowcaseTour.h"
#include "UI/CheatMenu.h" // US-T158
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

	/// One run, stepped once a frame on the core ticker (as sg.VoidStation's).
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
					UE_LOG(LogSpaceGod, Display, TEXT("sg.EvaRepair: no game running; starting a new one"));
					SpaceGod::GameLaunch::NewGame();
				}
				if (now - started > 180.0) return Finish(TEXT("no game came up"));
				return true;
			}
			if (at >= steps.size()) return Finish(nullptr);
			if (stepStart < 0) stepStart = now;
			if (steps[at].second(*app, now - stepStart))
			{
				UE_LOG(LogSpaceGod, Display, TEXT("sg.EvaRepair: %s"), *steps[at].first);
				at++;
				stepStart = -1;
			}
			else if (now - stepStart > 120.0) return Finish(*FString::Printf(TEXT("timed out at '%s'"), *steps[at].first));
			return true;
		}

		bool Finish(const TCHAR* failure)
		{
			if (failure) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.EvaRepair: %s"), failure); }
			else { UE_LOG(LogSpaceGod, Display, TEXT("sg.EvaRepair: done")); }
			if (quit) FPlatformMisc::RequestExit(false);
			return false;
		}
	};

	struct Scene
	{
		cs::Ref<Battle> battle;
		cs::Ref<BattleUnit> unit;
		cs::Ref<Subsystem> generator;
	};

	/// A capital hull fighting with a shield generator on it, in any fight on; null if there is none.
	bool FindHull(SpaceGod::UnitySpaceApp& app, Scene& s)
	{
		for (const auto& b : app.World()->War->Battles)
		{
			if (b->Over) continue;
			for (const auto& u : b->Units)
			{
				if (!u->Present() || u->Captured || RepairWorker::TeamsOn(u) <= 0 || u->Combat == nullptr) continue;
				for (const auto& sub : u->Combat->Subsystems)
					if (sub->Kind() == SubsystemKind::ShieldGenerator && !sub->Destroyed())
					{
						s.battle = b;
						s.unit = u;
						s.generator = sub;
						return true;
					}
			}
		}
		return false;
	}

	cs::Ref<RepairWorker> HeldWorker(SpaceGod::UnitySpaceApp& app) { return cs::As<RepairWorker>(app.Seat()); }

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
		auto scene = std::make_shared<Scene>();
		auto wait = [](double seconds) { return [seconds](SpaceGod::UnitySpaceApp&, double t) { return t >= seconds; }; };
		run->steps.push_back({ TEXT("game up"), wait(2.0) });
		run->steps.push_back({ TEXT("a fight with a capital hull"), [scene](SpaceGod::UnitySpaceApp& app, double)
		{
			if (FindHull(app, *scene)) return true;
			// None on: a skirmish at Uranus (empty, so the live war stays out of it), two battleships a side.
			auto uranus = (*app.Catalog())["Uranus"];
			if (uranus == nullptr) throw std::runtime_error("no Uranus in this system");
			double jd = app.Clock->Jd;
			auto dir = DVec3(1, 0.15, 0.4).Normalized();
			auto side = DVec3::Cross(dir, DVec3::UnitZ).Normalized();
			for (int i = 0; i < 2; i++)
			{
				app.Fleet()->SpawnAt(cs::Cat("EVA demo ", app.World()->Sol->Name, " ", i + 1), ShipClass::Battleship, app.World()->Sol, uranus->Index, dir * (2000 + i * 400), dir * -1.0, jd);
				app.Fleet()->SpawnAt(cs::Cat("EVA demo ", app.World()->Reach->Name, " ", i + 1), ShipClass::Battleship, app.World()->Reach, uranus->Index, dir * (2000 + i * 400) + side * 3000, dir, jd);
			}
			app.World()->War->TryStartBattle(uranus->Index, app.World()->Sol, jd);
			if (!FindHull(app, *scene)) throw std::runtime_error("the skirmish did not start");
			return true;
		} });
		run->steps.push_back({ TEXT("shield generator damaged, worker out on the hull"), [scene](SpaceGod::UnitySpaceApp& app, double)
		{
			auto& s = *scene;
			// Hurt, not shot away: a third of its hit points left, its field down.
			s.generator->Hp = s.generator->Def->MaxHp * 0.3;
			s.generator->Shield = 0;
			auto workers = Embodiables::WorkersOn(app.World(), s.battle, s.unit);
			if (workers.Count() == 0) throw std::runtime_error("no EVA crew on the hull");
			// US-T140: a debug run onto a hull already fighting: the dev waiver lifts the embody limits (never saved).
			SpaceGodInfluence::Of(app.World())->Waived = true;
			app.PickSeat(workers[0]);
			auto w = HeldWorker(app);
			if (w == nullptr) throw std::runtime_error("the worker was not taken");
			// Set it down beside the generator, facing it, mid-way through a good run of welds.
			auto at = RepairWorker::SurfaceOf(s.unit, s.generator);
			auto n = at.Normalized();
			auto side = DVec3::Cross(n, DVec3(0, 0, 1));
			if (side.LengthSquared() < 1e-6) side = DVec3::Cross(n, DVec3(1, 0, 0));
			w->Local = at - side.Normalized() * (RepairWorker::ReachLengths * 0.6);
			w->Facing = side.Normalized();
			w->Weld.Hits = 4;
			w->Weld.Streak = 4;
			w->Weld.Quality = 0.8;
			w->Weld.VerdictS = 30;
			w->Weld.VerdictGood = true;
			return true;
		} });
		auto setScale = [](double goal)
		{
			return [goal](SpaceGod::UnitySpaceApp& app, double)
			{
				if (app.ScaleGoal != goal) app.ToggleScale();
				return !app.ScaleAnimating();
			};
		};
		auto holdWeld = [](SpaceGod::UnitySpaceApp& app)
		{
			if (auto w = HeldWorker(app))
			{
				w->Weld.Quality = cs::Math::Max(w->Weld.Quality, 0.8);
				w->Weld.VerdictS = 1;
			}
		};
		if (shots.IsEmpty())
		{
			if (!scale.IsEmpty()) run->steps.push_back({ TEXT("scale set"), setScale(scale == TEXT("game") ? 1.0 : 0.0) });
		}
		else
		{
			for (double goal : { 1.0, 0.0 })
			{
				const TCHAR* label = goal > 0.5 ? TEXT("game") : TEXT("real");
				run->steps.push_back({ FString::Printf(TEXT("%s scale"), label), setScale(goal) });
				run->steps.push_back({ TEXT("camera settled"), [holdWeld](SpaceGod::UnitySpaceApp& app, double t) { holdWeld(app); return t >= 4.0; } });
				FString path = FPaths::Combine(shots, FString::Printf(TEXT("eva-repair-%s.png"), label));
				run->steps.push_back({ FString::Printf(TEXT("shot %s"), *path), [path, holdWeld](SpaceGod::UnitySpaceApp& app, double)
				{
					holdWeld(app);
					FScreenshotRequest::RequestScreenshot(path, true, false);
					return true;
				} });
				run->steps.push_back({ TEXT("shot written"), [holdWeld](SpaceGod::UnitySpaceApp& app, double t) { holdWeld(app); return t >= 1.0; } });
			}
			run->steps.push_back({ TEXT("the job's progress logged"), [scene](SpaceGod::UnitySpaceApp& app, double)
			{
				auto w = HeldWorker(app);
				UE_LOG(LogSpaceGod, Display, TEXT("sg.EvaRepair: generator at %.0f%%, welded %.1f hp, x%.2f the team's rate"),
					scene->generator->Health() * 100, w ? w->Welded : 0.0, w ? w->Weld.Multiplier() : 0.0);
				return true;
			} });
		}

		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float)
		{
			try { return run->Tick(); }
			catch (const std::exception& e) { return run->Finish(*FString(UTF8_TO_TCHAR(e.what()))); }
		}));
	}

	/// US-T135: <c>sg.EvaRepair [game|real]</c> puts the SpaceGod out on a capital ship's hull in a fight (a skirmish at
	/// Uranus if none is on) as an EVA repair worker beside a damaged shield generator; <c>shots &lt;dir&gt; [quit]</c> takes
	/// eva-repair-game.png and eva-repair-real.png into dir, then quits if asked. Works from the menu or -ExecCmds too.
	FAutoConsoleCommand GEvaRepairCommand(
		TEXT("sg.EvaRepair"),
		TEXT("US-T135: out on a capital ship's hull as an EVA repair worker at a damaged shield generator: sg.EvaRepair [game|real]; "
			"sg.EvaRepair shots <dir> [quit] screenshots it at both scales."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Start(args); }));

	/// US-T158: in the dev menu (US-T126), by its console command.
	SpaceGod::UI::CheatRegistration GEvaRepairCheat([]
	{
		SpaceGod::UI::CheatRegistry::Add("Embodiment", SpaceGod::UI::CheatEntry::Console("EVA repair on a capital's hull",
			"out on a battleship's plating mid-fight as a repair worker, welding a damaged shield generator", "sg.EvaRepair"));
	});

	/// US-T147: the showcase tour's stop, set up by this command.
	SpaceGod::Tour::TourRegistration GEvaRepairTourStop([]
	{
		SpaceGod::Tour::TourRegistry::Add(SpaceGod::Tour::TourStop::Console("Embodiment", "eva-repair", "EVA repair worker",
			"Out on a battleship's hull mid-fight as one of its repair crew, welding a damaged shield generator: keep the weld in the green", "sg.EvaRepair game")
			.At(20).Until([](SpaceGod::UnitySpaceApp& app, double t) { return HeldWorker(app) != nullptr && t > 4; }, 40).For(9).AtBothScales());
	});
}
