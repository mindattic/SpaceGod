// US-T446: sg.Possess, the possession show and the possessed views, screenshotted. A battleship, a fighter, a freighter far
// out and a raider running silent are put by a world with a defence platform; then: the god's view of them; the battleship's
// helm taken (shots as the energy crosses to it and seeps into the hull); the helm's own view (the raider gone, the far
// freighter where the light shows it); a fight started by the platform and the platform taken (its panoramic view); a repair
// worker out on a hull taken (a person's eyes); the battleship's helm again, then let go (shots as the energy comes back out,
// then the god's full view again). Each shot logs what the view perceives and how far the show is.
//   sg.Possess [shot <dir>] [quit]   (screenshots possess-*.png; without shot, Saved/Screenshots)

#include "UnitySpaceApp.h"

#include "Camera/SpaceCamera.h"
#include "GameLaunch.h"
#include "SimClock.h"
#include "SpaceGodLog.h"
#include "Target.h"
#include "UnitySpaceAppActor.h"
#include "Views/PossessionFx.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Combat/Battle.h"
#include "Combat/War.h"
#include "Embodiment/Embodiables.h"
#include "Embodiment/Influence.h"
#include "Embodiment/RepairWorker.h"
#include "Embodiment/ShipHelm.h"
#include "Embodiment/StationSeat.h"
#include "Fleet.h"
#include "Sensors/Sensors.h"
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
#include <memory>
#include <stdexcept>
#include <vector>

using namespace SpaceGod::Core;
using SpaceGod::Target;
using SpaceGod::UnitySpaceApp;

namespace
{
	UnitySpaceApp* RunningApp()
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

	struct Run
	{
		using Step = std::function<bool(UnitySpaceApp&, double)>;
		std::vector<std::pair<FString, Step>> steps;
		size_t at = 0;
		double stepStart = -1, started = FPlatformTime::Seconds();
		bool askedNewGame = false, quit = false;

		bool Tick()
		{
			auto* app = RunningApp();
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
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Possess: %s"), *steps[at].first);
				at++;
				stepStart = -1;
			}
			else if (now - stepStart > 90.0) return Finish(*FString::Printf(TEXT("timed out at '%s'"), *steps[at].first));
			return true;
		}

