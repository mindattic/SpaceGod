// US-T449: sg.Frontier, the frontier seen and screenshotted. The generated galaxy is searched outward from Sol for one
// system of each strange kind (Core::Frontier), each is built and travelled to in turn, and shot twice: as the arrival
// shows it (its overview) and up close on what makes it strange (the pulsar, the black hole, the relic...). Each stop logs
// what it is, how deep into the frontier, how many of its worlds are still uncharted and what the survey reveals. Last, the
// galaxy map in its Exploration mode, with the frontier gradient about Sol.
//   sg.Frontier [shot <dir>] [quit]

#include "UnitySpaceApp.h"

#include "Camera/SpaceCamera.h"
#include "Camera/StarTravel.h"
#include "GameLaunch.h"
#include "SimClock.h"
#include "SpaceGodLog.h"
#include "Target.h"
#include "UI/GalaxyMapScreen.h"
#include "UnitySpaceAppActor.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Embodiment/Influence.h"
#include "Systems/DiscoveryLog.h"
#include "Systems/Frontier.h"
#include "Systems/Galaxy.h"
#include "Systems/ProceduralSystems.h"
#include "Systems/SpaceFeatures.h"
#include "Systems/StarSystemSpec.h"
#include "Systems/SystemSources.h"
#include "World.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace SpaceGod::Core;
using SpaceGod::Target;
using SpaceGod::UnitySpaceApp;

namespace
{
	UnitySpaceApp* App()
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

	FString T(const cs::String& s) { return FString(UTF8_TO_TCHAR(s.c_str())); }

	struct Run
	{
		using Step = std::function<bool(UnitySpaceApp&, double)>;
		std::vector<std::pair<FString, Step>> steps;
		size_t at = 0;
		double stepStart = -1, started = FPlatformTime::Seconds();
		bool askedNewGame = false, quit = false;
		int shots = 0;

		bool Tick()
		{
			auto* app = App();
			double now = FPlatformTime::Seconds();
			if (app == nullptr)
			{
				if (!askedNewGame && now - started > 2.0)
				{
					askedNewGame = true;
					SpaceGod::GameLaunch::NewGame();
				}
				return now - started > 180.0 ? Finish(TEXT("no game came up")) : true;
			}
			if (at >= steps.size()) return Finish(nullptr);
			if (stepStart < 0)
			{
				stepStart = now;
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Frontier: %s"), *steps[at].first);
			}
			if (steps[at].second(*app, now - stepStart))
			{
				at++;
				stepStart = -1;
			}
			else if (now - stepStart > 120.0) return Finish(*FString::Printf(TEXT("timed out at '%s'"), *steps[at].first));
			return true;
		}

		bool Finish(const TCHAR* failure)
		{
			if (auto* app = App())
			{
				app->ShowGalaxyMap = false;
				app->Clock->SetPaused(false);
			}
			if (failure) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.Frontier: %s"), failure); }
			else { UE_LOG(LogSpaceGod, Display, TEXT("sg.Frontier: done, %d screenshots"), shots); }
			if (quit) FPlatformMisc::RequestExit(false);
			return false;
		}
	};

	/// The kinds in the order they are visited.
	const FrontierKind Kinds[] = { FrontierKind::Pulsar, FrontierKind::BlackHole, FrontierKind::WhiteDwarf, FrontierKind::BrownDwarfs,
		FrontierKind::RoguePlanet, FrontierKind::Binary, FrontierKind::Trinary, FrontierKind::Nebula, FrontierKind::CometCloud,
		FrontierKind::Relic, FrontierKind::Wanderers };

	struct Scene
	{
		std::map<int, cs::Ref<StarEntry>> found;
		cs::Ref<StarSystem> system;
	};

	/// Searches outward from Sol, in a spread of directions, for the nearest generated star of each strange kind.
	void Search(UnitySpaceApp& app, Scene& scene)
	{
		const DVec3 dirs[] = { DVec3(1, 0, 0), DVec3(-1, 0, 0), DVec3(0, 1, 0), DVec3(0, -1, 0), DVec3(0, 0, 1), DVec3(0, 0, -1),
			DVec3(1, 1, 0).Normalized(), DVec3(-1, 1, 0).Normalized(), DVec3(1, -1, 0).Normalized(), DVec3(-1, -1, 0).Normalized() };
		const int wanted = static_cast<int>(sizeof(Kinds) / sizeof(Kinds[0]));
		for (double r = 90; r <= 520 && static_cast<int>(scene.found.size()) < wanted; r += 40)
			for (const auto& d : dirs)
			{
				for (const auto& e : SystemSources::StarsNear(d * r, 14, app.Galaxy->Seed))
				{
					if (e->Source != ProceduralSystems::SourceName || e->Spectral != "?") continue;
					auto kind = Frontier::Classify(e->Seed, e->PositionLy);
					if (kind == FrontierKind::None || scene.found.count(static_cast<int>(kind)) > 0) continue;
					scene.found[static_cast<int>(kind)] = e;
					UE_LOG(LogSpaceGod, Display, TEXT("sg.Frontier: found %hs at %.1f ly (%s, %.0f%% deep)"), ToString(kind), e->PositionLy.Length(), *T(e->Name),
						Frontier::Depth(e->PositionLy) * 100);
				}
				if (static_cast<int>(scene.found.size()) >= wanted) break;
			}
	}

