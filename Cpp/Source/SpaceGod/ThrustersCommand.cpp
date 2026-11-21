// US-T432 / US-T433: sg.Thrusters, every hull's thrusters seen where they sit. A row of hulls (a capital of each kind for
// the navy and the pirates, the strike craft, the work and trade hulls) is held still in open space; each in turn is
// looked at from astern (its main engines) and from abeam forward (its RCS jets) with every jet lit (sg.ThrustersLit),
// and screenshotted.
//   sg.Thrusters shot <dir> [quit]

#include "UnitySpaceApp.h"

#include "Camera/SpaceCamera.h"
#include "GameLaunch.h"
#include "SimClock.h"
#include "SpaceGodLog.h"
#include "Target.h"
#include "UnitySpaceAppActor.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Fleet.h"
#include "Ships.h"
#include "World.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

#include <functional>
#include <iterator>
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
			if (stepStart < 0) stepStart = now;
			if (steps[at].second(*app, now - stepStart))
			{
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Thrusters: %s"), *steps[at].first);
				at++;
				stepStart = -1;
			}
			else if (now - stepStart > 60.0) return Finish(*FString::Printf(TEXT("timed out at '%s'"), *steps[at].first));
			return true;
		}

		bool Finish(const TCHAR* failure)
		{
			if (auto* lit = IConsoleManager::Get().FindConsoleVariable(TEXT("sg.ThrustersLit"))) lit->Set(0, ECVF_SetByCode);
			if (auto* app = App()) app->Clock->SetPaused(false);
			if (failure) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.Thrusters: %s"), failure); }
			else { UE_LOG(LogSpaceGod, Display, TEXT("sg.Thrusters: done")); }
			if (quit) FPlatformMisc::RequestExit(false);
			return false;
		}
	};

	/// The drawn root of a ship's model: the ancestor of its renderers named after it.
	cs::Ref<SpaceGod::Transform> RootOf(UnitySpaceApp& app, const cs::Ref<Ship>& ship)
	{
		for (const auto& r : app.ModelRenderersOf(Target::OfShip(ship)))
		{
			if (!r) continue;
			for (auto t = r->transform(); t; t = t->parent.lock())
				if (t->gameObject() && t->gameObject()->name == ship->Name) return t;
		}
		return nullptr;
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
		struct Pick { ShipClass Class; bool Pirate; const TCHAR* Tag; };
		static const Pick picks[] = {
			{ ShipClass::Battleship, false, TEXT("battleship") }, { ShipClass::Cruiser, false, TEXT("cruiser") },
			{ ShipClass::Destroyer, false, TEXT("destroyer") }, { ShipClass::Carrier, false, TEXT("carrier") },
			{ ShipClass::Battleship, true, TEXT("pirate-battleship") }, { ShipClass::Destroyer, true, TEXT("pirate-destroyer") },
			{ ShipClass::Fighter, false, TEXT("fighter") }, { ShipClass::Interceptor, false, TEXT("interceptor") },
			{ ShipClass::Bomber, false, TEXT("bomber") }, { ShipClass::Gunship, false, TEXT("gunship") },
			{ ShipClass::Courier, false, TEXT("courier") }, { ShipClass::Freighter, false, TEXT("freighter") },
			{ ShipClass::Miner, false, TEXT("miner") }, { ShipClass::OreTug, false, TEXT("oretug") },
		};
		struct Stage { std::vector<cs::Ref<Ship>> Ships; DVec3 At; int Body = 0; };
		auto s = std::make_shared<Stage>();
		auto hold = [s](UnitySpaceApp& app)
		{
			for (const auto& ship : s->Ships)
				if (ship) { ship->Phase = ShipPhase::Parked; ship->PhaseEndJd = app.Clock->Jd + 1; }
		};
		auto push = [&run](FString name, Run::Step step) { run->steps.push_back({ name, std::move(step) }); };

		push(TEXT("game up"), [](UnitySpaceApp&, double t) { return t >= 6.0; });
		push(TEXT("hulls staged in open space"), [s](UnitySpaceApp& app, double)
		{
			app.SkyLeaveHelm();
			if (!app.SpaceCamera->FocusTarget.IsNone()) app.SpaceCamera->ExitFocus();
			app.Clock->SetPaused(true);
			cs::Ref<Faction> pirates;
			for (const auto& ship : app.Fleet()->Ships())
				if (ship && ship->Faction && ship->Faction->IsPirate) { pirates = ship->Faction; break; }
			s->Body = app.Catalog()->Root()->Index;
			s->At = DVec3(-0.4, 1.3, 0.05) * Astro::AuKm;
			int n = 0;
			for (const auto& p : picks)
			{
				auto faction = p.Pirate && pirates ? pirates : app.World()->Sol;
				// Far enough apart that each is framed alone.
				auto ship = app.Fleet()->SpawnAt(cs::Cat("Thruster Check ", n), p.Class, faction, s->Body, s->At + DVec3(20000.0 * n, 0, 0), DVec3::UnitY, app.Clock->Jd);
				if (!ship) throw std::runtime_error("a hull would not spawn");
				s->Ships.push_back(ship);
				n++;
			}
			if (auto* lit = IConsoleManager::Get().FindConsoleVariable(TEXT("sg.ThrustersLit"))) lit->Set(1, ECVF_SetByCode);
			return true;
		});
		for (int i = 0; i < static_cast<int>(std::size(picks)); i++)
		{
			const auto& p = picks[i];
			// Astern, a little to starboard and above (its main engines); then abeam and forward of it, above (its RCS).
			for (int view = 0; view < 2; view++)
			{
				SpaceGod::Vector3 off = view == 0 ? SpaceGod::Vector3(0.55f, 0.45f, -1.0f) : SpaceGod::Vector3(-1.0f, 0.55f, 0.45f);
				push(FString::Printf(TEXT("%s framed %s"), p.Tag, view == 0 ? TEXT("astern") : TEXT("abeam")), [s, hold, i, off](UnitySpaceApp& app, double t)
				{
					hold(app);
					auto ship = s->Ships[i];
					auto root = RootOf(app, ship);
					auto centre = app.FocusScene(Target::OfShip(ship));
					double length = app.ShipDisplayLength(ship);
					if (length > 0)
					{
						// Brought close first (the model is drawn only once it is big enough), then turned with the hull.
						auto rot = root ? root->rotation() : SpaceGod::Quaternion::identity();
						auto d = rot * off.normalized();
						auto eye = centre + DVec3(d.x, d.y, d.z) * (length * 1.25);
						auto look = centre - eye;
						app.SpaceCamera->SetPose(eye, SpaceGod::Quaternion::LookRotation(
							SpaceGod::Vector3(static_cast<float>(look.X), static_cast<float>(look.Y), static_cast<float>(look.Z)), rot * SpaceGod::Vector3::up()));
					}
					if (!root && t > 19) UE_LOG(LogSpaceGod, Warning, TEXT("sg.Thrusters: %s's model never came up"), *T(ship->Name));
					return (root && t > 2.0) || t > 20;
				});
				FString file = FPaths::Combine(dir, FString::Printf(TEXT("thrusters-%s-%s.png"), p.Tag, view == 0 ? TEXT("astern") : TEXT("abeam")));
				push(TEXT("shot"), [file, s, i](UnitySpaceApp& app, double)
				{
					auto ship = s->Ships[i];
					UE_LOG(LogSpaceGod, Display, TEXT("sg.Thrusters: %s (%s) drawn %.4g long"), *T(ship->Name), *T(cs::Str(ship->Spec->Class)), app.ShipDisplayLength(ship));
					if (!file.IsEmpty())
					{
						FScreenshotRequest::RequestScreenshot(file, false, false);
						UE_LOG(LogSpaceGod, Display, TEXT("sg.Thrusters: screenshot %s"), *file);
					}
					return true;
				});
				push(TEXT("written"), [hold](UnitySpaceApp& app, double t) { hold(app); return t >= 0.8; });
			}
		}
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float)
		{
			try { return run->Tick(); }
			catch (const std::exception& e) { return run->Finish(*FString(UTF8_TO_TCHAR(e.what()))); }
		}));
	}

	FAutoConsoleCommand GThrustersCommand(
		TEXT("sg.Thrusters"),
		TEXT("US-T432/US-T433: sg.Thrusters [shot <dir>] [quit] holds a row of hulls still with every thruster lit and frames each from astern "
			"(main engines) and abeam (RCS), screenshotting each into <dir>."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Start(args); }));
}
