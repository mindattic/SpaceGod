// US-T434: sg.SeatScale, the world drawn round what the player inhabits, seen from four seats at the same world: a fighter's
// helm, a battleship's helm, a defence platform (a station seat) and a repair worker out on a hull (a person). Each seat is
// framed twice with the view looking past the world's limb: once with the seat's zoom held at 1 (the world as every seat
// drew it before) and once at the seat's own zoom, eased in; the world's apparent size from the camera is logged each time.
//   sg.SeatScale [quit]   (screenshots: Saved/Screenshots/seat-scale-*.png)

#include "UnitySpaceApp.h"

#include "Camera/SpaceCamera.h"
#include "UnityCompat/UnityRuntime.h"
#include "GameLaunch.h"
#include "SimClock.h"
#include "SpaceGodLog.h"
#include "Target.h"
#include "UnitySpaceAppActor.h"

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
#include "ScaleModel.h"
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

	/// One run, stepped once a frame on the core ticker (as sg.EvaRepair's).
	struct Run
	{
		using Step = std::function<bool(UnitySpaceApp&, double)>;
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
					UE_LOG(LogSpaceGod, Display, TEXT("sg.SeatScale: no game running; starting a new one"));
					SpaceGod::GameLaunch::NewGame();
				}
				if (now - started > 180.0) return Finish(TEXT("no game came up"));
				return true;
			}
			if (at >= steps.size()) return Finish(nullptr);
			if (stepStart < 0) stepStart = now;
			if (steps[at].second(*app, now - stepStart))
			{
				UE_LOG(LogSpaceGod, Display, TEXT("sg.SeatScale: %s"), *steps[at].first);
				at++;
				stepStart = -1;
			}
			else if (now - stepStart > 120.0) return Finish(*FString::Printf(TEXT("timed out at '%s'"), *steps[at].first));
			return true;
		}

		bool Finish(const TCHAR* failure)
		{
			if (auto* app = RunningApp()) app->SeatZoomOverrideKm = 0;
			if (failure) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.SeatScale: %s"), failure); }
			else { UE_LOG(LogSpaceGod, Display, TEXT("sg.SeatScale: done")); }
			if (quit) FPlatformMisc::RequestExit(false);
			return false;
		}
	};

	struct Scene
	{
		int body = -1;
		cs::Ref<StationSeat> station;
		cs::Ref<Ship> fighter, battleship;
		/// The way every seat looks (ecliptic axes): 22 degrees off the world's centre, so its limb is in the frame.
		DVec3 look;
	};

	/// An ecliptic direction in Unity axes, as the seat's stick takes it.
	SpaceGod::UnityCompat::Vector3 UnityAxes(DVec3 v)
	{
		auto u = v.SwapYZ();
		return SpaceGod::UnityCompat::Vector3(static_cast<float>(u.X), static_cast<float>(u.Y), static_cast<float>(u.Z));
	}

	/// A seat's stick (Unity axes) 22 degrees off the line from the camera to the world's centre, so its limb is in the frame
	/// wherever the seat is (a worker's hull may be anywhere in the fight).
	SpaceGod::UnityCompat::Vector3 LookPastLimb(UnitySpaceApp& app, int body)
	{
		auto to = app.BodyScene(body) - app.SpaceCamera->ScenePosition;
		if (to.LengthSquared() < 1e-18) return SpaceGod::UnityCompat::Vector3(0, 0, 1);
		to = to.Normalized();
		auto side = DVec3::Cross(to, DVec3::UnitY);
		if (side.LengthSquared() < 1e-9) side = DVec3::Cross(to, DVec3::UnitX);
		side = side.Normalized();
		double rad = 22.0 * Astro::Deg2Rad;
		auto a = to * cs::Math::Cos(rad) + side * cs::Math::Sin(rad);
		return SpaceGod::UnityCompat::Vector3(static_cast<float>(a.X), static_cast<float>(a.Y), static_cast<float>(a.Z));
	}

	/// The world's apparent radius from the camera (degrees), for the log.
	double ApparentDeg(UnitySpaceApp& app, int body)
	{
		double d = (app.BodyScene(body) - app.SpaceCamera->ScenePosition).Length();
		double r = app.BodyDisplayRadius(body);
		return d > r ? cs::Math::Asin(r / d) * Astro::Rad2Deg : 90.0;
	}

	void Start(const TArray<FString>& args)
	{
		bool quit = false;
		for (const FString& a : args)
			if (a.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) quit = true;

		auto run = std::make_shared<Run>();
		run->quit = quit;
		auto scene = std::make_shared<Scene>();
		auto wait = [](double seconds) { return [seconds](UnitySpaceApp&, double t) { return t >= seconds; }; };
		run->steps.push_back({ TEXT("game up"), wait(3.0) });
		run->steps.push_back({ TEXT("a world with a defence platform, a fighter and a battleship beside it"), [scene](UnitySpaceApp& app, double)
		{
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
			auto out = pose.OffsetKm.Normalized();
			auto side = DVec3::Cross(out, DVec3::UnitZ);
			if (side.LengthSquared() < 1e-9) side = DVec3::Cross(out, DVec3::UnitX);
			side = side.Normalized();
			double rad = 22.0 * Astro::Deg2Rad;
			scene->look = (out * -cs::Math::Cos(rad) + side * cs::Math::Sin(rad)).Normalized();
			scene->fighter = app.Fleet()->SpawnAt("Seat scale fighter", ShipClass::Fighter, app.World()->Sol, scene->body, pose.OffsetKm + side * 60.0, scene->look, jd);
			scene->battleship = app.Fleet()->SpawnAt("Seat scale battleship", ShipClass::Battleship, app.World()->Sol, scene->body, pose.OffsetKm - side * 120.0, scene->look, jd);
			if (scene->fighter == nullptr || scene->battleship == nullptr) throw std::runtime_error("the hulls did not spawn");
			UE_LOG(LogSpaceGod, Display, TEXT("sg.SeatScale: at %s, %.0f km out; platform %s"), *FString(UTF8_TO_TCHAR((*app.Catalog())[scene->body]->Name.c_str())),
				pose.OffsetKm.Length(), *FString(UTF8_TO_TCHAR(scene->station->Name().c_str())));
			return true;
		} });
		run->steps.push_back({ TEXT("scale settled"), [](UnitySpaceApp& app, double t) { return t >= 1.0 && !app.ScaleAnimating(); } });

		// One seat: taken, framed with the zoom held at 1, shot, then at its own zoom, shot.
		auto seat = [&](const char* label, std::function<cs::Ref<IEmbodiable>(UnitySpaceApp&)> pick)
		{
			FString name(label);
			run->steps.push_back({ FString::Printf(TEXT("%s taken"), *name), [pick, scene](UnitySpaceApp& app, double)
			{
				auto next = pick(app);
				if (next == nullptr) return false;
				app.SeatZoomOverrideKm = ScaleModel::SeatZoomFullKm; // zoom 1: the world as before US-T434
				app.PickSeat(next);
				app.AimSeatForTest(UnityAxes(scene->look));
				return true;
			} });
			for (int zoomed = 0; zoomed < 2; zoomed++)
			{
				FString tag = zoomed ? name : name + TEXT("-unscaled");
				run->steps.push_back({ FString::Printf(TEXT("%s framed"), *tag), [scene, zoomed](UnitySpaceApp& app, double t)
				{
					app.SeatZoomOverrideKm = zoomed ? 0.0 : ScaleModel::SeatZoomFullKm;
					app.AimSeatForTest(LookPastLimb(app, scene->body));
					return t >= 5.0;
				} });
				FString path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), FString::Printf(TEXT("seat-scale-%s.png"), *tag));
				run->steps.push_back({ FString::Printf(TEXT("%s logged"), *tag), [scene, tag](UnitySpaceApp& app, double)
				{
					UE_LOG(LogSpaceGod, Display, TEXT("sg.SeatScale: %s: inhabited %.4f km, zoom %.3f (goal %.3f), world's apparent radius %.1f deg"),
						*tag, app.InhabitedSizeKm(), app.Scale()->SeatZoom(), ScaleModel::SeatZoomFor(app.InhabitedSizeKm()), ApparentDeg(app, scene->body));
					if (auto w = cs::As<RepairWorker>(app.Seat()))
					{
						DVec3 at, up, forward;
						double h = 0;
						if (app.WorkerPose(w, at, up, forward, h))
							UE_LOG(LogSpaceGod, Display, TEXT("sg.SeatScale: %s: worker %.5f units tall, %.5f units from the camera; near plane %.6f units"),
								*tag, h, (at - app.SpaceCamera->ScenePosition).Length(), app.UnityCamera != nullptr ? app.UnityCamera->nearClipPlane : -1.0f);
					}
					return true;
				} });
				run->steps.push_back({ FString::Printf(TEXT("shot %s"), *path), [path](UnitySpaceApp&, double)
				{
					FScreenshotRequest::RequestScreenshot(path, true, false);
					return true;
				} });
				run->steps.push_back({ TEXT("shot written"), wait(1.0) });
			}
		};
		seat("fighter", [scene](UnitySpaceApp& app) -> cs::Ref<IEmbodiable> { return Embodiables::HelmOf(app.World(), scene->fighter); });
		seat("battleship", [scene](UnitySpaceApp& app) -> cs::Ref<IEmbodiable> { return Embodiables::HelmOf(app.World(), scene->battleship); });
		seat("station", [scene](UnitySpaceApp&) -> cs::Ref<IEmbodiable> { return scene->station; });
		run->steps.push_back({ TEXT("a fight beside the platform"), [scene](UnitySpaceApp& app, double)
		{
			double jd = app.Clock->Jd;
			auto pose = scene->station->PoseAt(jd);
			auto out = pose.OffsetKm.Normalized();
			auto side = DVec3::Cross(out, DVec3::UnitZ).Normalized();
			for (int i = 0; i < 2; i++)
				app.Fleet()->SpawnAt(cs::Cat("Seat scale raider ", i + 1), ShipClass::Battleship, app.World()->Reach, scene->body, pose.OffsetKm + side * (300.0 + 200.0 * i), side * -1.0, jd);
			app.World()->War->TryStartBattle(scene->body, app.World()->Sol, jd);
			return true;
		} });
		seat("person", [scene](UnitySpaceApp& app) -> cs::Ref<IEmbodiable>
		{
			for (const auto& b : app.World()->War->Battles)
			{
				if (b->Over || b->Body != scene->body) continue; // the fight at the same world
				for (const auto& u : b->Units)
				{
					if (!u->Present() || u->Captured) continue;
					auto workers = Embodiables::WorkersOn(app.World(), b, u);
					if (workers.Count() > 0) return workers[0];
				}
			}
			return nullptr;
		});
		// The person again, looking down along the plating at the suit and the hull it stands on, at its own zoom.
		run->steps.push_back({ TEXT("person-plating framed"), [](UnitySpaceApp& app, double t)
		{
			if (auto w = cs::As<RepairWorker>(app.Seat()))
			{
				DVec3 at, up, forward;
				double h = 0;
				if (app.WorkerPose(w, at, up, forward, h))
				{
					auto a = (forward - up * 0.35).Normalized();
					app.AimSeatForTest(SpaceGod::UnityCompat::Vector3(static_cast<float>(a.X), static_cast<float>(a.Y), static_cast<float>(a.Z)));
				}
			}
			return t >= 3.0;
		} });
		{
			FString path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("seat-scale-person-plating.png"));
			run->steps.push_back({ FString::Printf(TEXT("shot %s"), *path), [path](UnitySpaceApp&, double) { FScreenshotRequest::RequestScreenshot(path, true, false); return true; } });
			run->steps.push_back({ TEXT("shot written"), wait(1.0) });
		}
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float)
		{
			try { return run->Tick(); }
			catch (const std::exception& e) { return run->Finish(*FString(UTF8_TO_TCHAR(e.what()))); }
		}));
	}

	/// US-T434: <c>sg.SeatScale [quit]</c> takes a fighter's helm, a battleship's helm, a defence platform and a repair worker
	/// in turn at the same world, each framed past its limb with the seat's zoom held at 1 and then at its own, screenshotting
	/// each (Saved/Screenshots/seat-scale-*.png) and logging the world's apparent size; then quits if asked.
	FAutoConsoleCommand GSeatScaleCommand(
		TEXT("sg.SeatScale"),
		TEXT("US-T434: the world drawn round the seat held, from a fighter, a battleship, a station and a person at one world, "
			"each with the zoom held at 1 and at its own: sg.SeatScale [quit] (screenshots in Saved/Screenshots/seat-scale-*.png)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Start(args); }));
}
