// US-T132: sg.Skybox, a debug tour of the painted skies beyond Sol, one system per biome, with a ship in frame.

#include "UnitySpaceApp.h"

#include "Camera/SpaceCamera.h"
#include "Camera/StarTravel.h"
#include "GameLaunch.h"
#include "Rendering/SystemSkies.h"
#include "Rendering/SystemSkyBake.h"
#include "BodyCatalog.h"
#include "Fleet.h"
#include "ScaleModel.h"
#include "Ships.h"
#include "SimClock.h"
#include "SpaceGodLog.h"
#include "Systems/Biomes.h"
#include "Systems/Galaxy.h"
#include "Target.h"
#include "Tour/ShowcaseTour.h"
#include "UnitySpaceAppActor.h"
#include "Views/BodyView.h"
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
#include <set>
#include <stdexcept>
#include <vector>

using namespace SpaceGod::Core;
using SpaceGod::UnityCompat::Quaternion;
using SpaceGod::UnityCompat::Vector3;

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

	/// One run, stepped once a frame on the core ticker (as sg.VoidStation): each step returns true when done.
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
					UE_LOG(LogSpaceGod, Display, TEXT("sg.Skybox: no game running; starting a new one"));
					SpaceGod::GameLaunch::NewGame();
				}
				if (now - started > 180.0) return Finish(TEXT("no game came up"));
				return true;
			}
			if (at >= steps.size()) return Finish(nullptr);
			if (stepStart < 0) stepStart = now;
			if (steps[at].second(*app, now - stepStart))
			{
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Skybox: %s"), *steps[at].first);
				at++;
				stepStart = -1;
			}
			else if (now - stepStart > 240.0) return Finish(*FString::Printf(TEXT("timed out at '%s'"), *steps[at].first));
			return true;
		}

		bool Finish(const TCHAR* failure)
		{
			if (failure) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.Skybox: %s"), failure); }
			else { UE_LOG(LogSpaceGod, Display, TEXT("sg.Skybox: done")); }
			if (quit) FPlatformMisc::RequestExit(false);
			return false;
		}
	};

	/// Up to <paramref name="count"/> systems beyond Sol, each of a biome not yet picked (void stations too), nearest first.
	cs::List<cs::String> Pick(SpaceGod::UnitySpaceApp& app, int count)
	{
		cs::List<cs::String> names;
		std::set<int> kinds;
		for (const auto& s : app.Galaxy->Systems)
		{
			if (names.Count() >= count) break;
			if (!SpaceGod::Rendering::SystemSkies::IsWild(s->Name)) continue;
			int kind = static_cast<int>(Biomes::KindOf(s->Name));
			if (kinds.count(kind)) continue;
			kinds.insert(kind);
			names.Add(s->Name);
		}
		return names;
	}

	cs::Ref<Ship> AShip(SpaceGod::UnitySpaceApp& app)
	{
		if (!app.World() || !app.World()->Fleet) return nullptr;
		for (const auto& s : app.World()->Fleet->Ships())
			if (!s->Destroyed && s->Phase != ShipPhase::Landed && s->Phase != ShipPhase::Queued) return s;
		return nullptr;
	}

	/// US-T159: a ship to frame at every stop: a freshly realised system has none yet, so one is placed in front of the camera.
	cs::Ref<Ship> ShipInShot(SpaceGod::UnitySpaceApp& app)
	{
		if (!app.World() || !app.Fleet()) return nullptr;
		int frame = app.SpaceCamera->FrameAt(app.SpaceCamera->ScenePosition);
		auto offsetKm = app.Scale()->InverseLocalOffset((*app.Catalog())[frame], app.SpaceCamera->ScenePosition - app.BodyScene(frame));
		auto fu = app.SpaceCamera->Rotation * Vector3::forward();
		auto forward = DVec3(fu.x, fu.y, fu.z).SwapYZ();
		auto ship = app.Fleet()->SpawnAt("Skybox ship", ShipClass::Freighter, app.World()->Sol, frame, offsetKm, forward, app.Clock->Jd);
		ship->Ports = app.World()->FreeShipPorts();
		return ship;
	}

	/// The view turned to the painted sky's bright heart, with the ship (or the star) off-centre in front of it.
	void FaceSkyHeart(SpaceGod::UnitySpaceApp& app, const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship)
	{
		auto layout = SpaceGod::Rendering::SystemSkyBake::Layout(Biomes::For(system->Name));
		Vector3 heart = SpaceGod::Views::BodyView::ToUnity(DVec3(layout.Heart.X, layout.Heart.Y, layout.Heart.Z));
		DVec3 dir(heart.x, heart.y, heart.z);
		DVec3 subject = ship ? app.ShipScene(ship) : app.BodyScene(0);
		double d = (app.SpaceCamera->ScenePosition - subject).Length();
		Vector3 sideU = Vector3::Cross(Vector3::up(), heart).normalized();
		DVec3 side(sideU.x, sideU.y, sideU.z);
		app.SpaceCamera->ExitFocus();
		app.SpaceCamera->SetPose(subject - dir * d + side * (d * 0.35) + DVec3(0, d * 0.15, 0), Quaternion::LookRotation(heart, Vector3::up()));
	}

	/// US-T147: the showcase tour's stops, one per biome: the nearest system of it, a ship in frame, the view on its sky's heart.
	SpaceGod::Tour::TourRegistration GSkyboxTourStops([]
	{
		using SpaceGod::Tour::TourStop;
		for (int k = 1; k < static_cast<int>(BiomeKind::Count); k++)
		{
			auto kind = static_cast<BiomeKind>(k);
			cs::String biome = Biomes::ToString(kind);
			auto system = std::make_shared<cs::Ref<StarSystem>>();
			auto ship = std::make_shared<cs::Ref<Ship>>();
			auto focusedOn = std::make_shared<cs::Ref<StarSystem>>();
			SpaceGod::Tour::TourRegistry::Add(TourStop::Make("Places", cs::Cat("sky-", cs::Strings::ToLowerInvariant(biome)), cs::Cat("Biome sky: ", biome),
				"Every system beyond Sol is painted into its biome's sky: its nebula, star clouds and colour, here with a ship in front of it")
				.At(10).In(cs::String())
				.WithSetup([kind, system, focusedOn](SpaceGod::UnitySpaceApp& app) -> cs::String
				{
					*system = nullptr;
					*focusedOn = nullptr;
					auto here = app.ActiveSystem != nullptr ? app.ActiveSystem->PositionLy : DVec3::Zero;
					double best = cs::Double::PositiveInfinity;
					cs::String name;
					// Any star on the charts (running or not: a new game's Systems holds few); Realize builds it on arrival.
					auto consider = [&](const cs::String& n, const DVec3& pos)
					{
						if (!SpaceGod::Rendering::SystemSkies::IsWild(n) || Biomes::KindOf(n) != kind) return;
						double d = (pos - here).LengthSquared();
						if (d < best) { best = d; name = n; }
					};
					for (const auto& s : app.Galaxy->Systems) consider(s->Name, s->PositionLy);
					for (const auto& n : app.Galaxy->NearbyStars) consider(n.Name, n.PositionLy);
					for (const auto& n : app.Galaxy->Undiscovered()) consider(n.Name, n.PositionLy);
					if (name.empty()) return "no system of this biome near enough";
					SpaceGod::Rendering::SystemSkies::Prefetch(name);
					*system = app.Galaxy->Realize(name, app.Clock->Jd);
					if (*system == nullptr) return "the system could not be built";
					if (app.ActiveSystem != *system) app.TravelTo(*system);
					return cs::String();
				})
				.Until([system, ship, focusedOn](SpaceGod::UnitySpaceApp& app, double t)
				{
					if (app.ActiveSystem != *system || app.Travel->Active)
					{
						if (!app.Travel->Active && t > 5) app.TravelTo(*system);
						return false;
					}
					if (*focusedOn != *system)
					{
						*focusedOn = *system;
						*ship = ShipInShot(app);
						app.SpaceCamera->FocusOn(*ship ? SpaceGod::Target::OfShip(*ship) : SpaceGod::Target::OfBody(0), *ship ? 3.0 : 40.0);
					}
					return t > 4 && SpaceGod::Rendering::SystemSkies::IsReady((*system)->Name);
				}, 120)
				.WithCamera([system, ship](SpaceGod::UnitySpaceApp& app, double t)
				{
					if (t == 0 && *system != nullptr) FaceSkyHeart(app, *system, *ship);
				})
				.For(7));
		}
	});

	void Start(const TArray<FString>& args)
	{
		int count = 4;
		FString shots;
		bool quit = false;
		for (int i = 0; i < args.Num(); i++)
		{
			const FString& a = args[i];
			if (a.IsNumeric()) count = FMath::Clamp(FCString::Atoi(*a), 1, static_cast<int>(BiomeKind::Count));
			else if (a.Equals(TEXT("shots"), ESearchCase::IgnoreCase) && i + 1 < args.Num()) shots = args[++i];
			else if (a.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) quit = true;
		}

		auto run = std::make_shared<Run>();
		run->quit = quit;
		auto names = std::make_shared<cs::List<cs::String>>();
		auto wait = [](double seconds) { return [seconds](SpaceGod::UnitySpaceApp&, double t) { return t >= seconds; }; };
		run->steps.push_back({ TEXT("game up"), wait(2.0) });
		run->steps.push_back({ TEXT("systems picked"), [names, count](SpaceGod::UnitySpaceApp& app, double)
		{
			*names = Pick(app, count);
			if (names->Count() == 0) throw std::runtime_error("no system beyond Sol in this galaxy");
			for (const auto& n : *names)
			{
				// Painted ahead, as streaming would, so each is up the moment the trip ends.
				SpaceGod::Rendering::SystemSkies::Prefetch(n);
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Skybox: %s is %s"), UTF8_TO_TCHAR(n.c_str()), UTF8_TO_TCHAR(Biomes::ToString(Biomes::KindOf(n))));
			}
			return true;
		} });
		for (int i = 0; i < count; i++)
		{
			auto system = std::make_shared<cs::Ref<StarSystem>>();
			run->steps.push_back({ FString::Printf(TEXT("travelling to system %d"), i + 1), [names, system, i](SpaceGod::UnitySpaceApp& app, double)
			{
				if (i >= names->Count()) return true;
				*system = app.Galaxy->Realize((*names)[i], app.Clock->Jd);
				if (*system == nullptr) throw std::runtime_error("system not found");
				app.TravelTo(*system);
				return true;
			} });
			run->steps.push_back({ TEXT("arrived"), [system](SpaceGod::UnitySpaceApp& app, double t)
			{
				if (*system == nullptr) return true;
				if (app.ActiveSystem != *system && !app.Travel->Active && t > 5.0) app.TravelTo(*system);
				return app.ActiveSystem == *system && !app.Travel->Active;
			} });
			auto ship = std::make_shared<cs::Ref<Ship>>();
			run->steps.push_back({ TEXT("focused on a ship"), [system, ship](SpaceGod::UnitySpaceApp& app, double)
			{
				if (*system == nullptr) return true;
				*ship = ShipInShot(app);
				app.SpaceCamera->FocusOn(*ship ? SpaceGod::Target::OfShip(*ship) : SpaceGod::Target::OfBody(0), *ship ? 3.0 : 40.0);
				return true;
			} });
			run->steps.push_back({ TEXT("sky up and camera settled"), [system](SpaceGod::UnitySpaceApp& app, double t)
			{
				return *system == nullptr || (t >= 5.0 && SpaceGod::Rendering::SystemSkies::IsReady((*system)->Name));
			} });
			run->steps.push_back({ TEXT("turned toward the sky's heart"), [system, ship](SpaceGod::UnitySpaceApp& app, double)
			{
				if (*system == nullptr) return true;
				FaceSkyHeart(app, *system, *ship);
				return true;
			} });
			run->steps.push_back({ TEXT("held"), wait(0.4) });
			if (!shots.IsEmpty())
			{
				auto shot = [shots, system, i](SpaceGod::UnitySpaceApp&, double)
				{
					if (*system == nullptr) return true;
					FString biome = UTF8_TO_TCHAR(Biomes::ToString(Biomes::KindOf((*system)->Name)));
					FString path = FPaths::Combine(shots, FString::Printf(TEXT("skybox-%d-%s.png"), i + 1, *biome));
					FScreenshotRequest::RequestScreenshot(path, false, false);
					UE_LOG(LogSpaceGod, Display, TEXT("sg.Skybox: shot %s (%s)"), *path, UTF8_TO_TCHAR((*system)->Name.c_str()));
					return true;
				};
				run->steps.push_back({ TEXT("shot"), shot });
				run->steps.push_back({ TEXT("shot written"), wait(1.0) });
			}
		}

		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float)
		{
			try { return run->Tick(); }
			catch (const std::exception& e) { return run->Finish(*FString(UTF8_TO_TCHAR(e.what()))); }
		}));
	}

	/// US-T132: <c>sg.Skybox [n] [shots &lt;dir&gt;] [quit]</c> visits n systems beyond Sol (4 by default), each of a different
	/// biome, turns the view toward each painted sky's heart with a ship in frame and, with shots, screenshots it into dir.
	FAutoConsoleCommand GSkyboxCommand(
		TEXT("sg.Skybox"),
		TEXT("US-T132: tour the painted skies beyond Sol, one system per biome: sg.Skybox [n] [shots <dir>] [quit]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Start(args); }));
}