	/// The body that makes the system strange, to look at close up (-1: the arrival's overview says it all).
	int Heart(UnitySpaceApp& app, FrontierKind kind)
	{
		auto cat = app.Catalog();
		if (cat == nullptr || cat->Count() == 0) return -1;
		switch (kind)
		{
		case FrontierKind::Relic:
			for (int i = 0; i < cat->Count(); i++)
				if (Frontier::IsRelic(*(*cat)[i])) return i;
			return -1;
		case FrontierKind::Binary:
		case FrontierKind::Trinary:
		case FrontierKind::BrownDwarfs:
			for (int i = 0; i < cat->Count(); i++)
				if ((*cat)[i]->IsStar()) return i;
			return -1;
		case FrontierKind::CometCloud:
		case FrontierKind::Wanderers:
			return -1;
		default:
			return 0;
		}
	}

	void LogSystem(UnitySpaceApp& app, const FString& tag)
	{
		auto s = app.ActiveSystem;
		if (s == nullptr) return;
		auto cat = s->Catalog;
		int stars = 0, worlds = 0;
		cs::String root;
		for (int i = 0; cat != nullptr && i < cat->Count(); i++)
		{
			const auto& b = (*cat)[i];
			if (b->IsStar()) { stars++; if (root.empty()) root = cs::Cat(b->Name, " ", b->SpectralType, " ", cs::Fmt(b->RadiusKm, "0.###"), " km"); }
			else if (b->IsVisible()) worlds++;
		}
		const auto& features = cat != nullptr && cat->Count() > 0 ? SpaceFeatures::Of(cat->Root()->Name) : SpaceFeatures::Of("");
		UE_LOG(LogSpaceGod, Display, TEXT("sg.Frontier: [%s] %s, %.1f ly (%.0f%% deep), oddity %hs, uncharted ground %d: %d star(s) (%s), %d other bodies, %d uncharted, %d nebulae, %d ice fields, %d discs; %s"),
			*tag, *T(s->Name), s->PositionLy.Length(), Frontier::Depth(s->PositionLy) * 100, ToString(Frontier::KindOf(*s)), Frontier::IsUncharted(*s) ? 1 : 0,
			stars, *T(root), worlds, app.UnchartedBodies(), features.Nebulae.Count(), features.IceFields.Count(), s->Discs.Count(),
			s->Spec != nullptr ? *T(s->Spec->Summary) : TEXT(""));
	}