		bool Finish(const TCHAR* failure)
		{
			if (failure) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.Possess: %s"), failure); }
			else { UE_LOG(LogSpaceGod, Display, TEXT("sg.Possess: done")); }
			if (quit) FPlatformMisc::RequestExit(false);
			return false;
		}
	};

	struct Scene
	{
		int body = -1;
		cs::Ref<StationSeat> station;
		cs::Ref<Ship> battleship, fighter, freighter, raider;
		DVec3 side, out;
	};

	const TCHAR* KindName(UnitySpaceApp::PerceptionKind k)
	{
		switch (k)
		{
			case UnitySpaceApp::PerceptionKind::Sensors: return TEXT("sensors");
			case UnitySpaceApp::PerceptionKind::Eyes: return TEXT("eyes");
			case UnitySpaceApp::PerceptionKind::Panorama: return TEXT("panorama");
			default: return TEXT("god");
		}
	}

	void Log(UnitySpaceApp& app, const FString& tag, const cs::Ref<Scene>& scene)
	{
		auto picture = app.PerceivedPicture();
		auto level = [&](const cs::Ref<Ship>& s) -> FString
		{
			if (picture == nullptr) return TEXT("all");
			auto c = picture->Of(s);
			if (c == nullptr) return TEXT("unseen");
			return FString::Printf(TEXT("%s%s lag %.2fs"), UTF8_TO_TCHAR(ToString(c->Level)), c->Cold() ? TEXT(" cold") : TEXT(""), c->LagSeconds());
		};
		auto fx = app.possessionFx;
		UE_LOG(LogSpaceGod, Display, TEXT("sg.Possess: %s: view %s (shown %.2f), show %s t=%.2f, motes %d, rim halo %.2f, sheen %.2f, fov %.1f; picture %d tracks; freighter %s, raider %s, fighter %s"),
			*tag, KindName(app.Perceiving()), app.PerceptionShown(), app.PossessionShowing() ? (app.PossessionEntering() ? TEXT("in") : TEXT("out")) : TEXT("off"),
			app.PossessionT(), fx ? fx->MotesLit : -1, fx ? fx->HaloNow : -1.0f, fx ? fx->SweepNow : -1.0f, app.UnityCamera ? app.UnityCamera->fieldOfView : 0.0f,
			picture ? picture->Count() : -1, *level(scene->freighter), *level(scene->raider), *level(scene->fighter));
	}

	void Start(const TArray<FString>& args)
	{
		FString dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"));
		bool quit = false;
		for (int i = 0; i < args.Num(); i++)
		{
			if (args[i].Equals(TEXT("shot"), ESearchCase::IgnoreCase) && i + 1 < args.Num()) dir = args[++i];
			else if (args[i].Equals(TEXT("quit"), ESearchCase::IgnoreCase)) quit = true;
		}
		auto run = std::make_shared<Run>();
		run->quit = quit;
		auto scene = cs::New<Scene>();
		auto push = [&run](FString name, Run::Step step) { run->steps.push_back({ name, std::move(step) }); };
		auto wait = [](double seconds) { return [seconds](UnitySpaceApp&, double t) { return t >= seconds; }; };
		auto shot = [&push, dir, scene](const TCHAR* tag)
		{
			FString file = FPaths::Combine(dir, FString::Printf(TEXT("possess-%s.png"), tag));
			FString name(tag);
			push(FString::Printf(TEXT("shot %s"), *file), [file, name, scene](UnitySpaceApp& app, double)
			{
				Log(app, name, scene);
				FScreenshotRequest::RequestScreenshot(file, true, false);
				return true;
			});
		};
		// A shot as the show reaches t seconds (going in, or coming out).
		auto showShot = [&push, &shot](const TCHAR* tag, bool entering, float t)
		{
			push(FString::Printf(TEXT("show at %.2f s"), t), [entering, t](UnitySpaceApp& app, double waited)
			{
				return (app.PossessionShowing() && app.PossessionEntering() == entering && app.PossessionT() >= t) || waited > 20.0;
			});
			shot(tag);
		};

		push(TEXT("game up"), wait(5.0));
		push(TEXT("staged by a world with a defence platform"), [scene](UnitySpaceApp& app, double)
		{
			app.SkyLeaveHelm();
			if (app.ScaleGoal < 0.5) app.ToggleScale();
			SpaceGodInfluence::Of(app.World())->Waived = true; // a debug run: the embody limits lifted (never saved)
			double jd = app.Clock->Jd;
			for (const auto& s : Embodiables::StationsIn(app.World()))
				if (s != nullptr && s->CanEmbody() && s->Settlement != nullptr)
				{
					scene->station = s;
					break;
				}
			if (scene->station == nullptr) throw std::runtime_error("no defence platform to take in this system");
			auto pose = scene->station->PoseAt(jd);
			scene->body = pose.FrameBody;
			scene->out = pose.OffsetKm.Normalized();
			scene->side = DVec3::Cross(scene->out, DVec3::UnitZ);
			if (scene->side.LengthSquared() < 1e-9) scene->side = DVec3::Cross(scene->out, DVec3::UnitX);
			scene->side = scene->side.Normalized();
			auto look = (scene->out * -0.6 + scene->side * 0.8).Normalized();
			auto fleet = app.Fleet();
			auto sol = app.World()->Sol;
			scene->battleship = fleet->SpawnAt("Possession Battleship", ShipClass::Battleship, sol, scene->body, pose.OffsetKm - scene->side * 120.0, look, jd);
			scene->fighter = fleet->SpawnAt("Possession Fighter", ShipClass::Fighter, sol, scene->body, pose.OffsetKm + scene->side * 60.0, look, jd);
			// Far out: a freighter squawking (seen by its transponder, a light-second and more away) ...
			scene->freighter = fleet->SpawnAt("Possession Freighter", ShipClass::Freighter, sol, scene->body, pose.OffsetKm + scene->side * 380000.0 + scene->out * 60000.0, look, jd);
			// ... and a courier of the same flag running silent: beyond the eye, under the scanners' reach.
			scene->raider = fleet->SpawnAt("Possession Silent Courier", ShipClass::Courier, sol, scene->body, pose.OffsetKm - scene->side * 260000.0, look, jd);
			if (!scene->battleship || !scene->fighter || !scene->freighter || !scene->raider) throw std::runtime_error("a hull would not spawn");
			scene->raider->SilentRunning = true;
			app.Clock->SetWarp(0);
			return true;
		});
		push(TEXT("scale settled"), [](UnitySpaceApp& app, double t) { return t >= 1.0 && !app.ScaleAnimating(); });

		// ---- the god's view: the free camera off the battleship's quarter, looking at it and the world beyond
		push(TEXT("god framed"), [scene](UnitySpaceApp& app, double t)
		{
			if (!app.SpaceCamera->FocusTarget.IsNone()) app.SpaceCamera->ExitFocus();
			auto target = Target::OfShip(scene->battleship);
			auto centre = app.FocusScene(target);
			double r = app.TargetRadius(target);
			auto toWorld = (app.BodyScene(scene->body) - centre).Normalized();
			auto up = DVec3::UnitY;
			auto side = DVec3::Cross(up, toWorld).Normalized();
			auto eye = centre - toWorld * (r * 9) + side * (r * 5) + up * (r * 2.5);
			auto look = (centre + toWorld * (r * 2) - eye).Normalized();
			app.SpaceCamera->SetPose(eye, SpaceGod::Quaternion::LookRotation(SpaceGod::Vector3(static_cast<float>(look.X), static_cast<float>(look.Y), static_cast<float>(look.Z)), SpaceGod::Vector3::up()));
			return t >= 3.0;
		});
		shot(TEXT("1-god"));
		push(TEXT("shot written"), wait(1.0));

		// ---- into the battleship's helm: the energy crossing, the seep into the hull
		push(TEXT("helm taken"), [scene](UnitySpaceApp& app, double)
		{
			app.PickSeat(Embodiables::HelmOf(app.World(), scene->battleship));
			return true;
		});
		showShot(TEXT("2-enter-0.35"), true, 0.35f);
		showShot(TEXT("3-enter-0.70"), true, 0.70f);
		showShot(TEXT("4-enter-1.05"), true, 1.05f);
		showShot(TEXT("5-enter-1.50"), true, 1.50f);
		push(TEXT("helm settled"), wait(5.0));
		shot(TEXT("6-helm-view"));
		push(TEXT("shot written"), wait(1.0));

		// ---- a fight by the platform, and the platform taken: a station's panoramic view
		push(TEXT("a fight beside the platform"), [scene](UnitySpaceApp& app, double)
		{
			double jd = app.Clock->Jd;
			auto pose = scene->station->PoseAt(jd);
			for (int i = 0; i < 2; i++)
				app.Fleet()->SpawnAt(cs::Cat("Possession raider ", i + 1), ShipClass::Destroyer, app.World()->Reach, scene->body, pose.OffsetKm + scene->side * (300.0 + 200.0 * i), scene->side * -1.0, jd);
			app.World()->War->TryStartBattle(scene->body, app.World()->Sol, jd);
			return true;
		});
		push(TEXT("fight on"), wait(2.0));
		push(TEXT("platform taken"), [scene](UnitySpaceApp& app, double)
		{
			app.PickSeat(scene->station);
			return true;
		});
		showShot(TEXT("7-station-enter-0.60"), true, 0.60f);
		push(TEXT("station settled"), wait(5.0));
		shot(TEXT("8-station-view"));
		push(TEXT("shot written"), wait(1.0));

		// ---- a person out on the plating: a pair of eyes in a suit
		push(TEXT("worker taken"), [scene](UnitySpaceApp& app, double t)
		{
			for (const auto& b : app.World()->War->Battles)
			{
				if (b->Over) continue;
				for (const auto& u : b->Units)
				{
					if (!u->Present() || u->Captured) continue;
					auto workers = Embodiables::WorkersOn(app.World(), b, u);
					if (workers.Count() > 0)
					{
						app.PickSeat(workers[0]);
						return true;
					}
				}
			}
			if (t > 15.0) throw std::runtime_error("no repair worker to take");
			return false;
		});
		showShot(TEXT("9-eva-enter-0.70"), true, 0.70f);
		push(TEXT("worker settled"), wait(5.0));
		shot(TEXT("10-eva-view"));
		push(TEXT("shot written"), wait(1.0));

		// ---- back to the battleship's helm, then let go: the energy comes back out and the god's view returns
		push(TEXT("helm again"), [scene](UnitySpaceApp& app, double)
		{
			app.PickSeat(Embodiables::HelmOf(app.World(), scene->battleship));
			return true;
		});
		push(TEXT("helm settled again"), wait(5.0));
		push(TEXT("let go"), [](UnitySpaceApp& app, double)
		{
			app.PickCommand();
			return true;
		});
		showShot(TEXT("11-leave-0.30"), false, 0.30f);
		showShot(TEXT("12-leave-0.80"), false, 0.80f);
		push(TEXT("god again"), wait(3.5));
		shot(TEXT("13-god-again"));
		push(TEXT("shot written"), wait(1.0));

		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float)
		{
			try { return run->Tick(); }
			catch (const std::exception& e) { return run->Finish(*FString(UTF8_TO_TCHAR(e.what()))); }
		}));
	}

	FAutoConsoleCommand GPossessCommand(
		TEXT("sg.Possess"),
		TEXT("US-T446: sg.Possess [shot <dir>] [quit] screenshots the possession show and the possessed views: the god's view, the energy "
			"crossing into a battleship's helm, the helm's own view, a station's, a person's, and letting go back to the god's view "
			"(possess-*.png)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Start(args); }));
}
