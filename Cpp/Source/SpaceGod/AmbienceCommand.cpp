// US-T448: sg.Ambience, the space soundscape toured and logged. The free camera is set down in Sol's deep void, beside the
// Sun, off Jupiter and Mars, in the main belt and beside a port; then the helm of a hull by that port is taken (the reactor's
// hum, space muffled through the hull), a defence platform's seat (a station) and a repair worker out on the plating in the
// fight that comes to it (a suit's breathing); the seat is let go, and the camera flies between the stars to the nearest
// system with a nebula (the wider bed between the stars) and is set down in the nebula, then on to the nearest starless
// system if one is simulated (a void station's well: deep space's eerie bed). Each step logs where the
// listener is read to be (Ambient::Place), which beds play at what volume and pitch, and which are wanted.
//   sg.Ambience tour [quit]
// Sound plays only with a device and -GameAudio in a scripted run; without it the place and the wanted mix are still logged
// (AmbientSounds::Watch) but every bed reads silent.

#include "UnitySpaceApp.h"

#include "Audio/AmbientSounds.h"
#include "Audio/GameAudio.h"
#include "Camera/SpaceCamera.h"
#include "Camera/StarTravel.h"
#include "GameLaunch.h"
#include "SimClock.h"
#include "SpaceGodLog.h"
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
#include "Ephemeris.h"
#include "Fleet.h"
#include "Infrastructure/Infrastructure.h"
#include "ScaleModel.h"
#include "Ships.h"
#include "Systems/Galaxy.h"
#include "Systems/SpaceFeatures.h"
#include "World.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"