	void Start(const TArray<FString>& args)
	{
		FString dir;
		bool quit = false;
		for (int i = 0; i < args.Num(); i++)
		{
			if (args[i].Equals(TEXT("shot"), ESearchCase::IgnoreCase) && i + 1 < args.Num()) dir = args[++i];
			else if (args[i].Equals(TEXT("quit"), ESearchCase::IgnoreCase)) quit = true;
		}
		auto run = std::make_shared<Run>();
		run->quit = quit;
		auto scene = std::make_shared<Scene>();
		auto push = [&run](FString name, Run::Step step) { run->steps.push_back({ name, std::move(step) }); };
		auto wait = [](double seconds) { return [seconds](UnitySpaceApp&, double t) { return t >= seconds; }; };
		auto shot = [&push, dir, run](FString name)
		{
			FString file = dir.IsEmpty() ? FString() : FPaths::Combine(dir, FString::Printf(TEXT("frontier-%s.png"), *name));
			push(FString::Printf(TEXT("shot %s"), *name), [file, name, run](UnitySpaceApp& app, double)
			{
				LogSystem(app, name);
				if (!file.IsEmpty())
				{
					FScreenshotRequest::RequestScreenshot(file, true, false);
					run->shots++;
					UE_LOG(LogSpaceGod, Display, TEXT("sg.Frontier: screenshot %s"), *file);
				}
				return true;
			});
		};

		push(TEXT("game up"), wait(6.0));
		push(TEXT("staged"), [scene](UnitySpaceApp& app, double)
		{
			app.SkyLeaveHelm();
			if (app.IsPiloting() || app.HeldSeat() != nullptr) app.PickCommand();
			if (!app.SpaceCamera->FocusTarget.IsNone()) app.SpaceCamera->ExitFocus();
			app.Clock->SetPaused(true);
			SpaceGodInfluence::Of(app.World())->Waived = true; // a debug run: the embody limits lifted (never saved)
			Search(app, *scene);
			if (scene->found.empty()) throw std::runtime_error("no strange system found on the frontier");
			return true;
		});

		for (auto kind : Kinds)
		{
			const int k = static_cast<int>(kind);
			const FString name = FString(ANSI_TO_TCHAR(ToString(kind))).ToLower();
			push(FString::Printf(TEXT("to the %s"), *name), [scene, k](UnitySpaceApp& app, double)
			{
				scene->system = nullptr;
				auto it = scene->found.find(k);
				if (it == scene->found.end())
				{
					UE_LOG(LogSpaceGod, Warning, TEXT("sg.Frontier: no %hs found within the search"), ToString(static_cast<FrontierKind>(k)));
					return true;
				}
				scene->system = app.Galaxy->Realize(it->second, app.Clock->Jd);
				if (scene->system == nullptr) throw std::runtime_error("a frontier star would not build");
				if (Frontier::KindOf(*scene->system) != static_cast<FrontierKind>(k)) throw std::runtime_error("a frontier star built as another kind");
				if (!app.SpaceCamera->FocusTarget.IsNone()) app.SpaceCamera->ExitFocus();
				app.CheatTravelTo(scene->system);
				return true;
			});
			push(FString::Printf(TEXT("arriving at the %s"), *name), [scene](UnitySpaceApp& app, double t)
			{
				if (scene->system == nullptr) return true;
				bool travelling = app.Travel != nullptr && app.Travel->Active;
				return !travelling && app.ActiveSystem == scene->system && t > 3.0;
			});
			push(TEXT("settle"), [scene](UnitySpaceApp&, double t) { return scene->system == nullptr || t > 2.5; });
			// The overview, as the arrival shows it.
			{
				FString file = dir.IsEmpty() ? FString() : FPaths::Combine(dir, FString::Printf(TEXT("frontier-%s-overview.png"), *name));
				push(FString::Printf(TEXT("shot %s overview"), *name), [scene, file, name, run](UnitySpaceApp& app, double)
				{
					if (scene->system == nullptr) return true;
					LogSystem(app, name + TEXT(" overview"));
					if (!file.IsEmpty())
					{
						FScreenshotRequest::RequestScreenshot(file, true, false);
						run->shots++;
						UE_LOG(LogSpaceGod, Display, TEXT("sg.Frontier: screenshot %s"), *file);
					}
					return true;
				});
			}
			// Then close on what makes it strange.
			push(FString::Printf(TEXT("close on the %s"), *name), [scene, kind](UnitySpaceApp& app, double t)
			{
				if (scene->system == nullptr) return true;
				int heart = Heart(app, kind);
				if (heart < 0) return true;
				if (t < 0.05)
				{
					app.Select(Target::OfBody(heart));
					app.FocusSelected();
				}
				return t > 3.5;
			});
			{
				FString file = dir.IsEmpty() ? FString() : FPaths::Combine(dir, FString::Printf(TEXT("frontier-%s.png"), *name));
				push(FString::Printf(TEXT("shot %s"), *name), [scene, file, name, run, kind](UnitySpaceApp& app, double)
				{
					if (scene->system == nullptr || Heart(app, kind) < 0) return true;
					LogSystem(app, name);
					if (!file.IsEmpty())
					{
						FScreenshotRequest::RequestScreenshot(file, true, false);
						run->shots++;
						UE_LOG(LogSpaceGod, Display, TEXT("sg.Frontier: screenshot %s"), *file);
					}
					return true;
				});
			}
			push(TEXT("logged"), [](UnitySpaceApp& app, double t)
			{
				if (t < 0.5) return false;
				const auto& log = app.Discoveries();
				int phenomena = log.CountOf(DiscoveryKind::Phenomenon);
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Frontier: the discovery log holds %d phenomena, %d bodies surveyed"), phenomena, log.CountOf(DiscoveryKind::Body));
				return true;
			});
		}

		// The galaxy map, Exploration mode: the frontier gradient about Sol and the strange systems on it.
		push(TEXT("galaxy map"), [](UnitySpaceApp& app, double t)
		{
			if (t < 0.05)
			{
				if (!app.SpaceCamera->FocusTarget.IsNone()) app.SpaceCamera->ExitFocus();
				SpaceGod::UI::GalaxyMapScreen::RequestedMode = static_cast<int>(SpaceGod::UI::GalaxyMapScreen::MapMode::Exploration);
				SpaceGod::UI::GalaxyMapScreen::RequestFit = true;
				app.ShowGalaxyMap = true;
			}
			return t > 2.5;
		});
		shot(TEXT("galaxy-map"));
		push(TEXT("map closed"), [](UnitySpaceApp& app, double t)
		{
			app.ShowGalaxyMap = false;
			return t > 0.5;
		});
		push(TEXT("written"), wait(1.0));

		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float)
		{
			try { return run->Tick(); }
			catch (const std::exception& e) { return run->Finish(*FString(UTF8_TO_TCHAR(e.what()))); }
		}));
	}

	FAutoConsoleCommand GFrontierCommand(
		TEXT("sg.Frontier"),
		TEXT("US-T449: sg.Frontier [shot <dir>] [quit] searches the generated galaxy outward from Sol for one system of each strange "
			"frontier kind (pulsar, black hole, white dwarf, brown dwarf pair, rogue planet, binary, triple, nebula, comet cloud, relic, "
			"wandering worlds), travels to each and screenshots its overview and a close-up, then the galaxy map's Exploration mode "
			"with the frontier gradient."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Start(args); }));
}
