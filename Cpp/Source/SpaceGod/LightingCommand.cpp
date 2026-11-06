// US-T172: sg.Lighting, the hull lighting's scenes (a USC fighter, a capital in the Battle of Io, a Space Station
// Creator station, a ship on a world's night side, a Void Station), its screenshots and frame times at both scales, its
// dev menu entries and its showcase tour stop.

#include "UnitySpaceApp.h"

#include "Camera/SpaceCamera.h"
#include "Camera/StarTravel.h"
#include "GameLaunch.h"
#include "Rendering/HullLighting.h"
#include "Rendering/SscKit.h"
#include "SceneTour.h"
#include "SimClock.h"
#include "SpaceGodLog.h"
#include "Target.h"
#include "Tour/ShowcaseTour.h"
#include "UI/CheatMenu.h"
#include "UnitySpaceAppActor.h"
#include "Views/StructureViews.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Combat/Battle.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Infrastructure/Infrastructure.h"
#include "Ships.h"
#include "Systems/Galaxy.h"
#include "Systems/VoidStations.h"
#include "World.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace SpaceGod;
using namespace SpaceGod::Core;

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

	int BodyNamed(UnitySpaceApp& app, const cs::String& name)
	{
		for (const auto& b : app.Catalog()->Bodies())
			if (b->Name == name) return b->Index;
		return -1;
	}

	DVec3 Side(DVec3 d) { return DVec3::Cross(DVec3(0, 1, 0), d).Normalized(); }

	/// The camera on <paramref name="subject"/> (a ship or a structure) from scene direction <paramref name="dir"/> (unit,
	/// from it) at <paramref name="radii"/> of its half-length, entering focus there (as SceneTour::ViewShip does).
	void Frame(UnitySpaceApp& app, const SpaceGod::Target& t, DVec3 at, double length, DVec3 dir, double radii)
	{
		app.Select(t);
		auto eye = at + dir * (length * 0.5 * radii * 1.3);
		auto look = -dir;
		app.SpaceCamera->PlaceFree(eye, static_cast<float>(cs::Math::Atan2(look.X, look.Z) * Mathf::Rad2Deg),
			static_cast<float>(-cs::Math::Asin(look.Y) * Mathf::Rad2Deg));
		auto local = Quaternion::Inverse(app.SpaceCamera->UpFrame) * Vector3(static_cast<float>(dir.X), static_cast<float>(dir.Y), static_cast<float>(dir.Z));
		app.SpaceCamera->FocusOn(t, radii, Mathf::Asin(Mathf::Clamp(local.y, -1.0f, 1.0f)) * Mathf::Rad2Deg);
	}

	/// From <paramref name="at"/>, the scene direction to the system's main star.
	DVec3 SunDir(UnitySpaceApp& app, DVec3 at) { return (app.BodyScene(0) - at).Normalized(); }

	TAutoConsoleVariable<float> CVarRake(TEXT("sg.Lighting.Rake"), 0.8f,
		TEXT("US-T172: how far round from the star the lighting scenes' camera sits (0 the star behind it, 1 square across its light)"));

	/// A raking key: the camera well round from the star's line, so the light crosses the hull and its greebles throw
	/// shadows across it, the lit side and the shadow side both in view (the angle a ship is shot from in EVE or Elite).
	DVec3 ThreeQuarter(UnitySpaceApp& app, DVec3 at)
	{
		DVec3 sun = SunDir(app, at);
		double rake = std::clamp(static_cast<double>(CVarRake.GetValueOnGameThread()), 0.0, 1.0);
		return (sun * (1.0 - rake) + Side(sun) * rake + DVec3(0, 0.25, 0)).Normalized();
	}

	/// What a scene keeps between its setup and its framing.
	struct SceneState
	{
		cs::Ref<Ship> Hull;
		cs::Ref<OrbitalStructure> Structure;
		cs::Ref<StarSystem> System;
		cs::Ref<Battle> Fight;
		DVec3 Dir;
	};

	/// One lighting scene: set up once (spawn, battle, trip), then ready, then framed (again at each scale).
	struct Scene
	{
		const char* Name;
		std::function<cs::String(UnitySpaceApp&, SceneState&)> Setup;
		std::function<bool(UnitySpaceApp&, SceneState&, double)> Ready;
		std::function<void(UnitySpaceApp&, SceneState&)> Framing;
		/// Time held from its setup on (a parked hull that must stay where it was put: in the sun, in a world's shadow).
		bool Freeze = false;
	};

	cs::Ref<Ship> SpawnNear(UnitySpaceApp& app, const cs::String& name, ShipClass cls, int body, DVec3 offsetKm, DVec3 forward)
	{
		return app.Fleet()->SpawnAt(name, cls, app.World()->Sol, body, offsetKm, forward, app.Clock->Jd);
	}

	void FrameShip(UnitySpaceApp& app, SceneState& s, double radii)
	{
		if (!s.Hull || s.Hull->Destroyed) return;
		auto at = app.ShipScene(s.Hull);
		Frame(app, SpaceGod::Target::OfShip(s.Hull), at, app.ShipDisplayLength(s.Hull), s.Dir.LengthSquared() > 0 ? s.Dir : ThreeQuarter(app, at), radii);
	}

	std::vector<Scene> Scenes()
	{
		std::vector<Scene> scenes;
		auto at = [](double seconds) { return [seconds](UnitySpaceApp&, SceneState&, double t) { return t >= seconds; }; };

		// A Sol navy fighter (USC HyperFalcon07) parked off Earth's day side.
		scenes.push_back({ "usc-fighter", [](UnitySpaceApp& app, SceneState& s) -> cs::String
		{
			int earth = BodyNamed(app, "Earth");
			if (earth < 0) return "no Earth";
			DVec3 outward;
			double km = app.BodyStandoffKm(earth, outward);
			s.Hull = SpawnNear(app, "Lighting Fighter", ShipClass::Fighter, earth, outward * km, -outward);
			return s.Hull ? cs::String() : cs::String("no fighter spawned");
		}, at(2.5), [](UnitySpaceApp& app, SceneState& s) { FrameShip(app, s, 2.6); }, true });

		// The Battle of Io: Sol's battleship line against the Reach off Jupiter's moon, a capital in close.
		scenes.push_back({ "capital-io", [](UnitySpaceApp& app, SceneState& s) -> cs::String
		{
			int io = BodyNamed(app, "Io");
			if (io < 0) return "no Io";
			s.Fight = SpaceGod::SceneTour::TriggerSkirmish(app, io, app.World()->Sol, app.World()->Reach,
				cs::List<ShipClass>{ ShipClass::Battleship, ShipClass::Cruiser, ShipClass::Destroyer });
			if (!s.Fight) return "the Battle of Io did not start";
			app.WatchBattle(s.Fight);
			for (const auto& u : s.Fight->Units)
				if (u->Ship && u->Side == 0 && u->Ship->Spec->Class == ShipClass::Battleship) { s.Hull = u->Ship; break; }
			return s.Hull ? cs::String() : cs::String("no battleship in it");
		}, at(7.0), [](UnitySpaceApp& app, SceneState& s) { FrameShip(app, s, 1.9); } });

		// A Space Station Creator station in orbit (a habitat wheel or a factory complex), Earth's first.
		scenes.push_back({ "ssc-station", [](UnitySpaceApp& app, SceneState& s) -> cs::String
		{
			auto infra = app.World()->Infrastructure;
			if (!infra) return "no infrastructure";
			int earth = BodyNamed(app, "Earth");
			cs::Ref<OrbitalStructure> best;
			int bestScore = -1;
			for (const auto& st : infra->Structures)
			{
				if (SpaceGod::Rendering::SscKit::ForStructure(st->Kind, st->Industry, st->Name).empty()) continue;
				int score = (st->Body == earth ? 2 : 0) + (st->Kind == StructureKind::Habitat ? 1 : 0);
				if (score > bestScore) { best = st; bestScore = score; }
			}
			s.Structure = best;
			return best ? cs::String() : cs::String("no station the pack draws");
		}, at(1.0), [](UnitySpaceApp& app, SceneState& s)
		{
			auto t = SpaceGod::Target::OfStructure(s.Structure);
			DVec3 scene;
			double size = 0;
			if (!app.Structures || !app.Structures->Scenes.TryGetValue(s.Structure, scene) || !app.Structures->Sizes.TryGetValue(s.Structure, size))
			{
				app.Select(t);
				app.SpaceCamera->FocusOn(t, 3.0, 20.0f);
				return;
			}
			Frame(app, t, scene, size, ThreeQuarter(app, scene), 2.6);
		} });

		// A frigate on Earth's night side, in its shadow: no star on it, only the sky, the rim and its own lights.
		scenes.push_back({ "dark-side", [](UnitySpaceApp& app, SceneState& s) -> cs::String
		{
			int earth = BodyNamed(app, "Earth");
			if (earth < 0) return "no Earth";
			auto def = (*app.Catalog())[earth];
			auto helio = app.Ephemeris()->Helio(earth);
			DVec3 night = helio.Normalized(); // away from the Sun (ecliptic km)
			s.Hull = SpawnNear(app, "Lighting Nightwatch", ShipClass::Frigate, earth, night * (def->RadiusKm * 1.25), Side(night.SwapYZ()).SwapYZ());
			return s.Hull ? cs::String() : cs::String("no frigate spawned");
		}, at(2.5), [](UnitySpaceApp& app, SceneState& s)
		{
			if (!s.Hull) return;
			auto at = app.ShipScene(s.Hull);
			DVec3 sun = SunDir(app, at);
			// Out beyond it, looking back at it with the night limb of the world behind.
			s.Dir = (-sun * 0.55 + Side(sun) * 0.75 + DVec3(0, 0.25, 0)).Normalized();
			FrameShip(app, s, 2.6);
		}, true });

		// Void Station 1 (Space Station Creator's Combined20) and its three Embers.
		scenes.push_back({ "void-station", [](UnitySpaceApp& app, SceneState& s) -> cs::String
		{
			cs::String name = cs::Cat(VoidStations::Prefix, " ", 1);
			s.System = app.Galaxy->Realize(name, app.Clock->Jd);
			if (!s.System)
				for (const auto& x : app.Galaxy->Systems)
					if (x->IsVoidBase) { s.System = x; break; }
			if (!s.System) return "no void station";
			app.TravelTo(s.System);
			return cs::String();
		}, [](UnitySpaceApp& app, SceneState& s, double t)
		{
			if (app.ActiveSystem != s.System && !app.Travel->Active && t > 5.0) app.TravelTo(s.System);
			return app.ActiveSystem == s.System && !app.Travel->Active && t > 3.0;
		}, [](UnitySpaceApp& app, SceneState&)
		{
			auto t = SpaceGod::Target::OfBody(0);
			app.Select(t);
			app.SpaceCamera->FocusOn(t, 2.6, 16.0f);
		} });
		return scenes;
	}

	/// One run of the command, a step a frame on the core ticker (VoidStationCommand's pattern): each step waits until done.
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
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Lighting: %s"), *steps[at].first);
				at++;
				stepStart = -1;
			}
			else if (now - stepStart > 240.0) return Finish(*FString::Printf(TEXT("timed out at '%s'"), *steps[at].first));
			return true;
		}

		bool Finish(const TCHAR* failure)
		{
			if (failure) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.Lighting: %s"), failure); }
			else { UE_LOG(LogSpaceGod, Display, TEXT("sg.Lighting: done")); }
			if (quit) FPlatformMisc::RequestExit(false);
			return false;
		}
	};

	/// Frame times over a few seconds: the game thread's frame and the GPU's.
	struct Meter
	{
		double sum = 0, gpu = 0;
		int frames = 0;
	};

	void Start(const TArray<FString>& args)
	{
		FString shots, tag = SpaceGod::Rendering::HullLighting::Enabled() ? TEXT("after") : TEXT("before"), only;
		bool quit = false;
		for (int i = 0; i < args.Num(); i++)
		{
			const FString& a = args[i];
			if (a.Equals(TEXT("shots"), ESearchCase::IgnoreCase) && i + 1 < args.Num()) shots = args[++i];
			else if (a.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) quit = true;
			else if (a.StartsWith(TEXT("tag="), ESearchCase::IgnoreCase)) tag = a.Mid(4);
			else if (a.StartsWith(TEXT("only="), ESearchCase::IgnoreCase)) only = a.Mid(5);
			else if (!a.IsEmpty() && a != TEXT("game") && a != TEXT("real")) only = a;
		}
		bool realFirst = args.ContainsByPredicate([](const FString& a) { return a.Equals(TEXT("real"), ESearchCase::IgnoreCase); });
		if (args.Num() == 0)
		{
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Lighting: %s"), UTF8_TO_TCHAR(SpaceGod::Rendering::HullLighting::Describe().c_str()));
			return;
		}

		auto run = std::make_shared<Run>();
		run->quit = quit;
		auto wait = [](double seconds) { return [seconds](UnitySpaceApp&, double t) { return t >= seconds; }; };
		run->steps.push_back({ TEXT("game up"), wait(3.0) });
		auto setScale = [](double goal)
		{
			return [goal](UnitySpaceApp& app, double)
			{
				if (app.ScaleGoal != goal) app.ToggleScale();
				return !app.ScaleAnimating();
			};
		};
		int n = 0;
		for (const auto& scene : Scenes())
		{
			n++;
			FString name = UTF8_TO_TCHAR(scene.Name);
			if (!only.IsEmpty() && !only.Contains(name)) continue;
			auto state = std::make_shared<SceneState>();
			auto failed = std::make_shared<bool>(false);
			run->steps.push_back({ FString::Printf(TEXT("%s set up"), *name), [scene, state, failed, name](UnitySpaceApp& app, double)
			{
				app.Clock->SetPaused(false);
				cs::String why = scene.Setup(app, *state);
				*failed = !why.empty();
				if (scene.Freeze) app.Clock->SetPaused(true);
				if (*failed) UE_LOG(LogSpaceGod, Warning, TEXT("sg.Lighting: %s skipped: %s"), *name, UTF8_TO_TCHAR(why.c_str()));
				return true;
			} });
			run->steps.push_back({ FString::Printf(TEXT("%s ready"), *name), [scene, state, failed](UnitySpaceApp& app, double t)
			{
				return *failed || scene.Ready(app, *state, t);
			} });
			// Each scale: the subject framed, the time held (so both scales, and the before and after runs, see one moment), a
			// few seconds to settle, frame times metered, then the shot.
			for (double goal : shots.IsEmpty() ? std::vector<double>{ realFirst ? 0.0 : 1.0 } : std::vector<double>{ 1.0, 0.0 })
			{
				const TCHAR* scale = goal > 0.5 ? TEXT("game") : TEXT("real");
				run->steps.push_back({ FString::Printf(TEXT("%s at the %s scale"), *name, scale), [setScale, goal, failed](UnitySpaceApp& app, double t)
				{
					return *failed || setScale(goal)(app, t);
				} });
				run->steps.push_back({ FString::Printf(TEXT("%s framed"), *name), [scene, state, failed](UnitySpaceApp& app, double)
				{
					if (!*failed) scene.Framing(app, *state);
					return true;
				} });
				if (shots.IsEmpty()) continue;
				run->steps.push_back({ TEXT("held"), [failed](UnitySpaceApp& app, double t)
				{
					if (t >= 3.0 && !*failed) app.Clock->SetPaused(true);
					return *failed || t >= 3.0;
				} });
				run->steps.push_back({ TEXT("settled"), [scene, state, failed](UnitySpaceApp& app, double t)
				{
					// Framed again once the focus has flown in (the hull drawn at its size now).
					if (t < 0.1 && !*failed) scene.Framing(app, *state);
					// Not before the shaders are in: from the editor binaries a material first drawn here (the lit hulls on a
					// fresh run, or with no shader cache to write to) draws as the default grey until its compile finishes, so
					// the first scene's shot and frame times were of a grey, untextured hull.
					if (GShaderCompilingManager != nullptr && GShaderCompilingManager->GetNumRemainingJobs() > 0) return *failed;
					return *failed || t >= 3.0;
				} });
				auto meter = std::make_shared<Meter>();
				run->steps.push_back({ FString::Printf(TEXT("%s %s metered"), *name, scale), [meter, failed, name, scale, tag](UnitySpaceApp&, double t)
				{
					if (*failed) return true;
					meter->sum += FApp::GetDeltaTime() * 1000.0;
					meter->gpu += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
					meter->frames++;
					if (t < 3.0) return false;
					UE_LOG(LogSpaceGod, Display, TEXT("sg.Lighting: perf %s %s %s frame %.2f ms gpu %.2f ms over %d frames | %s"), *tag, *name, scale,
						meter->sum / meter->frames, meter->gpu / meter->frames, meter->frames, UTF8_TO_TCHAR(SpaceGod::Rendering::HullLighting::Describe().c_str()));
					return true;
				} });
				FString path = FPaths::Combine(shots, FString::Printf(TEXT("%s-%d-%s-%s.png"), *tag, n, *name, scale));
				run->steps.push_back({ FString::Printf(TEXT("shot %s"), *path), [path, failed](UnitySpaceApp&, double)
				{
					if (!*failed) FScreenshotRequest::RequestScreenshot(path, false, false);
					return true;
				} });
				run->steps.push_back({ TEXT("shot written"), wait(1.5) });
				if (!scene.Freeze) run->steps.push_back({ TEXT("time runs"), [](UnitySpaceApp& app, double) { app.Clock->SetPaused(false); return true; } });
			}
		}

		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float)
		{
			try { return run->Tick(); }
			catch (const std::exception& e) { return run->Finish(*FString(UTF8_TO_TCHAR(e.what()))); }
		}));
	}

	/// <c>sg.Lighting</c> says what the hull lights are doing; <c>sg.Lighting &lt;scene&gt; [real]</c> sets one of the lighting
	/// scenes up (usc-fighter, capital-io, ssc-station, dark-side, void-station) at the game (or realistic) scale;
	/// <c>sg.Lighting shots &lt;dir&gt; [only=a,b] [tag=t] [quit]</c> shoots every scene at both scales with its frame times.
	FAutoConsoleCommand GLightingCommand(
		TEXT("sg.Lighting"),
		TEXT("US-T172: the hull lighting. sg.Lighting: what the lights are doing; sg.Lighting <usc-fighter|capital-io|ssc-station|dark-side|void-station> [real]: "
			"that scene; sg.Lighting shots <dir> [only=a,b] [tag=t] [quit]: every scene at both scales, shot and timed. sg.HullLighting.* tune it."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Start(args); }));

	/// The dev menu (US-T126): Presentation > the lighting scenes.
	void RegisterLightingCheats()
	{
		using SpaceGod::UI::CheatEntry;
		using SpaceGod::UI::CheatRegistry;
		CheatRegistry::Add("Presentation", CheatEntry::Console("Lighting: a fighter in the sun", "a USC fighter three-quarter lit: key, self-shadows, sky light, rim, nav lights (US-T172)", "sg.Lighting usc-fighter"));
		CheatRegistry::Add("Presentation", CheatEntry::Console("Lighting: the Battle of Io", "a battleship line off Io, a capital close (US-T172)", "sg.Lighting capital-io"));
		CheatRegistry::Add("Presentation", CheatEntry::Console("Lighting: a station", "a Space Station Creator station in orbit (US-T172)", "sg.Lighting ssc-station"));
		CheatRegistry::Add("Presentation", CheatEntry::Console("Lighting: the night side", "a frigate in Earth's shadow: only the sky, the rim and its own lights (US-T172)", "sg.Lighting dark-side"));
		CheatRegistry::Add("Presentation", CheatEntry::Console("Lighting: a Void Station", "the station lit by its Embers (US-T172)", "sg.Lighting void-station"));
	}
	SpaceGod::UI::CheatRegistration GLightingCheats(&RegisterLightingCheats);

	/// US-T147: the showcase tour's stop, set up by the command.
	SpaceGod::Tour::TourRegistration GLightingTourStop([]
	{
		SpaceGod::Tour::TourRegistry::Add(SpaceGod::Tour::TourStop::Console("Presentation", "hull-lighting", "Hull lighting",
			"One hard star and the dark: the hull's own shadows across its greebles, the sky's colour on its shadow side, a cool rim, red and green nav lights and a strobe",
			"sg.Lighting usc-fighter")
			.At(70).For(9).AtBothScales());
		SpaceGod::Tour::TourRegistry::Add(SpaceGod::Tour::TourStop::Console("Presentation", "hull-lighting-night", "A hull in a world's shadow",
			"No star on it: the sky, the rim, its windows and nav lights carry the shape", "sg.Lighting dark-side")
			.At(71).For(9).AtBothScales());
	});
}