#include <cmath>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace SpaceGod::Core;
using namespace SpaceGod::Audio;
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

	FString T(const std::string& s) { return FString(UTF8_TO_TCHAR(s.c_str())); }

	FString Distance(double v, const TCHAR* unit)
	{
		return std::isfinite(v) ? FString::Printf(TEXT("%.3g%s"), v, unit) : FString(TEXT("-"));
	}

	/// One line: where the listener is read to be, what plays (volume as applied, the wanted level, pitch), what is silent.
	void Log(const FString& step)
	{
		const auto& r = AmbientSounds::Now();
		const auto& p = r.Place;
		FString place = FString::Printf(TEXT("seat=%s inside=%hs star=%s@%s planet=%s@%s%s port=%s@%s%s nebula=%.2f belt=%.2f starless=%d outside=%.2f riding=%.2f ly=%s"),
			*T(r.Where), Ambient::InteriorName(p.Inside), *T(r.Star), *Distance(p.StarRadii, TEXT("R")), *T(r.Planet), *Distance(p.PlanetRadii, TEXT("R")),
			p.GasGiant ? TEXT("(giant)") : p.Moon ? TEXT("(moon)") : TEXT(""), *T(r.Port), *Distance(p.PortKm, TEXT("km")), p.Docked ? TEXT("(docked)") : TEXT(""),
			p.Nebula, p.Belt, p.Starless ? 1 : 0, p.Outside, p.Riding, *Distance(p.LyFromCharted, TEXT("ly")));
		FString playing, wanted;
		for (int i = 0; i < Ambient::BedCount; i++)
		{
			const auto bed = static_cast<Ambient::Bed>(i);
			if (r.Live[static_cast<size_t>(i)])
				playing += FString::Printf(TEXT(" %hs %.3f@%.2f"), Ambient::BedName(bed), r.Volume[static_cast<size_t>(i)], r.Pitch[static_cast<size_t>(i)]);
			if (r.Wanted.Level[static_cast<size_t>(i)] > 0.0005f)
				wanted += FString::Printf(TEXT(" %hs %.3f"), Ambient::BedName(bed), r.Wanted.Level[static_cast<size_t>(i)]);
		}
		if (playing.IsEmpty()) playing = TEXT(" none");
		UE_LOG(LogSpaceGod, Display, TEXT("sg.Ambience: [%s] %s | sounding=%d voices=%d | playing:%s | wanted:%s (loudest %hs)"), *step, *place,
			r.Sounding ? 1 : 0, GameAudio::Playing(), *playing, *wanted, Ambient::BedName(r.Wanted.Loudest()));
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
				at++;
				stepStart = -1;
			}
			else if (now - stepStart > 120.0) return Finish(*FString::Printf(TEXT("timed out at '%s'"), *steps[at].first));
			return true;
		}

		bool Finish(const TCHAR* failure)
		{
			AmbientSounds::Watch(false);
			if (auto* app = RunningApp()) app->Clock->SetPaused(false);
			if (failure) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.Ambience: %s"), failure); }
			else { UE_LOG(LogSpaceGod, Display, TEXT("sg.Ambience: done")); }
			if (quit) FPlatformMisc::RequestExit(false);
			return false;
		}
	};

	struct Scene
	{
		cs::Ref<OrbitalStructure> port;
		cs::Ref<Ship> hull;
		cs::Ref<StationSeat> platform;
		int platformBody = 0;
		DVec3 platformAt, side;
		cs::Ref<StarSystem> misty;
		DVec3 mistAt;
		double lastLog = 0;
		bool noDark = false;
	};

	/// The free camera at <paramref name="offsetKm"/> (true km) from body <paramref name="body"/>, looking at it.
	void PlaceCamera(UnitySpaceApp& app, int body, DVec3 offsetKm)
	{
		if (!app.SpaceCamera->FocusTarget.IsNone()) app.SpaceCamera->ExitFocus();
		const auto def = (*app.Catalog())[body];
		const DVec3 centre = app.BodyScene(body);
		const DVec3 eye = centre + app.Scale()->LocalOffset(def, offsetKm);
		DVec3 look = centre - eye;
		if (look.LengthSquared() < 1e-24) look = DVec3(0, 0, 1);
		look = look.Normalized();
		SpaceGod::Vector3 f(static_cast<float>(look.X), static_cast<float>(look.Y), static_cast<float>(look.Z));
		SpaceGod::Vector3 up = std::abs(f.y) > 0.98f ? SpaceGod::Vector3::forward() : SpaceGod::Vector3::up();
		app.SpaceCamera->SetPose(eye, SpaceGod::Quaternion::LookRotation(f, up));
	}

	int BodyIndex(UnitySpaceApp& app, const char* name)
	{
		const auto& cat = app.Catalog();
		for (int i = 0; i < cat->Count(); i++)
			if ((*cat)[i]->Name == name) return i;
		throw std::runtime_error(std::string("no body named ") + name);
	}

	void Start(const TArray<FString>& args)
	{
		bool quit = false;
		for (const FString& a : args)
			if (a.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) quit = true;
		auto run = std::make_shared<Run>();
		run->quit = quit;
		auto scene = std::make_shared<Scene>();
		auto push = [&run](FString name, Run::Step step) { run->steps.push_back({ name, std::move(step) }); };
		auto wait = [](double seconds) { return [seconds](UnitySpaceApp&, double t) { return t >= seconds; }; };
		constexpr double Dwell = 6.0;
		// Holds the camera at a place for the dwell (the crossfades settle), then logs it.
		auto at = [&push](const TCHAR* name, std::function<void(UnitySpaceApp&)> place)
		{
			FString label(name);
			push(label, [label, place](UnitySpaceApp& app, double t)
			{
				place(app);
				if (t < Dwell) return false;
				Log(label);
				return true;
			});
		};
		// Holds a seat for the dwell, then logs it.
		auto held = [&push](const TCHAR* name)
		{
			FString label(name);
			push(label, [label](UnitySpaceApp&, double t)
			{
				if (t < Dwell) return false;
				Log(label);
				return true;
			});
		};

		AmbientSounds::Watch(true);
		push(TEXT("game up"), wait(6.0));
		push(TEXT("staged"), [](UnitySpaceApp& app, double)
		{
			app.SkyLeaveHelm();
			if (app.IsPiloting() || app.HeldSeat() != nullptr) app.PickCommand();
			if (!app.SpaceCamera->FocusTarget.IsNone()) app.SpaceCamera->ExitFocus();
			app.Clock->SetPaused(true);
			SpaceGodInfluence::Of(app.World())->Waived = true; // a debug run: the embody limits lifted (never saved)
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Ambience: sound %s (Ambience bus %.2f, master %.2f)"), GameAudio::Enabled() ? TEXT("on") : TEXT("off"),
				GameAudio::BusVolume(AudioBus::Ambience), GameAudio::BusVolume(AudioBus::Master));
			return true;
		});

		const double au = Astro::AuKm;
		at(TEXT("deep void, 70 AU out"), [au](UnitySpaceApp& app) { PlaceCamera(app, 0, DVec3(70 * au, 0, 15 * au)); });
		at(TEXT("beside the Sun, 0.04 AU"), [au](UnitySpaceApp& app) { PlaceCamera(app, 0, DVec3(0.04 * au, 0, 0.005 * au)); });
		at(TEXT("off Jupiter, 4 radii"), [](UnitySpaceApp& app)
		{
			const int j = BodyIndex(app, "Jupiter");
			const double r = (*app.Catalog())[j]->RadiusKm;
			PlaceCamera(app, j, DVec3(4 * r, 0, r));
		});
		at(TEXT("off Mars, 3 radii"), [](UnitySpaceApp& app)
		{
			const int m = BodyIndex(app, "Mars");
			const double r = (*app.Catalog())[m]->RadiusKm;
			PlaceCamera(app, m, DVec3(0, 3 * r, 0.5 * r));
		});
		at(TEXT("in the main belt, 2.75 AU"), [au](UnitySpaceApp& app)
		{
			// Across the Sun from Jupiter, on the ecliptic.
			const DVec3 jup = app.Ephemeris()->Helio(BodyIndex(app, "Jupiter")) - app.Ephemeris()->Helio(0);
			DVec3 away(-jup.X, -jup.Y, 0);
			away = away.LengthSquared() > 0 ? away.Normalized() : DVec3(1, 0, 0);
			PlaceCamera(app, 0, away * (2.75 * au));
		});
		push(TEXT("a port found"), [scene](UnitySpaceApp& app, double)
		{
			for (const auto& s : app.World()->Infrastructure->Structures)
				if (s != nullptr && (s->Kind == StructureKind::Habitat || s->Kind == StructureKind::Shipyard || s->Kind == StructureKind::Gateway || s->Kind == StructureKind::Depot))
				{
					scene->port = s;
					if (s->Kind == StructureKind::Habitat) break;
				}
			if (scene->port == nullptr) throw std::runtime_error("no port in orbit in this system");
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Ambience: port %s (%hs) over %s"), *FString(UTF8_TO_TCHAR(scene->port->Name.c_str())), ToString(scene->port->Kind),
				*FString(UTF8_TO_TCHAR((*app.Catalog())[scene->port->Body]->Name.c_str())));
			return true;
		});
		auto portOffset = [scene](UnitySpaceApp& app, double outKm)
		{
			const auto& eph = app.Ephemeris();
			const DVec3 rel = scene->port->Helio(eph) - eph->Helio(scene->port->Body);
			const DVec3 out = rel.LengthSquared() > 0 ? rel.Normalized() : DVec3(1, 0, 0);
			return rel + out * outKm;
		};
		at(TEXT("beside the port, 150 km"), [scene, portOffset](UnitySpaceApp& app) { PlaceCamera(app, scene->port->Body, portOffset(app, 150)); });

		// ---- inside: a hull's helm by the port
		push(TEXT("helm taken"), [scene, portOffset](UnitySpaceApp& app, double)
		{
			const DVec3 at = portOffset(app, 2);
			scene->hull = app.Fleet()->SpawnAt("Ambience Frigate", ShipClass::Frigate, app.World()->Sol, scene->port->Body, at, DVec3::UnitY, app.Clock->Jd);
			if (scene->hull == nullptr) throw std::runtime_error("the hull would not spawn");
			app.PickSeat(Embodiables::HelmOf(app.World(), scene->hull));
			return true;
		});
		held(TEXT("at a frigate's helm by the port"));

		// ---- a defence platform's seat, and a repair worker out on a hull in the fight that comes to it
		push(TEXT("platform taken"), [scene](UnitySpaceApp& app, double)
		{
			app.PickCommand();
			const double jd = app.Clock->Jd;
			for (const auto& s : Embodiables::StationsIn(app.World()))
				if (s != nullptr && s->CanEmbody() && s->Settlement != nullptr)
				{
					scene->platform = s;
					break;
				}
			if (scene->platform == nullptr) throw std::runtime_error("no defence platform to take in this system");
			auto pose = scene->platform->PoseAt(jd);
			scene->platformBody = pose.FrameBody;
			scene->platformAt = pose.OffsetKm;
			scene->side = DVec3::Cross(pose.OffsetKm.Normalized(), DVec3::UnitZ);
			if (scene->side.LengthSquared() < 1e-9) scene->side = DVec3(1, 0, 0);
			scene->side = scene->side.Normalized();
			app.Fleet()->SpawnAt("Ambience Battleship", ShipClass::Battleship, app.World()->Sol, scene->platformBody, scene->platformAt - scene->side * 120.0, scene->side, jd);
			for (int i = 0; i < 2; i++)
				app.Fleet()->SpawnAt(cs::Cat("Ambience raider ", i + 1), ShipClass::Destroyer, app.World()->Reach, scene->platformBody, scene->platformAt + scene->side * (300.0 + 200.0 * i), scene->side * -1.0, jd);
			app.Clock->SetPaused(false);
			app.Clock->SetWarp(0);
			app.World()->War->TryStartBattle(scene->platformBody, app.World()->Sol, jd);
			app.PickSeat(scene->platform);
			return true;
		});
		held(TEXT("in a defence platform's seat"));
		push(TEXT("worker taken"), [](UnitySpaceApp& app, double t)
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
			if (t > 20.0)
			{
				UE_LOG(LogSpaceGod, Warning, TEXT("sg.Ambience: no repair worker to take (no fight with a crew out)"));
				return true;
			}
			return false;
		});
		held(TEXT("a repair worker out on the plating"));
		push(TEXT("let go"), [](UnitySpaceApp& app, double)
		{
			app.PickCommand();
			return true;
		});
		held(TEXT("the god view again, by the platform"));

		// ---- between the stars to a nebula
		push(TEXT("a nebula found"), [scene](UnitySpaceApp& app, double)
		{
			double best = 1e300;
			for (const auto& s : app.Galaxy->Systems)
			{
				if (s == nullptr || s == app.ActiveSystem || s->Catalog == nullptr || s->Catalog->Count() == 0) continue;
				const auto& features = SpaceFeatures::Of(s->Catalog->Root()->Name);
				if (features.Nebulae.Count() == 0) continue;
				const double ly = (s->PositionLy - app.ActiveSystem->PositionLy).Length();
				if (ly >= best) continue;
				best = ly;
				scene->misty = s;
				// The densest point offered: the nebula's centre, or one of its knots.
				const auto& n = features.Nebulae[0];
				const cs::String root = s->Catalog->Root()->Name;
				DVec3 pick = n.CentreKm;
				double mask = SpaceFeatures::SensorMask(root, pick);
				for (const auto& puff : n.Puffs)
					for (const DVec3 c : { n.CentreKm + puff.OffsetKm, puff.OffsetKm })
					{
						const double m = SpaceFeatures::SensorMask(root, c);
						if (m < mask) { mask = m; pick = c; }
					}
				scene->mistAt = pick;
			}
			if (scene->misty == nullptr) throw std::runtime_error("no system with a nebula");
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Ambience: travelling %.2f ly to %s for its nebula"), best, *FString(UTF8_TO_TCHAR(scene->misty->Name.c_str())));
			app.CheatTravelTo(scene->misty);
			return true;
		});
		push(TEXT("between the stars"), [scene](UnitySpaceApp& app, double t)
		{
			const double now = FPlatformTime::Seconds();
			const bool travelling = app.Travel != nullptr && app.Travel->Active;
			if (now - scene->lastLog >= 0.6)
			{
				scene->lastLog = now;
				Log(FString::Printf(TEXT("in transit %.1f s%s"), t, travelling ? TEXT("") : TEXT(" (arrived)")));
			}
			return (!travelling && app.ActiveSystem == scene->misty && t > 2.0);
		});
		push(TEXT("arrived"), wait(2.0));
		at(TEXT("inside the nebula"), [scene](UnitySpaceApp& app) { PlaceCamera(app, 0, scene->mistAt); });

		// ---- on to the nearest starless system (a void station's own well): deep space's eerie bed
		push(TEXT("a void station found"), [scene](UnitySpaceApp& app, double)
		{
			double best = 1e300;
			cs::Ref<StarSystem> dark;
			for (const auto& s : app.Galaxy->Systems)
			{
				if (s == nullptr || s == app.ActiveSystem || s->Catalog == nullptr || s->Catalog->Count() == 0) continue;
				bool star = false;
				for (int i = 0; i < s->Catalog->Count() && !star; i++) star = (*s->Catalog)[i]->Kind == BodyKind::Star;
				const double ly = (s->PositionLy - app.ActiveSystem->PositionLy).Length();
				if (star || ly >= best) continue;
				best = ly;
				dark = s;
			}
			if (dark == nullptr)
			{
				UE_LOG(LogSpaceGod, Warning, TEXT("sg.Ambience: no starless system to visit (deep space left to AmbientMixTests)"));
				scene->noDark = true;
				return true;
			}
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Ambience: travelling %.2f ly to %s, a starless system"), best, *FString(UTF8_TO_TCHAR(dark->Name.c_str())));
			scene->misty = dark;
			app.CheatTravelTo(dark);
			return true;
		});
		push(TEXT("between the stars again"), [scene](UnitySpaceApp& app, double t)
		{
			if (scene->noDark) return true;
			const double now = FPlatformTime::Seconds();
			const bool travelling = app.Travel != nullptr && app.Travel->Active;
			if (now - scene->lastLog >= 0.6)
			{
				scene->lastLog = now;
				Log(FString::Printf(TEXT("in transit %.1f s%s%s"), t, travelling && app.Travel->ThroughGate ? TEXT(" (gates)") : TEXT(""), travelling ? TEXT("") : TEXT(" (arrived)")));
			}
			return (!travelling && app.ActiveSystem == scene->misty && t > 2.0);
		});
		push(TEXT("arrived again"), [scene](UnitySpaceApp&, double t) { return scene->noDark || t >= 2.0; });
		push(TEXT("by the void station, 50,000 km out"), [scene](UnitySpaceApp& app, double t)
		{
			if (scene->noDark) return true;
			PlaceCamera(app, 0, DVec3(50000, 0, 5000));
			if (t < Dwell) return false;
			Log(TEXT("by the void station, 50,000 km out"));
			return true;
		});
		push(TEXT("written"), wait(0.5));

		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float)
		{
			try { return run->Tick(); }
			catch (const std::exception& e) { return run->Finish(*FString(UTF8_TO_TCHAR(e.what()))); }
		}));
	}

	FAutoConsoleCommand GAmbienceCommand(
		TEXT("sg.Ambience"),
		TEXT("US-T448: sg.Ambience tour [quit] moves the camera through Sol's void, beside the Sun, off Jupiter and Mars, the main belt "
			"and a port, takes a helm, a station's seat and a repair worker's suit, then flies between the stars into a nebula, logging "
			"which ambient beds play at what volume at each step."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Start(args); }));
}
